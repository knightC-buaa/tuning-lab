#include "common/cpu.hpp"
#include "test_util.hpp"

#include <cstdio>
#include <unistd.h>

int main() {
  const auto cores = bench::physical_cores();
  CHECK(!cores.empty());

  const long online = sysconf(_SC_NPROCESSORS_ONLN);
  CHECK(online > 0);
  for (int cpu : cores) {
    CHECK(cpu >= 0);
    CHECK(cpu < online);
  }

  if (cores.size() != 6) {
    std::printf("test_pinning: 本机物理核数=%zu（预期 6，允许换机器）\n",
                cores.size());
  }

  CHECK(bench::pin_current_thread(cores.front()));
  CHECK(bench::current_cpu() == cores.front());

  std::puts("test_pinning: all checks passed");
  return 0;
}
