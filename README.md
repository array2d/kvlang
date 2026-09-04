# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-140%20examples-4c1)](tutorial/)
[![Docs site](https://img.shields.io/badge/docs-stdlib-blueviolet)](https://array2d.github.io/kvlang/#/stdlib/kvlang/kvlangbrief.kv)

**A plaintext, interpreted language whose addressing space and memory space are both kvspace — a small core with extensions on top (the front-end language of deepx, formerly dxlang).** Code and data live in one KV tree; the PC is a KV path (crash-resumable), the source is the IR, and every KV value is plaintext. The core runtime does only the execute loop and control flow; all other capabilities are carried by rwirext extensions (`term` / `json` are example base extensions).

> 中文文档: [README_CN.md](README_CN.md) | Design: [deep-dive](https://github.com/array2d/deepx-design/blob/master/doc/kvlang/kvlang-design-and-implementation) — root design doc; README is the teaching derivative. All behavior norms (p0–p7), instruction model (§2), Link call mechanism (§6), type system (§9), diagnostics (§12) live there.
>
> Design docs (CN): [deepx-design/doc/kvlang-design-and-implementation](https://github.com/array2d/deepx-design/tree/master/doc/kvlang-design-and-implementation) · (EN): [deepx-design/doc-en/kvlang-design-and-implementation](https://github.com/array2d/deepx-design/tree/master/doc-en/kvlang-design-and-implementation) — 19 chapters covering architecture, parser, runtime, kvspace, and language design reference.

---

## Use Cases

- **Model front-end for deepx**: the front-end language of the train-and-infer framework deepx (formerly dxlang) — model structure and operator graphs are expressed as a KV tree, with code, parameters, and intermediate results living in one tree addressed by path.
- **Self-iterating agents (e.g. byteseek)**: the agent harness is written in kvlang; because code is data, the PC is a path, and execution resumes from the PC after a crash, the agent reads and rewrites its own code and state through the same KV reads/writes.

---

## Core Model in One Screen

**No IR layers — source IS the IR.** The program counter is a kvspace path string; call-stack depth equals path depth:

```
PC    = "/vthread/tid/[0,0]/[0,0]/[1,0]"   the program counter is a KV path
fetch = kv.Get(PC)                           instruction fetch is one KV read
call  = create subtree; return = clean it    crash? restart and resume from PC
goto/br = rewrite irseq in the same frame    if/while do not create frames
```

Every instruction occupies a 2-D coordinate `[s0, s1]`: `[s0,0]` is always the opcode, `[s0,-j]` read params, `[s0,+j]` write params.

```kv
lib main {
    rwfunc add(A:int64, B:int64) -> (C:int64) { A + B -> C }
}
```

```
/lib/main.add/[0,0]  = "+"     /lib/main.add/[0,-1] = "A"
/lib/main.add/[0,-2] = "B"     /lib/main.add/[0,1]  = "C"
```

Two address-space domains exist: `/lib` (function library — signatures, instruction trees, `.src`) and `/vthread` (runtime frames). Everything else under `/` is user-defined. There is **no `/dev` device domain and no terminal** — the KV world holds only keys and values; I/O such as `print` is an [extension rwir](#builtins-and-extension-rwir), not an address-space domain.

---

## Ecosystem Architecture

kvspace is the addressing and memory space at the core; the language is a small runtime with extensions on top.

![kvlang ecosystem architecture](docs/kvlang-ecosystem-architecture.png)

- **kvspace** — one C ABI (`kvspace_*`, 24 symbols), two implementations selected by DSN: `kvspace-c` (C, `shm://`, links `blockmalloc` + `slotsboxmalloc`) and `kvspace-durable` (Rust, `redis://` / `fs://`, s3/tikv planned).
- **kvlang** — `layout` (Rust, compile) and `runtime` (C, execute), both depending only on the `kvspace_*` C ABI.
- **rwirext** — extensions on top of the runtime. Embedded (Rust `term`, links `libkvlang_runtime` via `kvlang_rwirext.h`) or process-separated by handoff (Go `json`, Python `numpy`). `term` / `json` are example base extensions, not headline features.

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

**Top level: `lib name { }`, `rwfunc`, and single instructions.** Always wrap code in `rwfunc main() -> () { … }; main()`. Bare `if` / `while` / `for` at top level may auto-wrap into implicit `init()` but this is unreliable — explicitly wrapping in `main()` is the only guaranteed pattern. Never name your function `init` to avoid conflicts with implicit wrapping.

```kv
rwfunc main() -> () {
    total = 0  # = is equivalent to <-
    1 -> i
    while (i <= 5) {
        total <- total + i
        i + 1 -> i
    }
    println(total)
}

main()
```

### rwir（Read-Write IR）：Three Assignment Forms

```kv
x = 40 + 2            # = : write slot on the left (≡ <-); = is NOT an expression, cannot nest in conditions
y <- x                # left arrow: write slot on the left
x × y -> z            # right arrow: write slot on the right
f(a, b) -> r          # write-param mapping for calls; multiple: -> x, y; discard: -> _
```

A write slot must be a **location**: a bare name (frame-local), `/abs/path` (global key), or `base.name` (member). Literals are not locations.

**`rwfunc func(ra,rb) -> (wa,wb) { … }` = composite rwir**, the named form. Single-line rwir like `A + B -> C` is atomic (one opcode + reads + writes); `rwfunc` packs multiple rwir into a named unit with the same arrow interface — `(ra,rb)` declare read params, `-> (wa,wb)` declare write params. Calling `add(3,4) -> s` binds arguments to read slots, maps write slots back to the caller frame. No return values, only write-param mapping.

`-> (C:int64)` in a `rwfunc` signature is a **write-param declaration**. The function writes results into its write-param slots; the caller maps them with `-> r`.
**Read params are read-only**: the body may not place a read param in a write slot (e.g. `A = A + 1`). This includes array element writes — `a[i] <- v` writes through `a`, so `a` must be a write param if you need to modify it. **Array/dict to mutate → write param; array/dict to read only → read param.**
```kv
# ❌ wrong: array as read param, a[i] <- v writes through read-param slot → parser rejects
rwfunc bad(a:int64) -> () { 99 -> a[0] }

# ✅ correct: array as write param, readable and writable inside the body
rwfunc good() -> (a:int64) { a:int64 = [10, 20]; 99 -> a[0]; a }
```

Decide the role first —
**an accumulator is an output, so declare it as a write param** (write params start at zero, are readable and writable in the body — like Go named return values): `rwfunc sum(arr:int64) -> (acc:int64) { acc + arr[i] -> acc }`.
A pure working variable is copied to a local first (`A -> a`, then use `a`):

```kv
lib mylib {
    rwfunc add(A:int64, B:int64) -> (C:int64) {
        A + B -> C
    }
}

rwfunc main() -> () {
    mylib.add(3, 4) -> s
    println(s)          # 7
}

main()
```

### dict, Member Access, and Linked Lists

```kv
d = { name="kv"; ver=1 }    # dict literal: members are the flat key-family d.name, d.ver
println(d.name)               # member read
d.ver = 2                   # member write
k = "name"; d.*k -> v       # dynamic key: reads d.name (k's value becomes the key)
```

**Pointer via path string**: store an absolute path in a variable, then use `.member` to read/write at that path — the variable's string value becomes the path prefix.
```kv
/node = { val=42 }       # dict at absolute path
"/node" -> p             # p holds the path string
p.val -> v               # reads /node.val → 42
```

Data structures shared across functions (e.g. linked lists) create nodes at **absolute paths** (frame-locals die when the frame returns):

```kv
rwfunc build() -> () {
    /n1 = { val=1; next="/n2" }  # = is equivalent to <-
    /n2 <- { val=2; next="/n3" }
    { val=3; next="" } -> /n3
}

rwfunc main() -> () {
    build()
    "/n1" -> p                   # p holds a path string (a pointer)
    while (p != "") {
        p.val -> v               # pointer deref: reads /n1.val
        println(v)
        p.next -> p
    }
}

main()
```

### Numeric Types (exact-width only — no `int` or `float`)

```kv
f = float32(3)        # ten constructors: int8/16/32/64 uint8/16/32/64 float32/64 — they construct AND convert
w = int8(300)         # 44: narrowing wraps (two's complement); float→int truncates toward zero; arithmetic domain is int64/float64
x:int64 = 42          # type-annotated variable declaration
```

`int` and `float` are **rejected** by the parser — use exact-width types only. The ten precision operators are both constructors and converters.

### Control Flow (inside rwfunc bodies only)

```kv
i = 1; sum = 0
while (i <= 10) { sum + i -> sum; i + 1 -> i }
if (sum > 50) { println("big") } else { println("small") }   # sum=55 → big
for (x in [7, 2, 9, 4]) { println(x) }
```

Conditions may be compound expressions: `if (7 % 2 != 0)` and `while (i < string.len(s))` both work (auto-flattened to temp slots at compile time).

### Operators

| Category | Symbols |
|------|------|
| Arithmetic | `+` `-` `×` `÷` `%` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logic | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `<<` `>>` |

> `÷`: both ints → integer division (C-style, `7÷2`=3, `-9÷2`=-4); either side float → float division (`7.0÷2`=3.5).
> `/` is reserved for paths and path separators. `*` is reserved for future pointer dereference.

### Builtins and Extension rwir

**Builtins** are the rwir the runtime evaluates in-process (the `bi_is_native` set). They are pure KV→KV computations — no I/O:

**Scalar:** `abs` `neg` `sign` `pow` `sqrt` `exp` `log` `min` `max` (variadic, e.g. `max(a,b,c)`) `debugger`\
**Types:** `bool` `int8` `int16` `int32` `int64` `uint8` `uint16` `uint32` `uint64` `float32` `float64` `char/utf8` `char/utf32` `char/ascii`\
**Collections:** `array` `at` `set` `has` `array.sort` `array.slice` `array.append` `dict`\
**Shape:** `ndarray.numel` `ndarray.dim` `ndarray.shape` `xv.at` `xv.set`\
**KV tree:** `kv.get` `kv.set` `kv.del` `kv.deltree` `kv.list` `kv.mkindex` `kv.extindex` `kv.rmindexext` `kv.watch` `kv.has` `kv.at`\
**Strings:** `string.char` `string.ord` `string.len` `string.cmp` `string.find` `string.slice` `string.concat` `string.set`\
**Time:** `time.now` `time.sub` `time.add` `time.before` `time.after` `time/duration.nanos` `time/duration.as_nanos` (and `millis`/`seconds`/`minutes`/`hours` variants)\
**Random:** `random.uint64` `random.int63` `random.intn`

**`print` / `println` / `cerr` are NOT builtins.** In the KV world there is no terminal — only keys and values — so I/O is not a core-language primitive. They are **extension rwir**: the `term` extension runtime registers them at `/lib/<opcode>` (kind `rwir`) and writes to the host process's `stdout`/`stderr`. The core runtime recognizes any `/lib/<opcode>` that carries an `rwir` signature and is not a builtin as an extension rwir, and hands it off to its extension runtime. Same mechanism as `json.to` / `json.from` (the `json` extension) and tensor ops (the numpy / GPU extensions).

```kv
a:int64 = [7, 2, 9, 4]     # typed 1D array, = ≡ <-
ndarray.numel(a) -> n         # 4
at(a, 2) -> e            # 9 (0-indexed)
set(a, 1, 99) -> a       # modify element: a becomes [7, 99, 9, 4]
sort(a) -> sorted         # sorted copy: [2, 4, 7, 9]
```

```kv
s = "hello"
string.char(s, 1) = "a"     # replace char at index 1 → "hallo"
s + " world" -> t           # concatenation → "hallo world"
string.len(s) -> n          # 5
string.find(s, "ll") -> i   # 2 (first index of substring, -1 if absent)
string.slice(s, 0, 2) -> p  # "he"
```

Strings support indexing and concatenation with `+`; `at(s, i)` reads the i-th char, `string.char(s, i)` reads it, `string.char(s, i) = "X"` replaces one char.

---

## Tutorial

140 self-contained examples (129 with expected output, fully CI-verified), organized by topic:

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

python3 tutorial/test.py                     # all positive examples — CI verification
python3 tutorial/error_test.py               # all negative tests
```

---

## Design Documentation

In-depth design and implementation docs covering the full architecture:

| Chapter | EN | CN |
|---------|-----|-----|
| Architecture — storage/compute/control separation | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-01-storage-compute-control-flow-separation.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-01-存算控制流严格分离的kv树计算架构.md) |
| Architecture — everything is plaintext | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-02-everything-is-plaintext.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-02-一切皆明文.md) |
| Architecture — program as data + functions | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-03-program-is-data-plus-functions.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-03-程序即数据结构加函数加数据.md) |
| Architecture — four-level code hierarchy | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/architecture-04-four-level-code-hierarchy.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/total篇-04-代码四级层次.md) |
| Parser — instruction architecture | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-01-instruction-architecture.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-01-指令架构.md) |
| Parser — functions | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-02-functions.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-02-函数.md) |
| Parser — compiler pipeline | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-04-compiler-pipeline.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-04-编译器流水线.md) |
| Parser — diagnostics | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-05-diagnostics.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-05-诊断输出.md) |
| Parser — layoutrwir | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-06-layoutrwir.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser篇-06-layoutrwir.md) |
| Parser & Runtime — control flow | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/parser-runtime-01-control-flow.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/parser&runtime-01控制流篇.md) |
| Runtime — type system | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-01-type-system.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-01-类型系统.md) |
| Runtime — member access & data structures | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-02-member-access-and-data-structures.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-02-成员访问与数据结构.md) |
| Runtime — debugging & observability | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-03-debugging-and-observability.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-03-调试与可观测性.md) |
| Runtime — function calls & builtins | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/runtime-04-function-calls-and-builtins.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/runtime篇-04-函数调用builtin.md) |
| KVSpace — address space | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-01-address-space.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-01-地址空间.md) |
| KVSpace — addressing & naming | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-02-addressing-model-and-naming.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-02-寻址模型与命名.md) |
| KVSpace — code instruction layout | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-03-code-instruction-layout.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-03-代码指令的布局格式.md) |
| KVSpace — system variables | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/kvspace-04-system-variables.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/kvspace篇-04-系统变量.md) |
| Reference — how to design a programming language | [en](https://github.com/array2d/deepx-design/blob/master/doc-en/kvlang-design-and-implementation/reference-01-how-to-design-a-programming-language.md) | [cn](https://github.com/array2d/deepx-design/blob/master/doc/kvlang-design-and-implementation/reference篇-01-如何设计编程语言.md) |

Each English translation includes **Implementation Consistency Notes** cross-checked against the Go source.

## License

MIT — see [LICENSE](LICENSE)
