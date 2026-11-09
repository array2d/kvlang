# layout

把 kvlang 源码布局到 kvspace。**输入**：一个 `.kv` 文件；**输出**：`/lib` 下可直接执行的明文指令树。

## 用户的话（本目录的第一原则）

> 我不太熟悉 rust 语法和标准库，我的理念是只要给我正确的把 kvlang 源码布局到 kvspace 就 ok，
> 但我还是希望 layout 的实现，优雅一些。

这句话决定了两件事：

1. **正确性优先于一切**：读的人不打算读 Rust 代码，只打算看「布局到 kvspace 的结果对不对」。
   所以**产物是唯一的验收面**——`kvspace tree /lib/<f>` 打出来的东西不对，代码再漂亮也是错的。
2. **但优雅是要求，不是愿望**：因为读的人不看代码，代码的整洁不会靠 review 逼出来，
   所以它必须在每一次改动时被主动维护。

## 优雅在这里的具体含义

不是「用了 fancy 的 Rust 特性」——恰恰相反：

- **一条规则只有一处定义**。同一件事两个实现必然漂移，本轮已实测到多次
  （`lit_to_type` 与另外三处对字符串字面量给出不同答案）。
- **判据写在该判据的层**。不拿「文本长得像 X」代替「语义是 X」；不用显示层
  （CLI 的 `None`、`bin/` 里的旧产物）回答事实层的问题。
- **失败要有名字**。`unwrap_or_default()` 把失败折成空串，下游再也看不出这里错过。
  空类型本身是合法的「缺席」——用它当哨兵就是「哨兵越出数据域」。
- **不写注释解释烂代码**，而是把烂代码改成不需要解释。

## 为什么读的人不看 Rust 也不影响正确性

因为**布局产物是可验证的**：

```bash
./bin/kvlanglayout my.kv shm:///tmp/probe
KVSPACE=shm:///tmp/probe ./bin/kvspace tree /lib/<function>   # 看指令槽
./bin/kvlanglayout printlib my.kv /lib/<function>             # 反向重建源码 + 槽位注释
```

`printlib` 会把产物重建成可读的 kvlang 源码并逐槽标注类型——**产物对不对，
不需要读 Rust 就能判断。**

## 目录

| 文件 | 职责 |
|---|---|
| `src/scanner.rs` | 词法：源码 → Token 流（手写字符级扫描，非正则/DFA 生成器） |
| `src/parser.rs` | 语法：Token → AST（手写递归下降 + Pratt 表达式解析） |
| `src/ast.rs` | AST 定义与 `flat()`（指令 → 操作码 + 读参） |
| `src/symbol.rs` | 算子表：字形 → 语义名、优先级、是否比较/算术 |
| `src/lower.rs` | 语义分析与降级：类型推导、特化判定、指令展开 |
| `src/langtype.rs` / `src/kvkind.rs` | langtype 文法与 wire 形态 |
| `src/code.rs` | 落盘：把布局结果写进 kvspace |
| `src/capi.rs` / `src/ffi.rs` | 对 kvspace-durable 的 C ABI |

## 改这个目录时

- 改之前先看 `src/*.rs` 顶部的大段注释——那里记着「为什么是这样而不是那样」，
  包括踩过的坑与当时的实测数据；
- 改完必须跑 `tutorial/` 回归（那是主要验收手段），而不是只看 `cargo test`；
- **`bin/` 是 WIP 产物，不能当基准**：测量前先确认 `bin/kvlanglayout` 与
  `layout/target/release/kvlanglayout` 同代（可执行与 `.so` 是两个产物，各自可能落后）。
