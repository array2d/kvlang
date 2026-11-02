# runtimec specification

本目录是 runtimec 语言行为、RWIR operation facts 与 myrwircap 到 canonical BinRWIR
映射的唯一入口。它不保存执行策略，也不把当前 C 结构、内存布局或机器二进制接口冻结
为长期格式。

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
3. 在 `rwir_catalog.def` 中写入唯一 canonical BinRWIR operation 映射与
   `behavior-decision` 引用。
4. 由 vmkv 生成器消费该 manifest，验证 RWIR 并等价生成 canonical BinRWIR。
5. 由 `binrwir_compiler/` 从 canonical Program 生成 execution plan 和 compilation；JIT
   仅作为该组件的内部编译实现参与目标判定。

每个 myrwircap 必须恰好出现一次。缺失映射、重复 capability、重复稳定 identifier、参数
方向不一致、行为引用不存在或任何歧义都必须成为编译错误。禁止默认 operation、顺序推导、
兼容别名和静默修复。

当前两个 `.def` manifest 不含 operation 记录。未来所有生成表必须来自这两个输入，禁止
复制第二张手写表。
