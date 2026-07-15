# parser todo — 违反最高标准设计的代码

## S10：`isNum` 启发式误判字符串字面量（inst.go `parsePrimaryExpr`）

**文件**：`inst.go`

**现状**：
```go
isNum := len(v) > 0 && v[0] >= '0' && v[0] <= '9'
```
仅检查第一个字符，导致 `"5*6 ="` 之类以数字开头的字符串字面量被误判为数字，
不添加 `"` 前缀，运行时无法识别为字符串字面量，`print("5*6 =", x)` 输出 ` 6` 而非 `5*6 = 6`。

**目标**：改为判断整个字符串是否为合法数字（仅含 `0-9 . e E`）：
```go
isNum := isNumericLiteral(v)  // 全字符检查
```

**影响**：✅ 影响当前功能（含特殊字符的字符串字面量首字符为数字时语义错误）

---

## S11：写槽（write slot）不校验 KV 路径格式（inst.go `collectWriteList`）

**文件**：`inst.go`

**现状**：
```go
// collectWriteList / collectWritesUntilArrow
writes = append(writes, p.advance().Value)  // 裸标识符也被接受
```
`->` 右侧的写槽接受任意 token，包括裸标识符（`s`、`result`）。  
这使得 `funca() -> s` 被静默接受，语义上却暗示"函数有返回值"——与 kvlang 无返回值的设计矛盾。

**目标**：在 `collectWriteList` 和 `collectWritesUntilArrow` 中对每个写槽 token 做路径校验：
```go
if !isKVPathToken(tok.Value) {
    p.errors = append(p.errors, Diagnostic{
        Pos:     tok.Pos,
        Message: fmt.Sprintf("write slot must be a KV path (./x or /abs), got %q", tok.Value),
    })
}
```
其中 `isKVPathToken` 检查：以 `./` 或 `/` 开头。

**影响**：✅ 影响当前功能（裸标识符写槽在运行时静默出错，此校验可将问题前移到解析阶段）

**背景**：`funca() -> s` 是错误写法，`s` 应写为 `./s`。详见 `kvlang语言深度理解.md §3`。

---

## S9：`TopLevelCalls` → `def init()` 合并（Parser 层合成）

**文件**：`parser.go`、`ast/ast.go`、`load.go`

**现状**：
- `ast.File` 有两个出口：`Funcs []Func` 和 `TopLevelCalls []*Instruction`
- 裸顶层语句由 `load.go` 在运行时合成 `pre_main`，是消费层的特例处理

**目标**（见 `最高标准设计.md §2.7`）：

1. **`parser.go` `parseFile()` 末尾**：将 `topStmts` 合并进 `def init()`
   ```go
   if len(topStmts) > 0 {
       if idx := funcIndex(f.Funcs, "init"); idx >= 0 {
           f.Funcs[idx].Body = append(f.Funcs[idx].Body, topStmts...)
       } else {
           f.Funcs = append(f.Funcs, ast.Func{
               Sig:  ast.FuncSig{Name: "init"},
               Body: topStmts,
           })
       }
   }
   ```

2. **`ast/ast.go`**：删除 `File.TopLevelCalls []*Instruction` 字段

3. **`load.go`**：
   - 删除 `allCalls` 积累逻辑和 `pre_main` 合成
   - 所有文件的 `Funcs`（含各自的 `init()`）统一 `WriteFunc`
   - 多文件时：在 load 层合成一个 `main/init` 依次调用各 `<pkg>/init`
   - 入口由 `Bootstrap(ctx, kv, vtid, "init")` 统一触发

4. **`cmd/kvlang/serve.go`**：`pre_main` → `init`，`Bootstrap` 调用点更新

**影响**：✅ 影响当前功能（`-c` 内联执行已修复 ParseCode 门卫，此项是完整收尾）

---

# 最高标准设计本身的已知简化点

> 以下是当前 `最高标准设计.md` 与编程语言/编译器领域真正工业最高标准之间的已知差距。
> 每条标注：是否影响当前功能 / 未来需要时的升级方向。
>
> ✅ 已修复的标注为 **DONE**。

---

## S3：表达式解析无优先级 — **DONE**

**已实现**（2026-07-13）：`inst.go` 中 `parseExprInto` 替换为 Pratt 解析器 `parsePratt`。

- `ast.Expr` 节点（`ast/ast.go`）：`{Op string; Args []*Expr; Val string}`
- `ast.Instruction` 改为 `{Expr *Expr; Writes []string}`
- `ast.Leaf(v)` / `ast.Call(op, args...)` 构造函数
- `ast.InfixPrec(op)` 中缀优先级表（`||`=10 … `|`=100）
- `lower.flattenInstExpr`：将复合子表达式展开为 SSA 平坦序列
- 测试：`parsertest/main_test.go` `TestPratt_Precedence` / `TestPratt_CompoundCond`

**之前的问题**：
```
输入：a + b * c
旧结果：Opcode="+"  Reads=["a","b","*","c"]   ← 语义错误
新结果：Expr{Op:"+", Args:[Leaf("a"), Expr{Op:"*", Args:[Leaf("b"), Leaf("c")]}]}  ✅
```

---

## S6：注释被完全丢弃，破坏格式化的完整性 — **DONE**

**已实现**（2026-07-13）：

1. **Scanner**（`scanner.go`）：`#` 注释产生 `Comment` Token（Kind=Comment），不再丢弃。
2. **Parser**（`parser.go`）：新增 `collectLeadingComments()` 方法，在 `parseFile` / `parseBody` 中
   收集前置 Comment Token 并附加到紧随其后的 AST 节点。
3. **AST**（`ast/ast.go`）：所有 Stmt 类型（`Instruction`、`IfStmt`、`ForStmt`、`WhileStmt`、
   `BreakStmt`、`ContinueStmt`、`BlockStmt`）及 `Func` 均含 `Comments []string` 字段；
   `StmtComments(st Stmt) []string` 辅助函数统一读取。
4. **Format**（`ast/format.go`）：`Format()` / `formatBody()` 在每个节点前输出前置注释；
   `Func.FullText()` 也包含注释。
5. 测试：`TestComments_Preserved` / `TestFormat_CommentsPreserved` / `TestScan_CommentTokens`

**注意**：行尾内联注释（`x + 1  # inline`）在 `parseInst` 末尾被丢弃——不附加到当前指令，
只有独立行注释（`# comment\n next_stmt`）才作为下一语句的前置注释保留。

---

## 附加改进：break/continue 统一为关键字 Token

`scanner.go` 中 `break` → `Kind=Break`，`continue` → `Kind=Continue`，
与 `if/for/while/return` 对称。`parseStmt` 中直接用 Kind 分发，移除 `p.peek().Value` 字符串比较。

---

## S4：左右箭头并存导致 O(n) 前瞻，破坏 LL(1)

**当前**：`findTopLevelArrow()` O(n) 扫描，带线性前瞻的递归下降（非 LL(k)）。

**影响**：⚠️ 边缘（性能+文法）
**升级方向**：若语言决定废弃 `<-`，统一为 `->` 后置写槽，`findTopLevelArrow()` 可删除，
`parseInst` 变为严格 LL(1)。若保留双向箭头，当前方案是正确且必要的，记录为已知的非 LL(1) 点。

---

## S5：错误恢复只跳过一个 Token，可产生级联假错误

**当前**：`expect()` 不匹配时跳过当前 token，合成 token 继续解析。

**影响**：⚠️ 影响诊断质量
**升级方向**：实现 `sync(follow ...Kind)` 跳到同步集合（`}` `;` 换行 关键字），
在 `parseStmt`、`parseBody` 的错误路径上调用。

---

## S1：Token 只有起始位置，无终止位置（无 Span）

**影响**：⚠️ 影响诊断精度
**升级方向**：Token 增加 `End Pos`（或改为字节偏移对 `[Lo, Hi)`）；`Diagnostic.Span` 替换 `Diagnostic.Pos`。

---

## S2：位置存储用 Line/Col，而非字节偏移 + 懒惰 SourceMap

**影响**：⚠️ 架构债（多文件合并时位置无法统一编码）
**升级方向**：`Pos` 改为 `type Pos int32`（字节偏移）；`Scan` 返回时附带 `[]int`（每行起始偏移）。

---

## S7：无形式文法规范（EBNF/PEG）

**影响**：⚠️ 影响可维护性
**升级方向**：在 `最高标准设计.md` 中补充 EBNF 一节，作为 parser 实现的权威参考。

---

## S8：无增量解析

**影响**：❌ 当前无影响（批处理场景）
**升级方向**：LSP 需求出现时再引入 `rowan` 风格的绿树/红树架构。

---

## 优先级建议（更新后）

| 编号 | 影响当前功能 | 状态 |
|------|------------|------|
| S3 表达式优先级 | ✅ 是（语义错误） | **DONE** 2026-07-13 |
| S6 注释丢失 | ✅ 是（kvfmt 有损） | **DONE** 2026-07-13 |
| S10 isNum 误判 | ✅ 是（字符串首字符为数字时出错） | **DONE** 2026-07-14 |
| **S11 写槽不校验 KV 路径** | ✅ 是（裸标识符写槽静默出错） | **待实现** |
| **S9 TopLevelCalls→init()** | ✅ 是（架构收尾） | **待实现** |
| S4 线性前瞻 | ⚠️ 边缘（性能+文法） | 统一箭头方向时顺带修复 |
| S5 错误恢复 | ⚠️ 影响诊断质量 | 有余力时改进 |
| S1 Token Span | ⚠️ 影响诊断精度 | 有余力时改进 |
| S2 字节偏移位置 | ⚠️ 架构债 | 多文件支持时处理 |
| S7 形式文法 | ⚠️ 影响可维护性 | 语言稳定后补充 |
| S8 增量解析 | ❌ 当前无影响 | LSP 需求出现时 |
