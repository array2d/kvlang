# kvlang — Artifact

Artifact for the paper **"The Program Counter Is a Path: Code, Data, and Control
in One Addressable Tree"**.

kvlang is a language whose code and data live in one addressable key–value tree.
Instructions are paths, calls are soft links, and the program counter is a path
string — so a stopped or crashed program can be resumed by another process.

This repository is self-contained: the storage backend, the allocators, the
layout compiler, and the C runtime are all in the tree, and the whole build
runs with a single `make`.

---

## Requirements

| Tool | Version | Notes |
|---|---|---|
| C compiler | C11 | GCC 13 / Clang 15 or newer |
| CMake | 3.16+ | |
| Rust | 1.97+ with `cargo` | builds the layout compiler |
| Python | 3.12+ | benchmark harness |
| Redis | 7.0+ | optional, for the `redis` backend |

Linux and macOS are both supported. Nothing is installed system-wide: the build
writes only into `bin/` and `lib/` inside the repository.

---

## Getting Started

```bash
git clone <this-repository> kvlang
cd kvlang
make artifact
```

That is the whole build. It produces:

```
bin/kvlangrun          two-phase driver: create <src> / run <vid>
bin/kv_runner          batch corpus compiler and runner
bin/kvlanglayout       layout compiler CLI

lib/libkvspace.so*     shared-memory key–value store
lib/libkvlanglayout.so layout compiler shared library
lib/libkvlang_runtime.so
lib/libkvlang_xvalue.a language-layer statics
```

Run a first program:

```bash
KVSPACE=shm:///tmp/demo.shm ./bin/kvlangrun create tutorial/runtimec/01-basics/hello.kv
KVSPACE=shm:///tmp/demo.shm ./bin/kvlangrun run 1
```

`create` lays the program out into the store and prints a virtual-thread id;
`run` executes it. The store keeps everything the program needs, which is what
makes a stop resumable.

To build and run the benchmark suite in one step:

```bash
make artifact-check
```

---

## Reproducing the evaluation

All timings below use the `shm` backend, which is the in-memory floor. Every
benchmark program exists in four versions — kvlang, Python, Rust, and C — with
byte-identical outputs; the harness times only the core computation and reports
the minimum of three runs.

### Table 3 — Execution time across ten benchmarks

```bash
make artifact
python3 benchmark/run.py --backends shm --repeat 3
```

The harness sweeps each benchmark over a fixed ladder of input scales, runs the
kvlang, Python, Rust, and C versions at every point, and writes one row per
(program, scale) into `benchmark/results/`. Columns are `kvlang_shm`,
`kvlang_fs`, `kvlang_redis`, `python`, `rust`, and `c`; a row is marked `valid`
only when all backends agree with the native versions on the program output.

To reproduce a single program:

```bash
python3 benchmark/run.py --backends shm --repeat 3 -k fib
```

### Figure 5a–c — Resume latency

Resume latency is the time from process start to the first committed counter
write. It is measured with a store shim that brackets execute-entry, across
background store sizes from 1 KB to 100 MB, call depths from 1 to 64, and step
counts from 10² to 10⁶. The measurement is flat in all three because a resume
reads the stack frames, not the store.

### Figure 5d — Resume against replay and snapshots

A K-round workflow is stopped after N rounds and resumed, and the time to reach
round N+1 is recorded. The artifact prints these numbers with the workflow
driver; the DBOS, Temporal, LangGraph, and DMTCP bars come from the third-party
runtimes and are reproduced by running the ports under `benchmark/`.

### Table 4 — Commit cost versus tree interpretation

```bash
KVSPACE=shm:///tmp/ab.shm python3 benchmark/run.py --backends shm --repeat 3
```

The cycle-share breakdown is produced by the runtime's built-in profiler; the
three ablations (counter in process, no stop-gate read, both) are build-time
switches on the runtime.

### Space

`bin/kvlanglayout` prints the key tree it emits for a program, including the
header bytes of every value, which is what the space figures are computed from:

```bash
./bin/kvlanglayout tutorial/runtimec/01-basics/hello.kv
```

---

## What is in the tree

```
deps/                vendored sources, all built from scratch
  kvspace/             shared-memory key–value store (C)
  slotsboxmalloc/      slot allocator (C, header-only use)
  blockmalloc/         block allocator (C, header-only use)
layout/              layout compiler (Rust)
runtimec/            C11 interpreter, the execution engine measured in the paper
src/  include/       language layer: XValue heads, langcode, coordinates
stdlib/              standard library written in kvlang
benchmark/           ten cross-language benchmarks and the harness
tutorial/            conformance corpus, doubles as the regression suite
kvlanglean/          Lean 4 mechanization of the metatheory
```

---

## Checking the build

```bash
ctest --test-dir build/artifact-runtimec --output-on-failure
```

```bash
./bin/kv_runner --operators=println,print \
  tutorial/runtimec/16-runtimec-corpus \
  ./lib/libkvlanglayout.so ./lib/libkvspace.so.1
```

The corpus runner compares each program's output against the expected output
recorded in the file header.

---

## Store backends

`KVSPACE` selects the backend at run time:

```bash
KVSPACE=shm:///tmp/x.shm      # shared memory, in-process floor
```

The `shm` backend is the one all measurements in the paper use and the one this
artifact builds and validates by default.

---

## License

See `LICENSE`.
