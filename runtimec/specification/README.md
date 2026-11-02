# runtimec specification

本目录是 runtimec 语言行为与 RWIR 结构事实的唯一入口，不保存执行策略，也不把
当前 C 结构、BinRWIR 编码或机器二进制接口冻结为长期格式。

- `five-language-behavior-matrix.md`：语言行为决策的唯一事实源。
- `rwir_catalog.def`：RWIR 操作标识符、参数结构与行为决策引用。
- `scalar_operations.def`：已裁决行为到标量执行族的映射。

新增语言行为时，必须先在五语言矩阵的一行中完整写明 C、Python、Rust、Go、
V8/TypeScript 的行为、kvlang 选择、理由、边界测试数据与错误规则。随后，
`rwir_catalog.def` 或 `scalar_operations.def` 条目用
`behavior-decision: <decision>` 引用该行；manifest 不得复制行为正文。

没有完整矩阵决策的行为不得进入 manifest、vmkv、vmbin 或 vmjit。矩阵、引用和
边界测试数据必须在同一变更中完成，`check_behavior_matrix.py` 对此失败封闭。

当前两个 manifest 不含操作记录。未来生成表必须来自它们，禁止复制第二张手写表。
