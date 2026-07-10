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

## 2. 控制流的 KV 寻址优势

### 2.1 label 即路径

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

### 2.2 label = 无参 call

```
goto(merge)  ≡  call(父函数/merge)   ← 相同语义，不同语法
```

block 就是无参函数。控制流统一为 `call` + `return`，无需 `jmp`/`br`/`goto` 等额外原语。

### 2.3 与传统对比

| 操作 | x86 | Python | kvlang |
|------|-----|--------|--------|
| 条件跳转 | `cmp; je label` | `POP_JUMP_IF_FALSE` + offset | `br(cond, t, f)` → `call(t)` or `call(f)` |
| 无条件跳转 | `jmp label` | `JUMP_ABSOLUTE` + offset | `call(then)` |
| 函数调用 | `call addr` | `CALL_FUNCTION` | `call(funcName)` |
| 返回 | `ret` | `RETURN_VALUE` | `return` (PC 回父路径) |

kvlang 不区分"跳转"和"调用"——label block 就是无参函数，控制流就是 `call`/`return`。

## 3. 编译器/解释器架构对比

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

## 4. layoutcode 的设计原理

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

## 5. 设计决策总结

| 决策 | 理由 |
|------|------|
| PC = KV path string | KV 树寻址天然支持层级，无需整数映射 |
| label block = 无参 call | 消除 jmp/br/goto 原语，控制流统一 |
| WriteBody 写结构化 KV | 避免文本往返，直接映射 AST→KV |
| copyFunc 递归复制子树 | 函数调用 = 子树深拷贝（带参数替换） |
| lower 在 write 前执行 | 结构化 → 基本块的转换在 AST 层完成 |
| kvspace 抽象存储 | Redis 可替换，接口仅 Get/Set/Del/Watch/Notify |
