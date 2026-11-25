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
    // 仅乘奇数时 key 的低 j 位只依赖 i 的低 j 位，
    // 低 16 位以 65536 为周期重复，而异或右移可消除这种结构（哈希表常用低位取桶）。
    // (i*c) mod 2^j = ((i mod 2^j) * c) mod 2^j
    //               W= (((i + 2^j) mod 2^j) * c) mod 2^j
    //    key & 3 以 2^3 为周期重复
    //    key & 7 以 2^4 为周期重复
    // 该映射自逆（y ^ (y >> 16) 即还原），是双射，故不破坏 key 的唯一性。
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
