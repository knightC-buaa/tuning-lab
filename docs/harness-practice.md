# Harness Practice：C++ 协作约定与工具链

本文件记录本项目为「与 Claude 协作开发 C++」建立的全部配置及其理由，只描述当前最终状态；变更历史由 git 记录。

- 行为规则（必须遵守）：[CLAUDE.md](../CLAUDE.md)
- 项目事实（状态、构建、运行、判定）：[README.md](../README.md)

本文件不重复上述内容，只说明每类规则由哪个文件执行、为什么这样配置，以及当前基线。

## 1. 设计原则

1. 机械规则交给工具：格式、静态检查、告警、sanitizer 由配置和钩子执行，不写成散文。
2. 硬约束写进常驻文件：工具判断不了、错了代价大的规则（等价性、测量有效性）写进 `CLAUDE.md`。
3. 只记录不可推断的信息：命令、边界、刻意写法；不复制代码、README，也不重复全局 `~/.claude/CLAUDE.md` 已有的通用编码准则。
4. 先零告警，再收紧：`WarningsAsErrors` 和更强检查都建立在当前基线干净之上。

## 2. 文件地图

| 文件 | 职责 | 执行时机 |
|---|---|---|
| `CLAUDE.md` | 行为规则：硬不变量、C++ 写法、工具优先顺序 | 每次会话 |
| `docs/harness-practice.md` | 本文件：配置地图、理由、基线、暂缓项 | 人工查阅 |
| `.clang-tidy` | 静态检查基线 | 手动 / `PostToolUse` 钩子 |
| `CMakeLists.txt` | 编译器告警集、`-Werror` 开关、sanitizer 开关 | 构建时 |
| `CMakePresets.json` | `release` / `strict` / `sanitize` 三套预设 | 构建 / 测试时 |
| `.claude/settings.json` | 注册编辑后的静态检查钩子 | 每次 Edit/Write 后 |
| `.claude/hooks/cpp-check.sh` | 只检查本次改动行的 clang-tidy | 钩子调用 |

## 3. 常驻规则：CLAUDE.md

[CLAUDE.md](../CLAUDE.md) 的「C++ 开发与 AI 协作约定」一节是行为规则的唯一来源，只写四类内容：

- 硬不变量：baseline/optimized 等价性、测量有效性、验证闭环、外科手术式修改。
- C++ 写法：RAII、const 正确性、错误处理、注释语言、命名。
- 工具优先：clangd 符号查找、clang-tidy 与 strict 构建的使用方式。
- 格式化边界：不整文件重排（原因见第 7 节）。

修改行为规则时只改 `CLAUDE.md`，本文件不复制条文。

## 4. 静态检查：.clang-tidy

### 启用范围

只启用误报率低、与正确性/性能直接相关的检查：

- `clang-analyzer-*`：空指针、越界、未初始化等路径敏感问题。
- `bugprone-*`：常见误用（下方列出的例外除外）。
- `performance-*`：明显可避免的性能损耗。
- 少量与本项目口味一致的 `modernize-*`：`starts_with`、`nullptr`、`override`、`emplace`、`make_unique/shared`、范围 for、`bool` 字面量。
- 少量 `readability-*` 与 `misc-*`：容器判空、冗余 `c_str`、布尔化简、`const` 返回类型、未使用参数、按值抛出/按引用捕获、`unique_ptr` 误用。

### 刻意关闭的检查

以下是本项目的既定写法，不是缺陷。关闭是为了避免 Claude 被误导去改坏它们：

| 检查 | 关闭原因 |
|---|---|
| `portability-avoid-pragma-once` | 全项目统一使用 `#pragma once` |
| `cppcoreguidelines-avoid-non-const-global-variables` | `g_*` 是基准夹具，需要在 `init()` 中初始化、在 `body()` 中读取 |
| `cppcoreguidelines-macro-usage` | `BENCH_MAIN` 是刻意的入口宏 |
| `modernize-deprecated-headers` | 4.1.1b 使用 POSIX `clock_nanosleep`，必须包含 `<time.h>` |
| `misc-include-cleaner` | 对宏与传递包含噪声过大 |
| `misc-use-internal-linkage` | 实验入口与 `body` / `init` 由宏引用，保持外部链接更直观 |
| `misc-const-correctness` | 会误判 `strtol` 的 `char** endptr` 输出参数 |
| `readability-convert-member-functions-to-static` | 协程 awaitable 的成员函数按协议保留为成员 |
| `modernize-use-nodiscard` | 同上，协程协议方法不强制 `[[nodiscard]]` |
| `cppcoreguidelines-avoid-const-or-ref-data-members` | `EventLoop&` 是事件循环的刻意引用成员 |
| `bugprone-easily-swappable-parameters` | 基准代码中同类型参数多，噪声高 |
| `bugprone-implicit-widening-of-multiplication-result` | 存量代码存在 `int * int -> size_t` 惯用写法，另行清理 |

### 使用方式

```bash
# 单文件
clang-tidy -p build common/cpu.cpp

# 只检查本次改动的行（推荐，避免存量告警）
git diff -U0 | clang-tidy-diff-21.py -p1 -path build -config-file .clang-tidy
```

约束：

- 需要 `build/compile_commands.json`（`CMAKE_EXPORT_COMPILE_COMMANDS ON` 已设置）。
- 新增检查前先对全部源码跑一遍，确认零噪声。
- 不要在 `WarningsAsErrors` 中写不存在的检查名，否则 clang-tidy 会直接失败。

## 5. 编译器与预设

### 默认告警集

```text
-O2 -g
-Wall -Wextra -Wpedantic -Wshadow
-Wnull-dereference -Wformat=2 -Wimplicit-fallthrough -Wdouble-promotion
```

实测在 `-O2` 下零告警。`-Wnull-dereference` 在 2.1.1 的 `find()->second` 上发现过真实问题，已改为先判断 `end()`。

### 预设

| 预设 | 用途 | 命令 |
|---|---|---|
| `release` | 日常构建与测量 | `cmake --preset release && cmake --build --preset release -j"$(nproc)"` |
| `strict` | 告警即错误 | `cmake --preset strict && cmake --build --preset strict -j"$(nproc)"` |
| `sanitize` | ASan + UBSan 正确性验证 | `cmake --preset sanitize && cmake --build --preset sanitize -j"$(nproc)"` |

测试统一用 `ctest --preset <name> -j"$(nproc)"`。

### sanitizer 边界

- 仅用于正确性验证，**绝不能用于计时测量**：插桩会改变时间。
- 使用 `-fno-sanitize-recover=all`，让 UB 立即退出，避免「跑完但结果不可信」。
- 构建目录 `build-sanitize/` 已被忽略。

## 6. 编辑后钩子

项目级的`.claude/settings.json` 注册 `PostToolUse`，匹配 `Edit|Write`，调用 `.claude/hooks/cpp-check.sh`。

行为：

- 只看 `common/`、`exp/`、`tests/` 下、且已在 compile DB 中的源文件；其他文件直接放行。
- 已跟踪的源文件：`git diff -U0` 交给 `clang-tidy-diff`，只检查本次改动的行。
- 未跟踪的新源文件：已在 compile DB 中时整文件检查；尚未重新配置时跳过。
- 头文件不在 compile DB 中，钩子跳过；其检查由包含它的源文件在下次编辑时覆盖。
- 有诊断时写到 stderr 并返回退出码 2，Claude Code 会把内容反馈给模型；无诊断时返回 0。
- 缺少 compile DB、`.clang-tidy`、clang-tidy 或 clang-tidy-diff 时静默放行，不阻塞编辑。
- 钩子超时 120 秒，clang-tidy 单文件超时 30 秒。
- 依赖：`jq`、`clang-tidy`、`clang-tidy-diff-21.py`（或 `clang-tidy-diff.py`）。

钩子不做格式化，原因见第 7 节。

## 7. 暂缓项与已知限制

### 格式化门禁（暂缓）

- `.clang-format` 当前未纳入版本控制，且与存量代码不一致：配置为 4 空格 + Allman 大括号 + 命名空间缩进，代码为 2 空格 + K&R。
- 实测现有 20 个源文件全部无法通过 `clang-format --dry-run --Werror`。
- 因此不注册格式化钩子，避免 Claude 每次编辑被迫整文件重排。
- 收敛方式二选一，尚未决定：
  1. 以代码为准，把 `.clang-format` 调整到现有风格并纳入版本控制；
  2. 一次性全仓库重排，作为独立提交，再启用门禁。
- 在此之前：不要运行整文件 `clang-format -i`，只手工保持周围风格。

### 其他

- `-Wconversion` / `-Wsign-conversion`：全量约 38 条（多为 `int -> size_t`），未纳入默认构建，作为独立清理任务。
- 命名检查（`readability-identifier-naming`）：暂未启用，先保持现有命名。
- CI：尚未配置；`strict` / `sanitize` 预设可在 CI 中直接调用。
- `.claude/rules/` 路径级规则：暂不使用；当前规则量小，常驻 `CLAUDE.md` 足够。

## 8. 当前基线

| 项目 | 结果 |
|---|---|
| `release` 构建 | 0 告警 |
| `release` CTest | 16/16 通过 |
| `strict` 构建（`-Werror`） | 0 告警，16/16 通过 |
| `sanitize` 构建（ASan + UBSan） | 0 告警，16/16 通过 |
| clang-tidy（`common/`、`exp/`、`tests/`） | 0 条用户代码诊断 |
| 编辑后钩子 | 干净改动退出 0；注入空指针解引用后退出 2（均已验证） |

建立基线时顺带修正的存量问题：

| 位置 | 问题 | 处置 |
|---|---|---|
| `common/cpu.cpp` | `rfind(..., 0)` 的旧写法 | 改为 `starts_with` |
| `common/cpu.cpp` | fallback 向量未预留容量 | 循环前 `reserve` |
| `exp/4.1.1b_coroutine_vs_thread/baseline.cpp` | `rfind(..., 0)` 的旧写法 | 改为 `starts_with` |
| `exp/2.1.1_hash_vs_scan/optimized.cpp` | `find()->second` 可能解引用 `end()` | 先判断 `it != end()` |

## 9. 维护

- 新增 clang-tidy 检查：先对全部源码跑一遍，确认零噪声，再把检查名加入 `.clang-tidy`。
- 收紧告警：先保证 `release` 全量零告警，再考虑 `WarningsAsErrors: '*'`；可先在 `strict` 预设中验证。
- 新增预设：在 `CMakePresets.json` 同时加 configure / build / test 三项。
- 规则变更：行为规则改 `CLAUDE.md`，工具配置改对应文件，并同步本文件的映射表与基线。
- 工具版本：当前环境 clang-format / clang-tidy / clangd 21.1.8、g++ 15.2、CMake 4.2.3。检查名与 `clang-tidy-diff-21.py` 的命名和版本绑定，换工具链时需重跑基线。

## 参考

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [cpp-best-practices](https://github.com/cpp-best-practices/cppbestpractices) 与 [cmake_template](https://github.com/cpp-best-practices/cmake_template)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [Anthropic：CLAUDE.md 文档](https://code.claude.com/docs/en/memory)
- [AGENTS.md 指南](https://www.morphllm.com/agents-md-guide)
- [cpp-linter：AI coding agents](https://cpp-linter.github.io/ai-coding-agents/)
- [ThemisDB 的 C++ CLAUDE.md](https://github.com/makr-code/ThemisDB/blob/feature/phase3-index-codegen/CLAUDE.md)
