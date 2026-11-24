#pragma once

// 实验 2.1.1：哈希查表 vs 线性扫描。
//
// 两个变体共享同一份初始化逻辑，保证输入数据逐位一致，
// 唯一的差别只在查找结构（线性扫描 / unordered_map）。

#include <cstdint>
#include <random>
#include <vector>

constexpr int kN = 100000; // 表中条目数
constexpr int kQ = 2000;   // 查询数

struct Entry {
  uint32_t key;
  uint32_t value;
};

inline std::vector<Entry> g_table;
inline std::vector<uint32_t> g_queries;

// 确定性初始化：key 由 i 经「乘奇数 + 异或右移」得到，二者都是双射，
// 故 i 在 [0, kN) 内互不相同时 key 也互不相同。
inline void init_base() {
  g_table.clear();
  g_table.reserve(kN);
  for (int i = 0; i < kN; ++i) {
    uint32_t key = static_cast<uint32_t>(i) * 2654435761u;
    key ^= key >> 16;
    g_table.push_back(Entry{key, key >> 7});
  }

  g_queries.clear();
  g_queries.reserve(kQ);
  std::mt19937 rng(0x5EED1234u);
  for (int i = 0; i < kQ; ++i) {
    const uint32_t idx = rng() % static_cast<uint32_t>(kN);
    g_queries.push_back(g_table[idx].key);
  }
}

constexpr const char* kSemantics = "两种实现必须返回相同查询结果之和";
