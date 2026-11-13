# pathlang — Artifact

Artifact for the paper **"Stop, Edit, and Resume without Replay: Control State
in a Single Addressable Tree"**.

pathlang is a language whose code and data live in one addressable key–value tree.
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
| Lean 4 | 4.35.0 (`elan`) | optional, checks the `formal/` mechanization |

Linux and macOS are both supported. Nothing is installed system-wide: the build
writes only into `bin/` and `lib/` inside the repository.

---

## Getting Started

```bash
git clone <this-repository> pathlang
cd pathlang
make artifact
```

That is the whole build. It produces:

```
bin/pathlangrun          two-phase driver: create <src> / run <vid>
bin/kv_runner          batch corpus compiler and runner
bin/pathlanglayout       layout compiler CLI

lib/libpathspace.so*     shared-memory key–value store
lib/libpathlanglayout.so layout compiler shared library
lib/libpathlang_runtime.so
lib/libpathlang_xvalue.a language-layer statics
```

Run a first program:

```bash
PATHSPACE=shm:///tmp/demo.shm ./bin/pathlangrun create tutorial/runtimec/01-basics/hello.kv
PATHSPACE=shm:///tmp/demo.shm ./bin/pathlangrun run 1
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
benchmark program exists in four versions — pathlang, Python, Rust, and C — with
byte-identical outputs; the harness times only the core computation and reports
the minimum of three runs.

### Table 3 — Execution time across ten benchmarks

```bash
make artifact
python3 benchmark/run.py --backends shm --repeat 3
```

The harness sweeps each benchmark over a fixed ladder of input scales, runs the
pathlang, Python, Rust, and C versions at every point, and writes one row per
(program, scale) into `benchmark/results/`. Columns are `pathlang_shm`,
`pathlang_fs`, `pathlang_redis`, `python`, `rust`, and `c`; a row is marked `valid`
only when all backends agree with the native versions on the program output.

To reproduce a single program:

```bash
python3 benchmark/run.py --backends shm --repeat 3 -k fib
```

### Figure 4a–c — Resume latency

Resume latency is the time from process start to the first committed counter
write. It is measured with a store shim that brackets execute-entry, across
background store sizes from 1 KB to 100 MB, call depths from 1 to 64, and step
counts from 10² to 10⁶. The measurement is flat in all three because a resume
reads the stack frames, not the store.

### Figure 4d — Resume against replay and snapshots

A K-round workflow is stopped after N rounds and resumed, and the time to reach
round N+1 is recorded. The artifact prints these numbers with the workflow
driver; the DBOS, Temporal, LangGraph, and DMTCP bars come from the third-party
runtimes and are reproduced by running the ports under `benchmark/`.

### Table 4 — Commit cost versus tree interpretation

```bash
PATHSPACE=shm:///tmp/ab.shm python3 benchmark/run.py --backends shm --repeat 3
```

The cycle-share breakdown is produced by the runtime's built-in profiler; the
three ablations (counter in process, no stop-gate read, both) are build-time
switches on the runtime.

### Space

`bin/pathlanglayout` prints the key tree it emits for a program, including the
header bytes of every value, which is what the space figures are computed from:

```bash
./bin/pathlanglayout tutorial/runtimec/01-basics/hello.kv
```

---

## What is in the tree

```
deps/                vendored sources, all built from scratch
  pathspace/           shared-memory key–value store (C)
  slotboxalloc/        slot allocator (C, header-only use)
  blockalloc/          block allocator (C, header-only use)
layout/              layout compiler (Rust)
runtimec/            C11 interpreter, the execution engine measured in the paper
src/  include/       language layer: XValue heads, langcode, coordinates
stdlib/              standard library written in pathlang
benchmark/           ten cross-language benchmarks and the harness
tutorial/            conformance corpus, doubles as the regression suite
formal/              Lean 4 mechanization of the metatheory (16 files, 403 theorems, zero sorry)
```

---

## Lean 4 mechanization (Appendix A.10, Table 7)

```bash
cd formal
lake build --wfail
lake env lean Audit.lean
```

`formal/lean-toolchain` pins Lean 4.35.0 (core library only, no Mathlib, zero `sorry`). `formal/scripts/check.sh` additionally runs `axiom-audit`, `leanchecker`, and the Rust kernel `nanoda`; see `formal/README.md` for full theorem-to-module correspondence.

---

## Checking the build

```bash
ctest --test-dir build/artifact-runtimec --output-on-failure
```

```bash
./bin/kv_runner --operators=println,print \
  tutorial/runtimec/16-runtimec-corpus \
  ./lib/libpathlanglayout.so ./lib/libpathspace.so.1
```

The corpus runner compares each program's output against the expected output
recorded in the file header.

---

## Store backends

`PATHSPACE` selects the backend at run time:

```bash
PATHSPACE=shm:///tmp/x.shm      # shared memory, in-process floor
```

The `shm` backend is the one all measurements in the paper use and the one this
artifact builds and validates by default.

---

## License

See `LICENSE`.
