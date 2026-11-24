# 性能优化对比实验仓库 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为《现代应用性能优化思路全景文档》中每个可在本机验证的优化点，提供一对「普通实现 vs 优化实现」的最小 C++ 实验，并给出可复现的加速比。

**Architecture:** 单个 CMake 仓库。`common/` 提供唯一一份计时、绑核、校验和与 JSON 输出代码；每个实验目录只放 `setup.hpp`（共享数据与校验和来源）、`baseline.cpp`、`optimized.cpp`。构建产出两个独立可执行文件，CTest 先验证两者校验和一致，`scripts/run_all.sh` 交替运行并生成汇总表。

**Tech Stack:** C++23、GCC 15.2、CMake 4.2、POSIX threads/sockets、Python 3.14（仅报告与校验脚本，只用标准库）。

**Spec:** `性能优化概述.md`（需求来源）；本文件同时承担设计说明，实现偏离时以本文件为准并回头修正。

---

## Global Constraints

- 编译器 `g++ 15.2`，标准 `-std=c++23`。目录级用 `add_compile_options(-O2 -g -Wall -Wextra)`，线程用 `Threads::Threads` 链接。每个实验可追加 `target_compile_options`，其顺序在目录级选项之后，因此 `-O0` 能覆盖 `-O2`；该顺序在 Task 4 的构建检查中验证。baseline 与 optimized 的选项差异必须在该实验规格中写明。
- 本机无 AVX-512，只允许 AVX2/FMA；SIMD 实验显式使用 `<immintrin.h>`。
- 主计时只用 `std::chrono::steady_clock`。perf 计数器只作可选佐证：缺失、权限不足或解析失败时，主流程与主结论必须照常完成，报告对应列显示 `N/A`。
- `scripts/perf_check.sh` 只检测和采集，不调用 sudo、不修改 `kernel.perf_event_paranoid` 或 governor。本机默认 `perf_event_paranoid=4`；用户可自行执行 `sudo sysctl kernel.perf_event_paranoid=2` 启用，是否启用由用户决定。
- 所有 checksum 用整数运算；禁止浮点累加进入 checksum。不同求和顺序必须得到相同结果，因此矩阵乘等实验使用整数矩阵。
- 每个实验必须能在 `--verify` 下独立退出 0。环境不满足时抛 `bench::Skip`，退出码 2，CTest 记为 SKIP。
- 线程数 ≤ 6，只使用物理核 `{0,2,4,6,8,10}`，不得使用超线程兄弟核。
- 单实验常驻内存 ≤ 1.2 GB（3.3.2 为 1 GB，其余更小）。
- 单次重复 100–500 ms；必要时用 `--inner` 放大。全套 3 轮必须在 15 分钟内跑完。
- 负收益、无差异、噪声过大都必须如实进入报告。除修正实现错误外，不得通过反复调参制造正收益。
- 无第三方依赖。4.3.2 是唯一例外，见 Task 8 的依赖闸门。
- 需要真实磁盘行为的实验（4.2.1、4.3.2、6.4.2）把文件放在 `$HOME/.cache/perf-opt-overview/`（本机为 ext4/NVMe），**不得放 `/tmp`**：本机 `/tmp` 是 tmpfs，`fsync` 几乎免费，会把结论做反。其他临时文件用 `std::filesystem::temp_directory_path()/<exp_id>-<pid>/`，实验结束清理。
- 实验不得依赖当前工作目录；所有路径从 `getenv("HOME")` 或 `std::filesystem` 推导，保证 CTest、`run_all.sh`、手动运行三种方式行为一致。

## Review Focus

- **编译器消除被测循环**：每个 `body()` 的返回值必须参与 checksum，关键中间值经 `bench::keep()`。风险最高的实验是 2.2.1、2.2.2、2.2.3、2.3.2、3.4.2。
- **频率漂移造成假差异**：本机 governor 是 `powersave`。`run_all.sh` 每轮反转 A/B 顺序，报告用中位数与 IQR，区间重叠即标 INCONCLUSIVE。
- **有意的语义差异被误读为等价**：4.3.1 缓存 pid、5.2.2 各版本内容相同、6.4.2 异步 ack。每个实验的 `setup.hpp` 用 `kSemantics` 写明，报告原样输出。
- **资源超限**：3.3.2 申请 1 GB，4.1.2 创建 10k socketpair。`init()` 必须检查可用内存与 fd 上限，不满足则 SKIP 而不是崩溃。
- **多线程实验用错核**：机器是 6 物理核 + 超线程。`bench::physical_cores()` 每对兄弟只取一个，线程数上限 6。
- **perf 佐证缺失或被误用**：主结论只由墙钟与 checksum 决定；perf 列不可用时显示 `N/A`。I/O 与等待型实验不得用 `cycles` 替代墙钟，`task-clock` 也不等于墙钟。

---

## 范围：42 个叶子点的取舍

修订后的文档把原 2.2.1 拆为 2.2.1 指令级并行、2.2.2 分支预测、2.2.3 循环结构，叶子点从 40 增至 42。

| 处理 | 数量 | 点 |
|---|---|---|
| 本机可实测 | 35 | 2.1.1–2.3.2、3.1.1–3.5.1、4.1.1–4.3.1、5.1.1–5.4.2、6.1.1–6.1.2、6.3.1、6.4.1、6.4.2 |
| 条件可测 | 1 | 4.3.2 io_uring（缺 `liburing.h`，需最小封装，见 Task 8） |
| 本机不可测 | 6 | 3.5.2 NUMA（单节点）、4.4.1 单机内核旁路（无设备）、4.4.2 用户态 I/O 路径（无设备）、6.2.1 RDMA（无设备）、6.2.2 内核旁路网络（无设备）、6.3.2 在网计算（无设备） |

不可测的 6 个点在 `README.md` 中单独说明原因，不生成空壳实验。

### 全部实验一览

| 任务 | 实验 |
|---|---|
| Task 3 | 2.1.1 |
| Task 4 | 2.1.2、2.2.1、2.2.2、2.2.3、2.2.4、2.3.1、2.3.2 |
| Task 5 | 3.1.1、3.1.2、3.2.1、3.2.2 |
| Task 6 | 3.3.1、3.3.2、3.4.1、3.4.2、3.5.1 |
| Task 7 | 4.1.1、4.1.2、4.2.1、4.2.2、4.3.1 |
| Task 8 | 4.3.2（条件） |
| Task 9 | 5.1.1、5.1.2、5.2.1、5.2.2 |
| Task 10 | 5.3.1、5.3.2、5.4.1、5.4.2 |
| Task 11 | 6.1.1、6.1.2 |
| Task 12 | 6.3.1、6.4.1、6.4.2 |

Task 1 与 Task 2 是基础设施，必须先完成；Task 3 是试点，验证整条流水线；Task 4–12 内部相互独立，可按顺序执行，也可在 Task 3 通过后并行。

---

## File Structure

```
perf-opt-overview/
├── 性能优化概述.md              # 需求来源，不改动
├── plan.md                      # 本文件
├── CMakeLists.txt               # 顶层：C++23、perfbench 库、子目录
├── CMakePresets.json            # release 预设
├── .clangd                      # 指向 build/compile_commands.json
├── README.md                    # 构建、运行、结果解读、不可测项
├── .gitignore                   # build/、results/
├── common/
│   ├── bench.hpp                # Config/Result/接口声明、BENCH_MAIN
│   ├── bench.cpp                # 计时、warmup、JSON 输出、getrusage
│   ├── cpu.hpp / cpu.cpp        # physical_cores、pin_current_thread、current_cpu
│   └── black_box.hpp            # keep()、clobber()
├── cmake/
│   └── AddExperiment.cmake      # add_experiment() 宏
├── exp/
│   ├── CMakeLists.txt           # 唯一实验清单（SSOT）
│   └── <id>/
│       ├── setup.hpp            # 共享数据、init_base()、kSemantics
│       ├── baseline.cpp         # uint64_t body() + BENCH_MAIN
│       └── optimized.cpp        # uint64_t body() + BENCH_MAIN
├── scripts/
│   ├── check_env.sh             # 打印 CPU/governor/perf/RAM/fd/liburing
│   ├── verify_exp.py            # 跑两个 --verify，比较 checksum
│   ├── perf_check.sh            # 可选：perf stat / perf c2c 佐证采集
│   ├── run_all.sh               # 构建、CTest、交替跑、调用报告
│   └── report.py                # JSONL → Markdown 汇总表（可合并 perf.jsonl）
├── tests/
│   ├── test_util.hpp            # 断言宏
│   ├── test_bench.cpp           # 统计、JSON、参数解析
│   ├── test_pinning.cpp         # 物理核发现与绑定
│   ├── test_report.py           # 报告判定规则
│   ├── test_verify.py           # 校验脚本
│   ├── test_perf.py             # perf 采集与报告列
│   └── fixtures/                # 假的 --verify 脚本、假的 perf 输出
├── results/
│   └── .gitkeep
└── build/                       # 不提交
```

---

## 核心接口（所有实验共用）

实现以仓库文件为准；本节只记录契约，不复制代码正文。修改接口时必须同步所有已实现实验。

| 接口 | 位置 | 契约 |
|---|---|---|
| `bench::Config` | `common/bench.hpp` | `reps`、`inner`、`inner_set`、`cpu`、`warmup`、`verify`。 |
| `bench::Result` | `common/bench.hpp` | 实验 id、variant、reps/inner、samples_ns、median/min/p90、sys_ns、checksum、cpu、governor、ts、semantics、verify、skip。 |
| `bench::Skip` | `common/bench.hpp` | 环境不满足时抛出；`run` 捕获后输出 skip JSON 并返回 2。 |
| `bench::parse_args` | `common/bench.hpp/.cpp` | 解析 `--reps`、`--inner`、`--cpu`、`--warmup`、`--verify`；非法值打印错误并退出 1。 |
| `bench::run` | `common/bench.hpp/.cpp` | `run(exp, variant, cfg, body, default_inner, init, semantics)`；返回 0 成功、1 参数/校验错误、2 SKIP；正常路径计时并输出一行 JSONL。 |
| `BENCH_MAIN` | `common/bench.hpp` | 生成 `main()`；依赖每个实验定义的 `init()`、`body()`、`kSemantics`。 |
| `bench::median` / `percentile` | `common/bench.hpp/.cpp` | 排序后取中位数；percentile 用最近秩法。 |
| `bench::to_json` | `common/bench.hpp/.cpp` | 手工拼装 JSONL；checksum 以 `0x` 开头；`verify` / `skip` 按需出现。 |
| `bench::physical_cores` | `common/cpu.hpp/.cpp` | 从 `thread_siblings_list` 每对兄弟取一个，升序；读不到 sysfs 时退化。 |
| `bench::pin_current_thread` | `common/cpu.hpp/.cpp` | `sched_setaffinity`，成功返回 true。 |
| `bench::current_cpu` | `common/cpu.hpp/.cpp` | `sched_getcpu()`。 |
| `bench::keep` / `clobber` | `common/black_box.hpp` | 防止编译器消除被测计算。 |
| `add_experiment(id)` | `cmake/AddExperiment.cmake` | 生成 `<slug>_baseline` / `<slug>_optimized`，注册 `--verify` 与等价性测试，追加 `exp_index.txt`；支持 `BASELINE_FLAGS`、`OPTIMIZED_FLAGS`、`EXP_FILTER`。 |

约定：

- 每个实验的 `setup.hpp` 用 `inline` 变量存放共享数据，提供 `inline void init_base()`，并定义 `constexpr const char* kSemantics = "..."`（没有语义差异时写 `""`）。
- 每个 `baseline.cpp` / `optimized.cpp` 各自定义 `void init()`（内部先调用 `init_base()`，需要时再做本变体的准备），并定义 `uint64_t body()`，末尾调用 `BENCH_MAIN("<id>", "<variant>", <default_inner>)`。
- `body()` 只能读取 `init()` 建好的数据，不得在计时区内做数据初始化。
- baseline 与 optimized 的 checksum 必须相同；等价性由 `scripts/verify_exp.py` 在 CTest 中比较，不另设独立 oracle。
- `physical_cores()` 本机应返回 `{0,2,4,6,8,10}`。

### 构建与运行约定

- 目标名把实验 id 中的 `.` 换成 `_`，例如 `2_1_1_hash_vs_scan_baseline`。
- 可执行文件位于 `build/exp/<slug>_<variant>`。
- `add_experiment` 的 `BASELINE_FLAGS` / `OPTIMIZED_FLAGS` 追加在全局 `-O2` 之后，后者覆盖前者。
- `exp/CMakeLists.txt` 是实验清单的唯一来源；新增实验只改这里。
- `EXP_FILTER` 允许只构建某个实验，例如 `-DEXP_FILTER=2.1.1`。

---

## Task 1: 仓库骨架与计时框架（已完成）

**状态：** 已完成（2026-11-24，commit `2be2efc`）。实现以仓库文件为准，本节不再复述源码：

- 构建：`CMakeLists.txt`、`CMakePresets.json`、`.clangd`
- 计时与绑核：`common/bench.hpp`、`common/bench.cpp`、`common/cpu.hpp`、`common/cpu.cpp`、`common/black_box.hpp`
- 构建宏：`cmake/AddExperiment.cmake`
- 测试：`tests/CMakeLists.txt`、`tests/test_util.hpp`、`tests/test_bench.cpp`、`tests/test_pinning.cpp`

**契约：** 见「核心接口」表。

**验证：** `ctest --test-dir build --output-on-failure -R 'bench_unit|pinning'` → 2/2 PASS；`physical_cores()` 返回 `{0,2,4,6,8,10}`。

**Rulings：** 见 `.superpowers/sdd/plan/progress.md`（`Config::inner_set`、`exp_index.txt` 截断、额外测试覆盖、`compile_commands`）。

---

## Task 2: 运行器、校验脚本与报告

**Files:**
- Create: `scripts/verify_exp.py`, `scripts/report.py`, `scripts/run_all.sh`, `scripts/check_env.sh`
- Create: `tests/test_report.py`, `tests/test_verify.py`, `tests/fixtures/fake_ok_baseline.sh`, `tests/fixtures/fake_ok_optimized.sh`, `tests/fixtures/fake_bad_optimized.sh`（三个 fixture 都要 `chmod +x`）
- Modify: `tests/CMakeLists.txt`（加入两个 Python 测试）

**Interfaces:**
- Consumes: Task 1 的 JSONL 输出格式。
- Produces: `verify_exp.py <baseline_bin> <optimized_bin>`（退出 0/1/2）；`report.py <jsonl...>`（stdout 输出 Markdown）；`run_all.sh`（生成 `results/<timestamp>/raw.jsonl` 与 `summary.md`）。

- [ ] **Step 1: 先写失败的报告测试**

`tests/test_report.py` 用 `unittest` 和 `tempfile`，构造 JSONL 后调用 `report.py`，断言：

- baseline 中位数 200、optimized 中位数 100 → 表格含 `2.00x` 与 `SIGNIFICANT`
- 两组样本区间大幅重叠（如 100±40 vs 105±40）→ `INCONCLUSIVE`
- optimized 中位数 250 → `REGRESSION`
- 同一实验内 checksum 不一致 → `INVALID`
- 一行 `{"exp":"4.3.2_io_uring","variant":"optimized","skip":"缺少 liburing"}` → `SKIP`
- optimized 中位数 50 µs、baseline 1 s → 输出 `≥` 标记

`tests/test_verify.py` 用三个 shell 脚本 fixture：前两个输出相同 checksum，第三个输出不同 checksum；断言退出码分别为 0 和 1。

- [ ] **Step 2: 运行测试，确认失败**

```bash
python3 -m unittest discover -s tests -p 'test_report.py' -v
python3 -m unittest discover -s tests -p 'test_verify.py' -v
```

Expected: FAIL，`scripts/report.py` 与 `scripts/verify_exp.py` 不存在。

- [ ] **Step 3: 实现 `scripts/report.py`**

规则：

- 解析每个 JSONL 行；`verify` 行忽略，`skip` 行记为 SKIP，`checksum` 在同一实验内有多种取值记 INVALID。
- 按 `exp` 分组，variant 内把所有运行的 `median_ns` 取中位数；同时算 p10/p90。
- `speedup = baseline_median / optimized_median`。
- 判定：
  - `SIGNIFICANT`：`speedup >= 1.10` 且 `baseline_p10 > optimized_p90`
  - `REGRESSION`：`speedup <= 0.90` 且 `optimized_p10 > baseline_p90`
  - 其余 `INCONCLUSIVE`
- 若 `optimized_median < 100e3` ns（100 µs），speedup 前加 `≥`。
- 按实验 id 首位数字分到「计算 / 存储 / I/O / 并发 / 分布式」五组，组内输出 Markdown 表：`实验 | baseline | optimized | 加速比 | 判定 | 备注`。
- 备注列输出 JSON 行里的 `semantics` 字段；没有则留空。
- 最后输出每组的几何平均加速比与「SIGNIFICANT / INCONCLUSIVE / REGRESSION / SKIP / INVALID」计数。

- [ ] **Step 4: 实现 `scripts/verify_exp.py`**

- 依次运行两个可执行文件，参数 `--verify`。
- 解析 stdout 最后一行 JSON。
- 任一进程退出码 2 → 打印 SKIP 并退出 2；退出码非 0 → 打印 stderr 并退出 1。
- 两个 checksum 不同 → 打印双方值并退出 1；相同 → 打印值并退出 0。

- [ ] **Step 5: 实现 `scripts/run_all.sh` 与 `scripts/check_env.sh`**

`run_all.sh` 的行为（实现见 `scripts/run_all.sh`）：

- 提升 fd 上限；配置并构建 Release；先跑 CTest 作为正确性闸门。
- 读取 `build/exp_index.txt`，做 `RUNS` 轮（默认 3）；每轮反转 baseline/optimized 顺序，抵消频率漂移。
- 每次运行 `--reps 5 --cpu 2`，把 JSONL 追加到 `results/<timestamp>/raw.jsonl`；退出码 2 视为 SKIP，其他非零立即失败。
- 按 Task 2B 的开关采集 `perf.jsonl`，最后调用 `report.py` 生成 `summary.md`。

`check_env.sh` 只打印不修改：CPU 型号、物理核、governor、`perf_event_paranoid`、THP、可用内存、`ulimit -n`、`liburing.h` 是否存在。若 governor 不是 `performance`，提示用户可手动执行 `sudo cpupower frequency-set -g performance`，脚本本身不调用 sudo。

- [ ] **Step 6: 运行测试，确认通过**

```bash
python3 -m unittest discover -s tests -p 'test_report.py' -v
python3 -m unittest discover -s tests -p 'test_verify.py' -v
bash scripts/check_env.sh
```

Expected: 两个测试套件 PASS；`check_env.sh` 正常打印且不修改任何系统设置。

- [ ] **Step 7: 提交**

```bash
git add scripts tests exp/CMakeLists.txt
git commit -m "feat: 加入运行器、校验与汇总报告"
```

---

## Task 2B: 可选 perf 佐证层

本任务不改变主结论：墙钟仍是唯一判定依据，perf 只增加解释性列。`perf_check.sh` 不调用 sudo，不修改任何系统设置。

**Files:**
- Create: `scripts/perf_check.sh`
- Create: `tests/test_perf.py`, `tests/fixtures/fake_perf_ok.sh`, `tests/fixtures/fake_perf_blocked.sh`
- Modify: `scripts/report.py`（新增 `--perf` 参数与两列）
- Modify: `scripts/run_all.sh`（采集 `perf.jsonl` 并传给报告）
- Modify: `scripts/check_env.sh`（打印 perf 是否可用及启用命令）
- Modify: `tests/CMakeLists.txt`（注册 `test_perf.py`）

**Interfaces:**
- Consumes: Task 1 的 `build/exp/<slug>_<variant>`；Task 2 的 `report.py` 与 `run_all.sh`。
- Produces:
  - `perf_check.sh probe`：退出 0 表示 perf 可用，1 表示不可用。
  - `perf_check.sh <exp> <variant>`：输出一行 JSON，字段为 `exp`、`variant`、`perf_available`、`cycles`、`instructions`、`cache_misses`、`branch_misses`、`context_switches`；3.4.1 额外带 `c2c_hitm` 或 `c2c_available:false`。
  - `report.py --perf <jsonl>`：在主表追加 `cache-misses (B→O)`、`branch-misses (B→O)` 两列，并追加一张 perf 佐证表。

**环境事实：**
- 本机默认 `kernel.perf_event_paranoid=4`，`perf stat -e cycles -- true` 退出 1，连 `task-clock` 和 `perf record` 都被拒绝。
- 用户手动执行 `sudo sysctl kernel.perf_event_paranoid=2` 后，普通用户即可按进程采集用户态计数器。
- 脚本只能探测，不能也不应该替你改这个设置。

- [ ] **Step 1: 先写失败的测试**

`tests/test_perf.py` 用 `unittest`，覆盖：

- `PERF_BIN=tests/fixtures/fake_perf_ok.sh scripts/perf_check.sh probe` 退出 0。
- `PERF_BIN=tests/fixtures/fake_perf_blocked.sh scripts/perf_check.sh probe` 退出 1。
- 用 fake perf 和 `EXP_BIN=tests/fixtures/fake_bin.sh` 调用 `perf_check.sh 2.1.1_hash_vs_scan baseline`，输出 JSON 中 `cycles=1000`、`cache_misses=300`、`branch_misses=40`。
- fake perf 的 `c2c report` 输出 `Load HITM : 1234` 时，3.4.1 的 JSON 带 `c2c_hitm=1234`。
- `report.py --perf` 对同一实验输出 `cache-misses (B→O)` 列，值为 `300→30`；没有 perf 数据的实验显示 `N/A`；判定结果不受 perf 列影响。

`tests/fixtures/fake_perf_ok.sh` 要支持 `stat -x, -o <file> -e ... -- <cmd>` 和 `c2c record/report` 两种调用；`fake_perf_blocked.sh` 一律退出 1；`fake_bin.sh` 直接退出 0。三个 fixture 都要 `chmod +x`。

- [ ] **Step 2: 运行测试，确认失败**

```bash
python3 -m unittest discover -s tests -p 'test_perf.py' -v
```

Expected: FAIL，`scripts/perf_check.sh` 不存在或 `report.py` 不认 `--perf`。

- [ ] **Step 3: 实现 `scripts/perf_check.sh`**

先 `chmod +x scripts/perf_check.sh`。要点：

- 顶部 `set -u`，不要用 `set -e`，因为 perf 失败是预期分支。
- `probe`：`"${PERF_BIN:-perf}" stat -e cycles -- true >/dev/null 2>&1`，用退出码判断。
- 采集：运行 `perf stat -x, -o <tmp> -e cycles,instructions,cache-misses,branch-misses,context-switches -- <bin> --reps 1 --warmup 0 --cpu 2`，统计写入临时文件，被测程序的 stdout/stderr 丢弃，避免污染。
- 用 `awk -F,` 解析 `value,unit,event,...` 的第三列；缺失字段输出 `null`。
- 若完整事件集失败（例如 `context-switches` 不受支持），退化为 `cycles,instructions,cache-misses,branch-misses` 再试一次；仍失败才标记不可用。
- 不可用时输出 `{"exp":"...","variant":"...","perf_available":false,"reason":"..."}` 并退出 0，**不要**让 `run_all.sh` 失败。
- `bin` 默认 `build/exp/<slug>_<variant>`，允许 `EXP_BIN` 覆盖（供测试使用）。
- 3.4.1 额外执行 `perf c2c record -o <tmpdata> -- <bin> ...` 与 `perf c2c report -i <tmpdata> --stats`；解析包含 `HITM` 的行的第一个整数，失败则写 `"c2c_available":false`。任何情况下都清理临时文件。

- [ ] **Step 4: 扩展 `scripts/report.py`**

- 新增 `--perf PATH`；文件不存在或为空时所有 perf 列显示 `N/A`。
- `load_perf` 建立 `(exp, variant) -> counters` 映射；忽略没有 `exp`/`variant` 的全局 `perf_available:false` 行。
- 主表追加两列，表头 `cache-misses (B→O)`、`branch-misses (B→O)`；值用 `fmt_count` 格式化（`1.2M`、`34.0K`、`1234`），两边都有数据时输出 `baseline→optimized`，否则 `N/A`。
- 追加「perf 佐证」表：实验、variant、cycles、instructions、cache-misses、branch-misses、context-switches、c2c_hitm。
- 明确打印一行：「perf 列仅作解释，不参与 SIGNIFICANT / REGRESSION / INCONCLUSIVE 判定」。
- 判定逻辑保持不变。

- [ ] **Step 5: 扩展 `scripts/run_all.sh` 与 `scripts/check_env.sh`**

`run_all.sh` 在主循环之后增加 perf 采集：

- 开关为 `PERF=auto`（默认）或 `PERF=0`；auto 时先 `perf_check.sh probe`。
- 可用时逐实验、逐 variant 追加到 `results/<timestamp>/perf.jsonl`；不可用时写一行全局 `{"perf_available":false,...}`。
- 若 `perf.jsonl` 非空，以 `--perf <path>` 传给 `report.py`。

`check_env.sh` 增加一行「perf stat：可用 / 不可用」；不可用时提示用户可手动执行 `sudo sysctl kernel.perf_event_paranoid=2`（临时启用，重启失效）。脚本本身不调用 sudo。

- [ ] **Step 6: 运行测试，确认通过**

```bash
python3 -m unittest discover -s tests -p 'test_perf.py' -v
PERF_BIN=tests/fixtures/fake_perf_ok.sh scripts/perf_check.sh probe && echo "fake probe OK"
bash scripts/perf_check.sh probe; echo "real probe exit=$?"
```

Expected: 测试 PASS；fake probe 退出 0；在本机未降低 paranoid 时 real probe 退出 1，这是预期行为，不是失败。

- [ ] **Step 7: 提交**

```bash
git add scripts/perf_check.sh scripts/report.py scripts/run_all.sh scripts/check_env.sh tests
git commit -m "feat: 可选 perf 佐证层"
```

---

## Task 3: 试点实验 2.1.1（哈希 vs 线性扫描）

**Files:**
- Create: `exp/2.1.1_hash_vs_scan/setup.hpp`
- Create: `exp/2.1.1_hash_vs_scan/baseline.cpp`
- Create: `exp/2.1.1_hash_vs_scan/optimized.cpp`
- Modify: `exp/CMakeLists.txt`（加入一行 `add_experiment(2.1.1_hash_vs_scan)`）

**Interfaces:**
- Consumes: Task 1 的 `BENCH_MAIN`、`bench::keep`；Task 2 的 `verify_exp.py`。
- Produces: 后续所有实验复用的 `setup.hpp` 结构模板。

- [ ] **Step 1: 写 `setup.hpp`**

内容：

- `constexpr int kN = 100000; constexpr int kQ = 2000;`
- `struct Entry { uint32_t key; uint32_t value; };`
- `inline std::vector<Entry> g_table; inline std::vector<uint32_t> g_queries;`
- `inline void init_base()`：固定种子 `0x5EED1234` 生成互不相同的 key（`key = i * 2654435761u` 后打散），value 取 `key >> 7`；queries 从表中随机取 key。
- `constexpr const char* kSemantics = "两种实现必须返回相同查询结果之和";`

`baseline.cpp`：定义 `void init() { init_base(); }`；`uint64_t body()` 对每个 query 线性扫描 `g_table`，返回累加和，最终对和调用 `bench::keep(sum)`；末尾 `BENCH_MAIN("2.1.1_hash_vs_scan", "baseline", 1)`。

`optimized.cpp`：定义 `void init() { init_base(); g_index.reserve(kN); for (auto& e : g_table) g_index.emplace(e.key, e.value); }`，其中 `std::unordered_map<uint32_t,uint32_t> g_index;` 是本文件内的静态变量；`uint64_t body()` 只查表并返回累加和。不要在 `body()` 内建表，否则建表时间进入计时。

- [ ] **Step 2: 注册实验并构建**

在 `exp/CMakeLists.txt` 加入实验 `2.1.1_hash_vs_scan`；无额外编译选项。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '2_1_1'
```

Expected: `2_1_1_hash_vs_scan_baseline`、`2_1_1_hash_vs_scan_optimized`、`2_1_1_hash_vs_scan_equivalence` 三个测试 PASS。

- [ ] **Step 3: 实测并校准规模**

```bash
./build/exp/2_1_1_hash_vs_scan_baseline --reps 5 --cpu 2
./build/exp/2_1_1_hash_vs_scan_optimized --reps 5 --cpu 2
```

Expected：两者 checksum 相同；baseline 单次 ≈ 100 ms；optimized 单次 ≈ 50 µs；加速比 ≥ 100x。若 baseline 超过 500 ms，把 `kN` 降到 50000；若 optimized 接近 100 µs 以下，把 `--reps` 提到 9 并在报告中保留 `≥` 标记。

若 `scripts/perf_check.sh probe` 成功，额外跑 `scripts/perf_check.sh 2.1.1_hash_vs_scan baseline` 和 `optimized`，确认输出含 `cycles`、`cache_misses`；probe 失败则跳过，这是预期行为。

- [ ] **Step 4: 提交**

```bash
git add exp/2.1.1_hash_vs_scan exp/CMakeLists.txt
git commit -m "feat: 试点实验 2.1.1 哈希 vs 线性扫描"
```

---

## Task 4: 计算效率（7 个实验）

**Files:**
- Create: `exp/2.1.2_pruning/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.2.1_ilp/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.2.2_branch_prediction/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.2.3_loop_fusion/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.2.4_simd_avx2/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.3.1_compiler_opt/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `exp/2.3.2_runtime_dispatch/{setup.hpp,baseline.cpp,optimized.cpp}`
- Modify: `exp/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 的 `setup.hpp` 模板与 `BENCH_MAIN`。
- Produces: 无新增公共接口。

### 2.1.2 `2.1.2_pruning`
- baseline：对 2000 万个 int32 全部执行 8 次乘加的整型多项式，再求和。
- optimized：数据中 7/8 为 0；跳过 0，只对非 0 元素计算，并把循环不变量外提。
- 校验和：固定数据求和，两者必须相同。
- 规模：N=20,000,000，`inner=1`。
- 预期：5–8x。
- 选项：两者都加 `-fno-tree-vectorize`，隔离剪枝效果。
- 注意：多项式必须依赖 `i`，避免编译器把整段常量折叠。

### 2.2.1 `2.2.1_ilp`
- baseline：4 条独立 LCG 链，每条 3200 万步，**依次**跑完一条再跑下一条。
- optimized：同一个循环里每轮更新 4 条链，暴露指令级并行。
- 校验和：4 条链最终值的 XOR，两者相同。
- 规模：4 × 32,000,000 步，`inner=1`。
- 预期：2–4x。
- 注意：不要用浮点；整数 LCG 不存在重结合问题。

### 2.2.2 `2.2.2_branch_prediction`
- baseline：对 6400 万个随机 int32 做 `if (x > 0) { sum += x; count++; }`；编译加 `-fno-if-conversion -fno-if-conversion2`，阻止 GCC 自动改成无分支。
- optimized：`sum += (x > 0) ? x : 0; count += (x > 0);`，编译不加这两个选项。
- 校验和：把 sum 与 count 组合成一个 uint64（例如 `sum ^ (uint64_t(count) << 32)`），两者相同。
- 规模：N=64,000,000，`inner` 调到单次约 200 ms。
- 预期：2–4x。
- 注意：数据随机且固定种子，保证分支不可预测；`semantics:` 注明两种实现计算结果相同。

### 2.2.3 `2.2.3_loop_fusion`
- baseline：对 3200 万个 int32 做两遍独立循环，第一遍求和，第二遍统计正数个数；两个循环之间调用 `bench::clobber()`，确保编译器不把它们融合。
- optimized：融合成一个循环，同时求和与计数。
- 校验和：`sum ^ (uint64_t(count) << 32)`，两者相同。
- 规模：N=32,000,000，`inner` 调到单次约 200 ms。
- 预期：1.5–2.5x（内存流量减半）。
- 注意：`bench::clobber()` 是为了隔离「源码层面的循环结构」这一变量；报告中注明 baseline 带编译器屏障。

### 2.2.4 `2.2.4_simd_avx2`
- baseline：标量循环 `sum += a[i] * b[i]`（uint32 环绕），编译选项 `-fno-tree-vectorize`。
- optimized：AVX2 `_mm256_mullo_epi32` + `_mm256_add_epi32`，末尾水平归约，编译选项 `-mavx2 -mfma`。
- 校验和：uint32 环绕和（加法在模 2^32 下可任意分组，两者必等）。
- 规模：N=2,000,000，数组共 16 MB，驻留 L3；`inner` 调到单次约 200 ms。
- 预期：4–8x。
- 注意：数组不要超过 L3，否则变成内存带宽测试，SIMD 差距被掩盖。

### 2.3.1 `2.3.1_compiler_opt`
- baseline 与 optimized：同一份 `body()` 源码，baseline 编译加 `-O0`，optimized 加 `-O2`。
- workload：对 5000 万个 int32 做带边界检查和辅助函数调用的求和。
- 校验和：相同。
- 预期：5–15x。
- 注意：这组差异全部来自编译器，报告中必须注明「不是源码技巧」。

### 2.3.2 `2.3.2_runtime_dispatch`
- baseline：`std::vector<std::unique_ptr<Shape>>`，循环里通过基类指针调用 `virtual uint64_t area()`；编译加 `-fno-devirtualize`。
- optimized：`std::vector<Rect>`，直接调用内联方法。
- 校验和：所有 `area()` 之和。
- 规模：8,000,000 个对象 × 20 遍，`inner` 调到单次约 300 ms。
- 预期：2–5x。
- 注意：`semantics:` 注明两者计算相同的面积和；baseline 的 `-fno-devirtualize` 是为了隔离「运行时派发」这一变量。

### 注册

在 `exp/CMakeLists.txt` 注册下列实验；该文件是实验清单的唯一来源。额外编译选项：

| 实验 | baseline | optimized |
|---|---|---|
| `2.1.2_pruning` | `-fno-tree-vectorize` | `-fno-tree-vectorize` |
| `2.2.1_ilp` | — | — |
| `2.2.2_branch_prediction` | `-fno-if-conversion -fno-if-conversion2` | — |
| `2.2.3_loop_fusion` | — | — |
| `2.2.4_simd_avx2` | `-fno-tree-vectorize` | `-mavx2 -mfma` |
| `2.3.1_compiler_opt` | `-O0` | `-O2` |
| `2.3.2_runtime_dispatch` | `-fno-devirtualize` | — |

- [ ] **Step 1: 按上述规格创建 7 个实验目录**
- [ ] **Step 2: 在 `exp/CMakeLists.txt` 注册**
- [ ] **Step 3: 构建并跑 CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '2_1_2|2_2_1|2_2_2|2_2_3|2_2_4|2_3_1|2_3_2'
```

Expected: 全部 PASS（或环境性 SKIP）。

- [ ] **Step 4: 逐个实测，确认方向**

```bash
./build/exp/2_1_2_pruning_baseline  --reps 5 --cpu 2
./build/exp/2_1_2_pruning_optimized --reps 5 --cpu 2
# 其余实验同理，把 slug 换成对应名字
```

对每个实验：检查 checksum 一致、单次重复在 100–500 ms、加速比落在预期方向。若某个实验没有差距，先排查三件事：编译器是否消除了 baseline；baseline 是否被过度优化；工作集是否落在缓存里导致内存效应消失。修正后重跑，不得靠调参硬凑。

**额外检查 2.3.1 的 `-O0` 是否真的生效**：

```bash
cmake --build build --target 2_3_1_compiler_opt_baseline --verbose 2>&1 | grep -o -- '-O[0-9]' | tail -1
```

Expected: 输出 `-O0`。若输出 `-O2`，说明 `perfbench` 的接口选项排在目标选项之后；改为在 `2.3.1_compiler_opt/baseline.cpp` 顶部使用 `#pragma GCC optimize("O0")`，或在顶层 CMake 里把 `-O2` 从链接库的接口选项移走。

- [ ] **Step 5: 提交**

```bash
git add exp/2.1.2_pruning exp/2.2.1_ilp exp/2.2.2_branch_prediction exp/2.2.3_loop_fusion exp/2.2.4_simd_avx2 exp/2.3.1_compiler_opt exp/2.3.2_runtime_dispatch exp/CMakeLists.txt
git commit -m "feat: 计算效率 7 组对比实验"
```

---

## Task 5: 存储层次 A（4 个实验）

**Files:**
- Create: `exp/3.1.1_temporal_locality/*`
- Create: `exp/3.1.2_spatial_locality/*`
- Create: `exp/3.2.1_aos_vs_soa/*`
- Create: `exp/3.2.2_hot_cold_split/*`
- Modify: `exp/CMakeLists.txt`

### 3.1.1 `3.1.1_temporal_locality`
- baseline：1600 万 uint32 数组，1000 万次访问；90% 的访问落在 4096 个热点下标（16 KB，L1 可容纳），但访问序列被随机打乱。
- optimized：同一批访问，按「热点优先、热点内部升序」重排。
- 校验和：同一多重集的访问值之和，两者相同。
- 规模：Q=10,000,000；重排在 `init()` 完成，不计入计时。
- 预期：3–8x。
- 选项：两者 `-fno-tree-vectorize`。
- 注意：`semantics:` 注明「重排成本未计入，本实验只测局部性收益」。

### 3.1.2 `3.1.2_spatial_locality`
- baseline：4096×4096 int32 矩阵按列遍历求和（跨步 4096）。
- optimized：按行遍历求和。
- 校验和：相同。
- 规模：64 MB 矩阵，`inner` 调到单次约 200 ms。
- 预期：5–15x。
- 选项：两者 `-fno-tree-vectorize`，避免行遍历被自动向量化而混淆变量。

### 3.2.1 `3.2.1_aos_vs_soa`
- baseline：800 万条 `struct {int32 x,y,z,w;}`（128 MB），只累加 `x`。
- optimized：拆成 4 个独立数组，只累加 x 数组（32 MB）。
- 校验和：相同。
- 预期：2–4x。
- 选项：两者 `-fno-tree-vectorize`。

### 3.2.2 `3.2.2_hot_cold_split`
- baseline：400 万条 `struct {uint64 hot; char cold[56];}`（256 MB），只累加 hot。
- optimized：hot 数组与 cold 数组分离，只遍历 hot 数组（32 MB）。
- 校验和：相同。
- 预期：4–8x。
- 选项：两者 `-fno-tree-vectorize`。

### 注册

在 `exp/CMakeLists.txt` 注册 `3.1.1_temporal_locality`、`3.1.2_spatial_locality`、`3.2.1_aos_vs_soa`、`3.2.2_hot_cold_split`；四个实验的 baseline 与 optimized 都加 `-fno-tree-vectorize`。

- [ ] **Step 1: 按规格创建 4 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '3_1_1|3_1_2|3_2_1|3_2_2'
```

- [ ] **Step 3: 实测确认方向；若 3.1.1 差距不足，把热点集缩到 2048 个元素并重跑**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 存储层次 A 组 4 个对比实验"
```

---

## Task 6: 存储层次 B（5 个实验）

**Files:**
- Create: `exp/3.3.1_cache_blocking/*`
- Create: `exp/3.3.2_working_set/*`
- Create: `exp/3.4.1_false_sharing/*`
- Create: `exp/3.4.2_true_sharing/*`
- Create: `exp/3.5.1_allocator/*`
- Modify: `exp/CMakeLists.txt`

### 3.3.1 `3.3.1_cache_blocking`
- baseline：1024×1024 int32 矩阵乘，朴素 `i-j-k` 循环。
- optimized：`i-j-k` 分块，块大小 64×64。
- 校验和：C 全部元素之和（int64）。
- 规模：1024³，reps 默认 5；若单次超过 500 ms，依次降到 768×768、512×512，以单次 200–500 ms 为准。
- 预期：3–10x。
- 注意：矩阵值 0–15，累加不会溢出 int32。

### 3.3.2 `3.3.2_working_set`
- 数据结构：32 KB 热点表（4096 × uint64）+ 1 GB 冷数据流（uint8）。
- baseline：先把 1 GB 流完整扫一遍，再做 2000 万次热点表查询。
- optimized：每扫 1 MB 流就做 2 万次热点查询，热点表始终留在 L1/L2。
- 校验和：流字节和 + 表查询和，两者相同。
- 规模：1 GB 流，Q=20,000,000。
- 预期：2–5x。
- 注意：`init()` 检查可用内存，不足 1.5 GB 时抛 `bench::Skip`。

### 3.4.1 `3.4.1_false_sharing`
- baseline：6 个线程各自递增 `uint64_t counters[6]`（相邻 8 字节，落在同一缓存行）。
- optimized：`struct alignas(64) Padded { uint64_t v; }; Padded counters[6];`，每个计数器独占一个缓存行。
- 校验和：6 个计数之和。
- 规模：每线程 2,000,000 次；线程绑到 `{0,2,4,6,8,10}`。
- 预期：20–500x。
- 注意：`init()` 若物理核少于 4 个则 SKIP。

### 3.4.2 `3.4.2_true_sharing`
- baseline：6 个线程对单个 `std::atomic<uint64_t>` 做 2,000,000 次 `fetch_add`。
- optimized：每线程本地累加，结束时一次性合并。
- 校验和：总和相同。
- 预期：20–100x。
- 注意：`semantics:` 注明两者最终计数相同，只是协调频率不同。

### 3.5.1 `3.5.1_allocator`
- baseline：100 万个节点用 `new` 逐个分配、串成链表、再逐个 `delete`，重复 10 次。
- optimized：一次性 arena 分配 + 空闲链表，结束整体释放。
- 校验和：链表所有节点值的和。
- 预期：5–15x。
- 注意：节点大小 64 B，避免分配器把请求归到截然不同的大小类。

### 注册

在 `exp/CMakeLists.txt` 注册 `3.3.1_cache_blocking`、`3.3.2_working_set`、`3.4.1_false_sharing`、`3.4.2_true_sharing`、`3.5.1_allocator`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 5 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '3_3_1|3_3_2|3_4_1|3_4_2|3_5_1'
```

- [ ] **Step 3: 实测确认方向；3.4.1/3.4.2 若差距不足，检查线程是否真的落在不同物理核**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 存储层次 B 组 5 个对比实验"
```

---

## Task 7: I/O 与系统交互（5 个实验）

**Files:**
- Create: `exp/4.1.1_async_wait/*`
- Create: `exp/4.1.2_epoll_vs_poll/*`
- Create: `exp/4.2.1_zero_copy/*`
- Create: `exp/4.2.2_batching/*`
- Create: `exp/4.3.1_syscall_overhead/*`
- Modify: `exp/CMakeLists.txt`

### 4.1.1 `4.1.1_async_wait`
- 服务端：同一进程内建 64 条 UNIX stream 连接，每条由一个独立服务线程处理；每收到 8 字节请求就 `sleep_for(1ms)` 再回 8 字节，这样延迟本身可以重叠。
- baseline：64 个请求串行发送、等待（总耗时约 64 ms）。
- optimized：64 个请求同时发出到 64 条连接，再统一收（总耗时约 1–3 ms）。
- 校验和：64 个响应之和。
- 预期：30–60x。
- 注意：`semantics:` 注明 1 ms 是模拟的服务端延迟，本实验测的是等待重叠。

### 4.1.2 `4.1.2_epoll_vs_poll`
- 建立 10,000 个 `socketpair(AF_UNIX, SOCK_STREAM)`；其中 200 个对端各发 100 条 1 KB 消息（共 20,000 条）。
- baseline：`poll()` 扫描全部 10,000 个 fd，循环直到收完。
- optimized：`epoll` 只返回就绪的 fd。
- 校验和：收到的字节数之和。
- 预期：10–50x。
- 注意：`init()` 检查 `RLIMIT_NOFILE`，低于 32768 时 SKIP；创建 socketpair 失败也 SKIP。

### 4.2.1 `4.2.1_zero_copy`
- 准备 256 MB 文件（`$HOME/.cache/perf-opt-overview/`，不用 `/tmp`）；本地 TCP sink 线程负责读取并丢弃。
- baseline：`read` 64 KB 到用户缓冲区，再 `write` 到 socket。
- optimized：`sendfile(out_fd, in_fd, NULL, len)`。
- 校验和：发送字节数。
- 规模：256 MB；`init()` 先读一遍文件预热 page cache。
- 预期：1.5–3x（墙钟），系统态时间应明显下降。
- 注意：`semantics:` 注明 loopback 仍走内核协议栈，真实网卡上零拷贝收益更大。

### 4.2.2 `4.2.2_batching`
- 1,000,000 条 64 字节消息经 UNIX socket 发往 sink 线程。
- baseline：每条消息一次 `write`。
- optimized：攒满 64 条（4 KB）再 `write`。
- 校验和：发送字节数。
- 预期：5–20x。

### 4.3.1 `4.3.1_syscall_overhead`
- baseline：1000 万次 `syscall(SYS_getpid)`。
- optimized：1000 万次读取 `init()` 时缓存好的 pid。
- 校验和：pid 值相同。
- 预期：50–100x。
- 注意：用 `syscall()` 而不是 `getpid()`，避免 glibc 缓存干扰；`semantics:` 注明 optimized 不执行系统调用，本实验给出的是模式切换开销下界。

### 注册

在 `exp/CMakeLists.txt` 注册 `4.1.1_async_wait`、`4.1.2_epoll_vs_poll`、`4.2.1_zero_copy`、`4.2.2_batching`、`4.3.1_syscall_overhead`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 5 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '4_1_1|4_1_2|4_2_1|4_2_2|4_3_1'
```

- [ ] **Step 3: 实测确认方向；4.2.1 同时记录 `sys_ns`，报告中与墙钟并列**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: I/O 与系统交互 5 个对比实验"
```

---

## Task 8: io_uring（条件任务）

**Files:**
- Create: `exp/4.3.2_io_uring/{setup.hpp,baseline.cpp,optimized.cpp}`
- Create: `common/uring_min.hpp`（仅当头文件缺失时）
- Create: `tests/test_uring.cpp`（仅当使用最小封装时）
- Modify: `exp/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 的 harness。
- Produces: 可选实验 4.3.2。

- [ ] **Step 1: 依赖闸门**

```bash
test -f /usr/include/liburing.h && echo HAVE_LIBURING || echo NEED_WRAPPER
```

- 有头文件：`optimized.cpp` 直接 `#include <liburing.h>`，链接 `-luring`。
- 无头文件：实现 `common/uring_min.hpp`，只封装 `io_uring_setup`、`mmap` SQ/CQ、`io_uring_enter`，提供 `submit_n_writes(fd, iovs, n)` 与 `wait_completions(n)`。**不得**为了这个实验调用 sudo 或修改系统配置。

- [ ] **Step 2: 实现实验**

- 文件：`$HOME/.cache/perf-opt-overview/io_uring.bin`（ext4/NVMe，不用 `/tmp`）。
- baseline：20,000 次 `pwrite(fd, buf, 4096, off)`。
- optimized：每批 64 个写请求提交到 io_uring，等待完成。
- 校验和：写入总字节数；`--verify` 模式下额外读回前 1 MB 校验内容。
- 预期：3–10x。
- 注意：若最小封装在 2 小时内无法稳定通过校验，按 Global Constraints 记为 SKIP，在 README 说明原因，不强行交付。

- [ ] **Step 3: 构建、测试、实测**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R '4_3_2|uring'
```

- [ ] **Step 4: 提交**

```bash
git commit -am "feat: io_uring 批量提交对比实验（条件启用）"
```

---

## Task 9: 并发与协调 A（4 个实验）

**Files:**
- Create: `exp/5.1.1_thread_local/*`
- Create: `exp/5.1.2_sharding/*`
- Create: `exp/5.2.1_lock_granularity/*`
- Create: `exp/5.2.2_read_copy/*`
- Modify: `exp/CMakeLists.txt`

### 5.1.1 `5.1.1_thread_local`
- baseline：6 个线程各做 100 万次 map 更新，全部通过一把全局 `std::mutex` 保护一个 `std::unordered_map`。
- optimized：每线程自己的局部 map，结束后合并。
- 校验和：`Σ(key * value)`，两者相同。
- 预期：5–20x。
- 注意：线程绑到 `{0,2,4,6,8,10}`。

### 5.1.2 `5.1.2_sharding`
- 同一负载，逻辑上仍是一个共享 map。
- baseline：一把互斥锁。
- optimized：64 个分片，每片一把锁，`shard = key & 63`。
- 校验和：相同。
- 预期：3–10x。

### 5.2.1 `5.2.1_lock_granularity`
- baseline：加锁 → 查值 → 在锁内做 200 ns 计算 → 解锁。
- optimized：加锁 → 复制值 → 解锁 → 在锁外计算。
- 规模：6 线程 × 200,000 次。
- 校验和：相同。
- 预期：3–10x。

### 5.2.2 `5.2.2_read_copy`
- 共享配置：512 个 uint64，共 4 KB。
- baseline：读写都用 `std::mutex`。
- optimized：读者读取 `std::atomic<const Config*>`；写者克隆一份、原子交换指针，旧版本在运行结束后统一释放。所有版本内容相同，保证读者 checksum 确定。
- 规模：6 线程 × 100,000 次读，100 次写（每次读扫描 4 KB 配置，单次重复约 200–600 ms）。
- 校验和：每次读的 512 个值之和。
- 预期：3–6x。
- 注意：`semantics:` 注明各版本内容相同，这是为可重复校验做的简化。

### 注册

在 `exp/CMakeLists.txt` 注册 `5.1.1_thread_local`、`5.1.2_sharding`、`5.2.1_lock_granularity`、`5.2.2_read_copy`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 4 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**
- [ ] **Step 3: 实测；5.2.2 若 `shared_mutex` 版本更慢，保留 RCU 版本并在 README 记录该现象**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 并发与协调 A 组 4 个对比实验"
```

---

## Task 10: 并发与协调 B（4 个实验）

**Files:**
- Create: `exp/5.3.1_lockfree_spsc/*`
- Create: `exp/5.3.2_lockfree_mpmc/*`
- Create: `exp/5.4.1_batch_coordination/*`
- Create: `exp/5.4.2_barrier_reduction/*`
- Modify: `exp/CMakeLists.txt`

### 5.3.1 `5.3.1_lockfree_spsc`
- baseline：`std::mutex` + `std::deque<uint64_t>`，1 生产者 1 消费者，1000 万条消息。
- optimized：固定容量 65536 的 SPSC 环形队列，`std::atomic<size_t>` 头尾，acquire/release。
- 校验和：所有消息之和。
- 预期：1.5–4x。
- 注意：这是最可能出现「无锁不一定更快」的实验，负结果照实报告。

### 5.3.2 `5.3.2_lockfree_mpmc`
- baseline：`std::mutex` + `std::deque`，4 生产者 1 消费者，共 400 万条消息。
- optimized：Vyukov 有界 MPMC 环形队列（预分配，每槽一个序号原子量）。
- 校验和：相同。
- 预期：2–5x。

### 5.4.1 `5.4.1_batch_coordination`
- baseline：4 个线程把 400 万条记录逐条加锁 push 到共享容器。
- optimized：每线程本地攒 64 条，一次加锁推 64 条。
- 校验和：相同。
- 预期：5–10x。

### 5.4.2 `5.4.2_barrier_reduction`
- baseline：6 个线程，10,000 轮，每轮处理 1000 个元素后调用一次 `std::barrier`。
- optimized：循环内不设屏障，各线程本地累加，循环结束后一次屏障 + 合并。
- 校验和：相同。
- 预期：2–10x。
- 注意：`semantics:` 注明两种实现的最终总和相同；baseline 的屏障是「不必要的同步点」。

### 注册

在 `exp/CMakeLists.txt` 注册 `5.3.1_lockfree_spsc`、`5.3.2_lockfree_mpmc`、`5.4.1_batch_coordination`、`5.4.2_barrier_reduction`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 4 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**
- [ ] **Step 3: 实测；无锁实验若出现负收益，保留结果并在 README 解释**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 并发与协调 B 组 4 个对比实验"
```

---

## Task 11: 网络往返与协议（2 个实验）

**Files:**
- Create: `exp/6.1.1_round_trips/*`
- Create: `exp/6.1.2_protocol/*`
- Modify: `exp/CMakeLists.txt`

### 6.1.1 `6.1.1_round_trips`
- 环境：UNIX stream socket 回显服务线程。
- baseline：1000 次请求，每次发 8 字节、等 8 字节响应，串行。
- optimized：1000 个请求先全部发出（流水线），再收 1000 个响应。
- 校验和：响应之和。
- 预期：20–100x。
- 注意：`semantics:` 注明 loopback 的 RTT 远小于真实网络，真实网络中收益更大。

### 6.1.2 `6.1.2_protocol`
- 环境：TCP loopback 回显服务。
- baseline：每个请求新建一条 TCP 连接；请求分两次 `write`（4 字节头 + 4 字节体），不设 `TCP_NODELAY`，让 Nagle + 延迟 ACK 生效；文本协议。
- optimized：一条长连接；`TCP_NODELAY`；一次 `write` 发 8 字节二进制；连接复用。
- 规模：10 个请求。
- 校验和：响应之和。
- 预期：10–400x；baseline 主要时间花在 40 ms 级的延迟 ACK 上。
- 注意：若本机内核没有触发 40 ms 延迟，记录实际数字并注明；不得人为 `sleep` 伪造。

### 注册

在 `exp/CMakeLists.txt` 注册 `6.1.1_round_trips`、`6.1.2_protocol`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 2 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**
- [ ] **Step 3: 实测；6.1.2 记录 baseline 的实际单次耗时，确认 Nagle 是否生效**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 网络往返与协议 2 个对比实验"
```

---

## Task 12: 分布式弱演示（3 个实验）

**Files:**
- Create: `exp/6.3.1_data_locality_wire/*`
- Create: `exp/6.4.1_sharded_coordination/*`
- Create: `exp/6.4.2_async_replication/*`
- Modify: `exp/CMakeLists.txt`

这三个实验都用本机多进程 + loopback 近似分布式场景，结论必须标注「弱演示」。

### 6.3.1 `6.3.1_data_locality_wire`
- 共享 64 MB `mmap(MAP_SHARED)` 缓冲；worker 子进程负责求和。
- baseline：父进程把 64 MB 序列化并通过 UNIX socket 发给 worker。
- optimized：父进程只发 16 字节描述符（offset、length），worker 直接在共享内存上求和。
- 校验和：相同。
- 预期：1.5–3x。
- 注意：`semantics:` 注明 loopback 带宽远高于真实网络。

### 6.4.1 `6.4.1_sharded_coordination`
- 6 个子进程，各做 20,000 次计数，计数保存在文件中。
- baseline：所有进程每次操作都对同一个文件 `flock` 后读改写。
- optimized：6 个分片文件，每进程操作自己的分片，结束后合并。
- 校验和：总数相同。
- 预期：3–10x。
- 注意：本机 `flock` 比真实共识服务便宜得多，这是下界演示。

### 6.4.2 `6.4.2_async_replication`
- 1000 次操作，每次写 4 KB 到日志文件（`$HOME/.cache/perf-opt-overview/`，ext4/NVMe；放 `/tmp` 会让 `fsync` 免费，结论失效）。
- baseline：每次 `write` + `fsync` 后才确认。
- optimized：每 64 次操作 `write` 一次、`fsync` 一次；确认在 `fsync` 前返回（异步确认）。
- 校验和：日志文件内容哈希相同。
- 预期：10–50x。
- 注意：`semantics:` 注明异步确认牺牲了确认时刻的持久性，这是文档要求的权衡。

### 注册

在 `exp/CMakeLists.txt` 注册 `6.3.1_data_locality_wire`、`6.4.1_sharded_coordination`、`6.4.2_async_replication`；均无额外编译选项。

- [ ] **Step 1: 按规格创建 3 个实验目录**
- [ ] **Step 2: 注册、构建、跑 CTest**
- [ ] **Step 3: 实测；三个实验的报告中强制带「弱演示」备注**
- [ ] **Step 4: 提交**

```bash
git commit -am "feat: 分布式弱演示 3 个对比实验"
```

---

## Task 13: 全量回归、README 与汇总

**Files:**
- Create: `README.md`
- Modify: `scripts/run_all.sh`（如有必要修正二进制路径与 perf 开关）
- Create: `results/latest/summary.md`（运行产物，不提交）
- Modify: `exp/CMakeLists.txt`（确认 35 个常驻实验 + 可选 4.3.2）

**Interfaces:**
- Consumes: 全部实验。
- Produces: 可复现的 `results/<timestamp>/summary.md` 与项目说明。

- [ ] **Step 1: 全量构建与正确性回归**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Expected: 所有实验的 baseline/optimized/equivalence 测试 PASS 或 SKIP；无 FAIL。

- [ ] **Step 2: 全量计时 3 轮**

```bash
bash scripts/run_all.sh          # PERF=auto：可用则采集，不可用则跳过
PERF=0 bash scripts/run_all.sh   # 强制不采集 perf
```

Expected: 生成 `results/<timestamp>/raw.jsonl`、`summary.md`；perf 可用时另有 `perf.jsonl`，不可用时 `summary.md` 的 perf 列全为 `N/A`。总耗时 < 15 分钟（perf 采集额外约 1 分钟）。

- [ ] **Step 3: 人工复核报告**

逐行检查：

- 每个实验的 checksum 一致；INVALID 为 0。
- 预期正收益的实验是否为 SIGNIFICANT；若不是，在 README 的「本机未复现」小节写明现象与可能原因，不得删实验。
- 弱演示三个实验是否带备注。
- 4.3.2 是 SIGNIFICANT 还是 SKIP。
- perf 列若可用：`cache-misses`、`branch-misses` 的下降方向是否与墙钟加速一致；3.4.1 是否出现 `c2c_hitm` 且优化版明显低于 baseline。若 perf 列全为 `N/A`，不要把它当失败；在 README 注明「未启用 perf 佐证」。

- [ ] **Step 4: 写 `README.md`**

包含：项目目的；硬件与系统限制（i5-12490F、6 物理核、无 AVX-512、单 NUMA、无 RDMA/DPDK、governor=powersave）；构建与运行；结果判定含义（SIGNIFICANT / INCONCLUSIVE / REGRESSION / SKIP / INVALID）；实验清单与状态；6 个不可测点及原因；如何新增实验；测量注意事项。

另加一节「可选的 perf 佐证」：说明主结论只用墙钟，perf 列只是解释；默认 `perf_event_paranoid=4` 时不可用；启用方式是用户自己执行 `sudo sysctl kernel.perf_event_paranoid=2`（临时）或写 `/etc/sysctl.d/`（持久）；可用 `PERF=0` 关闭采集。不要在 README 里复制 `性能优化概述.md` 的正文，只引用路径。

- [ ] **Step 5: 提交**

```bash
git add README.md exp/CMakeLists.txt scripts
git commit -m "docs: 全量回归与使用说明"
```


---

## 附录：结果判定与报告规则

| 判定 | 条件 | 含义 |
|---|---|---|
| SIGNIFICANT | `speedup >= 1.10` 且 baseline p10 > optimized p90 | 在本机、本次运行下差异超出噪声 |
| REGRESSION | `speedup <= 0.90` 且 optimized p10 > baseline p90 | 优化反而更慢，如实保留 |
| INCONCLUSIVE | 区间重叠，或 0.90 < speedup < 1.10 | 本次无法区分，需增加轮次或减小噪声 |
| SKIP | 环境不满足（内存、fd、内核特性） | 不是失败，报告中说明原因 |
| INVALID | 同一实验内 checksum 不一致 | 实现有 bug，必须修复后重跑 |

报告中的加速比是「本机、本编译器、本参数」下的观测值，不外推到其他硬件。每个实验的 `semantics:` 备注必须出现在报告备注列。

perf 列（`cache-misses`、`branch-misses`、`c2c_hitm`）只作解释，不参与上表判定。perf 不可用时显示 `N/A`，不影响实验的 SIGNIFICANT / REGRESSION / INCONCLUSIVE 结论。
