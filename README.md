# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![Go Version](https://img.shields.io/badge/Go-1.24+-00ADD8?logo=go)](https://go.dev/)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-93%20examples-4c1)](tutorial/)

**The VM of deepx (formerly dxlang) — an agent-native, train-inference-unified, self-iterating AI compute architecture.** kvspace tree paths form a single unified address space; one syntax simultaneously serves as VM instructions, high-level language, compiler IR, and human-readable source.

> 中文文档: [README_CN.md](README_CN.md) | Design: [kvlang-deep-dive](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-deep-dive.md) — root design doc; README is the teaching derivative. All behavior norms (p0–p7), instruction model (§2), Link call mechanism (§6), type system (§9), diagnostics (§12) live there.

---

## Core Model in One Screen

**No IR layers — source IS the IR.** The program counter is a kvspace path string; call-stack depth equals path depth:

```
PC    = "/vthread/tid/[0,0]/.fn/[1,0]"    the program counter is a KV path
fetch = kv.Get(PC)                         instruction fetch is one KV read
call  = create subtree; return = clean it  crash? restart and resume from PC
```

Every instruction occupies a 2-D coordinate `[s0, s1]`: `[s0,0]` is always the opcode, `[s0,-j]` read params, `[s0,+j]` write params.

```kv
def add(A: int, B: int) -> (C: int) { A + B -> C }
```

```
/func/main/add/[0,0]  = "+"     /func/main/add/[0,-1] = "A"
/func/main/add/[0,-2] = "B"     /func/main/add/[0,1]  = "C"
```

Four address-space domains: `/src` (source) `/func` (compiled functions) `/vthread` (runtime frames) `/sys` (infrastructure).

---

## Quick Start

```bash
# Requirements: Go 1.24+, Redis
make build

./kvlang tutorial/01-basics/hello.kv         # run a file
./kvlang -c 'print("hello, world")'          # inline mode
echo '40 + 2 -> x; print(x)' | ./kvlang      # pipe mode (; separates statements on one line)
./kvlang vet my.kv                           # syntax check
./kvlang format my.kv                        # format
```

---

## Language Guide

### Program Structure (read this first)

**Top level allows only two things: single instructions (assignments / builtin calls) and function calls. `if` / `while` / `for` must live inside a `def` body.** The convention is to define `main` and call it:

```kv
def main() -> () {
    total = 0  # = is equivalent to <-
    1 -> i
    while (i <= 5) {
        total <- total + i
        i + 1 -> i
    }
    print(total)
}

main()
```

### Read-Write Code: Three Assignment Forms

```kv
x = 40 + 2            # = : write slot on the left (≡ <-); = is NOT an expression, cannot nest in conditions
y <- x                # left arrow: write slot on the left
x * y -> z            # right arrow: write slot on the right
f(a, b) -> r          # write-param mapping for calls; multiple: -> x, y; discard: -> _
```

A write slot must be a **location**: a bare name (frame-local), `/abs/path` (global key), or `base.name` (member). Literals are not locations.

### Functions: No Return Values, Only Write Params

`-> (C: int)` in a `def` signature is a **write-param declaration**. The function writes results into its write-param slots; the caller maps them with `-> r`.
**Read params are read-only**: the body may not place a read param in a write slot (e.g. `A = A + 1`). Decide the role first —
**an accumulator is an output, so declare it as a write param** (write params start at zero, are readable and writable in the body — like Go named return values): `def sum(arr) -> (acc:int) { acc + arr[i] -> acc }`.
A pure working variable is copied to a local first (`A -> a`, then use `a`):

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

### dict, Member Access, and Linked Lists

```kv
d = { name="kv"; ver=1 }    # dict literal: members are the flat key-family d.name, d.ver
print(d.name)               # member read
d.ver = 2                   # member write; dynamic key: d.*k (the value of k becomes the key)
```

Data structures shared across functions (e.g. linked lists) create nodes at **absolute paths** (frame-locals die when the frame returns):

```kv
def build() -> () {
    /n1 = { val=1; next="/n2" }  # = is equivalent to <-
    /n2 <- { val=2; next="/n3" }
    { val=3; next="" } -> /n3
}

def main() -> () {
    build()
    "/n1" -> p                   # p holds a path string (a pointer)
    while (p != "") {
        p.val -> v               # pointer deref: reads /n1.val
        print(v)
        p.next -> p
    }
}

main()
```

### Numeric Types (optional precision declaration)

```kv
f = float32(3)        # ten operators int8/16/32/64 uint8/16/32/64 float32/64 — they construct AND convert
w = int8(300)         # 44: narrowing wraps (two's complement); float→int truncates toward zero; arithmetic domain is int64/float64
```

### Control Flow (inside def bodies only)

```kv
if (cond) { ... } else { ... }
while (cond) { ... }
for (x in arr) { ... }        # iterate a key-family array
```

Conditions may be compound expressions: `if (7 % 2 != 0)` and `while (i < strlen(s))` both work (auto-flattened to temp slots at compile time).

### Operators

| Category | Symbols |
|------|------|
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logic | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `<<` `>>` |

> `/`: both ints → integer division (C-style, `7/2`=3, `-9/2`=-4); either side float → float division (`7.0/2`=3.5).

### Builtins

`abs` `neg` `sign` `pow` `sqrt` `exp` `log` `min` `max` (variadic, e.g. `max(a,b,c)`) `print` `cerr` `input`\
`int` `float` `bool` plus the ten precision operators · `char` `ord` `strlen` `strcmp` `strstr` `slice` `concat` · `array` `len` `at` `set` `has` `sort` `dict` `kvat` `kvhas`

Strings support indexing and concatenation: `s[i]` reads the i-th char (a single-char string, directly comparable to `"a"`; out of range → `""`), `s[i] = "X"` replaces one char (writes back a new string), `a + b` concatenates.
C-style API: `strlen`; `strcmp` returns -1/0/1; `strstr(hay, needle)` returns the first index (-1 if absent); `ord(c)` returns the byte code (for arithmetic, e.g. `ord(s[i])`).

---

## Tutorial

94 self-contained examples (93 with expected output, fully CI-verified), organized by topic:

```
01-basics/        hello, arith, precision, numtypes, strings  (6 files)
02-func/          def, call, accumulator                      (2 files)
03-control/       if, while, for, guess game                  (5 files)
04-algo/          fibonacci, gcd, collatz, ...                (13 files)
05-leetcode/      LeetCode solutions                          (69 files)
```

```bash
./kvlang tutorial/01-basics/hello.kv         # hello kvlang
./kvlang tutorial/04-algo/fibonacci.kv       # fib = 55
./kvlang tutorial/05-leetcode/001_two_sum.kv # LeetCode

python3 tutorial/test.py                     # all 93 examples — CI verification
```

---

## Error Cases

Negative tests verifying diagnostic accuracy: each `.kv` file is annotated with expected error/warning messages,
and `error_test.py` checks that the compiler and runtime produce those diagnostics exactly.

```
error_cases/
  type_error/       e.g. char(1) → TypeError
  index_error/      e.g. at([1,2], 5) → IndexError
  zero_division/    e.g. 1/0 → ZeroDivisionError
  key_error/        e.g. kvat("/x","y") → KeyError
  value_error/      e.g. log(-5) → ValueError
  name_error/       e.g. nosuch() → NameError
  read_only/        e.g. reading a read param → compiler rejection
  recursion_error/  e.g. unbounded recurse → RecursionError
  runtime_error/    e.g. Bootstrap failure → RuntimeError
```

```bash
python3 tutorial/error_test.py              # all 10 negative tests
python3 tutorial/test.py                    # all 94 positive tests — CI verification
```

Diagnostic categories follow the [diagnostic-style specification](https://github.com/array2d/deepx-design/blob/master/doc/reference/diagnostic-style-five-languages.md)
aligned to Python naming conventions (fix-028).

## License

MIT — see [LICENSE](LICENSE)
