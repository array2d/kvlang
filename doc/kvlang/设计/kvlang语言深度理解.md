# kvlang 深度理解

## 1. 寻址模型：KV 路径 vs 内存地址

### 传统 VM (Python/Lua/JVM)

```
程序计数器 PC = 0x7fff5fbff830 (64-bit 内存地址)
指令    = 内存[PC] → 1 字节 opcode → 操作数
跳转    = PC = 新地址 (直接修改寄存器)
调用    = push 返回地址 → PC = 函数入口地址
栈帧    = 连续内存 [rbp-8] = 局部变量
```

内存地址是**一维线性整数**，跳转和调用本质是整数算术。

### kvlang

```
程序计数器 PC = "[0,0]/entry/[0,0]" (KV 路径字符串)
指令    = kv.Get("/vthread/tid/[0,0]/entry/[0,0]")
跳转    = PC = "[0,0]/merge/[0,0]" (字符串拼接)
调用    = PC = "[0,0]/then/[0,0]" (路径嵌套)
栈帧    = /vthread/tid/[0,0]/ 子树 (KV key 层级)
```

KV 路径是**树形层级字符串**，跳转和调用本质是路径拼接 + 子树导航。

| 维度 | x86/ARM | Python | Lua | kvlang |
|------|---------|--------|-----|--------|
| PC 类型 | `uint64` | `*PyCodeObject + offset` | `Instruction*` | `string` (KV path) |
| 指令获取 | `mov rax, [rip]` | `_PyEval_EvalFrameDefault` 循环 | `luaV_execute` 循环 | `kv.Get("/vthread/tid/" + pc)` |
| 跳转 | `jmp 0x400100` | `next_instr += oparg` | `pc++` | `pc = new_path` |
| 调用 | `call 0x400200` | `call_function` 压栈 | `luaD_precall` | `pc = pc + "/[0,0]"` |
| 栈帧 | `push rbp; sub rsp, N` | `PyFrameObject` (堆分配) | `CallInfo + L->stack` | `/vthread/tid/<pc>/` KV 子树 |
| 作用域 | 栈偏移 | `f_localsplus` 数组 | 寄存器索引 | KV key 子路径 (`./x`, `./y`) |

## 2. 指令的二维空间模型：`[s0, s1]`

### 2.1 两个轴的含义

每条指令在 KV 树中占据一个**二维坐标** `[s0, s1]`：

```
s0 轴（横轴） — 执行顺序轴：第几条指令
s1 轴（纵轴） — 参数轴：该槽的角色

        s1 < 0           s1 = 0         s1 > 0
      (读参，输入)        (操作码)       (写参，输出)
           ←─────────────── 0 ───────────────→
s0 = 0  │  [0,-2] [0,-1]  [0,0]  [0,1] [0,2]
s0 = 1  │  [1,-2] [1,-1]  [1,0]  [1,1]
s0 = 2  │  [2,-1]         [2,0]  [2,1]
  ...
```

**铁律**：
- `[s0, 0]` **永远是 opcode**（操作符或被调用函数名）
- `[s0, -1], [s0, -2], ...` **读参**（read slots），负号代表"消费数据"
- `[s0,  1], [s0,  2], ...` **写参**（write slots），正号代表"产出数据"

---

### 2.2 具体示例

```kv
def add(A: int, B: int) -> (C: int) {
    A + B -> ./C
}
```

lower + layoutcode 后写入 KV：

```
/func/main/add/[0,0]   = "+"      ← s1=0: opcode
/func/main/add/[0,-1]  = "A"      ← s1=-1: 第1个读参（left operand）
/func/main/add/[0,-2]  = "B"      ← s1=-2: 第2个读参（right operand）
/func/main/add/[0,1]   = "./C"    ← s1=1:  第1个写参（result destination）

/func/main/add/[1,0]   = "return" ← s1=0: opcode（隐式 return，appendReturn 补充）
```

图示为一张二维表格：

```
       s1=-2   s1=-1   s1=0      s1=1
s0=0 │  "B"    "A"    "+"      "./C"
s0=1 │                "return"
```

数据流方向清晰：**从负轴读入，在零轴执行，向正轴写出**。

---

### 2.3 多参数指令与扇出

```kv
print("hello", ./x, ./y)
```

```
/func/main/foo/[3,0]   = "print"   ← opcode
/func/main/foo/[3,-1]  = "\"hello" ← 第1读参（字符串字面量，" 前缀编码）
/func/main/foo/[3,-2]  = "./x"     ← 第2读参
/func/main/foo/[3,-3]  = "./y"     ← 第3读参
（无写参，副作用是输出）
```

写参扇出（同一结果写入多个槽）：

```kv
./a + ./b -> ./sum, ./backup
```

```
/func/main/foo/[2,0]   = "+"
/func/main/foo/[2,-1]  = "./a"
/func/main/foo/[2,-2]  = "./b"
/func/main/foo/[2,1]   = "./sum"    ← 第1写参
/func/main/foo/[2,2]   = "./backup" ← 第2写参（扇出）
```

---

### 2.4 `op.Decode`：沿 s1 轴扫描

kvcpu 解码时沿 s1 轴向两侧扩展，直到遇到空 key 为止：

```go
// 读参：s1 = -1, -2, -3 ... 直到 kv.Get 返回空
// 写参：s1 = +1, +2, +3 ... 直到 kv.Get 返回空
```

这意味着参数数量是**隐式编码的**：紧凑分配，无需在 opcode 中存储 arity。

**WriteFunc 的正确性约束**：写新函数前必须先 `DelTree` 清除旧数据。  
若旧函数在 `[0,-1]` 有遗留值，新函数的 Decode 会把它当成真实读参——这正是之前 `a -> b` 被错误解读为 `a(1, 2) -> b` 的根因。

---

### 2.5 与传统字节码的对比

| 维度 | 传统字节码 | kvlang `[s0, s1]` |
|------|-----------|-------------------|
| 指令存储 | 线性数组 `code[PC]` | KV 树 `kv.Get(prefix + "[s0,0]")` |
| 操作数位置 | opcode 后紧跟操作数 | `s1<0`（读）/ `s1>0`（写）分离 |
| 参数数量 | opcode 内编码 arity | 隐式：扫描到空 key 停止 |
| 数据流方向 | 单向（操作数 → 结果） | 符号编码（负=读, 正=写） |
| 可观察性 | 字节不可独立寻址 | 每个槽是独立 KV key，可单独 Get/Watch |
| 调试 | 需要反汇编器 | `redis-cli KEYS "/func/main/add/*"` 即可 |

---

### 2.6 为什么负数表示读参

这不是约定俗成，而是数轴对称性的自然选择：

```
写（产出）→   0  ← 读（消费）
+1 +2 +3 … [opcode] … -1 -2 -3
```

- **正数轴**：数据**流出**（写入目标槽，可类比 "加法方向"，产出新值）
- **负数轴**：数据**流入**（读取源槽，可类比 "减法方向"，消费已有值）
- **零点**：执行中心（opcode 本身不消费也不产出，只定义操作语义）

同一条指令的二维坐标在整个 vthread 生命周期内唯一不变，既是地址，又是数据流图的节点描述。

---

## 3. 函数无返回值：只有读参与写参

### 3.1 核心原则

kvlang 函数**没有返回值**。这是和绝大多数语言的根本区别。

传统语言：
```python
result = add(a, b)        # 函数"返回"一个值，赋给 result
```

kvlang：
```kv
add(./a, ./b) -> ./result   # 错误写法：->result 暗示函数有返回值
add(./a, ./b)               # 函数只负责把结果写到自己的写参槽
```

kvlang 函数只有两种参数：

| 参数类型 | KV 槽位 | 方向 | 说明 |
|---------|---------|------|------|
| **读参**（Read Params） | `[s0, -1], [s0, -2], ...` | 调用方 → 被调方 | 函数的输入，调用时绑定值 |
| **写参**（Write Params） | `[s0, +1], [s0, +2], ...` | 被调方 → 调用方 | 函数的输出，return 时写回父帧 |

---

### 3.2 正确写法与错误写法

```kv
def add(A: int, B: int) -> (C: int) {
    A + B -> ./C          # 写参 C 在被调方帧中写入
}
```

函数签名中的 `-> (C: int)` 是**写参声明**，不是"返回值类型"。

调用时：
```kv
# ✅ 正确：写参目标是 KV 路径
add(./a, ./b)              # 写参 C 写到子帧，调用方不接收（纯副作用）

# ❌ 错误：-> s 用裸标识符接收，暗示"函数有返回值"
add(./a, ./b) -> s

# ❌ 错误：-> result 同上
add(./a, ./b) -> result
```

**`-> s`（裸标识符）在写槽位置永远是错误的。**  
写槽只接受 KV 路径：`./s`（相对）或 `/abs/path`（绝对）。

---

### 3.3 为什么没有返回值

传统语言的"返回值"本质是**调用栈上的一块内存**——函数执行完毕，这块内存的值被拷贝给调用者。

kvlang 没有线性调用栈，每个函数调用创建一个 KV 子树：

```
/vthread/run/               ← 调用方帧根
/vthread/run/._fn/          ← 调用方代码区
/vthread/run/[3,0]/_fn/     ← 被调方帧根（由调用PC决定）
/vthread/run/[3,0]/_fn/C    ← 被调方的写参槽 C
```

"返回值"在 kvlang 里的正确表述是：  
**HandleReturn 把被调方帧的写参槽值，写回到调用方帧的指定路径**。

这个"指定路径"通过调用时的写槽声明传递：

```
# 指令级别（lower 后的 KV 表示）：
[3,0] = "add"              ← opcode（函数名）
[3,-1] = "./a"             ← 读参 A
[3,-2] = "./b"             ← 读参 B
[3,1] = "./sum"            ← 写参 C 的目标路径（HandleReturn 时写入）
```

HandleCall 把 `[3,1]` 存为 `.ret0 = "./sum"`，HandleReturn 时：

```
kv.Get(childFrame + "/C")  →  value
kv.Set(parentFrame + "/sum", value)
```

这不是"函数返回值"，是**写参的跨帧路径映射**。

---

### 3.4 `->` 在指令中 vs 在函数调用后

同一个 `->` 符号在两个位置有不同含义：

| 位置 | 含义 | 合法写槽 |
|------|------|---------|
| 普通指令 `A + B -> ./C` | 表达式结果写入 KV 路径 | `./x`、`/abs` |
| 函数调用写参映射（含隐式 return 映射） | 被调方写参目标路径 | `./x`、`/abs` |
| ~~`funca() -> s`~~（错误） | 暗示函数有返回值 | — 不合法 — |

**铁律**：`->` 右侧必须是 KV 路径（`./` 开头或 `/` 开头）。  
裸标识符（`s`、`result`）在写槽位置是语法错误，Parser 应拒绝并报错。

---

### 3.5 Parser 当前的局限与待修复

当前 Parser 的 `collectWriteList` 接受任意 token 作为写槽，不区分是否为合法 KV 路径：

```go
// 现状（不完善）：
writes = append(writes, p.advance().Value)  // 裸标识符也被接受

// 应加校验：
if !isKVPath(token.Value) {
    p.errors = append(p.errors, Diagnostic{...})
}
```

这是已知待修复项（见 parser todo）：
- 写槽必须是 `./x` 或 `/abs/x` 形式
- 裸标识符写槽应在 Parser 层报错，不能等到运行时静默出错

---

## 4. 控制流的 KV 寻址优势

### 4.1 label 即路径

```
def 分支示例(flag, X) -> (R) {
    entry: { X + 1 -> './a'; br('./flag', then, else) }
    then:  { './a' * 2 -> './b'; goto(merge) }
    else:  { './a' * 3 -> './b'; goto(merge) }
    merge: { './b' + 10 -> './R'; return }
}
```

label `then` 不是符号表条目，是 KV 路径段：

```
/vthread/tid/[0,0]/entry/[0,0]  = "+"
/vthread/tid/[0,0]/entry/[0,-1] = "X"
/vthread/tid/[0,0]/entry/[0,1]  = "./a"

/vthread/tid/[0,0]/then/[0,0]   = "*"
/vthread/tid/[0,0]/merge/[0,0]  = "+"
```

`goto(merge)` → `PC = funcRoot + "/merge/[0,0]"` → **零查表，零计算，纯字符串拼接**。

### 4.2 label = 无参 call

```
goto(merge)  ≡  call(父函数/merge)   ← 相同语义，不同语法
```

block 就是无参函数。控制流统一为 `call` + `return`，无需 `jmp`/`br`/`goto` 等额外原语。

### 4.3 与传统对比

| 操作 | x86 | Python | kvlang |
|------|-----|--------|--------|
| 条件跳转 | `cmp; je label` | `POP_JUMP_IF_FALSE` + offset | `br(cond, t, f)` → `call(t)` or `call(f)` |
| 无条件跳转 | `jmp label` | `JUMP_ABSOLUTE` + offset | `call(then)` |
| 函数调用 | `call addr` | `CALL_FUNCTION` | `call(funcName)` |
| 返回 | `ret` | `RETURN_VALUE` | `return` (PC 回父路径) |

kvlang 不区分"跳转"和"调用"——label block 就是无参函数，控制流就是 `call`/`return`。

## 5. 编译器/解释器架构对比

### Python

```
源代码 → tokenizer → parser → AST
  → symtable (符号表分析, 作用域)
  → compile (AST → 基本块 → 字节码)
  → marshal (字节码 → .pyc)
  → ceval (解释器主循环: 取字节码 → 分发 → 执行)
```

关键特征：
- 基本块由编译器构建（`flowgraph.c`），包含跳转偏移
- 字节码操作数携带 PC 偏移量（整数）
- 解释器在连续字节码数组上递增 PC

### Lua

```
源代码 → lexer → parser → AST
  → codegen (AST → 寄存器指令)
  → luaV_execute (寄存器 VM: 取指令 → 分发 → 执行)
```

关键特征：
- 寄存器式 VM（非栈式），指令携带寄存器索引
- 控制流通过 `JMP`/`TEST`/`FORLOOP` 等指令 + 偏移量
- 无独立的基本块构建阶段

### kvlang

```
源代码 → lexer → parser → AST (if/while/for → IfStmt/WhileStmt/ForStmt)
  → lower  (结构化控制流 → BlockStmt + br/goto)
         (br/goto 又简化 → call(block_label))
  → layoutcode (AST → KV 结构化 key-value)
         (Stmt.SetKV: 递归写入 /src/func/<name>/<label>/[i,0] 格式)
  → kvcpu (执行循环: Decode → 分发 → 执行)
         (call = HandleCall: 复制指令到 /vthread/tid/<pc>/ 子树)
         (return = HandleReturn: 回传值, 清理子栈, 恢复父 PC)
```

关键特征：
- **PC 是 KV 路径字符串**，不是整数
- **指令在 KV 树中**，通过 `kv.Get` 获取，不是内存数组
- **调用 = 子树创建**（HandleCall 复制函数体到 vthread 子栈）
- **返回 = 子树删除**（HandleReturn 清理子栈, 回传值）
- **label block = 无参函数**，控制流统一为 call/return

## 6. layoutcode 的设计原理

传统编译器/VM：
```
编译器: AST → 线性字节码 [0x01, 0x02, 0x03, ...] → .pyc 文件
VM:     PC=0 → 读字节码 → PC++ → 读下一字节码
```

kvlang layoutcode：
```
layoutcode: AST → KV 结构化 key-value:
  /src/func/add/[0,0] = "+"
  /src/func/add/[0,-1] = "A"
  /src/func/add/[0,1] = "./C"

  /src/func/branch/entry/[0,0] = "+"
  /src/func/branch/entry/[0,-1] = "X"
  /src/func/branch/then/[0,0] = "*"

VM:
  PC="[0,0]" → kv.Get("/vthread/tid/[0,0]") → "+"
  PC="[0,0]/entry/[0,0]" → kv.Get("/vthread/tid/[0,0]/entry/[0,0]") → "+"
```

KV 树的每个节点天然支持层级命名，无需构建跳转表或符号表。

## 7. 设计决策总结

| 决策 | 理由 |
|------|------|
| PC = KV path string | KV 树寻址天然支持层级，无需整数映射 |
| `[s0,0]` = opcode，`s1<0` = 读参，`s1>0` = 写参 | 符号编码数据流方向；每个槽独立可寻址、可观察 |
| 参数数量隐式（扫描到空 key 停止） | 无需在 opcode 中存 arity，指令布局自描述 |
| label block = 无参 call | 消除 jmp/br/goto 原语，控制流统一 |
| WriteFunc 先 DelTree 再写 | KV 不是内存，覆写不清零旧槽；必须显式删除旧函数树 |
| WriteBody 写结构化 KV | 避免文本往返，直接映射 AST→KV |
| lower 在 write 前执行 | 结构化 → 基本块的转换在 AST 层完成 |
| kvspace 抽象存储 | Redis 可替换，接口仅 Get/Set/Del/Watch/Notify |
| 函数无返回值，只有写参 | `funca()->s` 暗示返回值语义，是错误写法；写参目标必须是 KV 路径 `./x` |
| `->` 右侧必须是 KV 路径 | 裸标识符作为写槽在 Parser 层应报错，不能静默进入运行时 |
