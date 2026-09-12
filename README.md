# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Tutorial Examples](https://img.shields.io/badge/tutorials-210%20examples-4c1)](tutorial/)
[![Spec](https://img.shields.io/badge/spec-84%20chapters-blueviolet)](stdlib/kvlang/spec/)

**A plaintext, interpreted language whose addressing space and memory space are both kvspace — a small core with extensions on top (the front-end language of deepx, formerly dxlang).** Code and data live in one KV tree; the PC is a KV path (crash-resumable), the source is the IR, and every KV value is plaintext. The core runtime does only the execute loop and control flow; every other capability is supplied by other runtimes and registered at `/lib/<opcode>`.

> 中文文档: [README_CN.md](README_CN.md)
>
> **The specification is the single source of truth.** Language facts live in [`stdlib/kvlang/spec/`](stdlib/kvlang/spec/) — 84 chapters of normative text plus the authoritative grammar, written in kvlang itself and laid out into `/lib/kvlang/spec/…`. Any change to language behavior is spec-driven: edit the spec clause and its anchored example first, then the implementation, until the anchored tutorials pass on every backend. When spec and implementation disagree, the spec wins and the implementation is the defect. This README is a teaching derivative.

---

## Use Cases

- **Model front-end for deepx**: the front-end language of the train-and-infer framework deepx (formerly dxlang) — model structure and operator graphs are expressed as a KV tree, with code, parameters, and intermediate results living in one tree addressed by path.
- **Self-iterating agents (e.g. byteseek)**: the agent harness is written in kvlang; because code is data, the PC is a path, and execution resumes from the PC after a crash, the agent reads and rewrites its own code and state through the same KV reads/writes.

---

## Core Model in One Screen

**No IR layers — source IS the IR.** The program counter is a kvspace path string; call-stack depth is the frame number in that path:

```
PC    = "/vthread/<vid>/[1]/[3,0]"            vthread vid, frame 1, instruction 3
fetch = GetBatch(frame/, ["[3,0]"])           take opcode from the frame dir (extindex → /lib)
call  = create frame [d+1]; return = DelTree  crash? restart and resume from PC
goto/br = rewrite the irseq in the same frame  if/while do not create frames
```

Every instruction occupies a 2-D coordinate `[s0, s1]`: `s0=0` is the signature row, `s0≥1` the instructions (the **irseq**); along `s1`, `[s0,0]` is always the opcode, `[s0,-j]` read params, `[s0,+j]` write params.

Code has three levels — `lib` (package) / `rwfunc` (function) / `rwir` (atomic instruction) — each aligned with a level of the KV tree. `if` / `while` / `for` are not a fourth level: layout lowers them to `goto`/`br`.

```kv
lib main {
    rwfunc add(a:int64, b:int64) -> (c:int64) { a + b -> c }
}
```

```
/lib/main·add/[0,0]  = "+"     /lib/main·add/[0,-1] = "a"
/lib/main·add/[0,-2] = "b"     /lib/main·add/[0,1]  = "c"
```

Three address-space domains have fixed structural semantics: `/lib` (function library — signatures, instruction trees, and the `def rwir` route headers), `/vthread` (runtime frames) and `/networld` (external-world registry). Everything else under `/` is user-defined, with no core schema. There is **no `/dev` device domain and no terminal** — the KV world holds only keys and values; I/O such as `print` is not a core-language primitive.

Key forms are three-way split, and the three classes never overlap: `X/name` (structure — frames, slots, directories; core-owned), `X·name` (user data member), `X/‥name` (system variable — shadow metadata; core-owned).

---

## Ecosystem Architecture

kvspace is the addressing and memory space at the core; the language is a small runtime with extensions on top.

![kvlang ecosystem architecture](docs/kvlang-ecosystem-architecture.png)

- **kvspace** — one C ABI in PascalCase with no underscores (`kvspaceGet`, `kvspaceWriteInPlace`, `kvlangRuntimeConnect`); two implementations selected at run time by DSN: `kvspace-c` (C, `shm://`) and `kvspace-durable` (Rust, `redis://` / `fs://`; s3 planned). Backends must be byte-identical.
- **layout** — Rust crate (`bin/libkvlanglayout.so`, CLI `bin/kvlanglayout`). The **o0 compiler**: scan → parse → lower (control-flow lowering, type inference, specialization) → write the KV instruction tree into `/lib`. It performs **no optimization** — optimizing here would erase the high-level semantics that other runtimes rely on to optimize on heterogeneous hardware. It writes only to `/lib`, and diagnoses at three levels: `error` / `warn` / `info`.
- **runtime-c** — the core interpreter (`bin/libkvlang_runtime.so`, C11). Pure interpretation: fetch-decode-execute, native rwir dispatch, the copy opcode `=`, call/return/br/goto, and the user-function call. It does not compile, optimize, or reorder. Depends only on the `kvspace*` C ABI.
- **runtime-rs** — Rust (`bin/kvlang`, the `kvlang` CLI). A reference **myrwir host**: it links layout and runtime-c, interprets its own opcodes, and routes the rest. Its in-process families: `term`, `json`, `http`, `networld`, `kvlang·*`.
- **Three stages** — layout (o0) → compiler (an extension) → runtime (pure interpretation). The compiler is not part of the language core; §06 of the spec defines only its contract with the core (front-end/back-end operators, backend binding, operator versioning). Uncompiled `/lib` runs as-is.

### myrwircaps: how an opcode is fulfilled

Each runtime declares **`myrwircaps`** — the table of opcodes it can interpret in place (`opcode → handler`). Dispatch is a fixed integer jump table: an opcode in `myrwircaps` is fulfilled locally; control ops, the copy opcode, and user-function calls are handled by the core; anything else is `notinmyrwircaps` — the runtime looks up `/lib/<opcode>`, where a **`def rwir` route header** registers the op, and hands it to a runtime whose `myrwircaps` contains it. `def rwir` exists only at `/lib/<op>`: it is never declared in kv source and never produced by layout. Because an `rwfunc` carries an interpretable body, it needs no route header — hence `def rwir` exists but there is no `def rwfunc`.

Cross-process handoff uses the queue `/lib/<opcode>/vids/<vtid> = pc`: the calling runtime `kvspace·watch`es until the item disappears; an in-process opcode dispatches in the driver loop with no handoff.

---

## Quick Start

Requirements: a C toolchain + cmake, Rust (cargo), and the kvspace ABI libraries installed to `/usr/lib/kvspace` (fetched by [`ci/deps.sh`](ci/deps.sh) from the tags in [`deps.json`](deps.json)). Go is needed only for the Go `json` example extension.

```bash
git clone git@github.com:array2d/kvlang.git
cd kvlang
make all                                     # runtime(C) + layout(Rust) + runtime-rs(Rust) + json(Go)

./bin/kvlang tutorial/01-basics/hello.kv     # run a file (entry rwfunc test)
./bin/kvlang -c 'println("hello, world")'    # inline mode (entry init)
./bin/kvlang vet my.kv                       # parse + lower only
./bin/kvlang format my.kv                    # format to stdout
./bin/kvlang layout my.kv                    # print the entry point
./bin/kvlang dump my.kv                      # reconstruct /lib as runnable kvlang
```

`make` targets: `all` · `runtime` · `layout` · `runtime-rs` · `json` · `oldhero` · `test` · `install` · `clean`. `make install` places the libraries in `/usr/lib`, the CLIs (`kvlang`, `kvlanglayout`) in `/usr/bin`, and headers in `/usr/include/kvlang`.

**Backends** are chosen by the `KVSPACE` DSN (default `redis://127.0.0.1:6379`):

```bash
KVSPACE=shm:///tmp/kvlang.shm ./bin/kvlang tutorial/01-basics/hello.kv   # kvspace-c
KVSPACE=fs:///tmp/kvlang-fs   ./bin/kvlang tutorial/01-basics/hello.kv   # kvspace-durable
```

Two more modes: `kvlang create <func>` prints a vthread `vid`, and `kvlang run <vid>` resumes from the persisted PC (crash recovery). With no arguments and `KVLANG_LIB=p1:p2:…` set, kvlang lays out every `.kv` under those paths and runs each lib's `init`.

---

## Language Guide

### Ten Rules (read this first)

1. Multiplication is `×`, division is `÷`. `*` is not multiply (it is the pointer / dereference operator). `/` is not divide (it is the path separator and comment marker).
2. There is no `int` / `float` / `char` shorthand and no `object`. Numbers are exact-width (`int64`, `float64`, …); a string is `[]char/<encoding>`; heterogeneous records are `{k=v}` literals or `struct`.
3. Assignment goes both ways: `expr -> slot` (write slot on the right) and `slot = expr` (write slot on the left). `=` is not an expression — equality is `==`. There is no `<-`.
4. One instruction per line, clearest; `;` is equivalent to a newline (`a = 1; b = 2` is legal).
5. Comments are `//` line and `/* */` block (block comments nest). There is no `#` comment.
6. Strings are only `"…"` (escaped) or `r"…"` / `r#"…"#` (raw). No `"""` triple quotes, no backticks.
7. A write param copies in the caller's current value, which is `None` if the position is unassigned — so initialize before accumulating: `0 -> acc`, or `None + x` errors.
8. Arrays carry a `[]` prefix: `a:[]int64 = [1, 2, 3]`. Empty containers must be typed: `d:[]char/utf32·int64 = {}`.
9. `+` only joins like with like: char+char concatenates, number+number adds; `"label" + n` is a TypeError. For labeled output use multiple arguments: `println("answer =", n)` (space-separated).
10. Functions have no return value; results leave through write params: `rwfunc f(x:int64) -> (r:int64) { x + 1 -> r }`, called as `f(3) -> y`.

### Program Structure

A file is a sequence of `lib` blocks, `rwfunc` declarations, and instructions. There is **no `import`** — the `/lib` tree itself is the global namespace, and a cross-lib call is written with the full path or the qualified name `pkg·func`. Every parameter must carry a type annotation.

```kv
rwfunc test() -> () {
    total = 0
    1 -> i
    while (i <= 5) {
        total + i -> total
        i + 1 -> i
    }
    println(total)          // 15
}

test()
```

The entry point is conventionally `rwfunc test()` for a file and `init` for inline source; bare instructions at lib level are combined into the implicit `init`. `if` / `while` / `for` may appear only inside an `rwfunc` body.

### Write Forms (`=` and `->`)

```kv
x = 40 + 2                  // = : write slot on the left
x × y -> z                  // -> : write slot on the right
f(a, b) -> r                // write-param mapping for calls; multiple: -> x, y; discard: -> _
```

`=` is only a source-level alias for "write left"; what lands in kvspace is always the `->` axis structure, with read params on the negative axis and write params on the positive axis. A write slot must be a **location**: a bare name (frame-local), `/abs/path` (global key), or `base·member`. Literals are not locations.

**Read params are read-only** — including through member/index writes and `kvspace·set(base, …)`, and aliasing does not launder them. Write params are readable and writable. To mutate an array or map inside a function, declare it as a write param: `a[i] = v` writes through `a`.

### Types

Exact-width numbers: `int8 int16 int32 int64 uint8 uint16 uint32 uint64 float32 float64`. Low-precision kinds for tensor annotation (the runtime moves bytes, it does not arithmetic them): `float16` `bfloat16` `float8/e4m3` `float8/e5m2`. Others: `bool`, `char/utf8` `char/utf32` `char/ascii`, `stringkeymap`, `index`, `struct`, `time`, `duration`, `any`.

```kv
float64(3) -> f          // 3.0
int64(3.9) -> i          // 3 (truncates toward zero)
int64("42") -> n         // 42
bool(1) -> b             // true
char/utf32(x) -> s       // string encoding conversion (e.g. char/utf8 → char/utf32)
```

The ten numeric operators are both constructors and converters. Arithmetic keeps width (same-width ops stay same-width and wrap on overflow); mixed widths promote to the wider (`int32 + int64 → int64`), and any float promotes to float. `index` and `extindex` are **storetypes**, not kinds; `p` (ptr) and `None` are not kinds either.

`string·formatint(n, base)` / `string·formatuint(n, base)` render integers in a base; `string·parseint(s, base)` / `string·parseuint(s, base)` parse them (base 2..36).

### langtype: two forms

A **langtype** is a value's real type: one concrete kind plus determinate `[dims]`, with no unions and no axis quantifiers. A **def langtype** is the matching type at a definition site (a `def rwir` signature row, or an `rwfunc` param definition) and may contain unions `A|B`, the axis quantifiers `.` `?` `*` `+`, `any`, and mapexpr — one def langtype can match many langtypes. Both roles share one grammar (the appendix grammar is authoritative), and both are called a **kindexpr** when the emphasis is on "one concrete type-expression string".

### Arrays (compact `[…]`)

```kv
a:[]int64 = [7, 2, 9, 4]
ndarray·numel(a) -> n     // 4 (element count)
ndarray·dim(a) -> d       // 1 (rank)
ndarray·shape(a) -> sh    // per-axis lengths
xv·at(a, 2) -> e          // 9 (out of range errors)
xv·set(a, 1, 99) -> b     // new array [7, 99, 9, 4]
a[0] -> head              // 7
xv·langtype(a) -> lt      // "[4]int64"
xv·bodylen(a) -> bl       // 32 (body bytes)
```

An array has two physical forms, and **the literal brackets determine which**: `[1,2,3]` is **compact** (fixed-length, same-type elements packed contiguously in one XValue; `[N]T` / `[d0,d1]T`), while `{v0,v1,…}` is **stringkeymap** form (each element an independent child key, growable; variable-length strings live here, not in `[…]`). Multi-dimensional `[2,3]int64` is a compact ndarray. Fixed-length initialization is written `a:[1024]int32 = []` (all zero). Conversions are `array·scatter` (compact → stringkeymap) and `array·compact` (reverse).

Traverse a compact array with `while` + `ndarray·numel` + `xv·at`. A string array or a variable-length collection uses the `{}` form.

### Maps, Member Access, and struct

Container members are accessed with `·` (U+00B7) — **never** `[]`, which indexes only compact arrays. The access key is, verbatim, the key formatter's output; kvspace and the runtime must not rewrite, normalize, or auto-wrap it. A bare-scalar key and a 1-tuple key are different formatters and never interconvert: `b:int64·int64 = {}` is reached as `b·200`, not `b·[200]`. An array shape (`[N]T`) may not be a key at all.

```kv
d:[]char/utf32·int64 = {}     // empty stringkeymap must be typed
d·a = 10                      // static member write
kvspace·get(d, "a") -> x      // dynamic read (missing → None)
k = "a"
d·*k -> v                     // dynamic member read: k's value becomes the key
kvspace·set(d, "c", 30) -> _  // dynamic member write

m = map()                     // stringkeymap constructed empty

r = {name="kv", ver=1}        // struct literal (heterogeneous record; there is no object)
r·name -> n                   // member read
5 -> r·ver                    // struct members are writable
```

A map's declaration form is `memheadname : memitemkey_formatter · memitemvalue = {}` — the key side is a **formatter** (a key lives only in the path system, never in an XValue body), choosing among a bare scalar, a string key `[]char/<encoding>`, or a scalar tuple `[scalar,…]`. The value side recurses, so maps nest.

Named struct (for field defaults or a reusable type):

```kv
struct Point { x:int64=0 y:int64=0 }
rwfunc test() -> () {
    p:Point = {x=3 y=4}
    println(p·x, p·y)         // 3 4
}
```

`struct Name { field:type=default }` registers a prototype at `/lib/Name`; instantiating `Name{f=v}` clones the prototype, overrides the given fields, and type-checks them. A struct is not a parallel type registry — the prototype is itself KV data.

Data shared across functions lives at **absolute paths** (frame-locals die when the frame returns): `/n1·val = 1`.

### Pointers

`&x` is the **absolute-address** operator (`&x ≡ kvlang·abs(x)`): it yields a pointer, a `ref=1` value whose head storetype/langtype describe the target's full form and whose body is the target key path. `*p` dereferences; `p·field` auto-derefs for member access. Assignment checks the pointer's declared target langtype against the pointed-to key's actual langtype for **one hop only**. Value containers (a map langtype or a struct prototype path) have no body to copy and must be passed as `*T`. **An empty pointer is `None`** — there is no `""` sentinel, and `p == ""` is illegal; test `p != None`. A plain string variable is not a pointer.

A pointer used as a map key must be written with a leading `*` (`base·*k`); omitting it is illegal, because there is no auto-dereference. This is explicit dereference, not automatic key rewriting.

### Control Flow (inside rwfunc bodies only)

```kv
if (c) { … } else if (c2) { … } else { … }
while (c) { … }
for (e in arr) { … }        // iterates a compact array
break / continue
return                      // no return value
```

Map traversal uses `while` + `kvspace·listlen` + `kvspace·listn` (`for`-`in` is not reliable on maps) — note the directory path needs a trailing `/`. Control flow is source syntax only; layout flattens it into a linear irseq and lowers it to `goto` / `br`.

### Operators

| Category | Symbols |
|------|------|
| Arithmetic | `+` `-` `×` `÷` `%` |
| Unary prefix | `-` `!` `√` (sqrt) `&` (absolute address) `*` (dereference) |
| Comparison | `==` `!=` `<` `>` `<=` `>=` (and `≠` `≤` `≥`) |
| Logic / bitwise | `&&` `\|\|` `!` and `&` `\|` `^` `<<` `>>` |

> `÷`: both operands integers → integer division (`7÷2`=3); either side float → float division (`7.0÷2`=3.5).
> `/` is reserved for paths and comments; `*` is the pointer/dereference operator, not multiplication.

### Builtins

**Builtins** are the rwir in the core runtime's `myrwircaps`: pure KV→KV computation, no I/O.

**Math / compare / logic / bit:** `add`(`+`) `sub`(`-`) `mul`(`×`) `div`(`÷`) `mod`(`%`) `pow` `sqrt`(`√`) `exp` `log` `neg` `abs` `sign` `min` `max`; `eq` `neq` `lt` `gt` `le` `ge` `and` `or` `not` `bitand` `bitor` `bitxor` `shl` `shr`
**Type constructors:** `bool` `int8` … `int64` `uint8` … `uint64` `float32` `float64` `char/utf32` `char/utf8` `char/ascii`; container constructors `map` `array` `struct·new`
**`array·`:** `scatter` `compact` `append` `slice` `fill`  ·  **`ndarray·`:** `numel` `dim` `shape`
**`xv·`:** `at` `set` `reshape` `reinterpret` `langtype` `bodylen`
**`string·`:** `len` `char` `ord` `cmp` `find` `slice` `concat` `set` `formatint` `formatuint` `parseint` `parseuint`
**`kvspace·`:** `get` `set` `del` `deltree` `cp` `cpdir` `cplist` `list` `listlen` `listn` `mkindex` `extindex` `rmindexext` `watch`
**`time·` / `time/duration·` / `random·`:** `now` `sub` `add` `before` `after`; `nanos` `millis` `seconds` `minutes` `hours` and the `as_*` forms; `uint64` `int63` `intn`
**`vthread·` / debug:** `create` `run` `call` `sleep` `setstatus`; `debugger` (≡ `vthread·setstatus("paused")`)

`print` / `println` / `cerr` / `printf` / `input` are **not** builtins. In the KV world there is no terminal — only keys and values — so I/O is not a core-language primitive. They are opcodes of the `term` runtime, reached through the `def rwir` route-header mechanism described above. The same mechanism covers `json·to` / `json·from`, `http·call` / `http·get|post|put|del`, `networld/proc·exec`, `networld/fs·size|read|write|append|list|del|mkdir|exists`, and the self-hosting `kvlang·vet|format|layout|dump`.

```kv
print(x,…)              // no spaces, no newline
println(x,…)            // space-separated, newline
cerr(x,…)               // like println, to stderr
printf(fmt,…)           // C-style: %d %i %u %o %x %X %f %e %g %c %s %%; no newline
input(prompt) -> line   // read one line of stdin
```

String↔bytes goes through `xv·reinterpret`. A small kvlang-level stdlib wraps common patterns as rwfuncs: the [`stdlib/`](stdlib/) libs `kvspace` (`has`, `get_or`, `set_default`), `string`, `math`, `time`, `time/duration`, `xv`, `http`.

### Worked Example

```kv
rwfunc test() -> () {
    s = "hello"
    string·len(s) -> n
    println(n)                  // 5

    a:[]int64 = [1,2,3,4]
    0 -> acc
    0 -> i
    while (i < ndarray·numel(a)) {
        xv·at(a, i) -> e
        acc + e -> acc
        i + 1 -> i
    }
    println(acc)                // 10

    d:[]char/utf32·int64 = {}
    d·a = 10
    kvspace·get(d, "a") -> x
    println(x)                  // 10
}
```

For an LLM-oriented one-page summary of the language, see [`stdlib/kvlang/kvlangbrief.kv`](stdlib/kvlang/kvlangbrief.kv).

---

## Tutorial

210 self-contained examples (`// 期望输出` headers give expected output for 202 of them), organized by topic:

```
01-basics/      hello, arith, precision, numtypes, strings, …     (18)
02-func/        rwfunc, call, accumulator                         (4)
03-control/     if, while, for, guess                             (5)
04-ndarray/     compact arrays, subscript, multi-dim              (12)
05-dict/        maps, dynamic keys, missing-key → None            (3)
06-algo/        gcd, collatz, power, factorial, …                 (8)
07-lib/         lib blocks, nested, cross-lib, anonymous          (13)
08-leetcode/    LeetCode solutions                                (88)
09-debugger/    debugger builtin                                  (3)
10-types/       typed maps, nested types, tuple keys              (8)
11-string/      string operations                                 (9)
12-struct/      struct declaration, fields, pointers, lists       (10)
13-stdlib/      stdlib libs: time, duration, kv, xv, math, string (15)
14-networld/    process exec, filesystem, http                    (10)
15-vthread/     vthread create/call/run                           (4)
```

```bash
./bin/kvlang tutorial/01-basics/hello.kv          # run one example
./bin/kvlang tutorial/06-algo/gcd.kv              # gcd = 6

python3 tutorial/test.py                          # whole suite — the conformance test
python3 error_cases/error_test.py                 # expected-diagnostic cases
```

`tutorial/test.py` discovers every `.kv` under `tutorial/`, extracts the `// 期望输出` expectations, lays the file out with `bin/kvlanglayout` and runs it, then matches stdout. The tutorial suite **is** the conformance test: an implementation conforms when, for each anchored example, the output is byte-identical on every backend. `error_cases/` holds 37 cases across 10 diagnostic categories.

---

## Benchmark

`benchmark/` is a cross-language, cross-backend performance harness. Each case is one algorithm implemented identically in kvlang / Python / Rust / C; kvlang runs on all three kvspace backends (`shm` / `fs` / `redis`) as separate columns, with the native/scripting versions as baselines. Scales are fixed for long-term comparability, and every run appends to a versioned CSV — same host + case, sorted by version, gives the perf-evolution curve.

Ten cases: `binary_search`, `binary_trees`, `fib`, `hash_table`, `iops`, `k_nucleotide`, `matmul`, `nqueens`, `prime_sieve`, `quicksort`.

```bash
python3 benchmark/run.py            # all cases × three backends, appends a versioned CSV
python3 benchmark/run.py --show     # print recorded history
```

See [benchmark/README.md](benchmark/README.md) for cases, timing convention, and CSV schema.

---

## Specification

The normative language definition lives in [`stdlib/kvlang/spec/`](stdlib/kvlang/spec/), written in kvlang and laid out into `/lib/kvlang/spec/…`. It is the single source of truth; this README is a derivative.

| Volume | Contents |
|--------|----------|
| [00-导言](stdlib/kvlang/spec/00-导言/) | Scope, normative language, reading conventions |
| [01-词法](stdlib/kvlang/spec/01-词法/) | Source structure, tokens, comments, identifiers, path literals, literals, operators and precedence |
| [02-kvspace模型](stdlib/kvlang/spec/02-kvspace模型/) | Address space, structure domains, addressing and naming, the C ABI, XValue head wire format, the two array forms, instruction layout, vthread, system variables |
| [03-类型系统](stdlib/kvlang/spec/03-类型系统/) | Kinds and fixed-width types, langtype, the map container, member access, ptr, struct, the head's three axes, wire layout, string encodings |
| [04-layout语义](stdlib/kvlang/spec/04-layout语义/) | Instruction architecture, slot encoding, functions and write slots, control flow and its lowering, the layout pipeline and ABI, diagnostics |
| [05-runtime语义](stdlib/kvlang/spec/05-runtime语义/) | Execution model, member access, calls, runtime-c builtins, runtime development norms, notinmyrwircaps, the C ABI |
| [06-编译器语义](stdlib/kvlang/spec/06-编译器语义/) | The contract between the core and extension compilers: front-end/back-end operators, backend binding, operator versioning |
| [附录](stdlib/kvlang/spec/附录/) | The authoritative grammar: program structure, type expressions, instructions, expressions, control flow, builtins |
| [设计理由](stdlib/kvlang/spec/设计理由/) | Non-normative rationale: storage/compute separation, everything is plaintext, program as data, code hierarchy, how to design a language |

A rendered view of the stdlib and spec is published at [array2d.github.io/kvlang](https://array2d.github.io/kvlang/).

---

## License

MIT — see [LICENSE](LICENSE)
