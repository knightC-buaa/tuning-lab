## Ledger 语言规范（.superpowers/sdd/）

适用于 `.superpowers/sdd/**/progress.md` 及同目录文档。采用正向选择：只翻译下方明确列出的片段，未列出的内容一律保持原样。

### 必须用简体中文书写的片段

1. **`Ruling:` 行**：破折号之后的「为什么 / 错了的代价」说明。
   - 示例：`Ruling: 用连接池而非每请求建连 — 高并发下 QPS 不达标 — 错了则需重写数据访问层，约 2 天返工`
2. **`minor (deferred):` 之后的内容**：从该标记之后到行尾的全部描述。
   - 示例：`- Task 3: minor (deferred): init() 建表前未调用 g_index.clear()；单次初始化无害，模板复用时应防御`
3. **`Parked:` 之后的内容**：问题描述与处置说明。

### 以下一律保持原样

- 必须原样保留的标记 token：`# SDD ledger — plan: <path>`、`Task <N>: complete (...)`、`Ruling:`、`Parked:`、`BLOCKED`、fix-round 条目
- 任务编号、文件路径、commit hash、测试命令
- 代码块、命令行、原始测试输出
- `review-*.diff`：git diff 原始证据，不得翻译
- `task-N-brief.md`：由脚本从 plan.md 生成，下次派发会被覆盖，不翻译

### 执行责任

controller 派发实现或审查子代理时，必须把本节规则附在 dispatch prompt 中；子代理不保证读取项目 CLAUDE.md。

## C++ 开发与 AI 协作约定

本节是行为规则的唯一来源。工具链配置、刻意关闭的检查及当前基线见 [docs/harness-practice.md](docs/harness-practice.md)；项目事实以 [README.md](README.md) 为准。

### 硬不变量

- **等价性**：`baseline` 与 `optimized` 在 `--verify` 下必须输出相同 checksum。除该实验要对比的优化手段外，不得改动任一变体的语义；不得「顺手优化」baseline。
- **测量有效性**：计时的 `body()` 内不得加入日志、IO、动态分配或 sleep；不得改变 `bench::run` 的 warmup / reps / 统计口径。sanitizer 构建只用于正确性验证，绝不能用于计时测量。
- **验证闭环**：改完必须运行 `ctest --preset release -j"$(nproc)"`；新增实验还要分别跑两个变体的 `--verify`。未运行测试不得声称完成。
- **外科手术式修改**：只改与需求相关的行；不重构无关代码、不批量重排格式、不升级依赖。新增文件遵循现有目录与命名。

### C++ 写法

- 使用 C++23，不依赖编译器扩展（`CMAKE_CXX_EXTENSIONS OFF` 已设置）。
- 资源用 RAII；不新增裸 `new` / `delete`；所有权用值语义或智能指针表达。
- 保持 const 正确性；只读参数用 `const&` 或按值，容器视图优先 `std::span` / `std::string_view`。
- 错误处理：可恢复错误抛异常；环境不满足抛 `bench::Skip`（退出码 2），不要 `abort()`。
- 注释用中文，解释「为什么」而不是「是什么」；公共函数在头文件说明前置条件与失败方式。
- 命名沿用现有风格：类型 `CamelCase`，函数 / 变量 `snake_case`，编译期常量 `kCamelCase`，全局夹具 `g_` 前缀，宏 `UPPER_SNAKE`。
- 头文件用 `#pragma once`；命名空间收尾写 `} // namespace bench`。

### 工具优先

- 重命名或改签名先用 clangd 符号查找（`.clangd` 指向 `build/`），不要全局文本替换。
- 静态检查：`clang-tidy -p build <文件>`；配置以 [.clang-tidy](.clang-tidy) 为准。编辑后的钩子会检查格式和改动行的 clang-tidy，报告的问题要修，不要用 `NOLINT` 掩盖。
- 编译验证：`cmake --preset strict && cmake --build --preset strict -j"$(nproc)"` 必须零告警。
- 格式化：全仓库已统一为 [.clang-format](.clang-format)。编辑后对改动文件运行 `clang-format -i <文件>`，提交前用 `clang-format --dry-run --Werror <文件>` 确认。
