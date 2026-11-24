#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

void init() { init_base(); }

// 对每个 query 线性扫描全表，找到后累加其 value。
uint64_t body() {
  uint64_t sum = 0;
  for (const uint32_t q : g_queries) {
    for (const Entry& e : g_table) {
      if (e.key == q) {
        sum += e.value;
        break;
      }
    }
  }
  bench::keep(sum);
  return sum;
}

BENCH_MAIN("2.1.1_hash_vs_scan", "baseline", 1)
