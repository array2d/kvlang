# Native execution strategies

This experiment compares three approaches on the existing scalar SHM prototype. It retains its packed KV frame ABI, fixed mappings, exclusive worker, fresh PC/operand reads, and per-instruction redo commits. It does not integrate a compiler into the default runtime or extend the accepted language subset.

All modes use `compile.instruction_code` as the instruction semantics. The original AOT workers are byte-identical to the preceding address-cache experiment.

| Mode | Implementation |
|---|---|
| `aot` | Existing generated C with a function/row switch. |
| `decoded` | Decoded scalar micro-op interpreter; immutable instruction descriptors, runtime opcode/operand-mode dispatch. |
| `partial` | GCC specializes an inlined executor against constant instruction descriptors and opcodes. The resulting binaries eliminate the descriptor table. |
| `guarded` | Partial evaluation plus direct reads/writes when live slot tags match; otherwise use the original reference traversal. |
| `copy-patch` | Copy a 26-byte x86-64 call stencil per instruction, patch descriptor/handler addresses, then make the mapping RX. Each stencil jumps to an opcode-specific C helper. |
| `copy-patch-guarded` | The same JIT with guarded slot access in its helpers. |
| `address-cache` | Partial evaluation plus private 64-bit terminal-cell addresses. Every reference edge must decrease frame depth; same-depth/forward chains use wholly generic traversal. |

The copy-and-patch experiment uses opcode-helper stencils, not inlined arithmetic stencils or an optimizing trace JIT. Operand modes remain dynamic in its helpers. Templates and descriptors still require an offline C build (one observed build per variant/case: 0.20–0.50 s). Its roughly 5 us JIT time measures only mmap/copy/patch/mprotect, excluding layout, descriptor creation, offline compilation, and store attachment. It therefore does not establish an end-to-end compilation advantage.

The address cache stores no tags, scalar values, constants, or PC. Each access reads live tags and terminal values. Calls clear all address entries for the child frame before reusing it; each worker starts with empty caches. Descending aliases depend only on active ancestors, which cannot be reinitialized while the child executes. Stopped-worker KV edits are visible on a fresh run. Concurrent external writes remain excluded by the original prototype contract.

## Reproduce

```sh
python3 bench/native/strategies.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /path/to/release/libkvspace.so.1 \
  --backend /path/to/backend/build --include /path/to/abi/include \
  --output bench/native/results/strategies
python3 bench/native/validate_strategies.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /path/to/release/libkvspace.so.1 \
  --backend /path/to/backend/build --include /path/to/abi/include \
  --binaries bench/native/results/strategies/binaries \
  --output bench/native/results/strategies/validation.json
```

`compile.py --strategy <mode>` also compiles individual supported programs. Omitting the option preserves the original AOT generator. Linux x86-64 is required for this comparison.

## Results

CPU24, Xeon Gold 6418H, GCC 14.3, CPython 3.10.12, unchanged repository iops/fib/nqueens cores and Python fixtures, all three standard scales plus a larger scale, one warmup and nine alternating rounds. 864 measured outputs agree; all 108 groups have byte-identical final native KV-state digests. Metadata records source/library/binary hashes, compile observations and text sizes. `codegen.json` verifies descriptor elimination and opcode-specific JIT helpers.

Median execution times in ms, including redo and excluding init/attach/reporting/JIT preparation:

| Case | AOT | Partial | Guarded | Copy-patch | Copy-patch + guarded | Address cache | Python |
|---|---:|---:|---:|---:|---:|---:|---:|
| iops(200000) | 17.834 | 20.946 | 16.533 | 18.634 | 17.483 | 16.510 | 8.903 |
| fib(18) | 1.427 | 1.514 | 1.479 | 1.358 | 1.349 | 1.471 | 0.514 |
| nqueens(8) | 1.507 | 1.523 | 1.431 | 1.320 | 1.274 | 1.378 | 0.791 |

The nqueens combination improves on AOT by 1.18x (paired ratios 1.11–1.24x). Partial evaluation does not improve on the existing AOT in these large cases. Address caching reduces work relative to partial evaluation but does not improve fib relative to AOT. There is no uniform winning method or order-of-magnitude improvement; the fastest native execution remains 1.61–2.63x Python time on these three large inputs. Raw samples, startup-inclusive wall times and paired ranges are retained in `results/strategies/`.

Each of seven native modes passes 147 checks, including 32 boundary restarts, 24 SIGKILL/restarts, KV visibility/rejection and partial redo replay. Additional checks cover 49 cross-method recursive resumes, seven live-edit groups, seven partially applied call replays, and three address-cache fixtures: repeated calls/terminal value changes, deep aliases, and forward references into inactive frames after ancestor reuse. These validate the documented scalar prototype, not all runtime operations/backends or death during attach/init/reporting.
