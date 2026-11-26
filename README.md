# 性能优化对比实验（tuning-lab）

本仓库把《现代应用性能优化思路全景文档》中可在本机验证的优化点，实现为一对可独立运行的 C++ 程序：`baseline` 用普通写法，`optimized` 只改变该优化点对应的手段。两个变体只有优化手段不同，在 `--verify` 下必须输出相同校验和。

文档入口：需求来源与方法论 [性能优化概述.md](性能优化概述.md)，实施计划与接口契约 [plan.md](plan.md)，协作规则 [CLAUDE.md](CLAUDE.md)。**本 README 是项目事实（当前状态、构建与运行、判定规则、运行环境）的权威来源。**

## 当前状态

已实现 4 个实验（覆盖需求文档中的 3 个叶子点：2.1.1、4.1.1、4.2.1），当前 CTest 16/16 通过：

| 实验 | 对比内容 | 默认 inner_reps（baseline / optimized） | 语义备注 |
|---|---|---|---|
| `2.1.1_hash_vs_scan` | 哈希查表 vs 线性扫描 | 1 / 1 | 两种实现必须返回相同查询结果之和 |
| `4.1.1a_async_overlap` | 逐个等待响应 vs 先发全部再统一收齐 | 2 / 128 | 服务端是同进程单线程事件循环，1 ms 为模拟处理延迟；只测等待重叠，不测线程/协程管理成本 |
| `4.1.1b_coroutine_vs_thread` | 每任务一个 OS 线程 vs 每任务一个协程 | 2 / 128 | 4096 个任务、每任务等待 1 ms；测任务管理成本，不测代码可读性 |
| `4.2.1_zero_copy` | `pread` + `write` vs `sendfile` | 4 / 4 | 同一份 256 MB 文件经 loopback TCP 发送；loopback 仍走内核协议栈 |

计划共 42 个叶子点：35 个可测、1 个条件可测、6 个不可测。其余实验尚未实现，完整清单与任务分解见 [plan.md](plan.md)。

## 运行环境与复现

参考测量环境：i5-12490F（6 物理核 / 12 逻辑核）、g++ 15.2、governor `powersave` 的 Linux。这只是解释已有结果的背景，**不是运行前提**；完整的硬件假设见 [plan.md](plan.md) 的「Global Constraints」。

换机复现时（依赖项见下一节）：

1. 先运行 `bash scripts/check_env.sh`，确认 CPU 拓扑、fd/内存上限与 `liburing.h` 状态。
2. `run_all.sh` 固定用 `--cpu 2`，部分实验内部把服务线程/sink 绑到 CPU 4；换机前确认这些逻辑 CPU 存在，且不要让 `--cpu` 与实验内部绑核落在同一个物理核上。
3. 容量不足（内存、fd、线程数、缺失内核特性）的实验会自动 SKIP，不会硬失败。

磁盘类实验把数据放在 `$HOME/.cache/perf-opt-overview/`，需要真实磁盘；如果 `/tmp` 是 tmpfs，不要改放到 `/tmp`，否则 `fsync` 成本会把结论做反。临时文件使用 `std::filesystem::temp_directory_path()`，实验结束清理。

## 构建与运行

依赖：Linux/POSIX（pthread、socket、`/sys`）、支持 C++23 的编译器、CMake ≥ 3.25、Python 3（仅报告与校验脚本，只用标准库）。

```bash
# 查看测量环境（CPU、governor、perf、内存、fd、liburing）
bash scripts/check_env.sh

# 一条命令完成：构建 → CTest → 测量 → 报告
bash scripts/run_all.sh          # 默认 3 轮
RUNS=1 bash scripts/run_all.sh   # 快速验证
```

`run_all.sh` 以 `--reps 5 --cpu 2` 运行每个实验，产物位于 `results/<timestamp>/`：

- `raw.jsonl`：每次运行一行 JSON，字段包括 `exp`、`variant`、`samples_ns`、`median_ns`、`p90_ns`、`sys_ns`、`checksum`、`semantics` 等。
- `summary.md`：汇总表、判定与几何平均。

也可以分步手动执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

只构建、运行单个实验：

```bash
cmake -S . -B build -DEXP_FILTER=2.1.1
cmake --build build -j"$(nproc)"
build/exp/2_1_1_hash_vs_scan_baseline --reps 5 --cpu 2
build/exp/2_1_1_hash_vs_scan_baseline --verify
```

可执行文件位于 `build/exp/<slug>_<variant>`，目标名把实验 id 中的 `.` 换成 `_`。参数：

| 参数 | 含义 |
|---|---|
| `--reps N` | 重复次数，1..100 |
| `--inner-reps N` | 每次 body 的内层重复次数；省略时用实验设定的默认值 |
| `--cpu N` | 绑定到指定逻辑 CPU；`-1` 表示不绑核 |
| `--warmup N` | 预热次数 |
| `--verify` | 只跑一次用于校验和比对，不输出计时 |

退出码：`0` 成功，`1` 参数或校验错误，`2` 环境不满足（SKIP）。

## 结果判定

`report.py` 用两个变体的每轮样本计算：

```text
speedup = baseline median_ns / optimized median_ns
```

| 判定 | 条件 | 含义 |
|---|---|---|
| SIGNIFICANT | `speedup >= 1.10` 且 baseline p10 > optimized p90 | 本次运行下差异超出噪声 |
| REGRESSION | `speedup <= 0.90` 且 optimized p10 > baseline p90 | 优化反而更慢，如实保留 |
| INCONCLUSIVE | 其余情况 | 本次无法区分，需增加轮次或减小噪声 |
| SKIP | 环境不满足（内存、fd、内核特性） | 不是失败 |
| INVALID | 同一实验内 checksum 不一致 | 实现有 bug，必须修复后重跑 |

- 几何平均只统计 SIGNIFICANT / INCONCLUSIVE / REGRESSION。
- `median_ns` / `min_ns` / `p90_ns` 是 per-body 口径；`sys_ns` 是 per-rep 口径（累加后除以 reps，不除以 inner_reps），不同 `inner_reps` 之间不可直接比较。
- 加速比只对测量时的机器、编译器和参数有效；换机必须重测，不要与旧结果或其他机器比较。

## 不生成实验的 6 个点

这些点依赖本机不具备的条件，按计划不为它们生成空壳实验：

| 点 | 原因 |
|---|---|
| 3.5.2 NUMA | 单节点机器，无法对照跨节点行为 |
| 4.4.1 单机内核旁路 | 无对应设备 |
| 4.4.2 用户态 I/O 路径 | 无对应设备 |
| 6.2.1 RDMA | 无 RDMA 设备 |
| 6.2.2 内核旁路网络 | 无 DPDK 等设备 |
| 6.3.2 在网计算 | 无可编程交换机 |

4.3.2 io_uring 是条件可测点，但尚未实现；没有 `liburing.h` 的机器上，即使补上最小封装，实验也只会以 SKIP 结束。

## 可选的 perf 佐证

计划中的 perf 佐证层（[plan.md](plan.md) Task 2B）**尚未实现**：当前 `report.py` 不采集、也不消费 perf 数据，所有判定只使用墙钟与 checksum。仓库脚本不会调用 `sudo`，也不会修改 `perf_event_paranoid`，因此不需要为 perf 做任何配置。

## 如何新增实验

1. 新建 `exp/<id>/`，包含三个文件：
   - `setup.hpp`：`inline` 共享数据、`inline void init_base()`、`constexpr const char* kSemantics`；
   - `baseline.cpp` / `optimized.cpp`：各自定义 `void init()` 与 `uint64_t body()`，末尾调用 `BENCH_MAIN("<id>", "<variant>", <default_inner_reps>)`。
2. 在 `exp/CMakeLists.txt` 增加一行 `add_experiment(<id>)`——这里是实验清单的唯一来源。
3. 保证两个变体的 checksum 相同：`body()` 的返回值必须进入校验和，关键中间值用 `bench::keep()` 防止被编译器消除；不得在 `body()` 内做数据初始化。
4. 选择 `default_inner_reps`，使每轮 body 落在 100–500 ms；baseline/optimized 的编译选项差异用 `BASELINE_FLAGS` / `OPTIMIZED_FLAGS` 写明。

`add_experiment` 会自动注册三个 CTest：两个 `--verify` 测试和一个由 `scripts/verify_exp.py` 完成的等价性测试。

## 测量注意事项

- governor 不是 `performance` 时频率会漂移；`run_all.sh` 每轮反转 A/B 顺序以抵消其影响。
- 实验默认只使用物理核并限制线程数；换机时以 `bench::physical_cores()` 的实际结果为准。4.1.1b 的 4096 线程是实验自变量，属于例外。
- 负收益、无差异、噪声过大都如实进入报告，不通过反复调参制造正收益。
- `results/` 与 `build/` 不入库。

## 目录结构

```text
common/       计时、绑核、黑盒与 JSON 输出
cmake/        add_experiment() 构建宏
exp/<id>/     实验：setup.hpp + baseline.cpp + optimized.cpp
scripts/      check_env.sh、run_all.sh、verify_exp.py、report.py
tests/        单元测试与测试夹具
results/      运行产物
```

