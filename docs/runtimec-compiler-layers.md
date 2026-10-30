# runtimec 编译分层架构（2026-10-30 用户裁决）

> runtimec 默认指 **runtimec2**（c2，C++ 执行核心）。本文定义 kvlang 从高级函数语义到机器码的编译分层，
> 核心变化：**layout 只做高级→低级转换**，**runtimec 内部自带编译器**（top rwir → 中级 isa → 汇编码 → 机器码），
> **不再依赖 layout 的编译布局与 kvspace 的 isa map**。

## 一、分层架构图

```
codestring（kvlang 高级函数语义：for/while/continue/+ 等）
   │  layout（只做脱糖转换）
   ▼
B（kvlang top rwir）—— layout 产物，/lib 明文
   ├── runtimec_vmkv ──► C（kvlang isa 中级指令语义：goto/jmp/+ 等）
   └── ext-compiler ──► D（其它架构指令：deepx 后端绑定、op-gpu 等）
C ── runtimec_vmbin ──► E（runtimec 内置单一参数类型汇编码）
E ── runtimec_vmjit ──► F（低级机器指令 x86/arm 机器码）
```

## 二、每层职责与所在位置

| 层 | 职责 | 代码位置 | 指令/数据存在 |
|----|------|---------|--------------|
| **codestring** | kvlang 高级函数语义（源码） | `tutorial/*.kv`、`stdlib/*.kv` | kvspace `/lib/<pkg>·<name>.src`（源码副本） |
| **layout** | **只做高级→低级转换**：`continue`→`goto`、`for`→循环、`+` 保留语义；类型检查 | `layout/src/{parser,lower}.rs` | kvspace `/lib/<pkg>·<name>/`（top rwir 指令树：`[s0,±k]` 槽 + `‥ctime`） |
| **B top rwir** | layout 产出的 kvlang 指令（含高级语法脱糖后） | — | kvspace `/lib/<pkg>·<name>/[s0,·]` 明文 |
| **runtimec_vmkv** | **明文编译**：读 /lib 明文 top rwir → 编译为中级 isa 指令；不再"解释"（编译期建段） | `runtimec2/src/vmkv.cpp`（build_segment 是编译入口） | vm 内部：`Segment`（codeseg.hpp） |
| **C isa 中级指令** | kvlang 中级指令语义（goto/jmp/+/整数运算等单一语义） | `runtimec2/src/codeseg.cpp`（建段） | vm 内部：`Segment.insns`（CInsn 数组） |
| **runtimec_vmbin** | 中级指令 → 内置**单一参数类型**汇编码（typed 特化：int64·add、float64·lt 等） | `runtimec2/src/vmbin.cpp` | vm 内部：`BinProgram`（vmbin.hpp：LangType/PcBin/SlotOff） |
| **E 汇编码** | runtimec 内置单一参数类型指令（typed opcode） | `runtimec2/src/vmbin.cpp` + `rwir_gen.cpp`（myrwircaps 305） | vm 内部：BinProgram 段缓存 |
| **runtimec_vmjit** | 汇编码 → 低级机器码（x86_64/arm64） | `runtimec2/src/jit.cpp`（jit_x64/jit_arm） | vm 内部：mmap RX 机器码 + JitFn |
| **ext-compiler** | 其它架构指令（deepx 后端算子、GPU 融合） | `deepx-cpu-compute/`、op-gpu | kvspace `/‥isa/<op>`（扩展注册） |

## 三、与现状的差异（要删/要改）

| 现状 | 新架构 |
|------|--------|
| layout 做**编译布局**（desugar + 特化 + 落 /lib 指令槽） | layout **只脱糖**（高级→低级转换），不再决定最终执行布局 |
| runtimec2 依赖 **kvspace isa map**（`/‥isa` 签名查询，abi.cpp:153-197 双写） | runtimec **内部编译**，isa 表（myrwircaps）是 runtimec 自有，不依赖 /‥isa 外部 |
| vmbin 建段 = **解释器缓存**（build_segment 从 /lib 明文拉） | vmbin = **编译目标**（vmkv 编译产出中级 isa → vmbin 汇编码） |
| /‥isa 由 layout 写（签名来源） | /‥isa 只供**外部扩展**注册（ext-compiler），核心 runtimec 不读它 |

## 四、runtimec 内部编译器（vmkv 定位变化）

- **vmkv 从"明文解释器"变成"明文编译器"**：读 /lib 明文 top rwir → 编译到 vmbin 码栈（Segment → BinProgram）
- 编译发生在**函数段构建**（build_segment），结果缓存（cached_segment），`‥ctime` 失效重建
- 中间指令（isa 中级）是 vm 内部表示，**不落 kvspace**（kvspace 只有明文 top rwir 事实源）
- 崩溃恢复契约不变：kvspace 明文事实 + vmbin 可丢弃重建（#499 单一语义事实源）

## 四·五、编译产物落盘双模式（用户裁决）

runtimec2 内置编译器的**段代码（codeseg 产物）落盘可选**：

- **放 kvspace**：编译产物（Segment/段代码）写一份到 kvspace（如 `/lib/<pkg>·<name>/‥seg`）——跨进程复用，新进程启动免重编译（加载即用）
- **不放**：编译产物纯内存（cached_segment 缓存，进程内）——崩溃从明文 top rwir 重建（#499 单一语义事实源保持）

**语义**：
- 明文 top rwir（layout 产物）恒在 kvspace（事实源）；段代码是**派生编译产物**——可选落盘
- 落盘时：`‥ctime` 失效重建（明文变了 → 段代码重建）；不落盘时：进程缓存同样按 ctime 失效
- 崩溃恢复契约不变：无论段代码落盘与否，明文事实可重建执行状态

**选择策略**：
- 默认**不落盘**（派生可重建，单一事实源纯粹）
- 落盘用于：跨进程共享段代码（多 runtime 复用编译结果）、加速冷启动

## 四·六、myrwircap 对齐契约（用户裁决）

runtimec 编译后产出一个 **myrwircap（算子能力表）**，供 **layout 严格对齐**：

- **runtimec 编译产物**：myrwircap = runtimec 支持的算子表（opcode → 处理器语义），编译时以它为准（只编译表内算子）
- **layout 严格对齐**：layout 脱糖产出的 top rwir 必须落在 myrwircap 内——layout 不产 runtimec 不支持的算子（对齐 = 编译前提）
- **关系**：myrwircap 是 layout（脱糖目标）与 runtimec（编译源）之间的**契约**——layout 产什么、runtimec 收什么，双方以同一张表对齐

**位置**：
- runtimec 侧：`runtimec2/src/rwir_gen.cpp`（myrwircaps 305 条，进程内算子表）
- layout 侧：`layout/src/isa.rs`（Isa::load 对齐检查）
- 对齐方式：layout 脱糖时检查目标算子 ∈ myrwircap，不在则报错（不产未知算子）

## 五、指令流示例（`a + b -> c`）

```
codestring:  a + b -> c          （kvlang 源码）
layout:      + 保留 → top rwir    （+ 是二元算子）
vmkv:       top rwir → isa 中级   （add 指令，操作数 = RegId）
vmbin:      add → typed 汇编码    （int64·add，单一参数类型）
vmjit:      int64·add → x86 add   （机器码）
```

## 六、实施路径

1. **layout 收敛为脱糖器**：去掉编译布局职责（特化/落槽），只做高级→低级转换 + 类型检查
2. **vmkv 编译入口**：build_segment 明确为「明文编译」，产出中级 isa（CInsn）
3. **vmbin 作为编译目标**：中级 isa → typed 汇编码（已有 LangType/PcBin/SlotOff 基础）
4. **去 /‥isa 依赖**：核心 runtimec 不读 /‥isa（只保留 myrwircaps 自有表）；/‥isa 归扩展
5. **vmjit 收尾**：汇编码 → 机器码（已有 jit_x64/jit_arm 框架）
6. **验证**：tutorial + benchmark（每层可独立 A/B）

## 七、关联

- #499（双栈：vmkv 明文 + vmbin 二进制，单一事实源）
- coding5 的 layout/ISA 规划（workspace/layout-isa-compile-plan.md——不建 compile crate 的结论与本架构衔接：runtimec 内部编译器取代外部 compile 模块）
- coding5 的 runtimec2 调查（workspace/runtimec2-investigation.md——P0 修复与本架构并行）
