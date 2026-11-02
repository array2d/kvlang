# runtimec 架构与实现总纲

> 当前状态：目录、数据结构、函数边界和失败封闭骨架已经建立；语言执行语义尚未实现。
> `scaffold` 产物名必须保留到真实语义通过验收，防止骨架被误当成可发布实现。

`runtimec/` 是 kvlang 的全新 C11 运行时底座。它只允许一条从语言规范到执行的主干：
RWIR 先被等价编码为 canonical BinRWIR，再由顶层 BinRWIR Compiler 完成全部执行计划
生成和执行目标判定，最后由 route 调用唯一已确定的执行入口。

未实现的能力必须返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED`，非法输入必须返回
`RUNTIMEC_STATUS_INVALID`。空值、零、`false` 或默认结构不能伪装成功。

## 1. 绝对原则

1. **只有一条正常方向。** 正常完成只返回 `RUNTIMEC_STATUS_SUCCESS`；其它状态必须
   准确说明暂停、等待、未支持、未实现、非法输入、内存不足或错误。
2. **语义先于实现。** `myrwircaps` 与 RWIR specification 定义 runtimec 能处理的
   RWIR；五语言矩阵先裁决行为，代码只实现已裁决语义。
3. **等价生成。** vmkv 只验证 RWIR，并等价生成 canonical BinRWIR；它不优化、不选择
   执行器、不生成机器码。
4. **编译期一次决定。** BinRWIR Compiler 生成 execution plan，并在编译阶段把
   compilation 的执行目标确定为 VMJIT 或 VMBIN。
5. **执行期不改决定。** route 只读取 compilation 中已经确定的目标，并且只调用一个
   执行入口；执行期间不得重新判定、改选或隐式降级。
6. **只有一个策略汇合点。** `engine/route.c` 是唯一同时看见 vmkv、Compiler、vmjit
   和 vmbin 的文件，也是唯一执行调度与提交入口。
7. **错误立即停止。** 不猜测输入、不截断、不静默修复、不把错误转换成另一条路径。
8. **不保留旧入口。** 公共层只有 `include/kvlang/runtimec.h` 和
   `interface/public_interface.c` 定义的结构化接口。
9. **Less code, more useful capability.** 不建立第三执行器，不为错误分层增加修补代码。

## 2. 唯一主干

```text
myrwircaps + RWIR specification
    -> vmkv validates RWIR and emits equivalent canonical BinRWIR
    -> BinRWIR Compiler builds execution plan
    -> compile-time target decision: VMJIT or VMBIN
    -> compilation repository caches the complete compilation
    -> engine route reads the decided target before execution
    -> vmjit_execute or vmbin_execute
    -> route commits complete effects and checkpoint
```

主干顺序不能重排：

- `specification/` 保存行为决策、RWIR 操作事实及 myrwircap 到 canonical BinRWIR 的映射。
- `vmkv/` 从 kvspace 读取 RWIR，验证后只生成语义等价的 canonical BinRWIR Program。
- `binrwir/` 拥有中立的 Program、instruction、reference、signature、execution frame、
  execution exit 与 execution plan 合同；它不属于 vmbin。
- `compiler/` 是顶层 BinRWIR Compiler。它从 canonical Program 派生 execution plan，
  请求 vmjit 编译，并形成不可再改选的 compilation。
- `engine/compilation_repository` 缓存完整 compilation，使 canonical Program、execution
  plan 和可选机器码在同一租约内有效。
- `engine/route` 在执行前读取 `binrwir_execution_target_t`。目标为 VMJIT 时只调用
  `vmjit_execute`；目标为 VMBIN 时只调用 `vmbin_execute`。
- `vmjit_execute` 只执行已经生成的机器码；`vmbin_execute` 执行 canonical Program，
  并可消费 Compiler 生成的 execution plan。两者都不能改变 compilation 的目标。

## 3. 目录主干

```text
runtimec/
├── include/kvlang/runtimec.h          # 唯一公共 C 头
├── interface/                         # 公共请求、结果与精确导出名单
├── contract/                          # 跨组件状态、错误、所有权与主机服务合同
├── host/                              # kvspace 与外部能力适配
├── specification/                    # 行为、RWIR 与 canonical 映射事实源
├── vmkv/                              # RWIR 验证与等价 BinRWIR 生成
├── binrwir/                           # 中立 canonical Program 与执行数据合同
│   ├── instruction.*
│   ├── reference.*
│   ├── signature.*
│   ├── program.*
│   ├── execution_binding.*
│   ├── execution_frame.*
│   ├── execution_exit.h
│   └── execution_plan.*
├── compiler/                          # 顶层 BinRWIR 编译与优化唯一所有者
│   ├── compiler.*                     # compilation、目标判定、vmjit 请求
│   └── optimization.*                 # 六类 execution plan 优化
├── vmbin/                             # 完整 BinRWIR 基线执行器
├── vmjit/                             # 已验证 BinRWIR 的机器码编译与确定执行
│   ├── compiler.*                     # 唯一 Program + plan → code 入口
│   ├── verifier.*                     # 编译期 eligibility
│   ├── lowering_plan.*                # execution plan 的目标绑定
│   ├── lower.*                        # lowering plan → backend request
│   ├── backend/                       # x86_64 与 AArch64 并列实现
│   ├── code_memory.*                  # 可写到可执行的内存发布
│   ├── code.*                         # 已发布机器码句柄
│   ├── execution_interface.*          # 机器码调用合同
│   └── vmjit.*                        # 仅确定执行入口
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

`myrwircaps` 表示本 runtime 能就地兑现的 RWIR capability 集合。runtimec 不使用数组
下标、文件顺序或声明顺序作为操作身份。每个 myrwircap 必须在
`specification/rwir_catalog.def` 中拥有一条且只有一条映射记录，该记录必须显式给出：

- 稳定 RWIR operation identifier；
- myrwircap 的规范名称；
- 唯一 canonical BinRWIR operation identifier；
- Read 参数与 Write 参数结构；
- 五语言行为决策引用。

稳定 identifier 是规范数据，不是容器位置。重新排序 manifest、插入新操作或改变生成器
都不能改变既有 identifier；已删除 identifier 不得复用。以下情况都是编译错误：

- myrwircap 没有 canonical 映射；
- 同一个 myrwircap 出现多条映射；
- 两条记录复用同一个稳定 identifier；
- 名称、identifier、参数方向或行为决策无法得到唯一结果。

vmkv 必须逐条执行这个映射并保持语义等价。它不能用近似操作、默认操作、未知操作或
数组位置补洞。完整规则见 `specification/myrwircap-binrwir-mapping.md`。

## 5. BinRWIR 中立合同

BinRWIR 属于顶层 `binrwir/`，不属于 vmkv、vmbin 或 vmjit：

- vmkv 通过 builder 构造 canonical Program；
- Compiler 只读 canonical Program 并派生 execution plan；
- vmbin 与 vmjit 消费相同的中立合同；
- Program finalize 后不可变；
- execution plan、机器码、profile 与缓存都是可删除并重建的派生状态。

BinRWIR 保留 RWIR 的不固定参数数量语义，但编译完成后每条 instruction 的 Read 数量
与 Write 数量固定。消费者只看到规范化顺序：

```text
[ReadReference ...][WriteReference ...]
```

紧凑 instruction header 覆盖常见参数数量，宽形式可引用冷侧表。编码限制不能成为截断
参数或拆分一个原子 RWIR 操作的理由。

## 6. BinRWIR Compiler

`compiler/` 负责 canonical BinRWIR 之后、执行之前的全部派生工作：

1. 验证 canonical Program 与 specification identity。
2. 生成 execution plan，包括指令编码、类型固化、KV 寻址、指令融合、虚拟寄存器和
   控制流派生信息。
3. 在启用 vmjit 时请求 vmjit verifier、lowering 与 backend 生成机器码。
4. 把 Program、execution plan、已决定目标与可选机器码封装为 compilation。

vmjit 编译判定只有以下三种有意义的方向；顶层 compilation 对象只在前两种成功
方向产生，因此不存在可观察的“正在编译”或“错误 compilation”对象：

| Compiler 判定 | 调用状态 | compilation 内容 | 后续方向 |
|---|---|---|---|
| `COMPILED` | `RUNTIMEC_STATUS_SUCCESS` | VMJIT 目标、Program、plan、机器码 | route 只调用 `vmjit_execute` |
| `NOT_COMPILABLE` | `RUNTIMEC_STATUS_SUCCESS` | VMBIN 目标、Program、plan、无机器码 | route 只调用 `vmbin_execute` |
| `ERROR` | 明确错误状态 | 不产生可执行 compilation | 立即停止 |

`NOT_COMPILABLE` 是成功完成的可编译性判定，不是执行错误。未知目标架构、明确不支持的
操作或无法满足机器码执行合同可以得到该判定。非法 BinRWIR、映射不完整、内存不足与
内部错误必须进入 `ERROR` 并立即返回，不能生成 VMBIN compilation。

## 7. Repository 与 route

repository 的缓存单位是 compilation，不是裸 Program、裸 execution plan 或裸机器码。
缓存键至少绑定 authoritative snapshot、RWIR manifest version、canonical encoding
revision、BinRWIR binary interface generation 与 compiler revision。租约保证 execution
期间 compilation 的全部对象同时有效。
vmjit 不建立第二套 code cache，也不存在绕过顶层 Compiler 的 `get_or_compile` 入口。

route 的“选择”只表示读取已经编译好的 `execution_target` 并调用对应入口。route 不再次
询问 vmjit 是否可用，不重新验证 eligibility，也不在一个入口返回后调用另一个入口。执行
错误、机器码错误和内部错误都沿原路径报告并停止。

只有完整 instruction effect 成功后，route 才按 values → logical program counter →
status/error 的顺序提交 checkpoint。kvspace 明文状态始终是可恢复语义事实；compilation
及其缓存可以删除并从同一事实重建。

## 8. Read/Write 与别名

RWIR 和 BinRWIR 只有 Read 参数与 Write 参数。原地算法通过显式别名把两个不同 register
绑定到同一个 canonical Cell，不引入第三种参数方向：

```text
Read register  ──> Binding ──┐
                             ├──> canonical Cell
Write register ──> Binding ──┘
```

- Read register 只能读，Write register 只能写。
- 两个 register identifier 始终不同，即使 Binding 指向同一 Cell。
- callee Signature 是 alias policy 的唯一拥有者。
- 写入后，后续通过 Read 别名看到新值，并遵循程序顺序。
- 未声明允许的别名立即返回 `RUNTIMEC_STATUS_INVALID`。
- Compiler 的优化必须保留别名依赖与每个 source logical program counter 的观察边界。

## 9. 组件边界

### vmkv

vmkv 只做 RWIR source 读取、结构验证、稳定 identifier 解析和 canonical BinRWIR 生成。
类型固化、融合、执行计划、机器码、缓存与执行目标都不属于 vmkv。

### vmbin

vmbin 实现全部合法 canonical BinRWIR 的基线语义。generic、typed、fused 与 continuous
handler 只能是 `vmbin_execute` 背后的内部策略，不能成为额外执行入口。

### vmjit

vmjit 只做已验证 BinRWIR 的机器码 eligibility、lowering、backend 生成和已生成机器码
执行。可编译性必须在 compilation 建立期间确定；`vmjit_execute` 不接收 RWIR，不访问
kvspace，不依赖 vmbin，也不拥有执行目标选择能力。

## 10. 状态与错误

`runtimec_status_t` 的唯一成功值是 `RUNTIMEC_STATUS_SUCCESS`：

- `YIELD`、`WAIT`：执行尚未正常完成；
- `UNSUPPORTED`：当前合同明确不接受该能力；
- `NOT_IMPLEMENTED`：骨架尚无该能力；
- `INVALID`：输入违反合同；
- `OUT_OF_MEMORY`、`ERROR`：明确失败。

`COMPILED` 与 `NOT_COMPILABLE` 是 vmjit 编译判定，不是替代 `runtimec_status_t` 的
第二套调用状态。`ERROR` 只通过明确错误状态返回，并且不产生 compilation。所有输出
参数必须先进入明确无效状态，再开始可能失败的工作。

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
engine/route -> host + vmkv + compiler + binrwir + vmjit + vmbin
engine/compilation_repository -> compiler + contract
compiler -> contract + binrwir + vmjit compile contracts
vmkv -> contract + binrwir build contracts + specification generated facts
vmbin -> contract + binrwir execution contracts
vmjit -> contract + binrwir execution contracts
host -> contract + external kvspace interface
```

禁止方向：

```text
binrwir -> vmkv/vmbin/vmjit/engine
vmkv -> compiler/vmbin/vmjit
vmbin -> vmkv/vmjit/compiler
vmjit -> vmkv/vmbin/concrete host
repository -> vmkv/vmbin/vmjit
engine files other than route -> vmkv/vmbin/vmjit
public header -> internal runtimec header
```

## 13. 公共接口、名称与验收

公共层只安装 `include/kvlang/runtimec.h`，只导出 `interface/exports.txt` 中的符号。
公共头不暴露 RWIR、BinRWIR、vmkv、Compiler、vmbin、vmjit 或 kvspace 内部结构。

除 kvlang、kvspace、runtimec、RWIR、BinRWIR、vmkv、vmbin、vmjit、vthread、langtype
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
