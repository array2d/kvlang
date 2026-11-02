# runtimec 架构与实现总纲

> 当前状态：目录、数据结构、函数边界和验收闸门已经建立；语言执行语义尚未实现。
> `scaffold` 产物名必须保留到真实语义通过验收，防止骨架被误当成可发布实现。

`runtimec/` 是 kvlang 唯一目标运行时。它使用 C11 实现，在一个清晰主干中同时
容纳可恢复执行、RWIR 编译、BinRWIR 基线执行和 vmjit 加速。未实现的能力必须
返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED`，非法输入必须返回
`RUNTIMEC_STATUS_INVALID`；空值、零、`false` 或默认结构都不能伪装成功。

## 1. 绝对原则

1. **只有一条正常方向。** 正常完成只返回 `RUNTIMEC_STATUS_SUCCESS`；其它状态
   必须准确说明暂停、等待、回退、未支持、未实现、非法输入、内存不足或错误。
2. **只有一条执行主干。** `engine/route` 是唯一策略汇合点；任何组件都不能绕过它
   自行选择 vmjit 或 vmbin。
3. **只有一个正确性执行器。** vmbin 必须覆盖全部合法 BinRWIR；vmjit 可以拒绝，
   但不能成为正确性依赖。
4. **只有一个事实源。** kvspace 明文保存可恢复语义事实；BinRWIR Program、寄存器
   缓存、机器码和分析结果都可删除并重建。
5. **错误立即停止。** 不猜测输入、不截断、不静默修复、不转入另一套入口。
6. **不保留旧入口。** 公共层只有 `include/kvlang/runtimec.h` 和
   `interface/public_interface.c` 定义的结构化接口。
7. **行为先决策后实现。** 任何语言行为先填写五语言矩阵，再进入 specification
   manifest 或执行代码。
8. **名字表达完整含义。** 除 runtimec、RWIR、BinRWIR、vmkv、vmbin、vmjit、
   kvspace、vthread 和 langtype 外，自造缩写必须展开。
9. **Less code, more useful capability.** 不为错误分层增加修补代码，不建立第三执行器。

## 2. 唯一主干

```text
public interface
    -> engine route
    -> vmkv reads and compiles RWIR
    -> immutable BinRWIR Program
    -> vmjit try
       -> executed
       -> fallback to vmbin
    -> route commits effects and checkpoint
```

主干含义不能重排：

- `engine/route.c` 选择并驱动执行路径。
- `vmkv` 读取 kvspace 中的 RWIR，完成解析、校验、类型固化和 Program 构造。
- `vmbin` 拥有不可变 BinRWIR Program 和全部基线语义。
- `vmjit` 只读取已经验证的 Program；无法证明安全时返回
  `RUNTIMEC_STATUS_FALLBACK`。
- route 将同一 Program 和可恢复状态交给 vmbin，随后提交 effect、program counter
  和 checkpoint。

generic、typed、fused 和 continuous execution 都是 vmbin 内部 handler 选择，
不是新组件，也没有第二个公开执行入口。

## 3. 目录主干

```text
runtimec/
├── include/kvlang/runtimec.h          # 唯一公共 C 头
├── interface/                         # 公共请求、结果与精确导出名单
│   ├── public_interface.c
│   └── exports.txt
├── contract/                          # 跨组件状态、错误、所有权和 host port
├── host/                              # kvspace 与外部能力适配
├── vmkv/                              # RWIR -> BinRWIR Program
├── vmbin/                             # BinRWIR 所有者与正确性执行器
│   ├── value.h
│   ├── reference.*
│   ├── signature.*
│   ├── instruction.*
│   ├── program.*
│   ├── binding.*
│   ├── frame.*
│   ├── call.*
│   ├── interpreter.*
│   └── exit.h
├── vmjit/                             # 可拒绝的机器码加速器
│   ├── execution_interface.h
│   ├── verifier.h
│   ├── plan.h
│   ├── lower.h
│   ├── backend.h
│   └── backend/
├── engine/                            # session、Program repository、唯一 route
├── specification/                    # 行为与 RWIR manifest 的单一事实源
├── documentation/architecture/       # 架构说明、矩阵与 Mermaid 源
├── tools/architecture/               # 名称、依赖、矩阵、导出闸门
└── tests/                             # 公共接口、结构合同和失败封闭测试
```

组件没有源文件时，CMake 配置必须 `FATAL_ERROR`。禁止生成空翻译单元让缺失组件
看似构建成功。

## 4. BinRWIR 主干

### 4.1 所有权

BinRWIR 属于 vmbin，不属于 vmkv 或 vmjit：

- vmkv 只能通过 Program builder 构造它；
- vmbin 解释它并定义全部合法行为；
- vmjit 只消费只读 Program view；
- Program finalize 后不可变；
- profile、编译计划、机器码缓存和融合计划是可丢弃 sidecar。

### 4.2 参数数量

BinRWIR 必须保留 RWIR 的不固定参数数量语义，但编译完成后每条指令的读取数量和
写入数量固定。常用形式使用紧凑 instruction header；超出紧凑范围时引用冷侧表。
无论底层表示如何，消费者只看到一个规范化 view：

```text
[ReadReference ...][WriteReference ...]
```

不得截断参数，也不得为了编码方便把一个具有原子语义的 RWIR 操作拆成多个可观察
操作。

### 4.3 五条优化主线

1. 指令编码：减少解析与分发。
2. 类型固化：把已经确定的具体类型写入特化指令。
3. 指令融合：以 sidecar 计划选择更少的内部 handler，不改变 Program 语义。
4. 寄存器优化：中间结果留在寄存器或派生 Cell 中，提交边界再物化。
5. 控制流优化：减少分支和调度，同时保留逐 source instruction 的观察点。

这些都是 vmbin 或 vmjit 的优化方法，不产生第三执行器。

## 5. Read/Write 与别名

RWIR 和 BinRWIR 都只有读取参数与写入参数。原地算法通过显式别名绑定同一 canonical
Cell，不引入第三种参数方向：

```text
Read register  ──> Binding ──┐
                             ├──> canonical Cell
Write register ──> Binding ──┘
```

- 读取 register 只能读，写入 register 只能写。
- 两个 register identifier 始终不同，即使 Binding 指向同一 Cell。
- callee Signature 是 alias policy 的唯一拥有者。
- 写入后，后续通过读取别名看到新值，并遵循程序顺序。
- 未声明允许的别名立即返回 `RUNTIMEC_STATUS_INVALID`。
- 跨 frame 别名依靠 canonical Cell identity，不建立 pointer chain。

## 6. vmjit 边界

vmjit 只做加速：verify、plan、lower、backend、code memory 和 execute。它不能：

- 读取或解析 RWIR；
- 访问具体 kvspace 类型；
- 定义 opcode 或 builtin 语义；
- 修改不可变 Program；
- 吞掉 guard、机器码生成或平台不支持错误；
- 绕过 route 提交 effect。

x86_64 与 AArch64 是同一 backend contract 的并列实现。任一实现缺失时，该平台明确
回退 vmbin，不能报告 vmjit 可用。

## 7. 状态与提交

`runtimec_status_t` 的唯一成功值是 `RUNTIMEC_STATUS_SUCCESS`。控制状态与错误状态
不可混用：

- `YIELD`、`WAIT`：执行尚未正常完成；
- `FALLBACK`：vmjit 没有执行，route 必须转入 vmbin；
- `UNSUPPORTED`：当前能力不接受该操作；
- `NOT_IMPLEMENTED`：骨架没有该能力；
- `INVALID`：输入违反合同；
- `OUT_OF_MEMORY`、`ERROR`：明确失败。

所有输出参数先进入无效或空状态，再开始可能失败的工作。只有完整 instruction effect
成功后，route 才按 values → program counter → status/error 的顺序提交 checkpoint。

## 8. 五语言决策矩阵

`specification/five-language-behavior-matrix.md` 是语言行为决策的唯一事实源。

- C、Python、Rust、Go、V8/TypeScript 一致：kvlang 必须遵从。
- 五家分裂：记录选择阵营、原因、边界 fixture 和非法输入结果。
- 每个 specification manifest 条目都必须带
  `behavior-decision: <decision>` 并引用已有行。
- 没有矩阵记录，不能添加 specification 条目或执行语义。

`tools/architecture/check_behavior_matrix.py` 强制检查以上规则。矩阵变化是语言变化，
必须与对应 fixture 和设计记录同时评审。

## 9. 公共接口

公共层只安装 `include/kvlang/runtimec.h`，只导出 `interface/exports.txt` 中的符号。
当前结构化入口是 create、execute、destroy；请求、结果、错误和调用方 buffer 都有明确
类型。公共头不暴露 RWIR、BinRWIR、vmkv、vmbin、vmjit 或 kvspace 内部结构。

CMake 从导出清单生成 Linux、macOS 和 Windows 的平台导出文件，
`check_exports.py` 对最终动态库逐名复核。多一个或少一个符号都失败。

## 10. 依赖方向

允许方向：

```text
public header -> public header
interface -> public header + contract + engine
engine/route -> host + vmkv + BinRWIR + vmjit + vmbin
vmkv -> contract + BinRWIR build contract
vmbin -> contract + BinRWIR
vmjit -> contract + read-only BinRWIR execution contract
host -> contract + external kvspace interface
```

禁止方向：

```text
vmkv -> vmjit
vmbin -> vmjit
vmjit -> vmkv
vmjit -> concrete host implementation
any VM component -> concrete kvspace type
engine files other than route -> vmkv/vmbin/vmjit
public header -> internal runtimec header
```

`tools/architecture/check_layers.py` 检查 include graph 和 route 唯一性；新增依赖失败时，
先修正文件所有权，不扩大允许表。

## 11. 名称规则

允许保留的 kvlang 原子术语：

```text
kvlang  kvspace  runtimec  RWIR  BinRWIR  vmkv  vmbin  vmjit  vthread  langtype
```

其它名称按 C、Python、Rust、Go 和 V8/TypeScript 的主流直觉写完整单词。例如使用
`argument`、`buffer`、`context`、`descriptor`、`identifier`、`program_counter`、
`reference`、`request`、`source`，不用其缩写。

`tools/architecture/check_names.py` 还会拒绝：

- 旧目录名和非唯一公共头；
- 非唯一的成功状态名；
- 任何其它中间表示名称；
- 独立快速内核、执行窗口和其它第三执行器名称；
- 旧类型、宏和测试前缀。

## 12. 验收

最小 fresh 验收：

```bash
cmake -S runtimec -B /tmp/kvlang-runtimec-build \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/kvlang-runtimec-build --parallel
ctest --test-dir /tmp/kvlang-runtimec-build --output-on-failure
```

验收必须覆盖：

- 唯一公共头在 C11 与 C++17 中可直接包含；
- 公共函数声明和精确导出清单一致；
- 未实现入口失败封闭并清空输出；
- BinRWIR 跨组件紧凑结构尺寸；
- 唯一状态名、完整名称和零第三执行器；
- route 唯一性与依赖方向；
- 五语言矩阵完整且所有语义引用存在。

语义实现后按 shared memory → benchmark quick → benchmark full → tutorial 的顺序验证。
当前 scaffold 没有执行语义，不运行 benchmark 或 tutorial。

## 13. DeepSeek 实现合同

实现前按此顺序读取：

1. `README.md`
2. `documentation/architecture/runtimec-architecture.md`
3. `specification/five-language-behavior-matrix.md`
4. `specification/README.md`
5. 目标组件头文件

每次实现只沿主干增加真实能力：先矩阵和合同，再 vmkv 编译，再 vmbin 正确性，最后
vmjit 加速。遇到不清楚的输入或状态必须失败，不能添加第二入口、旧名字别名、空组件
兜底或隐藏执行路径。
