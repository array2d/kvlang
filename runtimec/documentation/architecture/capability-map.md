# runtimec Capability Ownership

每项 capability 只有一个最终所有者。共享合同可以被多个组件读取，但任何组件都不能
复制所有权、建立第二执行路径或在执行期改变 Compiler 的决定。

| Capability | Final owner | Required direction |
|---|---|---|
| Public create, execute, destroy | `interface/` | 只接受与返回结构化公共类型 |
| Status and error vocabulary | `include/kvlang/runtimec.h`, `contract/` | `RUNTIMEC_STATUS_SUCCESS` 是唯一正常调用状态 |
| Five-language behavior decisions | `specification/five-language-behavior-matrix.md` | 先裁决行为，再进入 operation manifest |
| myrwircap capability records | `specification/rwir_catalog.def` | 每个 capability 恰好一条 canonical 映射 |
| Stable operation identity | `specification/` | 显式 identifier；不得使用数组下标或声明顺序 |
| RWIR reading and validation | `vmkv/` | source 只能进入等价 canonical BinRWIR 生成 |
| Canonical BinRWIR Program | `binrwir/program.*` | 中立、不可变，同时供 Compiler 与两个执行器读取 |
| Instruction and Read/Write references | `binrwir/instruction.*`, `binrwir/reference.*` | 完整分区，不截断参数 |
| Signature and alias policy | `binrwir/signature.*` | Signature 声明权限；Binding 证明 Cell identity |
| Shared execution frame and exit | `binrwir/execution_frame.*`, `binrwir/execution_exit.h` | vmbin 与 vmjit 使用同一结构合同 |
| Execution plan representation | `binrwir/execution_plan.*` | canonical Program 的可删除派生物 |
| Plan generation and target decision | `compiler/` | 编译阶段一次决定 VMJIT 或 VMBIN |
| Unified compilation | `compiler/` | 同时持有 Program、plan、target 与可选机器码 |
| Compilation cache and leases | `engine/compilation_repository.h` | repository 只缓存已完成 compilation |
| Execution dispatch and commit ordering | `engine/route.c` | 执行前读取 target，只调用对应入口 |
| Complete baseline execution | `vmbin/` | 所有合法 canonical BinRWIR 都可由 VMBIN compilation 执行 |
| Machine-code eligibility | `vmjit/verifier.h` | 编译阶段产生 COMPILED、NOT_COMPILABLE 或 ERROR 判定 |
| Target-neutral lowering | `vmjit/lower.h` | 只消费已验证 BinRWIR 与 execution plan |
| x86_64 and AArch64 generation | `vmjit/backend/` | 同一个 backend 合同的并列实现 |
| Machine-code execution | `vmjit_execute` | 只接受已生成 code；不能编译或改选目标 |
| kvspace access and checkpoint storage | `host/` | 只通过窄主机服务合同访问 |
| Exported symbol set | `interface/exports.txt` | 每个平台必须精确匹配 |

## Rejected ownership

- BinRWIR 不属于 vmbin、vmjit 或 vmkv。
- vmkv 不拥有优化、execution plan、target decision 或 execution。
- Compiler 不读取 RWIR，不访问 kvspace，不执行 Program。
- repository 不编译、不调用执行器、不改变 target。
- route 不做 vmjit eligibility，不实现 operation 语义。
- vmjit 不拥有语言语义，不依赖 vmbin，不在执行时改变目标。
- vmbin 不拥有 canonical BinRWIR，也不选择 vmjit。
- host 不检查 BinRWIR instruction。
- 公共头不暴露内部 runtimec 结构。
- 任何组件都不能创建第三执行器或第二公开执行入口。
