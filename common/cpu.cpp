#include "common/cpu.hpp"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>
#include <sched.h>

namespace bench {
namespace {

// "0,1" 或 "0-1" -> 0
int parse_first_cpu(const std::string& s) {
  size_t end = 0;
  while (end < s.size() &&
         std::isdigit(static_cast<unsigned char>(s[end]))) {
    ++end;
  }
  if (end == 0) {
    return -1;
  }
  return std::stoi(s.substr(0, end));
}

} // namespace

std::vector<int> physical_cores() {
  std::set<int> cores;
  const std::filesystem::path root("/sys/devices/system/cpu");
  std::error_code ec;
  if (std::filesystem::is_directory(root, ec)) {
    for (const auto& entry :
         std::filesystem::directory_iterator(root, ec)) {
      const std::string name = entry.path().filename().string();
      if (name.size() < 4 || !name.starts_with("cpu")) {
        continue;
      }
      bool numeric = true;
      for (size_t i = 3; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
          numeric = false;
          break;
        }
      }
      if (!numeric) {
        continue;
      }
      std::ifstream in(entry.path() / "topology" / "thread_siblings_list");
      std::string line;
      if (!std::getline(in, line)) {
        continue;
      }
      const int cpu = parse_first_cpu(line);
      if (cpu >= 0) {
        cores.insert(cpu);
      }
    }
  }
  if (!cores.empty()) {
    return std::vector<int>(cores.begin(), cores.end());
  }

  const long n = sysconf(_SC_NPROCESSORS_ONLN);
  std::vector<int> fallback;
  fallback.reserve(n > 0 ? static_cast<std::size_t>(n) : 0);
  for (long i = 0; i < n; ++i) {
    fallback.push_back(static_cast<int>(i));
  }
  return fallback;
}

bool pin_current_thread(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

int current_cpu() { return sched_getcpu(); }

} // namespace bench
