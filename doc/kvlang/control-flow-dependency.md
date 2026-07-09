# kvlang 控制流关键字层级依赖分析

> 分析控制流语法关键字的层级依赖关系，定位最基础的关键字集合。

---

## 1. 关键字全景

| 关键字 | 语义 | 代码常量 | 实现状态 |
|--------|------|---------|---------|
| `call` | 函数调用，创建子栈帧 | `OpCall = "call"` | ✅ 已实现 |
| `return` | 函数返回，清除栈帧 | `OpReturn = "return"` | ✅ 已实现 |
| `if` | 条件分支 | `OpIf = "if"` | ✅ 已实现 |
| `else` | 分支否定路径 | `OpElse = "else"` | 🔧 常量已定义 |
| `for` | 计数循环 | `OpFor = "for"` | 🔧 常量已定义 |
| `while` | 条件循环 | `OpWhile = "while"` | 🔧 常量已定义 |
| `break` | 跳出循环 | `OpBreak = "break"` | 🔧 常量已定义 |
| `continue` | 跳过迭代 | `OpContinue = "continue"` | 🔧 常量已定义 |

设计文档中规划但未定义常量的：
| 关键字 | 语义 | 来源 |
|--------|------|------|
| `br` | 条件分支（基本块终止指令） | `control-flow.md` §3.6 |
| `jump` | 无条件跳转（基本块终止指令） | `control-flow.md` §3.6 |
| `switch` / `case` | 多路分支 | `control-flow.md` §3.5 |
| `loop` | 无限循环 | `control-flow.md` §3.5 |

---

## 2. 执行模型基础

所有控制流构建在同一个执行核心上：

```
┌─────────────────────────────────────────────────────┐
│                  Execute(ctx, kv, vtid)              │
│                                                     │
│  loop:                                              │
│    pc   ← vthread.PC                                │
│    inst ← op.Decode(ctx, kv, vtid, pc)              │
│    if inst.Opcode == "" → done                      │
│                                                     │
│    switch:                                          │
│      control → handleControl (call/return/if)        │
│      native  → builtin.Native (算术/比较/IO)          │
│      lifecycle → dispatch.Lifecycle (tensor new/del) │
│      function → rewrite as call → handleControl      │
│      compute → dispatch.Compute (op-plat)            │
│                                                     │
│    pc ← NextPC(pc)  // 隐式顺序前进                   │
│  end                                                │
└─────────────────────────────────────────────────────┘
```

**隐式基础：PC 自增**。这是所有控制流的根基——每条指令执行后 `NextPC(pc)` 将 `[i,0]` 推进到 `[i+1,0]`。控制流关键字的作用就是**覆盖这个默认行为**——将 PC 设置为非连续的下一个值。

---

## 3. 层级依赖图

```
                          ┌──────────────────┐
                          │   PC 自增 (NextPC) │  ← Layer 0: 隐式基础
                          │   顺序执行引擎      │     无关键字，每条指令依赖
                          └────────┬─────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              ▼                    ▼                    ▼
        ┌──────────┐       ┌──────────┐        ┌──────────┐
        │   call   │       │  return  │        │  jump    │  ← Layer 1: 控制流原子
        │  进入函数  │       │  退出函数  │        │  无条件跳转 │     最基础关键字
        └────┬─────┘       └────┬─────┘        └────┬─────┘
             │                  │                    │
             │    ┌─────────────┘                    │
             │    │                                  │
             ▼    ▼                                  ▼
        ┌──────────────────┐              ┌──────────────────┐
        │   栈帧管理        │              │   基本块模型       │
        │  子栈创建/清理    │              │   带标签的指令块    │
        └──────────────────┘              └────────┬─────────┘
                                                   │
                                          ┌────────┴────────┐
                                          ▼                 ▼
                                    ┌─────────┐      ┌─────────┐
                                    │   br    │      │  block  │  ← Layer 2:
                                    │ 条件分支  │      │  标签   │     基本块原语
                                    └────┬────┘      └─────────┘
                                         │
              ┌──────────────┬───────────┼──────────┬──────────────┐
              ▼              ▼           ▼          ▼              ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
        │ if/else  │  │   for    │  │  while   │  │  loop    │  │ switch/  │  ← Layer 3:
        │          │  │          │  │          │  │          │  │  case    │     结构化语法糖
        └──────────┘  └────┬─────┘  └────┬─────┘  └──────────┘  └──────────┘
                           │             │
                      ┌────┴────┐   ┌────┴────┐
                      ▼         ▼   ▼         ▼
                 ┌────────┐ ┌──────────┐ ┌────────┐ ┌──────────┐
                 │ break  │ │ continue │ │ break  │ │ continue │  ← Layer 4:
                 └────────┘ └──────────┘ └────────┘ └──────────┘     循环专用
```

---

## 4. 逐层分析

### Layer 0: 隐式基础 — PC 自增 (NextPC)

```
无关键字。每条指令执行完毕后：
  [0,0] → [1,0] → [2,0] → ... → [N,0] → opcode="" → done
```

这是控制流的"默认路径"。所有控制流指令的本质就是**偏离这个默认路径**。

**代码位置**：`internal/op/pc.go` — `NextPC(pc)`

---

### Layer 1: 控制流原子 — call / return / jump

这是三个不可再分的控制流原语，各自改变 PC 的方式不同：

#### `call` — 垂直跃迁

```
当前 PC = [2,0]
call 加法(A, B)

行为：
  1. 查找 /src/func/加法 → 获取签名 + 函数体
  2. 形参/实参绑定
  3. 在 /vthread/<vtid>/[2,0]/[0,0] 创建子栈指令序列
  4. PC = [2,0]/[0,0]    ← 进入子栈
```

**依赖**：PC 自增（在子栈内部继续顺序执行）  
**代码位置**：`internal/layoutcode/codegen.go` — `HandleCall`

#### `return` — 垂直回退

```
当前 PC = [2,0]/[3,0]
return

行为：
  1. 将返回值从子栈传递到父栈写入槽位
  2. DEL /vthread/<vtid>/[2,0]/*  （清除子栈）
  3. PC = [3,0]                  ← 回到父栈的下一条
```

**依赖**：call（必须先有调用才有返回）  
**代码位置**：`internal/layoutcode/codegen.go` — `HandleReturn`

#### `jump` — 水平跃迁（设计阶段，未实现）

```
当前 PC = [1,0]
jump @3

行为：
  PC = @3 对应的绝对坐标   ← 跳过 [2,0]，直接到 @3
```

**依赖**：基本块标签系统（`@0`, `@1`, ...）  
**来源**：`doc/kvlang/control-flow.md` §3.2

> **注意**：`jump` 在当前代码中不存在。当前实现用 `if` 直接操作 PC 路径字符串（`pc+"/true/0"`）模拟分支跳转，未经过基本块抽象层。

---

### Layer 2: 基本块原语 — br / block labels

Layer 2 引入了两个新概念：

1. **基本块**（basic block）：带唯一标签（`@0`, `@1`）的线性指令序列
2. **终止指令**：每个块的最后一个指令必须是 `br`、`jump`、`return` 或 `call` 之一

```
@0:
    newtensor("f32", "[4]") -> ./a
    sum(./x) -> ./s
    greater(./s, 0) -> ./cond
    br ./cond, @1, @2       ← 条件分支终止指令

@1:
    add(./x, 1.0) -> ./y
    jump @3                 ← 无条件跳转终止指令

@2:
    mul(./x, -1.0) -> ./y
    jump @3

@3:
    deltensor(./a)
    return ./y
```

**层级关系**：
- `br cond, @t, @f` 是 `jump @t` + 条件的合体
- 如果 `jump` 是 Layer 1 原子，那么 `br` 是 `jump` 的扩展（多了条件判断）
- 实际上 `jump @t` ⊆ `br true, @t, _`，即无条件跳转是条件跳转的特例

**当前实现**：不存在。当前 `if` 直接操作嵌套 PC 路径，绕过了基本块。

---

### Layer 3: 结构化语法糖 — if / for / while / loop / switch

Layer 3 的所有关键字都可以 **lowering 到 Layer 1+2 的组合**：

#### `if` / `else` → `br` + `jump`

```
# Layer 3 语法
if (cond) {
    then_body
} else {
    else_body
}
after_code

# lowering 到 Layer 2
@N:
    ... cond 求值 ...
    br cond, @then, @else

@then:
    then_body
    jump @merge

@else:
    else_body
    jump @merge

@merge:
    after_code
```

**当前实现**：`if` 直接在 `kvcpu/control.go` 中操作 PC 路径，不走基本块 lowering。

#### `for` → init + `br` + body + step + `jump`

```
# Layer 3 语法
for i in 0..100 {
    body
}

# lowering 到 Layer 2
    i = 0                        ← init
@cond:
    br (i < 100), @body, @exit  ← cond check
@body:
    body
@step:
    i = i + 1
    jump @cond                  ← loop back
@exit:
    after
```

#### `while` → `br` + body + `jump`

```
# Layer 3 语法
while (cond) {
    body
}

# lowering 到 Layer 2
@cond:
    br cond, @body, @exit
@body:
    body
    jump @cond
@exit:
    after
```

#### `loop` → body + `jump`

```
# 等价于 while(true)
@body:
    body
    jump @body
```

#### `switch` / `case` → 链式 `br` 或跳转表

```
switch (val) {
    case 1: body1
    case 2: body2
    default: body_default
}

# lowering 到 Layer 2
    br (val == 1), @case1, @next1
@next1:
    br (val == 2), @case2, @default
@case1:
    body1
    jump @merge
@case2:
    body2
    jump @merge
@default:
    body_default
    jump @merge
@merge:
```

---

### Layer 4: 循环专用控制 — break / continue

这两个关键字**完全依赖 Layer 3 的循环结构**，本质是跨块跳转的语法糖：

#### `break` → `jump @loop_exit`

```
for i in 0..100 {
    if (cond) {
        break        ← 等价于 jump @loop_exit
    }
    body
}
```

**语义**：跳出最内层循环的 body 块，跳转到 `@loop_exit`（循环之后的第一条指令）。

**依赖链**：`break` → 循环结构 → `jump` → 基本块标签

#### `continue` → `jump @loop_step` 或 `jump @loop_cond`

```
for i in 0..100 {
    if (cond) {
        continue      ← for: 等价于 jump @step
    }                 ← while: 等价于 jump @cond
    body
}
```

**语义**：跳过本次迭代剩余代码，跳转到循环的条件判断（while）或步进（for）。

**依赖链**：`continue` → 循环结构 → `jump` → 基本块标签

---

## 5. 最小基础关键字集合

移除所有可 lowering 的语法糖后，最基础的控制流关键字仅 **3 个**：

| 关键字 | 不可替代的原因 |
|--------|--------------|
| **`call`** | 唯一能进入函数体、创建子栈帧的机制。无法用 `jump` 替代（jump 不创建新栈帧、不做参数绑定、不产生返回点） |
| **`return`** | 唯一能清理子栈帧、将控制权交还调用方的机制。无法用 `jump` 替代（jump 不清理栈帧、不传递返回值） |
| **`br`** | 唯一能根据运行时条件选择不同执行路径的机制。`jump` 可以视为 `br(true, @t, _)` 的特例 |

**这个三元组 (call, return, br) 构成了图灵完备的控制流基础。** 所有结构化控制流 (`if`/`for`/`while`/`switch`/`break`/`continue`) 都可以 lowering 为这三者 + 基本块标签的组合。

```
最小集合: { call, return, br }

call    ← 垂直跃迁（跨函数）
return  ← 垂直回退（跨函数）
br      ← 水平跃迁（函数内，包含条件判断）

三者覆盖的控制流维度:
  call × br   = 函数内条件分支
  call × call = 嵌套/递归调用
  br × br     = 多路分支、循环回边
  return      = 终结任何路径
```

---

## 6. 当前实现与设计目标的对齐

```
当前实现路径:
  Layer 0 (PC++) ──→ Layer 1 (call/return) ──→ if (直接操作PC，无基本块)
                                                      │
                                              跳过了 Layer 2 (br/jump/block)
                                                      │
                                              无法到达 Layer 3/4 (for/while/...)

设计目标路径:
  Layer 0 ──→ Layer 1 ──→ Layer 2 ──→ Layer 3 ──→ Layer 4
   PC++       call       br/jump    if/for/     break/
              return     @block     while/      continue
                                    switch
```

当前阶段 `if` 的实现是一条**捷径**——它绕过了基本块模型，直接在 PC 路径上拼接 `/true/0` 和 `/false/0`。这种方式的优点是实现简单（`kvcpu/control.go` 仅 57 行），缺点是与未来基本块模型不兼容。

### 演进路径

1. **引入基本块标签 `@N`**：将指令序列组织为带标签的块，PC 不再隐式依赖 `[i,0]` 索引
2. **实现 `br` / `jump`**：作为 exec loop 的新 case，直接设置 PC 为目标 block 的第一条指令
3. **`if` 重构**：从直接 PC 拼接改为 lowering 到 `br` + 基本块
4. **实现 `for` / `while`**：lowering 到 init + `br` + body + `jump` 组合
5. **实现 `break` / `continue`**：编译器在 lowering 阶段解析为 `jump @exit` / `jump @cond`

---

## 7. 语言能力边界

基于此层级分析，kvlang **结构上无法支持**的控制流特性：

| 特性 | 受阻原因 |
|------|---------|
| **递归** | `call` 创建子栈帧是 eager inline，无运行时栈深度管理，无尾调用优化（TCO）支持 |
| **闭包/高阶函数** | 函数不是一等公民值，`call` 的 reads[0] 是字符串 opcode，不是运行时求值的函数引用 |
| **异常/try-catch** | `return` 是正常退出路径，无 unwind 机制；错误处理是 `vthread.SetError` 终止整个线程 |
| **协程/async-await** | vthread 是单执行流，无挂起/恢复语义 |
| **动态跳转目标** | `br` 的目标必须编译期确定（静态 block 标签），无法 `jump ./runtime_target` |

---

## 8. 总结

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│  控制流层级:                                                  │
│                                                              │
│    Layer 0: PC 自增        — 隐式，无关键字，一切的基础          │
│    Layer 1: call/return/br — 3 个原子，图灵完备                  │
│    Layer 2: @block 标签    — 结构化跳转目标                     │
│    Layer 3: if/for/while   — 语法糖，可 lowering                │
│    Layer 4: break/continue — 循环专用，可 lowering               │
│                                                              │
│  最小基础关键字 = { call, return, br }                        │
│                                                              │
│  当前实现: Layer 0 + Layer 1 的 call/return + if (捷径)         │
│  设计目标: Layer 0 → 1 → 2 → 3 → 4 逐层构建                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```
