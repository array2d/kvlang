# runtimec specification

本目录是 runtimec 语言行为、RWIR operation facts 与 myrwircap 到 canonical BinRWIR
映射的唯一入口。RWIR 是 kvspace 中包含 operation name、参数类型和参数名的明文可恢复
格式；BinRWIR 是当前进程中只含 `opid` 与重复 `argidx + uint64 argaddr` 的二进制派生格式。
本目录不保存执行策略，也不把进程内地址编码冻结为持久化格式。

- `five-language-behavior-matrix.md`：语言行为决策的唯一事实源。
- `myrwircap-binrwir-mapping.md`：每个 capability 到 canonical operation 的规范合同。
- `rwir_catalog.def`：稳定 operation identifier、规范名称、参数结构、canonical 映射与
  行为决策引用的唯一 manifest 输入。
- `scalar_operations.def`：已裁决行为到标量 execution family 的派生映射输入。

## Required order

1. 在五语言矩阵中记录 C、Python、Rust、Go、V8/TypeScript 行为、kvlang 选择、边界
   fixture 与非法输入规则。
2. 为 myrwircap 分配显式稳定 operation identifier。identifier 不得来自数组下标、文件
   顺序、声明顺序或生成顺序。
3. 在 `rwir_catalog.def` 中写入 operation name 到 `opid`、参数类型/名称到 `argidx` 的唯一
   canonical 映射与 `behavior-decision` 引用。
4. 由 vmkv 消费 manifest 并验证明文 RWIR；host 将每个静态参数 key 解引用为非零 64 位
   XValue 基址，vmkv 验证固定 8 字节 XValue head ABI、检查加法溢出，并且唯一执行
   `argaddr = xvalue_address + 8` 后等价生成 canonical BinRWIR。
5. 由 `binrwir_compiler/` 从 canonical Program 生成 execution plan 和 compilation；JIT
   仅作为该组件的内部编译实现参与目标判定。

所有 execution target 都必须保留每条 logical instruction completion 这一不可关闭的
recovery/observable boundary。JIT 只在当前逻辑指令完整完成后提交全部 completed dirty
state；guard failure、error、recovery 或 resume 必须丢弃当前未完成指令的 speculative/
partial register 与 spill state，不能写回。logical program counter/checkpoint 仅在完成
状态全部写回成功后推进。

每个 myrwircap 必须恰好出现一次。缺失映射、重复 capability、重复稳定 identifier、参数
方向不一致、行为引用不存在或任何歧义都必须成为编译错误。禁止默认 operation、顺序推导、
兼容别名和静默修复。

当前只允许 XValue 基址生命周期长且可验证的静态 key，例如 vthread `pc`、`status` 与
`/lib` 函数源。host 只能返回 XValue 基址，不能任意决定 body 偏移；BinRWIR 与 vmbin 只
保存或消费 vmkv 派生的最终 `argaddr`，不缓存 XValue 或 `V.body`。零 XValue 基址、`+8`
溢出、非 8 字节 head ABI、动态 key 或无法证明生命周期均失败封闭。map、struct 等动态
拼接 key 在 key/xvalue 生命周期 ABI 建立前必须返回 `RUNTIMEC_STATUS_UNSUPPORTED`；禁止
零 `argaddr`、延迟 key lookup 或间接 fallback。

当前两个 `.def` manifest 不含 operation 记录。未来所有生成表必须来自这两个输入，禁止
复制第二张手写表。
