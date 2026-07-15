# kvlang Language Specification v0.1

## 1. Overview

kvlang is a declarative, KV-path-addressed language. Programs are trees of key-value pairs where **instructions are paths** and **function calls are subtree copies**.

### 1.1 Design Principles

- **Unified store**: Code and data share one KV tree. No separate stack, heap, or register file.
- **Path addressing**: Every variable, instruction, and return value lives at a path.
- **Transparent state**: You can `GET /vthread/1/[5,-1]` to inspect any variable mid-execution.
- **Lean core**: The VM has 2 runtime dependencies (Go + Redis).

### 1.2 Hello World

```kvlang
str.set("kvlangrun") -> ./term

def hello() -> () {
    print("hello kvlang")
}
hello() -> ()
```

---

## 2. Lexical Structure

### 2.1 Comments

```kvlang
# single-line comment only
```

### 2.2 Identifiers

```
ident ::= [a-zA-Z_][a-zA-Z0-9_]*
```

### 2.3 Literals

| Type | Example | Notes |
|------|---------|-------|
| Integer | `42`, `-5`, `0` | 64-bit signed |
| Float | `3.14`, `-0.5`, `1.0` | 64-bit IEEE 754 |
| Boolean | `true`, `false` | |
| String | `"hello world"` | double-quoted only |
| Path | `./x`, `/func/main` | bare, no quotes |

### 2.4 Keywords

```
def  if  else  for  while  break  continue  return  br  goto
```

### 2.5 Operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `||` `!` |
| Bitwise | `&` `|` `^` `<<` `>>` |

---

## 3. Type System

### 3.1 Scalar Types

| Type | Syntax | Range |
|------|--------|-------|
| `int` | `42` | 64-bit signed |
| `float` | `3.14` | 64-bit IEEE 754 |
| `bool` | `true`, `false` | — |
| `str` | `"hello"` | UTF-8 |

### 3.2 Type Annotations

Parameters may be annotated. Annotations are optional but recommended:

```kvlang
def add(A: int, B: int) -> (C: int) { ... }
def greet(name: str) -> () { ... }
```

### 3.3 Type Coercion

Implicit coercion between compatible types:

| From → To | Behavior |
|-----------|----------|
| `int` → `float` | Widening (lossless) |
| `float` → `int` | Truncation |
| `int`/`float` → `bool` | `0` → `false`, non-zero → `true` |
| `bool` → `int` | `true` → `1`, `false` → `0` |

Explicit casts via builtins: `int(x)`, `float(x)`, `bool(x)`.

---

## 4. Path Addressing

### 4.1 Path Syntax

```
path ::= "./" ident   # relative to current frame
       | "/"  ident   # absolute (global)
```

Paths are **not strings**. They are first-class address tokens:

```kvlang
42 -> ./x            # write 42 to ./x relative to current frame
./x + 8 -> ./y       # read ./x, add 8, write to ./y
print("value:", ./x)  # read ./x, pass to print
```

### 4.2 Path Resolution

Paths resolve against the current execution frame. Inside `def add(...)`, `./C` refers to the return slot of `add`.

### 4.3 KV Path Layout

```
/vthread/<vtid>/<pc>/[i,0]      opcode
/vthread/<vtid>/<pc>/[i,-j]     read operand j
/vthread/<vtid>/<pc>/[i,+j]     write operand j
/vthread/<vtid>/<pc>/label/     control flow block
/src/<pkg>/<func>/              function body
/src/<pkg>/<func>/label/        block label sub-function
/func/main                      program entry signature
```

---

## 5. Functions

### 5.1 Definition

```kvlang
def name(p1: T1, p2: T2) -> (r1: T1, r2: T2) {
    # body
}
```

- Return types and names are specified after `->`.
- Multi-return is first-class.
- Functions with `-> ()` have no return values.

### 5.2 Calling

```kvlang
# Function syntax
add(2, 3) -> ./result

# C-style syntax (equivalent)
./result <- add(2, 3)

# Multi-return: destructure with matching targets
fib(10) -> ./a, ./b
```

Function calls copy the function body as a subtree under the caller's frame. Parameters are bound as paths in the new subtree.

### 5.3 Entry Point

A `.kv` file must contain at least one top-level function call. The first call is the entry point:

```kvlang
main() -> ()    # calls main, discards return
```

---

## 6. Control Flow

### 6.1 If/Else

```kvlang
if (condition) {
    # true branch
} else {
    # false branch
}
```

The condition is any expression. `0`, `0.0`, `false`, `""` are falsy.

### 6.2 While

```kvlang
while (./i < 10) {
    ./i + 1 -> ./i
}
```

### 6.3 For-in

```kvlang
for (item in ./list) {
    print(item)
}
```

### 6.4 Break/Continue

```kvlang
while (true) {
    if (./done) {
        break
    }
}
```

---

## 7. Expression Syntax

### 7.1 Prefix (Function Call)

```kvlang
print("result =", ./x)     # prefix: function call as statement
add(1, 2) -> ./r           # prefix with target
```

### 7.2 Infix (Operator)

```kvlang
./a + ./b -> ./c           # binary operator with target
-./x -> ./y                # unary negation
```

### 7.3 C-style

```kvlang
./c <- add(1, 2)           # c-style assignment
./c <- ./a + ./b           # c-style with expression
```

All three forms are equivalent. Choose based on readability.

---

## 8. Execution Model

### 8.1 Virtual Threads (vthread)

Programs execute inside virtual threads. Each vthread has:
- A unique ID (`/vthread/<id>/`)
- A program counter (`/vthread/<id>/pc`)
- A frame tree (subtree under `/vthread/<id>/<pc>/`)

### 8.2 Worker Pool

The `serve` daemon runs 128 goroutine workers. Workers pull ready vthreads and execute one instruction at a time.

### 8.3 Tail-Call Optimization (TCO)

Recursive calls in tail position reuse the current frame instead of creating a new one. This enables unbounded recursion:

```kvlang
def fact(n: int, acc: int) -> (r: int) {
    if (n <= 0) {
        acc -> ./r
    } else {
        fact(n - 1, acc * n) -> ./r    # TCO: reuses frame
    }
}
fact(10000, 1) -> ./f   # no stack overflow
```

---

## 9. Built-in Functions

### 9.1 Arithmetic

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs(x)` | `(int) -> (int)` | Absolute value |
| `pow(b, e)` | `(float, float) -> (float)` | Exponentiation |
| `sqrt(x)` | `(float) -> (float)` | Square root |
| `max(a, b, ...)` | `(float, ...) -> (float)` | Maximum |
| `min(a, b, ...)` | `(float, ...) -> (float)` | Minimum |
| `exp(x)` | `(float) -> (float)` | e^x |
| `log(x)` | `(float) -> (float)` | Natural logarithm |
| `sign(x)` | `(float) -> (int)` | -1, 0, or 1 |

### 9.2 Casting

| Function | Description |
|----------|-------------|
| `int(x)` | Convert to int |
| `float(x)` | Convert to float |
| `bool(x)` | Convert to bool (0 → false) |

### 9.3 I/O

| Function | Description |
|----------|-------------|
| `print(fmt, args...)` | Print to stdout |
| `cerr(fmt, args...)` | Print to stderr |
| `str.set("kvlangrun")` | Activate terminal output |

### 9.4 String

| Function | Description |
|----------|-------------|
| `str.set(s)` | Store string value |
| `str.concat(a, b)` | Concatenate strings |

---

## 10. KVSpace (Storage Layer)

### 10.1 Typed Values

kvlang uses a self-describing TLV encoding for all stored values:

```
[1B kind_len][N B kind_name][4B raw_len LE][M B raw_value]
```

| kind_name | raw_value |
|-----------|-----------|
| `int` | 8B int64 LE |
| `float` | 8B float64 IEEE 754 LE |
| `bool` | 1B (0x00/0x01) |
| `str` | UTF-8 bytes |
| `bytes` | raw bytes |

### 10.2 Soft Links

Paths can be redirected via soft links:

```bash
kvlang kvspace link /nodeA /nodeB   # /nodeB → /nodeA
```

Accessing `/nodeB/x` transparently resolves to `/nodeA/x`. Link resolution happens at the storage layer (Redis), not at the language level.

---

## Appendix A: Complete Grammar

```
# Lexical
comment   ::= "#" .* EOL
string    ::= '"' [^"]* '"'
path      ::= "./" ident | "/" ident
number    ::= digit+ ["." digit*]
ident     ::= [a-zA-Z_][a-zA-Z0-9_]*
op        ::= "+" | "-" | "*" | "/" | "%"
            | "==" | "!=" | "<" | ">" | "<=" | ">="
            | "&&" | "||" | "!"
            | "&" | "|" | "^" | "<<" | ">>"
keyword   ::= "def" | "if" | "else" | "for" | "while"
            | "break" | "continue" | "return" | "br" | "goto"
type      ::= "int" | "float" | "bool" | "str" | "..."

# Syntax
file      ::= (func_def | top_call)*

func_def  ::= "def" ident "(" [params] ")" "->" "(" [params] ")"
              "{" stmt* "}"

top_call  ::= call_expr "->" target

stmt      ::= prefix | infix | cstyle | control | block

prefix    ::= ident "(" [arg_list] ")" "->" target
infix     ::= (primary op primary | op primary) "->" target
cstyle    ::= target "<-" (call_expr | infix_expr)

call_expr ::= ident "(" [arg_list] ")"
target    ::= path

control   ::= if_stmt | for_stmt | while_stmt
            | break_stmt | continue_stmt

if_stmt   ::= "if" "(" expr ")" "{" stmt* "}"
              ["else" "{" stmt* "}"]

for_stmt  ::= "for" "(" ident "in" path ")" "{" stmt* "}"
while_stmt ::= "while" "(" expr ")" "{" stmt* "}"

break_stmt    ::= "break"
continue_stmt ::= "continue"

block     ::= ident ":" "{" stmt* terminator "}"
terminator ::= "br" "(" arg ("," arg)* ")"
             | "goto" "(" arg ")"
             | "return" ["(" [arg] ")"]

params    ::= ident [":" type] ("," ident [":" type])*
arg_list  ::= arg ("," arg)*
arg       ::= primary
primary   ::= ident | number | string | path | "true" | "false"
expr      ::= call_expr | primary (op primary)*
```
