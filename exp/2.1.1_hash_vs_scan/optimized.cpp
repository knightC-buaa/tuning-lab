#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

#include <unordered_map>

namespace {

// 索引只在 init() 中构建一次，绝不进入 body() 的计时区间。
std::unordered_map<uint32_t, uint32_t> g_index;

} // namespace

void init() {
  init_base();
  g_index.reserve(kN);
  for (const Entry& e : g_table) {
    g_index.emplace(e.key, e.value);
  }
}

// 对每个 query 直接查哈希表并累加 value。
uint64_t body() {
  uint64_t sum = 0;
  for (const uint32_t q : g_queries) {
    sum += g_index.find(q)->second;
  }
  bench::keep(sum);
  return sum;
}

BENCH_MAIN("2.1.1_hash_vs_scan", "optimized", 1)
