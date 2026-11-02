# runtimec BinRWIR Compiler Architecture

runtimec 只允许一条从 RWIR specification 到 execution 的主干。任何组件都不能建立旁路、
第二次目标判定或第三执行器。

## Unique trunk

```text
myrwircaps + RWIR specification
    -> vmkv validates RWIR and emits equivalent canonical BinRWIR
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
| `vmkv/` | RWIR source 读取、验证、稳定 operation identifier 解析、等价 BinRWIR 生成 | 优化、execution plan、执行目标或机器码 |
| `binrwir/` | canonical Program、instruction、reference、signature、execution frame、execution exit、execution plan 合同 | vmkv、vmbin 或 JIT 策略 |
| `binrwir_compiler/` | execution plan 生成、内部 JIT compilation 请求、统一 compilation 与目标判定 | RWIR 解析、执行或 checkpoint 提交 |
| `binrwir_compiler/jit/` | eligibility、lowering、backend、机器码生命周期与已生成机器码执行 | 独立 VM 身份、RWIR、vmbin 依赖、语言语义或执行期目标选择 |
| `engine/compilation_repository` | compilation 所有权、索引、租约与失效 | 编译、执行或目标改选 |
| `engine/route` | repository 查询、未命中编译、执行前目标读取、唯一执行调度、effect 与 checkpoint 提交 | operation 语义或编译 eligibility |
| `vmbin/` | 全部合法 canonical BinRWIR 的基线执行 | BinRWIR 所有权、JIT 选择或持久事实 |
| `host/` | kvspace 与外部能力适配 | BinRWIR 检查或执行策略 |
| `interface/` | 公共请求翻译与精确导出 | 内部结构、语言语义或执行策略 |

## Specification mapping

每个 myrwircap 必须恰好拥有一个 canonical BinRWIR operation 映射。映射记录显式保存
稳定 RWIR operation identifier、规范名称、canonical operation identifier、Read/Write
参数结构和行为决策引用。

operation identifier 是 specification 数据，不是 `myrwircaps` 数组下标，也不能由文件
顺序、声明顺序或生成顺序计算。重新排序或插入记录不能改变既有 identity。缺失记录、
同一 capability 的重复记录、identifier 重复、签名不一致或歧义都使 compilation 立即
失败。vmkv 不提供默认 operation，也不把不完整映射变成可执行 Program。

## Canonical BinRWIR

BinRWIR 是顶层中立模块，不属于 vmbin。canonical Program 只保存与已验证 RWIR 等价且
执行所必需的稳定事实。类型固化、KV 寻址、融合、虚拟寄存器、控制流分析、机器码、
profile 和缓存都属于可重建派生状态，不能写回 canonical Program。

每条 instruction 使用固定 Read 数量和 Write 数量，并通过一个规范化 view 暴露：

```text
[ReadReference ...][WriteReference ...]
```

RWIR 可以具有不固定参数数量；确定的一次调用在 canonical BinRWIR 中必须保存完整数量。
窄式与宽式编码只能改变存储形式，不能改变语义、参数顺序或观察边界。

## Compilation lifecycle

BinRWIR Compiler 的唯一内容输入是 canonical `binrwir_program_t`。它按固定顺序完成：

1. 验证 Program、manifest version 和 binary interface generation。
2. 构建 `binrwir_execution_plan_t`。
3. 若允许 JIT，调用内部 `jit/` 的 verifier、lowering 与 backend 编译合同。
4. 形成 `binrwir_compilation_t`，其中 execution target 已不可变。

execution plan 可包含六类派生优化：指令编码、类型固化、KV 寻址、指令融合、虚拟
寄存器和控制流信息。所有优化都必须保留 canonical Program 语义、Read/Write authority、
别名可见性和 source logical program counter 观察边界。

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
version、canonical encoding revision、BinRWIR binary interface generation 与 compiler
revision；租约保证 Program、plan 和可选机器码在执行期间一起存活。失效只阻止新租约，
不释放仍被租约持有的对象。
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

kvspace 明文保存可恢复语义事实：RWIR、值、frame facts、logical program counter、status、
error 与 recovery state。canonical Program、execution plan、机器码、register cache、profile
和 repository entry 都是可删除并重建的派生状态。

只有完整 instruction effect 成功后，route 才按 values、logical program counter、status
或 error 的顺序提交。任何组件都不能让半条 instruction 对外可见。

## Read/Write aliasing

RWIR 与 BinRWIR 只有 Read 和 Write 两种参数方向。Read register 与 Write register 可以按
Signature 的显式 alias policy 绑定同一个 canonical Cell。写入后，后续 Read 必须观察到
新值；Compiler 不能缓存、提升或重排会破坏该顺序的读取。未声明 alias 必须失败。

## Dependencies

```text
interface -> public header + contract + engine
engine/route -> host + vmkv + binrwir_compiler + binrwir + binrwir_compiler/jit executor + vmbin
engine/compilation_repository -> binrwir_compiler + contract
binrwir_compiler -> contract + binrwir + internal jit
vmkv -> contract + binrwir build contracts + generated specification facts
vmbin -> contract + binrwir execution contracts
binrwir_compiler/jit -> contract + binrwir execution contracts
host -> contract + external kvspace interface
```

`binrwir/` 不依赖任一执行器。vmkv 不依赖 BinRWIR Compiler 或执行器。vmbin 不依赖
`binrwir_compiler/jit/`；内部 JIT 也不依赖 vmbin。除了 route，engine 文件不能同时
看见两个确定执行入口。

## Acceptance

- 每个 myrwircap 恰好映射一个 canonical operation；stable identifier 与数组位置无关。
- vmkv 只进行验证与等价生成。
- Compiler 总是先生成 plan，再形成一个确定 execution target 的 compilation。
- repository 缓存 compilation，而不是分散缓存 Program、plan 与 code。
- route 在执行前读取目标，并只调用一个执行入口。
- `NOT_COMPILABLE` 返回 success 并选择 VMBIN；`ERROR` 立即停止。
- BinRWIR 保持顶层中立，内部 JIT 不依赖 vmbin。
- 未实现能力明确返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED`，输出先清空。
- 不存在第三执行器、旧入口、兼容别名或隐藏执行路径。
