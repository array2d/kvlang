# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-140%20examples-4c1)](tutorial/)
[![Docs site](https://img.shields.io/badge/docs-stdlib-blueviolet)](https://array2d.github.io/kvlang/#/stdlib/kvlang/kvlangbrief.kv)

**以 kvspace 为寻址空间和内存空间、小核心、扩展主导的明文解释执行语言（原训推框架 deepx 的前端语言，前身 dxlang）。** 代码与数据统一在一棵 KV 树；PC 即 KV 路径、可崩溃恢复，源码即 IR、KV 皆明文。核心 runtime 只做执行循环与控制流，其余能力交由 rwirext 扩展承担（`term` / `json` 等为基础示例）。

> English: [README.md](README.md) | 设计：[deep-dive](https://github.com/array2d/deepx-design/blob/master/doc/kvlang/deep-dive.md) — 根设计文档；README 为教学衍生。全部行为规范（p0–p7）、指令模型（§2）、Link 调用机制（§6）、类型系统（§9）、诊断体系（§12）均在其中。
>
> 设计文档 (CN): [deepx-design/doc/kvlang-design-and-implementation](https://github.com/array2d/deepx-design/tree/master/doc/kvlang-design-and-implementation) · (EN): [deepx-design/doc-en/kvlang-design-and-implementation](https://github.com/array2d/deepx-design/tree/master/doc-en/kvlang-design-and-implementation) — 19 章覆盖架构、parser、runtime、kvspace 及语言设计参考。

---

## 应用场景

- **deepx 的模型前端**：作为训推一体化框架 deepx 的前端语言（前身 dxlang），以 KV 树表达模型结构与算子图——代码、参数、中间结果同在一棵 KV 树，按路径寻址。
- **自迭代 agent（如 byteseek）**：agent harness 用 kvlang 编写；代码即数据、PC 即路径、崩溃可按 PC 恢复，agent 读改自身代码与状态走同一套 KV 读写。

---

## 核心模型：一屏看懂

**不分 IR 层，源码即 IR。** 程序计数器是 kvspace 路径字符串，调用栈深度是路径里的帧号：

```
PC   = "/vthread/tid/[1]/[3,0]"            vthread tid、第 1 帧、第 3 条指令
取指 = GetBatch(帧目录/, ["[3,0]"])         从帧目录取 opcode（extindex → /lib）
调用 = 创建 [d+1] 帧；返回 = DelTree        崩溃后按 PC 重启继续
goto/br = 只改同一帧的 irseq                if/while 不建帧
```

每条指令占据二维坐标 `[s0, s1]`：`[s0,0]` 恒为操作码，`[s0,-j]` 读参，`[s0,+j]` 写参。

```kv
lib main {
    rwfunc add(A:int64, B:int64) -> (C:int64) { A + B -> C }
}
```

```
/lib/main.add/[0,0]  = "+"     /lib/main.add/[0,-1] = "A"
/lib/main.add/[0,-2] = "B"     /lib/main.add/[0,1]  = "C"
```

地址空间只有两个域：`/lib`（函数库——签名、指令树、`.src` 源码）和 `/vthread`（运行时栈帧）。`/` 下其余路径全部由用户自定义。**没有 `/dev` 设备域，也没有终端**——KV 世界里只有 key 和 value；`print` 这类 I/O 是[扩展 rwir](#内建函数与扩展-rwir)，不是地址空间域。

---

## 生态架构

kvspace 是核心的寻址空间与内存空间；语言本体是小核心 runtime，能力由上层扩展承担。

![kvlang 生态架构](docs/kvlang-ecosystem-architecture.png)

- **kvspace** — 一套 C ABI（`kvspace_*`，24 符号），由 DSN 选择两种实现：`kvspace-c`（C，`shm://`，链接 `blockmalloc` + `slotsboxmalloc`）与 `kvspace-durable`（Rust，`redis://` / `fs://`，s3/tikv 规划中）。
- **kvlang** — `layout`（Rust，编译）与 `runtime`（C，执行），二者都只依赖 `kvspace_*` C ABI。
- **rwirext** — 构建在 runtime 之上的扩展。嵌入式（Rust `term`，经 `kvlang_rwirext.h` 链接 `libkvlang_runtime`）或独立进程 handoff（Go `json`、Python `numpy`）。`term` / `json` 只是基础示例扩展，不是招牌能力。

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

**顶层：`lib name { }`、`rwfunc`、单条指令。** 始终用 `rwfunc main() -> () { … }; main()` 包裹代码。裸 `if` / `while` / `for` 在顶层可能触发隐式 `init()` 包装，但不可靠——显式包裹 `main()` 是唯一保证模式。切勿将函数命名为 `init`，以避免与隐式包装冲突。

```kv
rwfunc main() -> () {
    total = 0  # = 等价于 <-
    1 -> i
    while (i <= 5) {
        total <- total + i
        i + 1 -> i
    }
    println(total)
}

main()
```

### rwir（读写码）：赋值三形态

```kv
x = 40 + 2            # = ：写槽在左（≡ <-）；= 不是表达式，不能嵌进条件里
y <- x                # 左箭头：写槽在左
x × y -> z            # 右箭头：写槽在右
f(a, b) -> r          # 函数写参映射；多写参 -> x, y；丢弃用 -> _
```

写槽必须是**位置**：裸名（帧内变量）、`/abs/path`（全局键）、`base.名`（成员）。字面量不是位置。

**`rwfunc func(ra,rb) -> (wa,wb) { … }` = 自定义复合 rwir**，单条 rwir 如 `A + B -> C` 是原子 rwir（一个操作码 + 读参 + 写参）；`rwfunc` 把多条 rwir 打包成命名单元，对外暴露相同的箭头接口——`(ra,rb)` 是读参声明，`-> (wa,wb)` 是写参声明。调用 `add(3,4) -> s` 即把实参绑入读槽、写槽映射回调用方帧。没有返回值，只有写参映射。

`rwfunc` 签名中 `-> (C:int64)` 是**写参声明**。函数把结果写进写参槽，调用方用 `-> r` 把写参映射到自己的位置。
**读参只读**：函数体内不可把读参放进写槽（如 `A = A + 1`）。数组元素写同理——`a[i] <- v` 写穿 `a`，要修改的数组/字典必须放写参位置。
```kv
# ❌ 错误：数组作读参，a[i] <- v 写读参槽 → parser 拒绝
rwfunc bad(a:int64) -> () { 99 -> a[0] }

# ✅ 正确：数组作写参，函数内读写自由
rwfunc good() -> (a:int64) { a:int64 = [10, 20]; 99 -> a[0]; a }
```
需要体内反复更新的量先想清角色——
**累加器是输出，声明为写参**（写参零值起步、体内可读可写，同 Go 命名返回值）：`rwfunc sum(arr:int64) -> (acc:int64) { acc + arr[i] -> acc }`；
纯工作变量则拷贝局部（`A -> a` 后用 `a`）：

```kv
rwfunc add(A:int64, B:int64) -> (C:int64) {
    A + B -> C
}

rwfunc main() -> () {
    add(3, 4) -> s
    println(s)          # 7
}

main()
```

### dict、成员访问与链表

```kv
d = { name="kv"; ver=1 }    # dict 字面量：成员是平坦键族 d.name、d.ver
println(d.name)               # 成员读
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
rwfunc build() -> () {
    /n1 = { val=1; next="/n2" }  # = 等价于 <-
    /n2 <- { val=2; next="/n3" }
    { val=3; next="" } -> /n3
}

rwfunc main() -> () {
    build()
    "/n1" -> p                   # p 存路径字符串（指针）
    while (p != "") {
        p.val -> v               # 指针解引用：读 /n1.val
        println(v)
        p.next -> p
    }
}

main()
```

### 数字类型（仅精确宽度——无 `int` / `float`）

```kv
f = float32(3)        # int8/16/32/64 uint8/16/32/64 float32/64 十算子，既创建也转换
w = int8(300)         # 44：窄化补码回绕；float→int 截断向零；算术域统一 int64/float64
x:int64 = 42          # 带类型标注的变量声明
```

`int` 和 `float` 被 parser **拒绝**——必须使用精确宽度类型。十个精度算子既是构造函数也是类型转换。

### 控制流（仅限 rwfunc 体内）

```kv
i = 1; sum = 0
while (i <= 10) { sum + i -> sum; i + 1 -> i }
if (sum > 50) { println("big") } else { println("small") }
for (x in [7, 2, 9, 4]) { println(x) }
```

条件支持复合表达式：`if (7 % 2 != 0)`、`while (i < string.len(s))` 均可（编译期自动展平为临时槽）。

### 操作符

| 类别 | 符号 |
|------|------|
| 算术 | `+` `-` `×` `÷` `%` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `\|\|` `!` |
| 位运算 | `&` `\|` `^` `<<` `>>` |

> `÷`：两侧均 int → 整除（C 风格，`7÷2`=3、`-9÷2`=-4）；任一侧 float → 浮除（`7.0÷2`=3.5）。
> `/` 保留用于路径及路径分隔。`*` 保留用于后续指针解引用。

### 内建函数与扩展 rwir

**内建（builtin）** 是 runtime 在进程内直接求值的 rwir（`bi_is_native` 集合），全部是纯 KV→KV 计算，不做 I/O：

**标量：** `abs` `neg` `sign` `pow` `sqrt` `exp` `log` `min` `max`（变参，如 `max(a,b,c)`）`debugger`\
**类型：** `bool` `int8` `int16` `int32` `int64` `uint8` `uint16` `uint32` `uint64` `float32` `float64` `char/utf8` `char/utf32` `char/ascii`\
**容器：** `array` `at` `set` `has` `array.sort` `array.slice` `array.append` `dict`\
**形状：** `ndarray.numel` `ndarray.dim` `ndarray.shape` `xv.at` `xv.set`\
**KV 树：** `kv.get` `kv.set` `kv.del` `kv.deltree` `kv.list` `kv.mkindex` `kv.extindex` `kv.rmindexext` `kv.watch` `kv.has` `kv.at`\
**字符串：** `string.char` `string.ord` `string.len` `string.cmp` `string.find` `string.slice` `string.concat` `string.set`\
**时间：** `time.now` `time.sub` `time.add` `time.before` `time.after` `time/duration.nanos` `time/duration.as_nanos`（及 `millis`/`seconds`/`minutes`/`hours` 变体）\
**随机：** `random.uint64` `random.int63` `random.intn`

**`print` / `println` / `cerr` 不是内建。** KV 世界里没有终端，只有 key 和 value——I/O 不是核心语言原语。它们是**扩展 rwir**：由 `term` 扩展运行时把签名注册到 `/lib/<opcode>`（kind=`rwir`），并写宿主进程的 `stdout`/`stderr`。核心 runtime 把任何"`/lib/<opcode>` 上带 `rwir` 签名、且不在 builtin 表里"的 opcode 识别为扩展 rwir，交给其扩展运行时执行。与 `json.to` / `json.from`（json 扩展）、tensor 算子（numpy / GPU 扩展）同一套机制。

```kv
a:int64 = [7, 2, 9, 4]     # 带类型 1D 数组，= ≡ <-
ndarray.numel(a) -> n         # 4
at(a, 2) -> e            # 9
set(a, 1, 99) -> a       # 修改元素：a 变为 [7, 99, 9, 4]
sort(a) -> sorted         # 排序副本：[2, 4, 7, 9]
```

```kv
s = "hello"
string.char(s, 1) = "a"     # 替换下标 1 的字符 → "hallo"
s + " world" -> t           # 拼接 → "hallo world"
string.len(s) -> n          # 5
string.find(s, "ll") -> i   # 2（子串首次下标，未找到 -1）
string.slice(s, 0, 2) -> p  # "he"
```

字符串支持索引和 `+` 拼接：`at(s, i)` 读字符，`string.char(s, i)` 读字符，`string.char(s, i) = "X"` 单字符替换。

---

## Tutorial

140 个自包含示例（129 例带期望输出，CI 全量验证），按主题组织：

```
01-basics/        hello, arith, precision, numtypes, strings, …  (15 files)
02-func/          rwfunc, call, accumulator                       (2 files)
03-control/       if, while, for, guess                           (5 files)
03-debugger/      chain_array, debugger builtin                   (4 files)
04-algo/          fibonacci, gcd, collatz, …                      (13 files)
06-lib/           lib block, nested, cross-lib, anon              (11 files)
07-leetcode/      LeetCode solutions                              (90 files)
error_cases/      type_error, index_error, zero_division, …       (36 files)
```

```bash
./kvlang tutorial/01-basics/hello.kv         # hello kvlang
./kvlang tutorial/04-algo/fibonacci.kv       # fib = 55
./kvlang tutorial/07-leetcode/001_two_sum.kv # LeetCode

python3 tutorial/test.py                     # 全部正例 — CI 验证
python3 tutorial/error_test.py               # 全部负例测试
```

---

## 设计文档

深度设计与实现文档，覆盖全部架构：

| 章节 | CN | EN |
|------|-----|-----|
| 总论 — 存算控制流分离 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-01-存算控制流严格分离的kv树计算架构.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-01-storage-compute-control-flow-separation.md) |
| 总论 — 一切皆明文 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-02-一切皆明文.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-02-everything-is-plaintext.md) |
| 总论 — 程序即数据结构+函数+数据 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-03-程序即数据结构加函数加数据.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-03-program-is-data-plus-functions.md) |
| 总论 — 代码四级层次 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-04-代码四级层次.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-04-four-level-code-hierarchy.md) |
| Parser — 指令架构 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-01-指令架构.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-01-instruction-architecture.md) |
| Parser — 函数 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-02-函数.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-02-functions.md) |
| Parser — 编译器流水线 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-04-编译器流水线.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-04-compiler-pipeline.md) |
| Parser — 诊断输出 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-05-诊断输出.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-05-diagnostics.md) |
| Parser — layoutrwir | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-06-layoutrwir.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-06-layoutrwir.md) |
| Parser & Runtime — 控制流 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser&runtime-01控制流篇.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-runtime-01-control-flow.md) |
| Runtime — 类型系统 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-01-类型系统.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-01-type-system.md) |
| Runtime — 成员访问与数据结构 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-02-成员访问与数据结构.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-02-member-access-and-data-structures.md) |
| Runtime — 调试与可观测性 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-03-调试与可观测性.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-03-debugging-and-observability.md) |
| Runtime — 函数调用与builtin | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-04-函数调用builtin.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-04-function-calls-and-builtins.md) |
| KVSpace — 地址空间 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-01-地址空间.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-01-address-space.md) |
| KVSpace — 寻址模型与命名 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-02-寻址模型与命名.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-02-addressing-model-and-naming.md) |
| KVSpace — 代码指令布局 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-03-代码指令的布局格式.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-03-code-instruction-layout.md) |
| KVSpace — 系统变量 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-04-系统变量.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-04-system-variables.md) |
| Reference — 如何设计编程语言 | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/reference篇-01-如何设计编程语言.md) | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/reference-01-how-to-design-a-programming-language.md) |

每篇英文翻译附带 **Implementation Consistency Notes**，已与 Go 源码逐条交叉核验。

## License

MIT — see [LICENSE](LICENSE)
