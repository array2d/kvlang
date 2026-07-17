# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Tutorial Tests](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg?job=tutorial-test)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Go Version](https://img.shields.io/badge/Go-1.24+-00ADD8?logo=go)](https://go.dev/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial](https://img.shields.io/badge/tutorial-87%20examples-4c1)](tutorial/)

**基于 KV 路径寻址的声明式 VM 解释器。指令即路径，调用即子树复制，状态即 KV 树。**

> English: [README.md](README.md) | 设计文档: [kvlang-design](https://github.com/array2d/kvlang-design)

---

## 为什么是 kvlang？

传统 VM 将代码和数据分离。kvlang 将它们统一在一棵 KV 树中：

```
/vthread/1/[0,0]  → "add"              # opcode
/vthread/1/[0,-1] → "/src/add/a"       # 读操作数
/vthread/1/[0,-2] → "/src/add/b"
/vthread/1/[0,1]  → "/src/add/c"       # 写结果
```

- **指令即路径。** opcode 存在 `[i,0]`，操作数以正负索引标注。
- **调用即子树复制。** 调用函数 = 将函数体复制到调用方帧下。
- **状态是一棵树。** 每个变量、返回值、栈帧都在路径上，随时可 `GET`。
- **崩溃恢复。** PC 是 KV 路径字符串。重启进程，原地续跑。
- **Agent 原生。** Agent 写代码、VM 执行、Agent 读状态 — 全部通过同一 KV API。

---

## 快速开始

```bash
# 前置: Go 1.24+, Redis
make build

# 运行教程
./kvlang tutorial/01-basics/hello.kv
./kvlang tutorial/03-control/guess.kv     # 猜数字游戏
./kvlang tutorial/04-algo/fibonacci.kv    # fib = 55

# 内联 / 管道 / 语法检查 / 格式化
./kvlang -c 'print("hello")'
echo '40 + 2 -> x  print(x)' | ./kvlang
./kvlang vet my.kv
./kvlang format my.kv
```

---

## 教程

87 个自包含示例，每个一行命令即可运行：

```bash
./kvlang tutorial/01-basics/hello.kv         # 你好 kvlang
./kvlang tutorial/03-control/guess.kv        # 二分猜数字
./kvlang tutorial/04-algo/fibonacci.kv       # fib = 55
./kvlang tutorial/05-leetcode/001_two_sum.kv # LeetCode
```

```
01-basics/        hello, vars, arith               (3)
02-func/          def, call, 嵌套调用               (1)
03-control/       if, while, for, 猜数字            (5)
04-algo/          fibonacci, gcd, collatz, ...     (13)
05-leetcode/      73 道 LeetCode 题解              (65)
```

```bash
python3 tutorial/test.py                  # 87/87 全过 — CI 验证
```

---

## 语言速览

### 读写码

```kv
表达式           -> 槽位        // 计算表达式，结果写入槽位
函数(a, b)       -> 结果        // 调用函数，单返回值
函数(a, b)       -> x, y        // 多返回值
函数(a, b)       -> _, y        // 丢弃首个返回值
```

### 类型

| 类型     | 字面量                      |
|----------|----------------------------|
| `int`    | `0`  `42`  `-7`             |
| `float`  | `3.14`  `0.5`  `1e9`       |
| `bool`   | `true`  `false`             |
| `string` | `"hello"`  `'world'`       |

### 运算符

| 类别 | 符号 |
|------|------|
| 算术 | `+` `-` `*` `/` `%` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `||` `!` |
| 位运算 | `&` `|` `^` `<<` `>>` |

### 函数与控制流

```kv
def add(A:int, B:int) -> (C:int) {
    A + B -> C
}

if (cond) { ... } else { ... }

while (cond) { ... }              // break, continue 可用
```

### 内建函数

`abs` `neg` `pow` `sqrt` `exp` `log` `min` `max` `int` `float` `bool` `print` `cerr` `input`

### 入口点

所有顶层指令（不在任何 `def` 内）自动包装为 `init()`。`main()` 无特殊地位，需显式调用：

```kv
def main() -> () { ... }
main() -> ()
```

---

## 架构

```
.kv ──▶ Lexer ──▶ Parser ──▶ AST ──▶ Lower (if/while→block+br) ──▶ WriteBody
                                                                      │
                                                               kvspace (Redis)
                                                                      │
                                                                   Execute
                                                                ├── builtin (标量)
                                                                └── vthread (调度)
```

| 层 | 包 | 职责 |
|----|----|------|
| Parser | `internal/parser` | `.kv` → AST |
| Lower | `internal/lower` | 控制流降级 |
| Layout | `internal/layoutcode` | AST → KV 树 |
| 调度 | `internal/kvcpu` | goroutine worker, vthread |
| 存储 | `internal/kvspace` | KVSpace 接口 (Redis) |
| 类型 | `internal/vtype` | int/float/bool/str/tensor |

## 依赖

仅 2 个直接依赖：`go-redis/v9` + `gorilla/websocket`。零框架，零代码生成。

## License

MIT — see [LICENSE](LICENSE)
