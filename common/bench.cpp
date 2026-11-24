#include "common/bench.hpp"

#include "common/cpu.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/resource.h>

namespace bench {
namespace {

void usage(const char* prog) {
  std::fprintf(stderr,
               "用法: %s [--reps N] [--inner N] [--cpu N] [--warmup N] "
               "[--verify]\n",
               prog);
}

long parse_long(const char* flag, const char* value) {
  char* end = nullptr;
  errno = 0;
  const long v = std::strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0') {
    std::fprintf(stderr, "错误: %s 需要整数，收到 %s\n", flag, value);
    std::exit(1);
  }
  return v;
}

std::string read_governor() {
  std::ifstream in("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
  std::string s;
  if (in && std::getline(in, s) && !s.empty()) {
    return s;
  }
  return "unknown";
}

std::string now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

long long sys_time_ns() {
  rusage ru{};
  if (getrusage(RUSAGE_SELF, &ru) != 0) {
    return 0;
  }
  return static_cast<long long>(ru.ru_stime.tv_sec) * 1'000'000'000LL +
         static_cast<long long>(ru.ru_stime.tv_usec) * 1000LL;
}

} // namespace

Config parse_args(int argc, char** argv) {
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char* flag) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "错误: %s 缺少取值\n", flag);
        usage(argv[0]);
        std::exit(1);
      }
      return argv[++i];
    };

    if (arg == "--reps") {
      const long v = parse_long("--reps", need_value("--reps"));
      if (v < 1 || v > 100) {
        std::fprintf(stderr, "错误: --reps 必须在 1..100 之间\n");
        std::exit(1);
      }
      cfg.reps = static_cast<int>(v);
    } else if (arg == "--inner") {
      const long v = parse_long("--inner", need_value("--inner"));
      if (v < 1) {
        std::fprintf(stderr, "错误: --inner 必须 >= 1\n");
        std::exit(1);
      }
      cfg.inner = v;
      cfg.inner_set = true;
    } else if (arg == "--cpu") {
      const long v = parse_long("--cpu", need_value("--cpu"));
      if (v < -1) {
        std::fprintf(stderr, "错误: --cpu 必须 >= -1\n");
        std::exit(1);
      }
      cfg.cpu = static_cast<int>(v);
    } else if (arg == "--warmup") {
      const long v = parse_long("--warmup", need_value("--warmup"));
      if (v < 0) {
        std::fprintf(stderr, "错误: --warmup 必须 >= 0\n");
        std::exit(1);
      }
      cfg.warmup = static_cast<int>(v);
    } else if (arg == "--verify") {
      cfg.verify = true;
    } else {
      std::fprintf(stderr, "错误: 未知参数 %s\n", arg.c_str());
      usage(argv[0]);
      std::exit(1);
    }
  }
  return cfg;
}

double median(std::vector<double> xs) {
  if (xs.empty()) {
    return 0.0;
  }
  std::sort(xs.begin(), xs.end());
  const size_t n = xs.size();
  if (n % 2 == 1) {
    return xs[n / 2];
  }
  return 0.5 * (xs[n / 2 - 1] + xs[n / 2]);
}

double percentile(std::vector<double> xs, double p) {
  if (xs.empty()) {
    return 0.0;
  }
  std::sort(xs.begin(), xs.end());
  const double rank = std::ceil(p * static_cast<double>(xs.size()));
  size_t idx = rank < 1.0 ? 0 : static_cast<size_t>(rank) - 1;
  if (idx >= xs.size()) {
    idx = xs.size() - 1;
  }
  return xs[idx];
}

std::string to_json(const Result& r) {
  std::ostringstream os;
  os << "{\"exp\":\"" << r.exp << "\",\"variant\":\"" << r.variant << "\"";
  os << ",\"reps\":" << r.reps << ",\"inner\":" << r.inner;
  os << ",\"samples_ns\":[";
  os << std::setprecision(17);
  for (size_t i = 0; i < r.samples_ns.size(); ++i) {
    if (i != 0) {
      os << ',';
    }
    os << r.samples_ns[i];
  }
  os << "],\"median_ns\":" << r.median_ns;
  os << ",\"min_ns\":" << r.min_ns;
  os << ",\"p90_ns\":" << r.p90_ns;
  os << ",\"sys_ns\":" << r.sys_ns;
  os << ",\"checksum\":\"0x" << std::hex << r.checksum << std::dec << "\"";
  os << ",\"cpu\":" << r.cpu;
  os << ",\"governor\":\"" << r.governor << "\"";
  os << ",\"ts\":\"" << r.ts << "\"";
  os << ",\"semantics\":\"" << r.semantics << "\"";
  if (r.verify) {
    os << ",\"verify\":true";
  }
  if (!r.skip.empty()) {
    os << ",\"skip\":\"" << r.skip << "\"";
  }
  os << "}";
  return os.str();
}

int run(const std::string& exp_id, const std::string& variant,
        const Config& cfg, Body body, long default_inner, Init init,
        const char* semantics) {
  Result r;
  r.exp = exp_id;
  r.variant = variant;
  r.reps = cfg.reps;
  r.inner = cfg.inner_set ? cfg.inner : default_inner;
  r.cpu = cfg.cpu;
  r.governor = read_governor();
  r.ts = now_iso8601();
  r.semantics = semantics == nullptr ? "" : semantics;
  r.verify = cfg.verify;

  auto emit = [](const Result& rr) {
    std::cout << to_json(rr) << '\n';
    std::cout.flush();
  };

  try {
    init();

    if (cfg.verify) {
      r.checksum = body();
      emit(r);
      return 0;
    }

    if (cfg.cpu >= 0 && !pin_current_thread(cfg.cpu)) {
      std::fprintf(stderr, "警告: 无法绑定到 CPU %d，继续运行\n", cfg.cpu);
    }

    for (int i = 0; i < cfg.warmup; ++i) {
      body();
    }

    uint64_t expected = 0;
    bool first = true;
    for (int rep = 0; rep < cfg.reps; ++rep) {
      const auto t0 = std::chrono::steady_clock::now();
      const long long s0 = sys_time_ns();
      uint64_t checksum = 0;
      for (long i = 0; i < r.inner; ++i) {
        checksum = body();
      }
      const long long s1 = sys_time_ns();
      const auto t1 = std::chrono::steady_clock::now();

      const double total_ns =
          std::chrono::duration<double, std::nano>(t1 - t0).count();
      r.samples_ns.push_back(total_ns / static_cast<double>(r.inner));
      r.sys_ns += static_cast<double>(s1 - s0);

      if (first) {
        expected = checksum;
        first = false;
      } else if (checksum != expected) {
        std::fprintf(stderr, "错误: checksum 在重复之间不一致\n");
        return 1;
      }
    }

    r.checksum = expected;
    r.sys_ns /= static_cast<double>(cfg.reps);
    r.median_ns = median(r.samples_ns);
    r.min_ns = *std::min_element(r.samples_ns.begin(), r.samples_ns.end());
    r.p90_ns = percentile(r.samples_ns, 0.9);

    emit(r);
    return 0;
  } catch (const Skip& e) {
    r.skip = e.what();
    emit(r);
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "错误: %s\n", e.what());
    return 1;
  }
}

} // namespace bench
