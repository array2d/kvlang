# kvlang 编译器前端设计

---

## 一、标准前端流水线

无论语言是否行导向，标准编译器前端的数据流只有一条：

```
Source string
    │
    ▼  Scanner.Scan() → []Token       词法分析：字符 → Token
    │
    ▼  Parser.Parse() → *ast.File     语法分析：Token → AST
```

**行导向只是语法规则的一种，不是简化流水线的理由。**
汇编语言是行导向的，但 NASM、GAS 都有完整的 Lexer + Parser。
kvlang 同理：换行符是词法噪音（跳过），`{` `}` 是结构 Token，
"一行一条指令"是语法规则，由 Parser 保证，而不是靠 `bufio.Scanner` 按行切割。

---

## 二、当前架构的根本问题

```
现在：
  io.Reader
    │ bufio.Scanner（按换行切割）
    ▼
  []string  ← 在 Scanner 层之前就已经结构化，违反了"字符→Token"的顺序
    │ Tokenize(line)（逐行）
    ▼
  [][]Token（隐式，未显式维护）
    │ 控制流解析用 strings.Count / strings.Index 操作原始行字符串
    ▼
  *ast.File
```

核心错误：**`[]string` 在 `[]Token` 之前流动**。
后果：
- `parseBracedBody` 用 `strings.Count(line,"{")` 数字符，字面量内的 `{` 被误计
- `parseIfStmt` 用 `strings.Index(line,"(")` 定位字节，字面量内的 `)` 会截错
- `parseTopLevelCall` 重复实现了 `parseInstFromTokens` 的全部逻辑
- `extractFuncName`、`looksLikeCall` 在字符串层面操作语法结构

---

## 三、目标架构

### 3.1 数据流

```
Source string
    │
    ▼ Scanner.Scan(src string) → []Token
      · 跳过空白（含换行）和 # 注释
      · 产生平坦 Token 流，含 EOF 哨兵
      · 不知道"函数定义"是什么
    │
    ▼ Parser{tokens []Token, pos int}
      · peek() / advance() / expect(kind)
      · 递归下降，消费 Token 驱动结构
      · 块深度由消费 LBrace/RBrace Token 自然追踪
      · 不碰原始字符串
    │
    ▼ *ast.File
```

### 3.2 Token 类型补充

在现有 Kind 基础上增加：

```go
EOF  // 文件结束哨兵，消除 pos 越界检查
```

（`Newline` 不需要作为 Token——换行是空白，Parser 通过 `}` 和关键字判断结构边界。）

### 3.3 Scanner API

```go
// Scan 将整个源字符串扫描为平坦 Token 流，末尾附 EOF。
// 输入可以是整个文件内容，不限于单行。
func Scan(src string) []Token
```

现有 `Tokenize(line string)` 保持向后兼容（内部调用 `Scan`），
调用方逐步迁移。

### 3.4 Parser 结构

```go
type parser struct {
    tokens []Token
    pos    int
}

func (p *parser) peek() Token        // 当前 Token，不消费
func (p *parser) advance() Token     // 消费并返回当前 Token
func (p *parser) eat(k Kind) bool    // 若匹配则消费，返回是否成功
func (p *parser) expect(k Kind) Token // 消费，不匹配则记录错误
```

### 3.5 核心解析函数结构

```
parseFile()          → *ast.File
  └─ parseFunc()     → ast.Func        (遇到 "def")
  └─ parseInst()     → *ast.Instruction (顶层调用)

parseFunc()
  ├─ 消费 "def" + Ident（函数名）
  ├─ 消费签名 Token 直到 LBrace
  ├─ eat(LBrace)
  ├─ parseBody()
  └─ expect(RBrace)

parseBody() → []ast.Stmt             (消费直到 RBrace 或 EOF)
  └─ parseStmt()
       ├─ "if"       → parseIf()
       ├─ "for"      → parseFor()
       ├─ "while"    → parseWhile()
       ├─ "break"    → BreakStmt
       ├─ "continue" → ContinueStmt
       ├─ Ident + ":" → parseBlockLabel()
       └─ 其他        → parseInst()

parseBlockLabel()
  ├─ 消费 label Ident + ":"
  ├─ eat(LBrace)
  ├─ parseBody()
  └─ expect(RBrace)

parseIf()
  ├─ 消费 "if"
  ├─ expect(LParen) → collectUntilRParen() → Cond string
  ├─ eat(LBrace)
  ├─ parseBody() → Then
  ├─ expect(RBrace)
  ├─ 若下一个是 Ident("else") → 消费 "else" + LBrace → parseBody() → Else + expect(RBrace)
  └─ 返回 *ast.IfStmt

parseFor() / parseWhile()  // 同理，collectUntilRParen 提取条件

parseInst()                // 消费 Token 直到下一个语句边界（下一个关键字 / } / EOF）
  └─ parseInstFromTokens(collected tokens)
```

**块深度追踪**：Parser 消费 `LBrace` 进入块、消费 `RBrace` 退出块。
不再需要 `strings.Count`，深度由调用栈自然体现。

### 3.6 `collectUntilRParen` — 提取括号内容

```go
// 消费从当前 LParen 到匹配 RParen（含嵌套）之间的 Token，
// 返回拼接后的条件字符串（空格分隔）。
func (p *parser) collectUntilRParen() string {
    p.expect(LParen)
    var parts []string
    depth := 1
    for depth > 0 && p.peek().Kind != EOF {
        t := p.advance()
        switch t.Kind {
        case LParen: depth++; parts = append(parts, t.Value)
        case RParen:
            depth--
            if depth > 0 { parts = append(parts, t.Value) }
        default: parts = append(parts, t.Value)
        }
    }
    return strings.Join(parts, " ")
}
```

---

## 四、文件职责（重新划分）

| 文件 | 职责 | 禁止 |
|------|------|------|
| `scanner.go` | `Scan(src string) → []Token`；`Tokenize` 作为兼容别名 | `import ast`；语法判断 |
| `expr.go` | `parseInstFromTokens([]Token) → *ast.Instruction`；`ParseLine` 保持公开 API | 直接操作原始字符串 |
| `stmt.go` | `parseBody` / `parseStmt` / `parseIf` / `parseFor` / `parseWhile` / `parseBlockLabel` | `strings.Count/Index` 做结构判断 |
| `file.go` | `parser` 结构体；`ParseFile` / `ParseCode` / `parseFile` / `parseFunc` | — |
| `signature.go` | `ParseSignature → FormalParams`（签名字符串来自 KV，格式已知） | — |

### 包内调用（单向依赖）

```
file.go → stmt.go → expr.go → scanner.go
```

---

## 五、改动优先级

### Phase 1 — 修 bug（正确性，立即做）

| # | 问题 | 改法 |
|---|------|------|
| 1 | `parseBracedBody`：`strings.Count` 误计字面量内 `{}` | 改为消费 Token 流计 LBrace/RBrace |
| 2 | `parseIfStmt` 等：`strings.Index(line,"(")` 字节定位 | 改为 `collectUntilRParen` |

### Phase 2 — 标准化（架构，下一步做）

| # | 改动 |
|---|------|
| 3 | `Scan(src string)` 支持全文扫描（不限单行），`Tokenize` 复用它 |
| 4 | 引入 `parser` 结构体（`tokens`+`pos`+`peek`/`advance`），替代传递 `[]string` |
| 5 | `parseFile`/`parseFunc`/`parseBody`/`parseStmt` 按 3.5 节重写 |
| 6 | 删除 `extractFuncName`（Token 流天然可得函数名） |
| 7 | `parseTopLevelCall` 合并入 `parseInst`（顶层调用与体内指令语法相同） |
| 8 | `looksLikeCall` 随 `[]string` 消失一并删除 |
| 9 | `file.go` → `stmt.go` + `file.go` 拆分 |

### Phase 3 — 工业级完善（未来）

| 能力 | 现状 | 做法 |
|------|------|------|
| 源位置 | Token 无行列信息 | `Token.Pos{Line,Col}`，`Scan` 追踪行列 |
| 错误恢复 | 首错即止 | `parser.errors []SyntaxError`；遇错跳至同步 Token（`}`/`def`/`EOF`）后继续 |
| 算符优先级 | `A+B*C` 右操作数按字符串拼接 | Pratt Parser；kvlang 目前 SSA 风格暂不暴露此问题 |

---

## 六、当前状态

| 改动 | 状态 |
|------|------|
| `lexer.go` → `scanner.go` 重命名 | ✅ |
| `line.go` → `expr.go` 重命名 | ✅ |
| `parseBody` 首 Token 分发 | ✅ |
| Phase 1-1 `parseBracedBody` Token 计深度 | ✅（Phase 2 一并解决） |
| Phase 1-2 `parse*Stmt` Token 提取条件 | ✅（Phase 2 一并解决） |
| Phase 2-3 `Scan(src string)` 支持全文扫描 | ✅ |
| Phase 2-4 `parser` 结构体（tokens+pos+peek/advance） | ✅ |
| Phase 2-5 `parseFile/parseFunc/parseBody/parseStmt` 重写 | ✅ |
| Phase 2-6 删除 `extractFuncName` | ✅ |
| Phase 2-7 `parseTopLevelCall` 合并入 `parseInst` | ✅ |
| Phase 2-8 删除 `looksLikeCall` | ✅ |
| Phase 2-9 `file.go` → `stmt.go` + `file.go` 拆分 | ✅ |

### 实现备注

**新增 Token 类型**（超出设计文档最小集，实践中必要）：

| Kind | 原因 |
|------|------|
| `EOF` | 设计文档明确要求的文件结束哨兵 |
| `While` | 关键字统一化，与 `If`/`For`/`Return` 对称 |
| `Colon` | 块标签 `label: {` 的分隔符；原来 `:` 被静默跳过，导致块标签无法与指令区分 |
| `Newline` | 语句分隔符；对于 `<-` 风格指令（写端在左），没有 Arrow 终止，只能靠换行终止 |

**设计决策**：`Newline` 作为语句分隔符

设计文档说"换行是空白"，指换行不用于判断**块结构**（`{}` 负责）。
但 kvlang 是行导向语言——`'./C' <- A + B` 后紧跟 `'./D' <- X - Y`，
没有换行 token 时无法判断第一条指令在哪结束。
因此 `Scan` 发出折叠后的 `Newline` token，`collectInstTokens` 以它作语句终止。
`Tokenize`（单行兼容接口）过滤 `Newline/EOF`，保持向后兼容。

**块标签关键字冲突**：`else: {`

`block_branch.kv` 把 `else` 用作块标签名。`Else` 是关键字 Kind，
`parseStmt` 在 switch 之前先做 `peekAt(1).Kind == Colon` 检查，
允许任何 token（含关键字）作为块标签，解决了冲突。
