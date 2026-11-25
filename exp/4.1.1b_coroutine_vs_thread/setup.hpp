#pragma once

// 实验 4.1.1b：协程 vs 线程的任务成本。
//
// N 个并发任务，每个任务等待 1 ms 后写出与调度时机无关的确定性结果
// out[i] = i * 2654435761。baseline 每个任务一个 64 KB 栈的 OS 线程，
// optimized 每个任务一个协程、由单线程事件循环统一驱动。
//
// 计时口径：任务（线程/协程）的创建、等待与回收都在 body() 内，是被测
// 工作量；out 数组只在 init() 分配，body() 只写结果。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr int kTasks = 4096;                       // 并发任务数
constexpr auto kTaskDelay = std::chrono::milliseconds(1);  // 每个任务的等待时长
constexpr uint64_t kTaskMultiplier = 2654435761u;  // 结果乘数

// 所有任务的结果；init() 分配，body() 只写。
inline std::vector<uint64_t> g_out;

// 任务 index 的确定性结果：只依赖 index，与时间、调度无关。
inline uint64_t task_value(int index) {
  return static_cast<uint64_t>(static_cast<uint64_t>(index) * kTaskMultiplier);
}

// 校验和：全部结果之和。
inline uint64_t sum_out() {
  uint64_t sum = 0;
  for (const uint64_t v : g_out) {
    sum += v;
  }
  return sum;
}

// 分配结果数组；两个变体共用，保证输入一致。
inline void init_base() {
  g_out.assign(static_cast<std::size_t>(kTasks), 0);
}

constexpr const char* kSemantics =
    "两变体的任务数（4096）、每任务等待时长（1 ms）、返回值完全一致；"
    "baseline 每任务一个 OS 线程，optimized 每任务一个协程跑在单线程事件循环"
    "上；本实验测任务管理成本，不测代码可读性。";
