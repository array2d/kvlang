# kvlang

[![CI](https://github.com/array2d/kvlang/actions/workflows/ci.yml/badge.svg)](https://github.com/array2d/kvlang/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**A declarative VM where code and data share the same key-value tree.**

Instructions are paths. Function calls are subtree copies. State is transparent and always inspectable — no hidden stack, no opaque heap.

> 中文文档: [README_CN.md](README_CN.md)

---

## Why kvlang?

Most VMs separate code from data. kvlang unifies them in a single KV tree:

```
/vthread/1/[0,0]  → "add"           # opcode
/vthread/1/[0,-1] → "/src/add/a"    # read arg
/vthread/1/[0,-2] → "/src/add/b"
/vthread/1/[0,1]  → "/src/add/c"    # write result
```

- **Instruction = path**. An opcode stored at `[i,0]`, operands as negative/positive indices.
- **Call = subtree copy**. Calling a function copies its body under the caller's frame.
- **State is a tree**. Every variable, every return value, every frame lives at a path you can `GET`.

This means you can `GET /vthread/1/[5,-1]` to see a variable mid-execution. Thread state is a KV tree you can inspect, migrate, or persist. No black box.

---

## Quick Start

```bash
# Prerequisites: Go 1.24+, Redis (any version)
make build                    # build kvlang binary

# Run a file
echo 'print("hello kvlang")' > hello.kv
kvlang hello.kv               # → hello kvlang

# Inline mode
kvlang -c '1 + 2 + 3 -> "./x"; print("x =", "./x")'   # → x = 6

# Serve mode (daemon with Redis persistence)
kvlang load my_program.kv
kvlang serve                  # workers execute, output to stdout
```

---

## Architecture

```mermaid
flowchart LR
    SRC[".kv source"] --> LEX[Lexer] --> PAR[Parser] --> AST
    AST --> REG["Register signatures"]
    AST --> LOWER
    LOWER["Lower control flow"] --> BODY["WriteBody: AST → KV tree"]
    REG --> KVSPACE[("kvspace / Redis")]
    BODY --> KVSPACE
    KVSPACE --> EXEC[Execute]
    EXEC --> BUILTIN["builtin ops\narith/compare/logic/cast"]
    EXEC --> VTHREAD["vthread scheduler\n128 workers"]
    VTHREAD --> IO["device I/O\nstdout/stderr/websocket"]
```

**Pipeline**: `.kv` source → parse → lower control flow → write opcodes/operands as KV paths → Redis → workers execute by reading/writing those paths.

**Key components**:

| Layer | Package | Role |
|-------|---------|------|
| Parser | `internal/parser` | `.kv` → AST |
| Lower | `internal/lower` | if/while → block + branch |
| Layout | `internal/layoutcode` | AST → KV tree (opcode paths) |
| Scheduler | `internal/kvcpu` | 128 goroutine workers, vthread dispatch |
| Storage | `internal/kvspace` | KVSpace interface (Redis impl) |
| Types | `internal/vtype` | int, float, bool, str, tensor |

---

## Language at a Glance

```kvlang
// Activate stdout
str.set("kvlangrun") -> './term'

// Define a function
def add(A: int, B: int) -> (C: int) {
    A + B -> './C'
}

// Call it
add(10, 32) -> './sum'
print("sum =", './sum')    // → sum = 42
```

### Control Flow

```kvlang
def abs(x: int) -> (r: int) {
    if (x < 0) {
        -x -> './r'
    } else {
        x -> './r'
    }
}
```

### Multi-return & Recursion (TCO)

```kvlang
def fib(n: int) -> (a: int, b: int) {
    if (n <= 1) {
        0 -> './a'
        1 -> './b'
    } else {
        fib(n - 1) -> './a', './b'
        './a' + './b' -> './x'
        './b' -> './a'
        './x' -> './b'
    }
}
fib(10) -> './_', './result'
print("fib =", './result')    // → fib = 55
```

---

## Tutorial

Get started in 5 minutes with progressive examples:

| Step | Topic | Code |
|------|-------|------|
| [01](tutorial/01-hello/main.kv) | Hello World | `print("hello kvlang")` |
| [02](tutorial/02-vars/main.kv) | Variables | `42 -> './x'` |
| [03](tutorial/03-arith/main.kv) | Arithmetic | `10 + 3`, `pow(2,5)`, `sqrt(144)` |
| [04](tutorial/04-func/main.kv) | Functions | `def add(...)` |
| [05](tutorial/05-if/main.kv) | Control Flow | `if (x < 0) { ... }` |
| [06](tutorial/06-recursion/main.kv) | Recursion | `fib(10)`, `fact(10)` with TCO |

```bash
kvlang tutorial/01-hello/main.kv    # run any tutorial step
```

---

## Examples

179 example programs, graded P0–P3:

| Grade | Category | Examples |
|-------|----------|----------|
| P0 | Builtin smoke | print, add, compare, logic |
| P1 | Equivalence classes | 18 compare variants, 10 logic, 10 arith edges, 7 call patterns |
| P2 | Frame isolation + TCO | scope isolation, tail-call depth (sum 5050, fact 3628800) |
| P3 | Algorithms | fibonacci, factorial, gcd, fizzbuzz, power, collatz, classify |

```bash
python3 example/run.py                    # 175 integration tests
python3 example/run.py --filter compare   # filter by keyword
```

---

## Dependencies

**Only 2 direct dependencies:**

| Package | Purpose |
|---------|---------|
| `redis/go-redis/v9` | KV storage backend |
| `gorilla/websocket` | Optional WebSocket terminal |

Zero framework. Zero code generation. Pure Go standard library + Redis.

---

## KV Path Reference

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

## License

MIT — see [LICENSE](LICENSE)
