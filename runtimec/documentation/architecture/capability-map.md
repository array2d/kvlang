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
| Plaintext RWIR reading and validation | `vmkv/` | 从 kvspace 读取 operation name、参数类型与参数名 |
| Static key address resolution | `vmkv/` | 参数 key 解引用为非零、生命周期有效的 `uint64 argaddr` |
| Canonical BinRWIR Program | `binrwir/program.*` | 进程内、不可变、地址失效时整体失效 |
| Hot instruction identity | `binrwir/instruction.*` | 每条 instruction 只保留稳定 `opid` |
| Addressed arguments | `binrwir/argument.*` | 每个参数只保留 `argidx + uint64 argaddr`，完整且不截断 |
| Signature and alias policy | `binrwir/signature.*` | `(opid, argidx)` 声明类型、方向与权限；相同 `argaddr` 证明实际 alias |
| Shared execution frame and exit | `binrwir/execution_frame.*`, `binrwir/execution_exit.h` | vmbin 与内部 JIT executor 使用同一结构合同 |
| Execution plan representation | `binrwir/execution_plan.*` | 不含 body cache 或通用寄存器分配的可删除派生物 |
| Plan generation and target decision | `binrwir_compiler/` | 编译阶段一次决定 JIT 或 VMBIN |
| Unified compilation | `binrwir_compiler/` | 同时持有 Program、plan、target 与可选机器码 |
| Compilation cache and leases | `engine/compilation_repository.h` | repository 只缓存已完成 compilation |
| Execution dispatch and commit ordering | `engine/route.c` | 只在 completed state 全部写回后推进 logical program counter/checkpoint |
| Address-driven baseline execution | `vmbin/` | 直接读写 `argaddr` 指向的 body；不拥有 value cache 或 register file |
| Machine-code eligibility | `binrwir_compiler/jit/verifier.h` | 编译阶段产生 COMPILED、NOT_COMPILABLE 或 ERROR 判定 |
| Target-neutral lowering | `binrwir_compiler/jit/lower.h` | 只消费已验证 BinRWIR 与 execution plan |
| x86_64 and AArch64 generation | `binrwir_compiler/jit/backend/` | 同一个 backend 合同的并列实现 |
| Mandatory instruction-completion boundary | `binrwir_compiler/jit/safepoint.*` | 每条 logical instruction completion 都是不可关闭的 recovery/observable boundary |
| Completed-state materialization | `binrwir_compiler/jit/safepoint.*` | 只把完整完成指令的全部 dirty state 提交到原 `argaddr` |
| Incomplete-state discard | `binrwir_compiler/jit/safepoint.*` | guard failure、error、recovery/resume 丢弃当前 speculative register/spill，不写回 |
| JIT register allocation | `binrwir_compiler/jit/register_allocation.*` | 唯一可在当前逻辑指令内部提升 body 到 register/spill 的组件 |
| Machine-code execution | `binrwir_jit_execute` | 只接受已生成 code；不能编译、改选目标、提交未完成状态或执行 fallback |
| Dynamic key policy | `specification/`, `vmkv/` | 生命周期 ABI 建立前 map/struct 动态 key 明确 `UNSUPPORTED` |
| kvspace access and checkpoint storage | `host/` | 只通过窄主机服务合同访问 |
| Exported symbol set | `interface/exports.txt` | 每个平台必须精确匹配 |

## Rejected ownership

- BinRWIR 不属于 vmbin、内部 JIT 或 vmkv。
- BinRWIR 不拥有 RWIR 明文字段、key、xvalue、`V.body` 或 register file。
- vmkv 不拥有优化、execution plan、target decision 或 execution。
- Compiler 不读取 RWIR，不访问 kvspace，不执行 Program。
- repository 不编译、不调用执行器、不改变 target。
- route 不做 JIT eligibility，不实现 operation 语义。
- JIT 只属于 `binrwir_compiler/` 内部，不拥有语言语义，不依赖 vmbin，不在执行时改变目标。
- JIT 不得关闭 instruction-completion boundary，不得 materialize speculative/partial state。
- 未完成状态 discard 后不得推进 logical program counter 或 checkpoint。
- vmbin 不拥有 canonical BinRWIR，不选择 JIT，不缓存 xvalue/body，也不建立 software register file。
- 动态 key 不得通过零 `argaddr`、间接查找或 JIT 特例进入可执行 compilation。
- host 不检查 BinRWIR instruction。
- 公共头不暴露内部 runtimec 结构。
- 任何组件都不能创建第三执行器或第二公开执行入口。
