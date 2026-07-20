# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Go Version](https://img.shields.io/badge/Go-1.24+-00ADD8?logo=go)](https://go.dev/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-99%20examples-4c1)](tutorial/)

**deepx 的 VM（原 dxlang），agent-native 训推一体自迭代强人工智能计算架构。** 以 kvspace 树形路径为统一地址空间，同种语法同时承担 VM 指令、高级语言、编译器 IR、人类可读源码四种职能。

> English: [README.md](README.md) | 设计：[deep-dive](https://github.com/array2d/deepx-design/blob/master/doc/kvlang/deep-dive.md) — 根设计文档；README 为教学衍生。全部行为规范（p0–p7）、指令模型（§2）、Link 调用机制（§6）、类型系统（§9）、诊断体系（§12）均在其中。

---

## 核心模型：一屏看懂

**不分 IR 层，源码即 IR。** 程序计数器是 kvspace 路径字符串，调用栈深度 = 路径深度：

```
PC   = "/vthread/tid/[0,0]/.fn/[1,0]"    程序计数器是 KV 路径
指令 = kv.Get(PC)                         取指是一次 KV 读
调用 = 创建子树；返回 = 清理子树           崩溃后按 PC 重启继续
```

每条指令占据二维坐标 `[s0, s1]`：`[s0,0]` 恒为操作码，`[s0,-j]` 读参，`[s0,+j]` 写参。

```kv
def add(A: int, B: int) -> (C: int) { A + B -> C }
```

```
/lib/main/add/[0,0]  = "+"     /lib/main/add/[0,-1] = "A"
/lib/main/add/[0,-2] = "B"     /lib/main/add/[0,1]  = "C"
```

地址空间四域：`/lib`（函数库）`/vthread`（运行时栈帧）`/sys`（基础设施）`/dev`（I/O 设备）。

---

## Quick Start

```bash
# 依赖: Go 1.24+, Redis
make build

./kvlang tutorial/01-basics/hello.kv         # 运行文件
./kvlang -c 'print("hello, world")'          # inline 模式
echo '40 + 2 -> x; print(x)' | ./kvlang      # pipe 模式（; 分隔同行语句）
./kvlang vet my.kv                           # 语法检查
./kvlang format my.kv                        # 格式化
```

---

## Language Guide

### 程序结构（先读这条）

**顶层：`lib name { }`、`def`、单条指令。** 裸 `if` / `while` / `for` 在顶层自动封装为隐式 `def init() { … }`。惯例是定义 `main` 再调用它：

```kv
def main() -> () {
    total = 0  # = 等价于 <-
    1 -> i
    while (i <= 5) {
        total <- total + i
        i + 1 -> i
    }
    print(total)
}

main()
```

### rwir（读写码）：赋值三形态

```kv
x = 40 + 2            # = ：写槽在左（≡ <-）；= 不是表达式，不能嵌进条件里
y <- x                # 左箭头：写槽在左
x * y -> z            # 右箭头：写槽在右
f(a, b) -> r          # 函数写参映射；多写参 -> x, y；丢弃用 -> _
divmod(17, 5) -> _, r # 多写参丢弃：用 _ 丢弃商，只取余数
```

写槽必须是**位置**：裸名（帧内变量）、`/abs/path`（全局键）、`base.名`（成员）。字面量不是位置。

**`def func(ra,rb) -> (wa,wb) { … }` = 自定义复合 rwir**，单条 rwir 如 `A + B -> C` 是原子 rwir（一个操作码 + 读参 + 写参）；`def` 把多条 rwir 打包成命名单元，对外暴露相同的箭头接口——`(ra,rb)` 是读参声明，`-> (wa,wb)` 是写参声明。调用 `add(3,4) -> s` 即把实参绑入读槽、写槽映射回调用方帧。没有返回值，只有写参映射。**必须匹配全部写参**——不想要的用 `._` 丢弃（对齐 Go `_`，引擎语义：不落盘）。

`def` 签名中 `-> (C: int)` 是**写参声明**。函数把结果写进写参槽，调用方用 `-> r` 把写参映射到自己的位置。
**读参只读**：函数体内不可把读参放进写槽（如 `A = A + 1`）。数组元素写同理——`a[i] <- v` 写穿 `a`，要修改的数组/字典必须放写参位置。
```kv
# ❌ 错误：数组作读参，a[i] <- v 写读参槽 → parser 拒绝
def bad(a:int64) -> () { 99 -> a[0] }

# ✅ 正确：数组作写参，函数内读写自由
def good() -> (a:int64) { a:int64 = [10, 20]; 99 -> a[0]; a }
```
需要体内反复更新的量先想清角色——
**累加器是输出，声明为写参**（写参零值起步、体内可读可写，同 Go 命名返回值）：`def sum(arr:int64) -> (acc:int64) { acc + arr[i] -> acc }`；
纯工作变量则拷贝局部（`A -> a` 后用 `a`）：

```kv
def add(A: int, B: int) -> (C: int) {
    A + B -> C
}

def main() -> () {
    add(3, 4) -> s
    print(s)          # 7
}

main()
```

### dict、成员访问与链表

```kv
d = { name="kv"; ver=1 }    # dict 字面量：成员是平坦键族 d.name、d.ver
print(d.name)               # 成员读
d.ver = 2                   # 成员写
k = "name"; d.*k -> v       # 动态键：读 d.name（k 的值作键名）
```

**路径即指针**：把绝对路径字符串存到变量，再用 `.成员` 语法读写该路径下的键——变量的字符串值会成为路径前缀。
```kv
/node = { val=42 }       # dict 节点位于绝对路径
"/node" -> p             # p 存路径字符串
p.val -> v               # 读 /node.val → 42
```

链表等跨函数共享的数据结构，节点用**绝对路径**创建（帧内变量随函数返回销毁）：

```kv
def build() -> () {
    /n1 = { val=1; next="/n2" }  # = 等价于 <-
    /n2 <- { val=2; next="/n3" }
    { val=3; next="" } -> /n3
}

def main() -> () {
    build()
    "/n1" -> p                   # p 存路径字符串（指针）
    while (p != "") {
        p.val -> v               # 指针解引用：读 /n1.val
        print(v)
        p.next -> p
    }
}

main()
```

### 数字类型（可选精度声明）

```kv
f = float32(3)        # int8/16/32/64 uint8/16/32/64 float32/64 十算子，既创建也转换
w = int8(300)         # 44：窄化补码回绕；float→int 截断向零；算术域统一 int64/float64
```

### 控制流（仅限 def 体内）

```kv
if (sum > 50) { print("big") } else { print("small") }
i = 1; sum = 0
while (i <= 10) { sum + i -> sum; i + 1 -> i }
for (x in [7, 2, 9, 4]) { print(x) }
```

条件支持复合表达式：`if (7 % 2 != 0)`、`while (i < strlen(s))` 均可（编译期自动展平为临时槽）。

### 操作符

| 类别 | 符号 |
|------|------|
| 算术 | `+` `-` `*` `/` `%` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `\|\|` `!` |
| 位运算 | `&` `\|` `^` `<<` `>>` |

> `/`：两侧均 int → 整除（C 风格，`7/2`=3、`-9/2`=-4）；任一侧 float → 浮除（`7.0/2`=3.5）。

### 内建函数

`abs` `neg` `sign` `pow` `sqrt` `exp` `log` `min` `max`（变参，如 `max(a,b,c)`）`print` `cerr` `input`\
`int` `float` `bool` 及十个精度算子 · `char` `ord` `strlen` `strcmp` `strstr` `slice` `concat` · `array` `len` `at` `set` `has` `sort` `dict` `kvat` `kvhas`

```kv
s = "hello"
s[1] = "a"             # 替换下标 1 的字符 → "hallo"
s + " world" -> t      # 拼接 → "hallo world"
```

字符串支持索引与拼接：`s[i]` 读第 i 个字符，`s[i] = "X"` 单字符替换，`a + b` 拼接。
a:int64 = [7, 2, 9, 4]     # typed 1D array, = ≡ <-
len(a) -> n              # 4
at(a, 2) -> e            # 9
```

C 风格 API：`strlen` 长度、`strcmp` 返 -1/0/1、`strstr(hay, needle)` 返首次下标（未找到 -1）、`ord(c)` 取字节码。

---

## Tutorial

96 个自包含示例（95 例带期望输出，CI 全量验证），按主题组织：

```
01-basics/        hello, arith, precision, numtypes, strings（6 files）
06-lib/        import lib + main                            （2 files）
02-func/          def, call, accumulator                   (2 files)
03-control/       if, while, for, guess game               (5 files)
04-algo/          fibonacci, gcd, collatz, ...             (13 files)
07-leetcode/      LeetCode solutions                       (69 files)
```

```bash
./kvlang tutorial/01-basics/hello.kv         # hello kvlang
./kvlang tutorial/04-algo/fibonacci.kv       # fib = 55
./kvlang tutorial/07-leetcode/001_two_sum.kv # LeetCode

python3 tutorial/test.py                     # 全部 99 例 — CI 验证
```

---

## License

MIT — see [LICENSE](LICENSE)
