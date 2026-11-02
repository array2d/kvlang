# runtimec 架构与实现总纲

> 当前状态：目录、数据结构、函数边界和失败封闭骨架已经建立；语言执行语义尚未实现。
> `scaffold` 产物名必须保留到真实语义通过验收，防止骨架被误当成可发布实现。

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
    BinRWIR 与 vmbin 只持有其 64 位地址，不复制、不缓存 body，也不建立 register file。
11. **动态地址失败封闭。** 在 key/xvalue 生命周期 ABI 建立前，不能稳定解析 body 地址的
    map、struct 等动态拼接 key 明确返回 `RUNTIMEC_STATUS_UNSUPPORTED`。
12. **每条指令完成都是强制边界。** 每个 logical instruction completion 都必须成为
    recovery/observable boundary，不能由配置、优化、融合或执行目标关闭。
13. **完成提交，未完成丢弃。** JIT 只能把完整完成指令的 dirty state 提交到原
    `argaddr`；当前未完成指令的 speculative/partial register 与 spill state 在 guard
    failure、error、recovery 或 resume 时必须丢弃，绝不写回。

## 2. 唯一主干

```text
myrwircaps + plaintext RWIR in kvspace
    -> vmkv validates RWIR, dereferences arguments, and emits addressed canonical BinRWIR
    -> binrwir_compiler builds execution plan and attempts internal JIT compilation
    -> compile-time target decision: JIT code or VMBIN plan
    -> compilation repository caches the complete compilation
    -> engine route reads the decided target before execution
    -> binrwir_jit_execute or vmbin_execute
    -> route commits complete effects and checkpoint
```

主干顺序不能重排：

- `specification/` 保存行为决策、RWIR 操作事实及 myrwircap 到 canonical BinRWIR 的映射。
- `vmkv/` 从 kvspace 读取含 operation name、参数类型和参数名的明文 RWIR，验证并解引用
  每个参数，只为地址生命周期稳定的参数生成 canonical BinRWIR Program。
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
│   ├── execution_frame.*
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

## 4. Specification 与 canonical 映射

RWIR 是 kvspace 中可恢复的明文存储格式。每条调用保留 operation name；每个参数保留
参数类型和参数名，并以 Read 或 Write 方向参与调用。`myrwircaps` 表示本 runtime 能就地
兑现的 RWIR capability 集合。runtimec 不使用数组
下标、文件顺序或声明顺序作为操作身份。每个 myrwircap 必须在
`specification/rwir_catalog.def` 中拥有一条且只有一条映射记录，该记录必须显式给出：

- 稳定 RWIR operation identifier；
- myrwircap 的规范名称；
- 唯一 canonical BinRWIR operation identifier；
- Read 参数与 Write 参数结构；
- 每个参数在 canonical signature 中的 `argidx`；
- 五语言行为决策引用。

稳定 identifier 是规范数据，不是容器位置。重新排序 manifest、插入新操作或改变生成器
都不能改变既有 identifier；已删除 identifier 不得复用。以下情况都是编译错误：

- myrwircap 没有 canonical 映射；
- 同一个 myrwircap 出现多条映射；
- 两条记录复用同一个稳定 identifier；
- 名称、identifier、参数方向或行为决策无法得到唯一结果。

vmkv 必须逐条执行这个映射并保持语义等价：operation name 映射为 `opid`，参数类型与
参数名经 signature 验证后映射为 `argidx`，参数 key 解引用后的 kvspace `V.body` 地址写入
`uint64 argaddr`。它不能用近似操作、默认操作、未知操作或数组位置补洞。完整规则见
`specification/myrwircap-binrwir-mapping.md`。

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
权限；`argaddr` 是 RWIR 参数 key 解引用后对应 kvspace `V.body` 所在内存的 64 位地址。
operation name、参数类型、参数名、key 文本和 body 均不复制进热 instruction。

公共二进制布局固定为：4 字节 instruction header 仅含 `opid`；每个参数记录在偏移 0
保存 32 位 `argidx`，偏移 4 是必须清零的 32 位地址对齐填充，偏移 8 保存 64 位
`argaddr`。对齐填充没有语义。可变参数的 Read/Write span、source mapping 与存储 framing
只存在于 Program 的私有实现和非二进制只读 view 中，不得重新进入 instruction 字段。

BinRWIR 保留 RWIR 的不固定参数数量语义；确定的一次调用必须编码全部参数对，不能截断
参数或拆分一个原子 RWIR 操作。两个不同 `argidx` 可以持有相同 `argaddr`，这就是已声明
Read/Write alias 在二进制形式中的表示。

BinRWIR 缓存的是静态 key 的地址解析结果，不是 xvalue，更不是 `V.body`。vthread 的
`pc`、`status` 以及 `/lib` 下函数源等 key 生命周期长且地址稳定，是当前允许的优化范围。
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

vmkv 只做明文 RWIR source 读取、结构验证、`opid`/`argidx` 映射、参数 key 解引用、静态
body 地址资格验证和 canonical BinRWIR 生成。类型固化、融合、执行计划、机器码、缓存与
执行目标都不属于 vmkv。

### vmbin

vmbin 是 address-driven memory executor。它从每个 `argaddr` 指向的 kvspace body 读取，
并在逻辑 instruction 完成前把 Write 结果写入对应 body。它不拥有 xvalue/value cache、
software register file 或跨逻辑指令的 body 临时副本。generic、typed、fused
与 continuous handler 只能是 `vmbin_execute` 背后的内部 dispatch 策略；融合不得消除
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
BinRWIR Compiler 也不得把它改写为运行时 key lookup 或借 JIT 绕过限制。

## 11. 五语言决策矩阵

`specification/five-language-behavior-matrix.md` 是语言行为决策的唯一事实源。

- C、Python、Rust、Go、V8/TypeScript 一致：kvlang 必须遵从。
- 五家分裂：记录选择阵营、原因、边界 fixture 和非法输入结果。
- 每个 specification manifest 条目必须引用已有 `behavior-decision`。
- 没有矩阵记录，不能添加 myrwircap 映射、canonical operation 或执行语义。

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
当前 scaffold 没有执行语义，不运行 benchmark 或 tutorial。

## 14. 后续实现合同

实现前按此顺序读取：

1. `README.md`
2. `documentation/architecture/runtimec-architecture.md`
3. `specification/README.md`
4. `specification/myrwircap-binrwir-mapping.md`
5. `specification/five-language-behavior-matrix.md`
6. 目标组件头文件

每次实现只沿唯一主干增加真实能力：先 specification 与映射，再 vmkv 等价生成，再
Compiler 形成 compilation，最后实现两个确定执行入口。遇到不清楚的输入、映射或状态
必须失败，不能添加旧名字别名、隐藏执行路径或空组件兜底。
