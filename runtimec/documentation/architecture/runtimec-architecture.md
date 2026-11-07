# runtimec BinRWIR Compiler Architecture

runtimec 只允许一条从 RWIR specification 到 execution 的主干。任何组件都不能建立旁路、
第二次目标判定或第三执行器。

## Unique trunk

```text
myrwircaps + plaintext RWIR in kvspace
    -> vmkv validates RWIR and requests each static key
    -> host dereferences static keys to nonzero uint64 XValue base addresses
    -> vmkv validates the fixed 16-byte XValue head and derives argaddr = xvalue_address + 16
    -> vmkv emits addressed canonical BinRWIR
    -> binrwir_compiler builds execution plan and attempts internal JIT compilation
    -> compile-time target decision: JIT code or VMBIN plan
    -> compilation repository caches the complete compilation
    -> engine route reads the decided target before execution
    -> binrwir_jit_execute or vmbin_execute
    -> route commits complete effects and checkpoint
```

这条顺序同时是职责边界：vmkv 之前是语言事实，vmkv 输出是中立 canonical Program，
Compiler 输出是可执行 compilation，route 只消费已完成的 compilation。

## Ownership

| Component | Owns | Must not own |
|---|---|---|
| `specification/` | 五语言行为决策、RWIR operation facts、myrwircap 到 canonical BinRWIR 的唯一映射 | 执行策略、缓存或机器码 |
| `vmkv/` | 明文 RWIR 读取、验证、`opid`/`argidx` 映射、XValue 基址资格验证、固定 16 字节 head 的唯一 `+16` 派生、`argaddr` 生成 | 动态 key fallback、任意 body 偏移、优化、execution plan、执行目标或机器码 |
| `binrwir/` | 进程内 canonical Program、`opid`、`argidx + uint64 argaddr`、signature、execution frame、execution exit、execution plan 合同 | RWIR 明文字段、V.body 副本、register file、vmkv/vmbin/JIT 策略 |
| `binrwir_compiler/` | execution plan 生成、内部 JIT compilation 请求、统一 compilation 与目标判定 | RWIR 解析、执行或 checkpoint 提交 |
| `binrwir_compiler/jit/` | eligibility、lowering、register/spill、强制 instruction-completion safepoint、机器码生命周期与执行 | 未完成状态写回、独立 VM 身份、RWIR、vmbin 依赖、语言语义或执行期目标选择 |
| `engine/compilation_repository` | compilation 所有权、索引、租约与失效 | 编译、执行或目标改选 |
| `engine/route` | repository 查询、未命中编译、执行前目标读取、唯一执行调度、effect 与 checkpoint 提交 | operation 语义或编译 eligibility |
| `vmbin/` | 通过 `argaddr` 直接读写 kvspace body 的基线执行 | xvalue/value cache、register file、BinRWIR 所有权、JIT 选择或持久事实 |
| `host/` | kvspace 与外部能力适配、静态 key 到 64 位 XValue 基址的解引用 | body 偏移决定、`+16` 派生、BinRWIR 检查或执行策略 |
| `interface/` | 公共请求翻译与精确导出 | 内部结构、语言语义或执行策略 |

## Specification mapping

RWIR 是 kvspace 中的明文可恢复格式：调用保存 operation name，参数保存类型、名称和
Read/Write 方向。每个 myrwircap 必须恰好拥有一个 canonical BinRWIR operation 映射。
映射记录显式保存规范 operation name、稳定 `opid`、每个参数的稳定 `argidx`、Read/Write
参数结构和行为决策引用。

operation identifier 是 specification 数据，不是 `myrwircaps` 数组下标，也不能由文件
顺序、声明顺序或生成顺序计算。重新排序或插入记录不能改变既有 identity。缺失记录、
同一 capability 的重复记录、identifier 重复、签名不一致或歧义都使 compilation 立即
失败。vmkv 不提供默认 operation，也不把不完整映射变成可执行 Program。

## Canonical BinRWIR

BinRWIR 是顶层中立模块，不属于 vmbin。它是从当前进程 kvspace 派生的二进制执行格式，
不能持久化，也不能在地址生命周期失效后继续使用。每条热 instruction 只保存：

```text
opid
  argidx + uint64 argaddr
  argidx + uint64 argaddr
  ...
```

`opid` 替代 RWIR operation name。`argidx` 在 canonical signature 中恢复参数顺序、类型和
Read/Write 权限。参数 key 解引用得到 64 位 XValue 基址，不是任意 body 地址；XValue head
ABI 固定为 16 字节，只有 vmkv 验证该 ABI、检查溢出并执行
`argaddr = xvalue_address + 16`，所以 `argaddr` 直接指向 kvspace `V.body`。BinRWIR 不复制
operation name、参数类型、参数名、key 文本、XValue 或 body。RWIR 可以具有不固定参数
数量；确定调用必须保存全部参数对，二进制编码不能改变参数顺序或观察边界。

BinRWIR 保存的唯一寻址信息是 vmkv 派生的最终 64 位 `argaddr`，不缓存 XValue 基址、
XValue 或 `V.body`；vmbin 也只消费最终 `argaddr`。
vthread `pc`、`status` 和 `/lib` 函数源等地址生命周期长的 key 属于当前优化范围。map、
struct 等动态拼接 key 可能尚未分配或发生地址变化；key/xvalue 生命周期 ABI 建立前，
vmkv 必须返回 `RUNTIMEC_STATUS_UNSUPPORTED`，不得生成零地址、间接 lookup 或 fallback。

## Compilation lifecycle

BinRWIR Compiler 的唯一内容输入是 canonical `binrwir_program_t`。它按固定顺序完成：

1. 验证 Program、manifest version 和 binary interface generation。
2. 构建 `binrwir_execution_plan_t`。
3. 若允许 JIT，调用内部 `jit/` 的 verifier、lowering 与 backend 编译合同。
4. 形成 `binrwir_compilation_t`，其中 execution target 已不可变。

通用 execution plan 可包含紧凑 dispatch、`opid` 特化、静态 `argaddr` 调度、保持写回
边界的指令融合和控制流信息。每个 source logical instruction completion 都必须保留为
不可关闭的 recovery/observable boundary，即使多个 rwir_runtimecimpl 被融合也不能合并完成边界。
plan 不得包含 xvalue 副本、body cache 或通用寄存器分配。register allocation 只属于
内部 JIT lowering。所有优化都必须保留
Read/Write authority、别名可见性和 source logical program counter 观察边界。

内部 JIT 编译判定严格如下；顶层 compilation 对象只在前两种成功方向产生：

| JIT compilation outcome | `runtimec_status_t` | Execution target | Required content |
|---|---|---|---|
| `COMPILED` | `RUNTIMEC_STATUS_SUCCESS` | `JIT` | Program、plan、非空机器码 |
| `NOT_COMPILABLE` | `RUNTIMEC_STATUS_SUCCESS` | `VMBIN` | Program、plan、空机器码 |
| `ERROR` | 明确错误 | 无 | 不产生可执行 compilation |

`NOT_COMPILABLE` 表示 JIT eligibility 已成功判定为不适用。它是建立 VMBIN compilation
的正常编译结果。非法 BinRWIR、映射错误、内存不足与内部错误进入 `ERROR`，必须原样
停止，不能变成任何可执行 compilation。

## Repository and route

repository 只缓存完整 compilation。缓存键绑定 authoritative snapshot、RWIR manifest
version、canonical encoding revision、BinRWIR binary interface generation、compiler
revision；完整 `snapshot.source_revision` 的变化使全部 `argaddr` 失效。租约保证 Program 中的 `argaddr`、plan 和可选机器码
在执行期间一起有效。地址失效后不得继续租用 compilation，必须从明文 RWIR 和当前
kvspace body 地址重新生成。
内部 JIT 不拥有独立 compilation cache 或独立编译入口；机器码只作为完整 compilation
的一部分进入 repository。

route 在执行前调用 `binrwir_compilation_get_view`，读取 Compiler 已决定的
`binrwir_execution_target_t`：

- `JIT`：只调用 `binrwir_jit_execute`；
- `VMBIN`：只调用 `vmbin_execute`；
- 其它值：立即报告非法 compilation。

route 不在执行时调用 eligibility，不在一个执行入口失败后调用另一个入口，也不修改
compilation。执行错误沿所选入口返回并停止。

## Facts and derived state

kvspace 明文保存可恢复语义事实：RWIR、全部 `V.body`、frame facts、logical program
counter、status、error 与 recovery state。canonical Program、execution plan、机器码、
JIT 寄存器状态、profile 和 repository entry 都是可删除并重建的派生状态。

vmbin 是 address-driven memory executor：每次从 `argaddr` 指向的 body 读取，并在逻辑
instruction 完成边界前把全部 Write 结果写回对应 body；它不拥有 value cache 或 register
file。每条 logical instruction completion 都是不可关闭的 recovery/observable boundary。

JIT 是唯一允许在当前逻辑指令内部临时把 body 提升到机器寄存器或 spill 的路径。它只能
对完整完成指令执行 `COMMIT_COMPLETED_STATE`：先把该指令的全部 completed dirty state
materialize 到原 `argaddr`，全部写回成功后再推进 logical program counter/checkpoint。
该提交是完成边界的唯一成功出口。

guard failure、error、recovery 或 resume 遇到当前指令未完成时，JIT 必须执行
`DISCARD_INCOMPLETE_STATE`，丢弃全部 speculative/partial register 与 spill state。discard
不含 materialization entry，不写任何 `argaddr`，也不推进 logical program counter 或
checkpoint。恢复从最后一个已提交完成边界的 kvspace body 重建。

## Read/Write aliasing

RWIR 与 BinRWIR 只有 Read 和 Write 两种参数方向。两个不同 `argidx` 持有相同 `argaddr`
表示实际 Read/Write alias；权限和许可仍由 `(opid, argidx)` 的 canonical signature 决定。
写入该 body 后，后续 Read 必须观察到新值。vmbin 不能保留旧 body 快照；JIT 必须在当前
指令的 speculative state 内维护 alias dependency。完整指令才可提交全部 aliased dirty
state；未完成指令必须全部丢弃。未声明 alias 必须失败。

## Dependencies

```text
interface -> public header + contract + engine
engine/route -> host + vmkv + binrwir_compiler + binrwir + binrwir_compiler/jit executor + vmbin
engine/compilation_repository -> binrwir_compiler + contract
binrwir_compiler -> contract + binrwir + internal jit
vmkv -> contract + host address-resolution contract + binrwir build contracts + generated specification facts
vmbin -> contract + binrwir execution contracts
binrwir_compiler/jit -> contract + binrwir execution contracts
host -> contract + external kvspace interface
```

`binrwir/` 不依赖任一执行器。vmkv 不依赖 BinRWIR Compiler 或执行器。vmbin 不依赖
`binrwir_compiler/jit/`；内部 JIT 也不依赖 vmbin。除了 route，engine 文件不能同时
看见两个确定执行入口。

## Acceptance

- 每个 myrwircap 恰好映射一个 canonical operation；stable identifier 与数组位置无关。
- vmkv 将 operation name 映射为 `opid`，将每个参数映射为 `argidx + uint64 argaddr`。
- host 只返回静态 key 的非零 64 位 XValue 基址；vmkv 唯一以固定 16 字节 head 和溢出检查
  生成 `argaddr = xvalue_address + 16`。
- BinRWIR 不保存参数类型、参数名、key 文本、xvalue/body 副本或通用寄存器状态。
- BinRWIR 只缓存静态地址解析结果；vmbin 消费地址，并在每条逻辑 instruction 完成前把 Write 放回 kvspace。
- 零 XValue 基址、`+16` 溢出、非 16 字节 head ABI、动态 key 或无法证明生命周期均失败
  封闭，不产生 Program。
- 每条 logical instruction completion 都是不可关闭的 recovery/observable boundary。
- JIT 只提交完整完成指令；未完成 register/spill state 只能丢弃，不能 materialize。
- logical program counter/checkpoint 只在 completed state 全部写回成功后推进。
- Compiler 总是先生成 plan，再形成一个确定 execution target 的 compilation。
- repository 缓存 compilation，而不是分散缓存 Program、plan 与 code。
- route 在执行前读取目标，并只调用一个执行入口。
- `NOT_COMPILABLE` 返回 success 并选择 VMBIN；`ERROR` 立即停止。
- BinRWIR 保持顶层中立，内部 JIT 不依赖 vmbin。
- 未实现能力明确返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED`，输出先清空。
- 不存在第三执行器、旧入口、兼容别名或隐藏执行路径。
- 删除全部派生缓存（Program、plan、机器码、JIT 状态、profile、repository）后，从
  kvspace 明文 RWIR 与当前 `V.body` 重建并继续执行（#499）。
- AArch64 backend 与 x86_64 实现同一合同，不允许在 x86_64 完成后长期保留 AArch64 空桩
  （#541）。
