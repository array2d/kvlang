# Current interpreter versus Python

Nine alternating runs per variant on each repository benchmark's largest frozen
scale. All 630 samples have identical functional output within each case.
Results are medians of the fixtures' `__bench_ns` interval in milliseconds;
process startup, layout loading and store setup are excluded. `samples.csv`
also records wall time. Each interpreter sample starts with a fresh SHM store.

Environment: Intel Xeon Gold 6418H, CPU 24 affinity, GCC 14.3.0,
`-O3 -g -DNDEBUG`, CPython 3.10.12, `LOG_LEVEL=warn`.

## Isolated variants

The runtime baseline is kvlang `774e5315eff9ec39caa1cd6e9791620811845a9d`.
The backend baseline is kvspace-c `0e5660c37d5584fe774b1c75d71eca3777115671`.
Both interpreters use the same CLI, layout library and kvspace v0.3.0 frontend.
Blockmalloc is v0.1.4. The allocator baseline is slotsboxmalloc v0.1.5
(header identical to main `e2c4c6bf67648a7c200d0354ff25e5f65c417a00`).

| Variant | Runtime | SHM write path | Allocator |
|---|---|---|---|
| main | baseline | baseline | v0.1.5 |
| runtime | this patch | baseline | v0.1.5 |
| backend | baseline | existing-value write patch | v0.1.5 |
| combined | this patch | existing-value write patch | v0.1.5 |
| allocator | baseline | baseline | bitmap allocation patch |
| all | this patch | existing-value write patch | bitmap allocation patch |
| python | stock case | — | — |

The bitmap patch replaces the reverse slot scan with word operations and rejects
requests exceeding a leaf's `max_obj_cap`. It retains the v0.1.5 pool format and
slot selection. The separate, previously submitted slotsboxmalloc PR #9 capacity
initialization fix is absent from every measured variant. The earlier kvspace-c
direct-child listing PR #39 is also absent.

## Results

These combined results require all three source patches; they are not the gain
from the runtime patch alone. Runtime-only medians range from 0.97x to 1.07x
baseline speed, including a 3.1% slower iops median. The larger improvements
come from the separately isolated write and allocator patches.

| Case | Scale | Main ms | All patches ms | Speedup | Python ms |
|---|---:|---:|---:|---:|---:|
| iops | 2000 | 33.231 | 26.754 | 1.24x | 0.094775 |
| prime_sieve | 100 | 43.125 | 33.189 | 1.30x | 0.078565 |
| fib | 10 | 116.614 | 39.435 | 2.96x | 0.019457 |
| nqueens | 6 | 319.365 | 103.438 | 3.09x | 0.080029 |
| quicksort | 128 | 50.026 | 38.346 | 1.30x | 0.089687 |
| binary_search | 64 | 141.625 | 53.834 | 2.63x | 0.048057 |
| binary_trees | 6 | 27.805 | 16.576 | 1.68x | 0.036597 |
| hash_table | 200 | 21.035 | 13.794 | 1.52x | 0.090475 |
| matmul | 8 | 21.447 | 16.185 | 1.33x | 0.092590 |
| k_nucleotide | 8 | 33.396 | 27.585 | 1.21x | 0.071613 |

The optimized interpreter remains 152–2027x slower than Python at these scales.
These are SHM results, not FS/Redis performance measurements. The dependency
manifest still pins slotsboxmalloc v0.1.5: consuming the bitmap gain requires
building the backend with the patched header, then a dependency release/update.

## Reproduce

Build each backend with the same flags and blockmalloc header. Put the allocator
include path first for the `allocator` and `all` variants:

```sh
cmake -S "$BACKEND_SOURCE" -B "$BACKEND_BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKVSPACE_BUILD_TESTS=ON \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O3 -g -DNDEBUG' \
  -DCMAKE_C_FLAGS="-I$ALLOCATOR_SOURCE/include -I$ABI_PREFIX/include"
cmake --build "$BACKEND_BUILD" -j

cmake -S "$KVLANG_SOURCE/runtime" -B "$RUNTIME_BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O3 -g -DNDEBUG' \
  -DKVSPACE_LIB_DIR="$ABI_PREFIX/lib/kvspace"
cmake --build "$RUNTIME_BUILD" -j
```

The checked snapshot used these paths (their actual loaded libraries and hashes
are recorded in `results/metadata.json`):

```sh
python3 bench/experiments/20261008-current-python/run.py \
  --baseline /tmp/kvlang-perf-current-baseline/bin/kvlang \
  --candidate /tmp/kvlang-perf-current/bin/kvlang \
  --baseline-backend /tmp/kvlang-perf-experiment/backend-main \
  --candidate-backend /tmp/kvspace-c-perf-write-build \
  --allocator-backend /tmp/kvspace-c-perf-leaf-build \
  --all-backend /tmp/kvspace-c-perf-total-build \
  --cpu 24 --pairs 9 --output results
```

`run.py` reuses the stock sweep and parser, substitutes only `__SCALE__`, rejects
failed or missing-time samples, checks every functional output against the other
variants and Python, and records source and loaded-library hashes.

## Validation

- Runtime CTest: 9/9 with the pinned backend and with all patches.
- SHM backend CTest: 6/6 for write-only, allocator-only and all patches.
- Candidate tutorials: SHM and FS each 222 passed, 0 failed, 1 skipped.
- Independent execution probes: external PC/value/status changes on cache hits,
  key fallback and write failures, worker exit/resume, codec bytes and ownership.
- All 30 frozen stock scale points pass with both pinned and combined SHM backends.
- Independent allocator oracle: 15,219,555 bitmap cases, 240,000 fragmented
  alloc/free operations, cross-address v0.1.5 pool handover, concurrent data
  integrity and persisted backend reopen checks.

Runtime's normal-length probes and all nine CTests pass ASan/UBSan. A separate
700-byte-ID probe reproduces an existing frame-key fallback leak on baseline
and candidate. Backend sanitizer comparison disables inherited unaligned ART
access checks; its independent write harness passes, while the full suite retains
the baseline `art_scan` leak. These limits are not full sanitizer-clean claims.
