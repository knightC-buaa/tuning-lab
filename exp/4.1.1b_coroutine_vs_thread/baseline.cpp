#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

#include <pthread.h>
#include <sys/resource.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kThreadStackBytes = 64 * 1024;  // 64 KB 栈
constexpr long kThreadMargin = 64;                    // 资源检查的安全余量

struct ThreadArg {
  int index;
};

// 线程句柄、参数与 attr 在 init() 准备并复用，body() 不做大块分配。
pthread_attr_t g_attr;
std::vector<pthread_t> g_threads;
std::vector<ThreadArg> g_args;

void* task_main(void* opaque) {
  const auto* arg = static_cast<const ThreadArg*>(opaque);
  std::this_thread::sleep_for(kTaskDelay);
  g_out[static_cast<std::size_t>(arg->index)] = task_value(arg->index);
  return nullptr;
}

std::string read_first_line(const std::string& path) {
  std::ifstream in(path);
  std::string line;
  if (in && std::getline(in, line)) {
    return line;
  }
  return {};
}

// 当前进程的线程数；读不到返回 -1。
long process_thread_count() {
  std::ifstream in("/proc/self/status");
  std::string line;
  while (std::getline(in, line)) {
    constexpr const char kPrefix[] = "Threads:";
    if (line.rfind(kPrefix, 0) == 0) {
      return std::strtol(line.c_str() + sizeof(kPrefix) - 1, nullptr, 10);
    }
  }
  return -1;
}

struct PidsLimit {
  bool found = false;
  bool unlimited = false;
  long max = 0;
  long current = -1;  // cgroup 当前任务数；未知为 -1
};

// 读取进程所在 cgroup 的 pids.max（cgroup v2 或 v1）。
// 文件不存在时返回 found=false，调用方跳过该检查。
PidsLimit read_cgroup_pids_limit() {
  std::vector<std::string> candidates;
  candidates.emplace_back("/sys/fs/cgroup/pids.max");  // v2 挂载根

  std::ifstream cg("/proc/self/cgroup");
  std::string line;
  while (std::getline(cg, line)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t first = line.find(':');
    const std::size_t last = line.rfind(':');
    if (first == std::string::npos || last == std::string::npos ||
        first == last) {
      continue;
    }
    const std::string controllers = line.substr(first + 1, last - first - 1);
    const std::string path = line.substr(last + 1);
    if (controllers.find("pids") != std::string::npos) {  // v1
      candidates.push_back("/sys/fs/cgroup/pids" + path + "/pids.max");
    }
    if (first == 1 && line[0] == '0') {  // v2: "0::/path"
      candidates.push_back("/sys/fs/cgroup" + path + "/pids.max");
    }
  }

  for (const std::string& p : candidates) {
    const std::string value = read_first_line(p);
    if (value.empty()) {
      continue;
    }
    PidsLimit r;
    r.found = true;
    if (value == "max") {
      r.unlimited = true;
    } else {
      char* end = nullptr;
      const long m = std::strtol(value.c_str(), &end, 10);
      if (end == value.c_str()) {
        continue;
      }
      r.max = m;
    }
    const std::size_t slash = p.rfind('/');
    if (slash != std::string::npos) {
      const std::string current =
          read_first_line(p.substr(0, slash) + "/pids.current");
      if (!current.empty()) {
        char* end = nullptr;
        const long c = std::strtol(current.c_str(), &end, 10);
        if (end != current.c_str()) {
          r.current = c;
        }
      }
    }
    return r;
  }
  return PidsLimit{};
}

// 只由 baseline 调用：确认还能再创建 N 个线程，否则 SKIP 而不是崩溃。
void check_task_capacity() {
  const long threads = process_thread_count();
  if (threads < 0) {
    return;  // 读不到自身线程数：交由 pthread_create 的失败路径处理
  }

  rlimit rl{};
  if (::getrlimit(RLIMIT_NPROC, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
    const long required = threads + kTasks + kThreadMargin;
    if (static_cast<unsigned long long>(rl.rlim_cur) <
        static_cast<unsigned long long>(required)) {
      throw bench::Skip("RLIMIT_NPROC 不足：需要 " + std::to_string(required) +
                        "，上限 " + std::to_string(rl.rlim_cur));
    }
  }

  const PidsLimit limit = read_cgroup_pids_limit();
  if (limit.found && !limit.unlimited) {
    const long current = std::max(limit.current, threads);
    const long required = current + kTasks + kThreadMargin;
    if (required > limit.max) {
      throw bench::Skip("cgroup pids.max 不足：需要 " +
                        std::to_string(required) + "，上限 " +
                        std::to_string(limit.max));
    }
  }
}

}  // namespace

void init() {
  init_base();
  check_task_capacity();

  const int attr_rc = ::pthread_attr_init(&g_attr);
  if (attr_rc != 0) {
    throw bench::Skip(std::string("pthread_attr_init 失败: ") +
                      std::strerror(attr_rc));
  }
  const int stack_rc =
      ::pthread_attr_setstacksize(&g_attr, kThreadStackBytes);
  if (stack_rc != 0) {
    ::pthread_attr_destroy(&g_attr);
    throw bench::Skip(std::string("pthread_attr_setstacksize 失败: ") +
                      std::strerror(stack_rc));
  }

  g_threads.resize(static_cast<std::size_t>(kTasks));
  g_args.resize(static_cast<std::size_t>(kTasks));
  for (int i = 0; i < kTasks; ++i) {
    g_args[static_cast<std::size_t>(i)].index = i;
  }
}

// 每个任务一个线程：创建、1 ms 等待与 join 都计入被测工作量。
uint64_t body() {
  int created = 0;
  for (int i = 0; i < kTasks; ++i) {
    const int rc = ::pthread_create(&g_threads[static_cast<std::size_t>(i)],
                                    &g_attr, &task_main,
                                    &g_args[static_cast<std::size_t>(i)]);
    if (rc != 0) {
      for (int j = 0; j < created; ++j) {
        ::pthread_join(g_threads[static_cast<std::size_t>(j)], nullptr);
      }
      throw bench::Skip(std::string("pthread_create 失败: ") +
                        std::strerror(rc));
    }
    ++created;
  }
  for (int i = 0; i < created; ++i) {
    ::pthread_join(g_threads[static_cast<std::size_t>(i)], nullptr);
  }

  const uint64_t sum = sum_out();
  bench::keep(sum);
  return sum;
}

BENCH_MAIN("4.1.1b_coroutine_vs_thread", "baseline", 2)
