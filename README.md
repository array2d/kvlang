# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Tutorial Tests](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg?job=tutorial-test)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Go Version](https://img.shields.io/badge/Go-1.24+-00ADD8?logo=go)](https://go.dev/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-87%20examples-4c1)](tutorial/)

**Agent-native 训推一体自迭代强人工智能计算架构。** 以 kvspace 树形路径为统一地址空间，同一语法同时承担 VM 指令、高级语言、编译器 IR、人类可读源码四种职能。

> 中文文档: [README_CN.md](README_CN.md) | 设计规范: [kvlang-design](https://github.com/array2d/kvlang-design)

---

## 架构概览

kvlang 的核心主张：**不分 IR 层，源码即 IR。** 程序计数器是 kvspace 路径字符串，调用栈深度等于路径深度，每个变量、每条指令、每个栈帧都在 kvspace 中独立可寻址。

### 与传统架构的对比

| | LLVM | JVM | kvlang |
|--|------|-----|--------|
| IR 层数 | C→IR→MIR→MC | Java→Bytecode→JIT | **单层**：源码即 IR |
| 地址空间 | 虚拟内存 | 堆+栈 | **kvspace 树形路径** |
| 数据流 | SSA (phi/alloca) | 操作数栈 | **读写码** `<-`/`->` 显式绑定槽 |
| 调用栈 | 内存栈段 | Stack Frame 链表 | **路径深度=栈深度** |
| 崩溃恢复 | 全失 | 全失 | **重启继续**（PC 已持久化） |

### KV 寻址模型

```
程序计数器 PC = "[0,0]/entry/[0,0]"       (KV 路径字符串)
指令           = kv.Get("/vthread/tid/[0,0]/entry/[0,0]")
跳转           = PC = "[0,0]/merge/[0,0]"  (字符串拼接)
调用           = PC = "[0,0]/then/[0,0]"   (路径嵌套)
栈帧           = /vthread/tid/[0,0]/ 子树   (KV key 层级)
```

### 地址空间四域

```
/src/{pkg}/{name}         源码文本
/func/{pkg}/{name}        编译后函数（签名 + 指令树）
/vthread/{vid}/           虚线程栈帧（运行时）
/sys/                     系统基础设施
```

### 模块

| 模块 | 路径 | 职责 |
|------|------|------|
| **ast** | `internal/ast/` | 单层 IR 类型体系：Operand/FuncSig/Stmt/Instruction/File |
| **parser** | `internal/parser/` | Scan→Token→递归下降→`*ast.File` |
| **lower** | `internal/lower/` | 同类型变换：IfStmt/WhileStmt → BlockStmt+br |
| **keytree** | `internal/keytree/` | 路径系统：运行时概念 → kvspace 键路径 |
| **layoutcode** | `internal/layoutcode/` | Linker：WriteFunc(编译期写入) + HandleCall/Return(运行时帧管理) |
| **kvcpu** | `internal/kvcpu/` | 执行引擎：Fetch-Decode-Execute + 调度器 |
| **kvspace** | `internal/kvspace/` | KV 存储接口：Get/Set/Del/List/Watch/Notify/Link |
| **vthread** | `internal/vthread/` | vthread 状态管理 |
| **vtype** | `internal/vtype/` | 可扩展算子类型注册 |
| **op** | `internal/op/` | 内建算子：算术/比较/逻辑/IO |

### 模块依赖图

```
cmd/kvlang
  ├── parser ──► ast
  ├── lower ──► ast
  ├── layoutcode ──► keytree + kvspace + ast
  ├── kvcpu ──► layoutcode + keytree + vthread + vtype + op
  ├── vthread ──► keytree + kvspace
  └── kvspace (接口)
```

---

## 指令的二维空间模型

每条指令在 KV 树中占据一个 **`[s0, s1]`** 坐标：

```
s0 轴（横轴） — 执行顺序轴
s1 轴（纵轴） — 参数轴

        s1 < 0           s1 = 0         s1 > 0
      (读参，输入)        (操作码)       (写参，输出)

s0 = 0  │  [0,-2] [0,-1]  [0,0]  [0,1] [0,2]
s0 = 1  │  [1,-2] [1,-1]  [1,0]  [1,1]
```

- `[s0, 0]` 永远是 opcode
- `[s0, -1], [s0, -2], ...` 读参（负号 = 消费数据）
- `[s0, +1], [s0, +2], ...` 写参（正号 = 产出数据）

```kv
def add(A: int, B: int) -> (C: int) { A + B -> C }
```

编译后写入 kvspace：

```
/func/main/add/[0,0]   = "+"
/func/main/add/[0,-1]  = "A"
/func/main/add/[0,-2]  = "B"
/func/main/add/[0,1]   = "C"
/func/main/add/[1,0]   = "return"
```

---

## Quick Start

```bash
# 依赖: Go 1.24+, Redis
make build

# 运行 tutorial
./kvlang tutorial/01-basics/hello.kv
./kvlang tutorial/03-control/if.kv
./kvlang tutorial/04-algo/fibonacci.kv

# inline 模式
./kvlang -c 'print("hello, world")'

# pipe 模式
echo '40 + 2 -> x  print(x)' | ./kvlang

# 语法检查
./kvlang vet my_program.kv

# 格式化
./kvlang format my_program.kv
```

---

## Language at a Glance

### 读写码

```kv
expr           -> slot        # 计算 expr，结果写入 slot
func(a, b)     -> result      # 调用函数，单写参映射
func(a, b)     -> x, y        # 多写参映射
func(a, b)     -> _, y        # 丢弃首个写参
```

### 函数与写参

kvlang 函数没有"返回值"——只有读参和写参。`def` 签名中的 `-> (C: int)` 是写参声明，不是返回值类型。

```kv
def add(A: int, B: int) -> (C: int) {
    A + B -> C          # 写参 C 在被调方帧中写入
}
```

### 控制流

```kv
if (cond) { ... }
if (cond) { ... } else { ... }
while (cond) { ... }
```

label block 就是无参函数，`goto(merge)` ≡ `call(父函数/merge)`，控制流统一为 call/return。

### 操作符

| 类别 | 符号 |
|------|------|
| 算术 | `+` `-` `*` `/` `%` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `\|\|` `!` |
| 位运算 | `&` `\|` `^` `<<` `>>` |

> `/` 始终返回 `float`。截断用 `int(a / b)`。

### 内建函数

`abs` `neg` `sign` `pow` `sqrt` `exp` `log` `min` `max` `int` `float` `bool` `print` `cerr` `input`

---

## Tutorial

87 个自包含示例，按主题组织：

```
01-basics/        hello, vars, arith               (3 files)
02-func/          def, call, nested calls          (1 file)
03-control/       if, while, for, guess game       (5 files)
04-algo/          fibonacci, gcd, collatz, ...     (13 files)
05-leetcode/      73 LeetCode solutions            (65 files)
```

```bash
./kvlang tutorial/01-basics/hello.kv         # hello kvlang
./kvlang tutorial/03-control/guess.kv        # binary search game
./kvlang tutorial/04-algo/fibonacci.kv       # fib = 55
./kvlang tutorial/05-leetcode/001_two_sum.kv # LeetCode

python3 tutorial/test.py                     # 全部 87 例 — CI 验证
```

---

## KV 路径参考

```
/vthread/<vtid>/<pc>/[i,0]      操作码
/vthread/<vtid>/<pc>/[i,-j]     读参 j
/vthread/<vtid>/<pc>/[i,+j]     写参 j
/vthread/<vtid>/<pc>/label/     控制流 block
/src/<pkg>/<func>/              函数体
/func/main                      程序入口签名
```

---

## License

MIT — see [LICENSE](LICENSE)
