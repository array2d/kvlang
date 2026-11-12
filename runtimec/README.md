# runtimec 架构与实现总纲

> 当前状态：目录、数据结构、函数边界和失败封闭骨架已经建立；语言执行语义尚未实现。
> 产物名 `libkvlang_runtime`（用户裁决 2026-11-10，TASK-059）。
> 注意：产物名不再带 `scaffold` 后缀，但**语言执行语义仍未全部通过验收**——名字
> 变了不等于骨架成了可发布实现，验收判据见各 issue 与 `workspace/` 下的报告。

`runtimec/` 是 kvlang 的全新 C11 运行时底座。它只允许一条从语言规范到执行的主干：
kvspace 中的明文 RWIR 先被解析并解引用为进程内 canonical BinRWIR，再由顶层 BinRWIR
Compiler 完成执行计划生成和执行目标判定，最后由 route 调用唯一已确定的执行入口。

未实现的能力必须返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED`，非法输入必须返回
`RUNTIMEC_STATUS_INVALID`。空值、零、`false` 或默认结构不能伪装成功。

## 1. 绝对原则

1. **只有一条正常方向。** 正常完成只返回 `RUNTIMEC_STATUS_SUCCESS`；其它状态必须
   准确说明暂停、等待、未支持、未实现、非法输入、内存不足或错误。
2. **语义先于实现。** `myrwircaps` 与 RWIR specification 定义 runtimec 能处理的
   RWIR；五语言矩阵先裁决行为，代码只实现已裁决语义。
3. **等价生成。** vmkv 只验证 RWIR，并等价生成 canonical BinRWIR；它不优化、不选择
   执行器、不生成机器码。
4. **编译期一次决定。** `binrwir_compiler/` 生成 execution plan，并在同一次编译中把
   compilation 的执行目标确定为 JIT code 或 VMBIN plan。
5. **执行期不改决定。** route 只读取 compilation 中已经确定的目标，并且只调用一个
   执行入口；执行期间不得重新判定、改选或隐式降级。
6. **只有一个策略汇合点。** `engine/route.c` 是唯一同时看见 vmkv、BinRWIR Compiler、
   内部 JIT executor 和 vmbin 的文件，也是唯一执行调度与提交入口。
7. **错误立即停止。** 不猜测输入、不截断、不静默修复、不把错误转换成另一条路径。
8. **不保留旧入口。** 公共层只有 `include/kvlang/runtimec.h` 和
   `interface/public_interface.c` 定义的结构化接口。
9. **Less code, more useful capability.** 不建立第三执行器，不为错误分层增加修补代码。
10. **V.body 只有一个权威驻留位置。** 语言可见且可恢复的 value body 始终位于 kvspace；
    XValue 固定由 16 字节 head 与紧随其后的 `V.body` 组成，因此
    `argaddr = xvalue_address + 16`。BinRWIR 与 vmbin 只持有最终 `argaddr`，不复制、
    不缓存 XValue 或 body，也不建立 register file。
11. **地址派生只有一个所有者。** host 对静态 key 只返回非零、生命周期有效的 64 位
    XValue 基址；只有 vmkv 可以验证固定 16 字节 head ABI、检查加法溢出并执行 `+16`。
    host、BinRWIR、Compiler、vmbin 与 JIT 都不得选择或重算 body 偏移。
12. **地址错误失败封闭。** 零 XValue 基址、`xvalue_address + 16` 溢出、非 16 字节
    head ABI、动态 key 或无法证明生命周期都不得产生 BinRWIR。map、struct 等动态拼接
    key 在 key/xvalue 生命周期 ABI 建立前明确返回 `RUNTIMEC_STATUS_UNSUPPORTED`。
13. **每条指令完成都是强制边界。** 每个 logical instruction completion 都必须成为
    recovery/observable boundary，不能由配置、优化、融合或执行目标关闭。
14. **完成提交，未完成丢弃。** JIT 只能把完整完成指令的 dirty state 提交到原
    `argaddr`；当前未完成指令的 speculative/partial register 与 spill state 在 guard
    failure、error、recovery 或 resume 时必须丢弃，绝不写回。
15. **可观测性与恢复不可交易。** 保留逐指令 `pc` 与 `status` 的可观察、可暂停、可恢复
    语义；不迁移 `mem_frame` / `segment_may_stop` 这类不可观察纯内存帧，调用边界优化
    不得以牺牲任意指令可恢复为代价。
16. **派生状态必须可丢弃可重建。** Program、execution plan、机器码、JIT register/spill、
    profile 与 repository 条目都是从 kvspace 明文 RWIR 与当前 `V.body` 派生的进程内状态。
    删除全部派生缓存后，必须能从明文重建并继续执行；它们不是第二份持久事实。

## 2. 唯一主干

```text
myrwircaps + plaintext RWIR in kvspace
    -> vmkv validates RWIR and requests each static key
    -> host dereferences each static key to a uint64 XValue base address
    -> vmkv validates the fixed 16-byte XValue head and derives argaddr = xvalue_address + 16
    -> vmkv emits addressed canonical BinRWIR
    -> binrwir_compiler builds execution plan and attempts internal JIT compilation
    -> compile-time target decision: JIT code or VMBIN plan
    -> compilation repository caches the complete compilation
    -> engine route reads the decided target before execution
    -> binrwir_jit_execute or vmbin_execute
    -> route commits complete effects and checkpoint
```

主干顺序不能重排：

- `specification/` 保存行为决策与两级能力事实：能力行（语义算子，唯一）与别名表
  （surface 名字 → 能力行，多对一）。一个 surface 名字恰好一条别名映射，一条能力行
  由多个名字共享。
- `vmkv/` 从 kvspace 读取含 operation name、参数类型和参数名的明文 RWIR，把每个
  参数收窄成单一具体类型，通过 host 将每个静态参数 key 解引用为 XValue 基址，并作为
  唯一组件校验固定 16 字节 head、检查 `+16` 溢出和生成最终 `argaddr`；只有全部地址
  合格时才生成 canonical BinRWIR Program。
- `binrwir/` 拥有中立的进程内 Program、`opid` 与 `argidx + uint64 argaddr` 参数记录、
  signature、execution frame、execution exit 与 execution plan 合同；它不属于 vmbin。
- `binrwir_compiler/` 是 canonical BinRWIR 的唯一编译组件。它派生 execution plan，
  在内部 `jit/` 尝试生成机器码，并形成不可再改选的 compilation。
- `engine/compilation_repository` 缓存完整 compilation，使 canonical Program、execution
  plan 和可选机器码在同一租约内有效。
- `engine/route` 在执行前读取 `binrwir_execution_target_t`。目标为 JIT 时只调用
  `binrwir_jit_execute`；目标为 VMBIN 时只调用 `vmbin_execute`。
- `binrwir_jit_execute` 只执行 compilation 已持有的机器码；`vmbin_execute` 按
  `argaddr` 直接读写 kvspace body。两者都不能编译或改变目标。

## 3. 目录主干

```text
runtimec/
├── include/kvlang/runtimec.h          # 唯一公共 C 头
├── interface/                         # 公共请求、结果与精确导出名单
├── contract/                          # 跨组件状态、错误、所有权与主机服务合同
├── host/                              # kvspace 与外部能力适配
├── specification/                    # 行为、RWIR 与 canonical 映射事实源
├── vmkv/                              # 明文 RWIR 验证、参数解引用与 BinRWIR 生成
├── binrwir/                           # 进程内 opid + argidx + argaddr 合同
│   ├── instruction.*
│   ├── argument.*
│   ├── signature.*
│   ├── program.*
│   ├── binvthread_frame.*
│   ├── execution_exit.h
│   └── execution_plan.*
├── binrwir_compiler/                  # BinRWIR 编译、优化与目标判定唯一所有者
│   ├── compiler.*                     # compilation、内部 JIT 请求与最终目标
│   ├── optimization.*                 # 不缓存 body 的 execution plan 优化
│   └── jit/                           # BinRWIR Compiler 的机器码内部实现
│       ├── compiler.*                 # 唯一 Program + plan → code 入口
│       ├── verifier.*                 # 编译期 eligibility
│       ├── lowering_plan.*            # execution plan 的目标绑定
│       ├── lower.*                    # lowering plan → backend request
│       ├── backend.h                  # 目标架构与机器码生成合同
│       ├── backend/                   # x86_64 与 AArch64 并列实现
│       ├── code_memory.*              # 可写到可执行的内存发布
│       ├── code.*                     # 已发布机器码句柄
│       ├── execution_interface.*      # 机器码调用合同
│       └── executor.*                 # 已生成机器码的确定执行入口
├── vmbin/                             # 完整 BinRWIR 基线执行器
├── engine/                            # owner graph、唯一 compilation cache、唯一 route
│   ├── session.*                      # runtimec owner graph 根
│   ├── compilation_repository.*       # 完整 compilation 的唯一缓存
│   └── route.*                        # 执行前单选目标与提交
├── documentation/architecture/       # 架构说明与 Mermaid 源
├── tools/architecture/               # 名称、依赖、矩阵与导出闸门
└── tests/                             # 公共合同与失败封闭测试
```

组件没有源文件时，构建配置必须立即失败。禁止生成空翻译单元让缺失组件看似成功。

## 4. Specification：两级能力表与 canonical 映射

RWIR 是 kvspace 中可恢复的明文存储格式。每条调用保留 operation name；每个参数保留
参数类型和参数名，并以 Read 或 Write 方向参与调用。`myrwircaps` 表示本 runtime 能就地
兑现的 RWIR capability 集合。runtimec 不使用数组下标、文件顺序或声明顺序作为操作身份。

能力事实分**两级**（裁决见 `workspace/622/architect/decision-union-operators.md`）：

```text
capability_table（131 条能力行）   语义算子：union 参数掩码 + 约束 + 结果规则
        ▲
        │  多对一
alias_table（316 条映射）          surface 名字（+ / add / int64·add …）→ 能力行
```

- `rwir/rwir_catalog.yml`：**能力行**，一行一个语义算子。例如 `Add` 只占
  一行，`int64·add`、`float32·add`、`+`、`add` 全部指向它。每行显式给出稳定
  identifier、`opid`、Read/Write 的 **TypeSet bitmask union**、操作数约束
  （`None`/`SameClass`/`NumericCompatible`/`NumericCompare`）、结果规则
  （`Same0`/`Same1`/`Widen`/`WidenInt`/`Bool`/`Float64`/`TargetType`/
  `Identity`/`CharacterUtf8`）、别名策略与五语言行为决策引用。
- `specification/rwir_alias_table.def`：**surface 名字 → 能力行 identifier** 的映射，
  纯数据、不带类型。316 个注册名收敛到 131 条能力行，因此是**多对一**。

一条能力行覆盖多种精度，**但 BinRWIR 里每个参数必须收窄成单一具体类型**：

| 层 | 粒度 | 参数类型 |
|---|---|---|
| capability_table | 语义算子 | union：一行覆盖多种精度 |
| BinRWIR | 每次调用 | 单一具体 langtype，每参数恰好一个 |

manifest 回答「这个算子能接受什么」；BinRWIR 回答「这一次调用实际是什么」。vmkv 负责
在编译期按实参类型把 union 收敛成具体类型（并按 promotion 表算出结果类型），此后不再
有二义。canonical Program 的签名按 `argidx` 索引逐个参数的具体类型；出现 union、
`any` 或空类型一律拒绝。

**具名族行的签名来源**：`union_mask = 0` 的能力行（`compactarray·*`、`string·*`、
`time/duration·*` 等具名族，以及 `call`/`return`/`goto`/`branch`/`debugger` 这类
不产生标量值的控制行）表示它不走标量 union 通道：参数与结果类型由 `/lib/<op>` 安装
的 `def rwir` 给出，`ResultRule = Identity` 意为「取安装签名」。这是「manifest 不得
复制第二张签名表」的直接结果，不是遗漏，也不是 wildcard。`obj`、`map·new`、
`struct·new`、`kvspace/key·abs` 是例外，带类别位。

**生成与门禁**：`rwir_catalog.yml` 与 `rwir_alias_table.def` 都由
`tools/architecture/generate_rwir_catalog.py` 生成。手改会被判 out of date：

```bash
python3 runtimec/binrwir/generate_rwir_catalog.py --check --require-complete
```

稳定 identifier 是规范数据，不是容器位置：重排 manifest、插入新算子或改生成器都不得
改变既有 identifier，已删除 identifier 不得复用。以下都是编译错误：

- 某个 surface 名字没有别名映射；
- 同一条能力行出现两条互相冲突的定义；
- 两条能力行复用同一个稳定 identifier 或同一个 `opid`；
- 名称、identifier、参数方向或行为决策无法得到唯一结果。

vmkv 必须逐条执行这个映射并保持语义等价：operation name 经 alias 表映射为能力行
`opid`，参数类型与参数名经 signature 验证后映射为 `argidx`，且每个参数的类型收窄到
单一具体 langtype；host 将参数 key 解引用为 64 位 XValue 基址，vmkv 验证 XValue head
ABI 固定为 16 字节并以溢出检查计算 `uint64 argaddr = xvalue_address + 16`。它不能用近似
操作、默认操作、未知操作或数组位置补洞。host 不得返回自行选择偏移后的 body 地址。
完整规则见 `specification/myrwircap-binrwir-mapping.md`。

## 5. BinRWIR 中立合同

BinRWIR 属于顶层 `binrwir/`，不属于 vmkv、vmbin 或 JIT 内部实现。它是进程内二进制
派生格式，不是持久化格式，也不是 RWIR 的另一份明文表示：

- vmkv 通过 builder 构造 canonical Program；
- Compiler 只读 canonical Program 并派生 execution plan；
- vmbin 与 `binrwir_compiler/jit/` 消费相同的中立合同；
- Program finalize 后不可变，但任何地址生命周期失效都会使整个 Program 失效；
- execution plan、机器码、profile 与缓存都是可删除并重建的派生状态。

热 instruction 只保存 `opid`；每个参数只保存：

```text
opid
  argidx + uint64 argaddr
  argidx + uint64 argaddr
  ...
```

`argidx` 在由 `opid` 选中的 canonical signature 中确定参数顺序、类型和 Read/Write
权限。key 解引用结果是 64 位 XValue 基址，不是任意 body 地址；XValue head ABI 固定为
16 字节，vmkv 唯一执行 `argaddr = xvalue_address + 16`，所以 `argaddr` 仍直接指向 kvspace
`V.body`。operation name、参数类型、参数名、key 文本、XValue 和 body 均不复制进热
instruction。

公共二进制布局固定为：2 字节 instruction header 仅含 `opid`；每个参数记录在偏移 0
保存 32 位 `argidx`，偏移 4 是必须清零的 32 位地址对齐填充，偏移 8 保存 64 位
`argaddr`。对齐填充没有语义。可变参数的 Read/Write span、source mapping 与存储 framing
只存在于 Program 的私有实现和非二进制只读 view 中，不得重新进入 instruction 字段。

BinRWIR 保留 RWIR 的不固定参数数量语义；确定的一次调用必须编码全部参数对，不能截断
参数或拆分一个原子 RWIR 操作。两个不同 `argidx` 可以持有相同 `argaddr`，这就是已声明
Read/Write alias 在二进制形式中的表示。

BinRWIR 保存的是 vmkv 派生后的最终 `argaddr`，不是 XValue 基址、XValue 或 `V.body`；
vmbin 也只消费该最终地址。vthread 的 `pc`、`status` 以及 `/lib` 下函数源等 key 生命周期
长且 XValue 基址稳定，是当前允许的优化范围。
map、struct 等依赖运行时数据动态拼接的 key，其 body 可能尚未分配或地址会变化；在
key/xvalue 生命周期 ABI 能提供 pin、generation 与失效合同前，vmkv 必须返回
`RUNTIMEC_STATUS_UNSUPPORTED`，不得写零地址、延迟间接查找或建立 fallback Program。

## 6. BinRWIR Compiler

`binrwir_compiler/` 负责 canonical BinRWIR 之后、执行之前的全部派生工作：

1. 验证 canonical Program 与 specification identity。
2. 生成 execution plan，包括紧凑指令调度、`opid` 特化、静态 `argaddr` 使用、在保持
   每条逻辑指令写回边界前提下的融合，以及控制流派生信息。
3. 在启用 JIT 时，由内部 `jit/` verifier、lowering 与 backend 尝试生成机器码。
4. 把 Program、execution plan、已决定目标与可选机器码封装为 compilation。

内部 JIT 编译判定只有以下三种有意义的方向；顶层 compilation 对象只在前两种成功
方向产生，因此不存在可观察的“正在编译”或“错误 compilation”对象：

| Compiler 判定 | 调用状态 | compilation 内容 | 后续方向 |
|---|---|---|---|
| `COMPILED` | `RUNTIMEC_STATUS_SUCCESS` | JIT 目标、Program、plan、机器码 | route 只调用 `binrwir_jit_execute` |
| `NOT_COMPILABLE` | `RUNTIMEC_STATUS_SUCCESS` | VMBIN 目标、Program、plan、无机器码 | route 只调用 `vmbin_execute` |
| `ERROR` | 明确错误状态 | 不产生可执行 compilation | 立即停止 |

`NOT_COMPILABLE` 是成功完成的可编译性判定，不是执行错误。未知目标架构、明确不支持的
操作或无法满足机器码执行合同可以得到该判定。非法 BinRWIR、映射不完整、内存不足与
内部错误必须进入 `ERROR` 并立即返回，不能生成 VMBIN compilation。

通用 BinRWIR execution plan 不得包含任何 value body 副本或通用寄存器分配。寄存器分配
只允许存在于内部 JIT lowering 结果中。

## 7. Repository 与 route

repository 的缓存单位是 compilation，不是裸 Program、裸 execution plan 或裸机器码。
缓存键至少绑定 authoritative snapshot、RWIR manifest version、canonical encoding
revision、BinRWIR binary interface generation 与 compiler revision。租约保证 execution
期间 compilation 的全部对象同时有效。
因为 BinRWIR 含进程内 `argaddr`，缓存身份还必须受完整 `snapshot.source_revision` 约束；地址
失效后不能继续租用旧 compilation，只能从 kvspace 中的 RWIR 与当前 body 地址重新生成。
内部 JIT 不建立第二套 code cache，也不存在绕过 BinRWIR Compiler 的独立编译入口。

route 的“选择”只表示读取已经编译好的 `execution_target` 并调用对应入口。route 不再次
询问 JIT 是否可用，不重新验证 eligibility，也不在一个入口返回后调用另一个入口。执行
错误、机器码错误和内部错误都沿原路径报告并停止。

每条 logical instruction completion 都是不可关闭的 recovery/observable boundary。vmbin
在该边界完成前，所有 Write 结果必须已经位于对应 `argaddr` 指向的 kvspace body。JIT
只能在当前逻辑指令内部把 body 临时提升到机器寄存器或 spill；指令完整完成后，才允许
执行 `COMMIT_COMPLETED_STATE`，把该指令的全部 dirty state materialize 到原 `argaddr`。
只有全部 body 写回成功，route 才能推进 logical program counter 与 checkpoint。

guard failure、error、recovery 或 resume 落在当前指令尚未完成时，JIT 必须执行
`DISCARD_INCOMPLETE_STATE`：丢弃该指令全部 speculative/partial register 与 spill state，
materialization entry 必须为空，不得向任何 `argaddr` 写回部分结果。logical program
counter 与 checkpoint 保持最后一个已提交完成边界；恢复只从 kvspace 重建机器状态。

## 8. Read/Write 与别名

RWIR 和 BinRWIR 只有 Read 参数与 Write 参数。原地算法通过两个不同 `argidx` 保存同一个
`argaddr` 表达显式别名，不引入第三种参数方向：

```text
Read argidx  ──> uint64 argaddr ──┐
                                  ├──> one kvspace V.body
Write argidx ──> uint64 argaddr ──┘
```

- Read `argidx` 只能读，Write `argidx` 只能写；权限由 `(opid, argidx)` 的 signature 决定。
- 两个 `argidx` 始终不同，即使 `argaddr` 完全相同。
- canonical signature 是 alias policy 的唯一拥有者，地址相等只证明实际发生 alias。
- 写入后，后续通过 Read 别名看到新值，并遵循程序顺序。
- 未声明允许的别名立即返回 `RUNTIMEC_STATUS_INVALID`。
- vmbin 不得用预读或 body cache 让后续 Read 看见旧值；JIT 必须维护 alias dependency。
  当前指令内的 aliased Read 观察 speculative Write，只有指令完整完成后才能整体写回；
  未完成状态必须整体丢弃。

## 9. 组件边界

### vmkv

vmkv 只做明文 RWIR source 读取、结构验证、`opid`/`argidx` 映射、通过 host 将参数 key
解引用为 XValue 基址、静态地址资格验证，以及固定 16 字节 head 的唯一 `+16` 派生和
canonical BinRWIR 生成。host 不能决定 body 偏移。类型固化、融合、执行计划、机器码、
缓存与执行目标都不属于 vmkv。

### vmbin

vmbin 是 address-driven memory executor。它从每个 `argaddr` 指向的 kvspace body 读取，
并在逻辑 instruction 完成前把 Write 结果写入对应 body。它不拥有 xvalue/value cache、
software register file 或跨逻辑指令的 body 临时副本。generic、typed、fused
与 continuous rwir_runtimecimpl 只能是 `vmbin_execute` 背后的内部 dispatch 策略；融合不得消除
kvspace 写回，也不得跳过任何 logical instruction completion recovery/observable boundary。

### binrwir_compiler/jit

JIT 是 BinRWIR Compiler 的内部机器码实现，不是 VM，也不是并列顶层组件。它是唯一允许
把 `V.body` 临时提升到机器寄存器、进行 register allocation 和使用 spill 的路径。每条
logical instruction completion 都是强制 safepoint：完整完成时执行
`COMMIT_COMPLETED_STATE`；guard failure、error、recovery 或 resume 遇到未完成指令时执行
`DISCARD_INCOMPLETE_STATE`。提交只能包含 completed dirty state；丢弃不能包含任何
materialization entry。logical program counter/checkpoint 只在完整提交成功后推进。
可编译性必须在 compilation 建立期间确定；`binrwir_jit_execute` 不接收 RWIR、不解析 key、
不依赖 vmbin，也不拥有执行期目标选择能力。

## 10. 状态与错误

`runtimec_status_t` 的唯一成功值是 `RUNTIMEC_STATUS_SUCCESS`：

- `YIELD`、`WAIT`：执行尚未正常完成；
- `UNSUPPORTED`：当前合同明确不接受该能力；
- `NOT_IMPLEMENTED`：骨架尚无该能力；
- `INVALID`：输入违反合同；
- `OUT_OF_MEMORY`、`ERROR`：明确失败。

`COMPILED` 与 `NOT_COMPILABLE` 是内部 JIT 编译判定，不是替代 `runtimec_status_t` 的
第二套调用状态。`ERROR` 只通过明确错误状态返回，并且不产生 compilation。所有输出
参数必须先进入明确无效状态，再开始可能失败的工作。

当前动态拼接 key 属于 `UNSUPPORTED`：vmkv 不得为 map/struct 参数产生零 `argaddr`，
BinRWIR Compiler 也不得把它改写为运行时 key lookup 或借 JIT 绕过限制。零 XValue 基址、
`+16` 溢出、非 16 字节 XValue head ABI 或无法证明地址生命周期同样必须在 Program 产生前
失败封闭。

### 10.1 诊断信息沿调用链**递减**：上层不许覆盖下层的具体消息

一条失败穿过 编码 → 编译 → 执行窗口 → route 时，**越上层越不知道原因**：
上层只知道「这一步失败了」，下层才知道「为什么失败」。所以：

> **从下层带上来的具体消息，上层不许用通用消息覆盖。**
> 上层可以补自己的分类（`detail`、状态），但消息必须让位给更精确的那一条。

反例（2026-11-05 实测）：`execution_window_fail` 曾无条件 `runtimec_error_set`，
把编码器的

```
vmkv local types: local '_16' is written with a non-concrete langtype ('')
```

覆盖成

```
compilation was not produced
```

于是排障只能看到「编译没产出」和一个 `detail`。根因在链上被通用消息擦掉了。

配套的两个约束：

1. **保留的前提是「下层本次确实说过话」**——接收 error 的入口必须**每次清空** `error`，
   否则「保留」会变成「沿用上一次调用的消息」，而那看起来完全正常。
2. **分类与消息各归其位**：`detail` 回答「哪一步」，`message` 回答「为什么」；
   保留消息不等于放弃本层分类，两者写在不同字段。

同族判据：`runtimec/tests/kv_runner.c` 抓失败输出时跳过 `info:` 行、只取 `error:` 行——
**报错必须贴真正把流程拦下来的那一行**。

## 11. 五语言决策矩阵

`specification/five-language-behavior-matrix.md` 是语言行为决策的唯一事实源。

- C、Python、Rust、Go、V8/TypeScript 一致：kvlang 必须遵从。
- 五家分裂：记录选择阵营、原因、边界 fixture 和非法输入结果。
- 每个 specification manifest 条目必须引用已有 `behavior-decision`。
- 没有矩阵记录，不能添加能力行、别名映射、canonical operation 或执行语义。

## 12. 依赖方向

允许方向：

```text
interface -> public header + contract + engine
engine/route -> host + vmkv + binrwir_compiler + binrwir + binrwir_compiler/jit executor + vmbin
engine/compilation_repository -> binrwir_compiler + contract
binrwir_compiler -> contract + binrwir + internal jit
vmkv -> contract + host address-resolution contract + binrwir build contracts + specification generated facts
vmbin -> contract + binrwir execution contracts
binrwir_compiler/jit -> contract + binrwir execution contracts
host -> contract + external kvspace interface
```

禁止方向：

```text
binrwir -> vmkv/vmbin/binrwir_compiler/engine
vmkv -> binrwir_compiler/vmbin
vmbin -> vmkv/binrwir_compiler
binrwir_compiler/jit -> vmkv/vmbin/concrete host
repository -> vmkv/vmbin/binrwir_compiler/jit
engine files other than route -> vmkv/vmbin/binrwir_compiler/jit
public header -> internal runtimec header
```

## 13. 公共接口、名称与验收

公共层只安装 `include/kvlang/runtimec.h`，只导出 `interface/exports.txt` 中的符号。
公共头不暴露 RWIR、BinRWIR、vmkv、BinRWIR Compiler、vmbin、JIT 或 kvspace 内部结构。

除 kvlang、kvspace、runtimec、RWIR、BinRWIR、vmkv、vmbin、JIT、vthread、langtype
及 C 标准类型外，自造名称使用完整单词，不保留旧名称别名或转发头。

最小 fresh 验收：

```bash
cmake -S runtimec -B /tmp/kvlang-runtimec-build \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/kvlang-runtimec-build --parallel
ctest --test-dir /tmp/kvlang-runtimec-build --output-on-failure
```

语义实现后按 shared memory → benchmark quick → benchmark full → tutorial 的顺序验证。
当前产物尚未通过完整的执行语义验收，不运行 benchmark 或 tutorial。

## 14. 后续实现合同

实现前按此顺序读取：

1. `README.md`
2. `documentation/architecture/runtimec-architecture.md`
3. `specification/README.md`
4. `specification/myrwircap-binrwir-mapping.md`
5. `specification/five-language-behavior-matrix.md`
6. 目标组件头文件

实现按 #622 的依赖顺序在 runtimec 结构内落地，禁止跨阶段抢跑。

**优化与执行的分层**（裁决见
`workspace/622/architect/decision-binrwir-optimization-pipeline.md`）：
明文 RWIR 先被 vmkv 收敛成不可变 BinRWIR Program，再经**多轮优化**（每轮
Program → Program 的纯函数）得到优化后 IR，最后交给 codegen 层。**VMBIN 与 JIT 是
同一份优化后 IR 的两个 codegen 后端，不是两套语义路径**；两者之间差的是代码生成方式，
不是语义。VMBIN 只承担语义正确性、不设速度门槛，性能由 JIT 承担。

- 阶段 1：填 `vmkv/` 与 `binrwir/` 的明文语义验证、单一 manifest 生成、具体类型收敛与
  等价 canonical BinRWIR 生成。
- 阶段 2：填 `vmbin/` 与 `engine/` 的逐指令 `pc`/`status` 执行引擎，作为优化后 IR 的
  基线 codegen 后端；只验收语义正确性，不设速度门槛。
- 阶段 3：填 `binrwir_compiler/` 的优化轮次（P1 常量折叠 → …，逐轮带合法性与等价性
  验收）与内部 JIT 后端：M1 手写发射器 → JIT2 → 生成器标量矩阵 → x86_64；AArch64
  后端同阶段一等实现，不得顺延为空桩。性能验收在本阶段的默认档（JIT 开启）上做。
- 阶段 4：runtimec 成为默认 runtime，冻结并删除 `runtimec2/`，旧 `runtime/` 不再作为
  独立实现继续演进。

每个阶段都必须先补齐对应 specification 决策与映射，再实现函数。遇到不清楚的输入、
映射或状态必须失败，不能添加旧名字别名、隐藏执行路径或空组件兜底。

## 15. 与 #622、#499、#541 的对齐

runtimec 是 #622“runtime 回归 C”的新落点：它取代旧 `runtime/` 与 `runtimec2/` 两支
实现，成为唯一 C11 运行时主干；两个旧目录只读冻结，不出现在 runtimec 构建、测试或
文档的主干中。

- 语义裁决先落 spec：`#622 4.1` 六项与 `#622 4.3` 两项已写入
  `specification/five-language-behavior-matrix.md`；没有矩阵决策，不得实现对应语义。
- 单一事实源：`rwir/rwir_catalog.yml` 是能力行（语义算子）的唯一 manifest，
  `specification/rwir_alias_table.def` 是 surface 名字到能力行的唯一映射；两份都由
  `generate_rwir_catalog.py` 生成，CI 用 `--check --require-complete` 断言不存在可与
  它漂移的第二份表。能力行唯一、别名多对一。
- 检查点粒度：保留逐指令 `pc` + `status`，不迁移 `mem_frame`（`#622 4.4`）。
- BIN 栈可重建：删除全部派生缓存后，从 kvspace 明文 RWIR 与当前 `V.body` 重建，并从
  最后已提交完成边界继续执行（#499）。
- AArch64 一等后端：`binrwir_compiler/jit/backend/aarch64.c` 与 x86_64 是 peer，必须实现
  同一 backend 合同，不得长期保留空实现（#541）。
- JIT 发射器先定形态：先手写 M1 发射器并用固定 opcode 表量出代码量与可读性，再决定
  JIT2 组织；标量矩阵由生成器产生，禁止手写第二份表。
