# Recoverable scalar native experiment

`compile.py` runs the existing layout frontend, reads its RWIR from KVSpace, and emits a C worker. The source algorithms are unchanged. This is an opt-in benchmark prototype, with a separate packed frame ABI; it is not integrated into the interpreter or a claim about all kvlang programs.

## State and recovery

Every native instruction reads the PC at `/vthread/native/‥pc`, loads its operands from KVSpace's shared mapping, and commits changes back to that mapping. Frames, argument references, literal values, status, and the redo journal are KV tensors/values. Local C values exist only while computing one instruction. Calls use KV frames, not a recursive C call stack. Constants are read from KV cells, not embedded execution literals. The generated code and frame layout are immutable for a compiled program; a stored program hash rejects a different worker.

A release-store publishes the complete redo record before any live frame/PC update. Restart replays a prepared record, including a partially written PC, then clears its marker. This covers process death during execution, not machine power loss or filesystem durability. The timing includes every journal commit. Initialization marks the store non-runnable before changing it; retry `init` if initialization is interrupted.

Linux x86-64 SHM with an absolute DSN, fixed value shapes, and one writer is required. Native workers take an advisory file lock; other KV clients must avoid writes while a worker runs, especially replacing or deleting mapped values. Between stopped workers, clients can edit values, PC, and status through KVSpace. Every `run` reattaches and validates its schema. This prototype does not provide structural mutation leases or concurrent transactions.

The accepted subset is fixed-arity functions with int64 parameters and one int64 output; int64/bool locals; assignment, int64 arithmetic/comparisons/bit operations/shifts, branch, and recursive calls. Add/sub/mul overflow and invalid shifts fail. Depth is limited to 256. Arrays, objects, strings, external functions, FS/Redis, and other opcodes are rejected or unsupported.

## Reproduce

Use an existing kvlang CLI, matching KVSpace ABI headers, a SHM backend, and two builds of the same KVSpace frontend source. The default build has no optimization flags; Release uses `-O3 -DNDEBUG`.

```sh
cmake -S /path/to/kvspace -B /tmp/frontend-default -DCMAKE_BUILD_TYPE=
cmake --build /tmp/frontend-default --target kvspace
cmake -S /path/to/kvspace -B /tmp/frontend-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/frontend-release --target kvspace
python3 bench/native/run.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /tmp/frontend-release/libkvspace.so.1 \
  --baseline-frontend /tmp/frontend-default/libkvspace.so.1 \
  --backend /path/to/backend/build \
  --include /path/to/abi/include --output bench/native/results
python3 bench/native/validate.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /tmp/frontend-release/libkvspace.so.1 \
  --backend /path/to/backend/build --include /path/to/abi/include \
  --binaries bench/native/results/binaries
```

To compile a single entry, supply `compile.py` with the same `--kvlang`, `--frontend`, `--backend`, `--include`, plus `--source file.kv --entry function --output /tmp/worker`. Initialize with `/tmp/worker shm:///tmp/store init <int64 arguments>` and execute with `/tmp/worker shm:///tmp/store run`. Optional `run <instruction budget>` exits 75 after that many committed instructions; a new worker resumes from KVSpace.

## Measurements

CPU24, nine alternating rounds, fresh store per sample. The interpreter includes kvlang#364, kvspace-c#40 and slotsboxmalloc#10; both frontend variants use source `3e733731`. Generated workers use `cc -O3` and that same Release frontend/backend. Python uses the unchanged repository fixtures. Exact hardware, versions, loaded libraries, hashes, generated schemas, and all 216 samples are in `results/`.

The generated benchmark wrapper calls the original core function; the nqueens wrapper also computes the mask inside the timed region. Native timing measures `execute`, excluding compilation, initialization, attach/schema validation, and final reporting/status. The interpreter/Python use their existing fixture timers. These are scalar-core comparisons with different startup/wrapper costs. CSV wall times include native initialization and execution as two processes; compilation is excluded.

Median kernel times (ms):

| Case | Interpreter, Release frontend | Native with redo | Python | Native speedup | Native / Python |
|---|---:|---:|---:|---:|---:|
| iops(2000) | 17.973 | 0.193 | 0.094 | 93.0x | 2.05x |
| iops(200000) | 1623.717 | 17.437 | 9.058 | 93.1x | 1.93x |
| fib(10) | 29.552 | 0.037 | 0.019 | 792.3x | 1.95x |
| fib(18) | 1392.759 | 1.421 | 0.519 | 980.3x | 2.74x |
| nqueens(6) | 77.564 | 0.117 | 0.080 | 661.4x | 1.46x |
| nqueens(8) | 989.394 | 1.505 | 0.796 | 657.6x | 1.89x |

Ratios use unrounded medians; raw samples preserve dispersion (large iops frontend A/B paired speedups: 1.39–1.47x). These results do not establish parity for the full interpreter, other programs/backends, or startup-inclusive performance.

Validation: 147 checks passed, including 32 instruction-boundary restarts, 24 SIGKILL/restarts, prepared partial-commit replay, KV value/constant/status visibility, blocked partial initialization, invalid-state/unsupported-source rejection, and worker exclusion. See `results/validation.json`.
