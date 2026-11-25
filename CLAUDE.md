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
