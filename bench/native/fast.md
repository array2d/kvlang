# Reduced instruction overhead

This extends the scalar native experiment without changing its KV ABI or recovery contract. Every instruction reads the canonical PC path and operands from KVSpace, publishes redo, writes live state and PC, and clears redo. Private caches contain only addresses. The default interpreter is unchanged.

## Methods

| Mode | Change from AOT |
|---|---|
| `fast-commit` | Inline the validated commit; apply generated redo without repeating startup index validation. |
| `fast-pc` | Read decimal PC fields with packed volatile word loads and validate every digit; use immutable decimal-format tables for PC writes. Read the seven-byte status with two overlapping four-byte loads. |
| `fast` | Combine commit and PC changes. |
| `fast-slots` | Add the existing guarded slot helpers directly to AOT instruction bodies. |
| `fast-address` | Add the existing terminal-address cache, checking direct scalar slots first. |
| `fast-loop` | Enable address caching only in functions with a compiled backwards branch; inline the guards with that constant hint. |

The decimal tables occupy 41,024 bytes of read-only format metadata; they do not hold the current PC or KV literal values. The PC remains `/vthread/native/[ddd]/[rrrr,0]`. All loads/stores retain `volatile`, including unaligned fields. Status reads stay within its seven-byte body. The depth-word update preserves the live fixed separator byte, so replay cannot repair a malformed path.

Commit still validates the patch count and next PC. Generated destination indices are checked during reference resolution or bounded by the frame layout. Startup replay retains full validation of an externally supplied prepared journal. Publication and marker clearing remain release stores; death during either frame or PC application is recovered by replay. No value is forwarded across instruction boundaries.

The loop hint is derived from immutable RWIR metadata, without benchmark names or inputs. It affects cache use, not control flow: branch constants and PC are always read live. Cache entries require references to descend through active ancestor frames. Same-frame and forward references use generic traversal. Calls invalidate child-frame entries before reuse; a fresh worker starts with empty caches.

## Results

CPU24, Xeon Gold 6418H, GCC 14.3, CPython 3.10.12, unchanged repository iops/fib/nqueens cores and Python fixtures. All three repository sweep points plus one larger input per case; one warmup and nine alternating rounds. All 864 outputs agree, and all 108 groups have identical final six-body KV-state digests, including the complete journal. The original AOT workers remain byte-identical to the prior experiment.

Median execution times in ms, including every redo commit and excluding compilation, initialization, attachment and reporting:

| Case | AOT | `fast-loop` | Python | AOT speedup | Native / Python |
|---|---:|---:|---:|---:|---:|
| iops(200000) | 17.726 | 8.091 | 9.073 | 2.19x | 0.89x |
| fib(18) | 1.453 | 0.787 | 0.514 | 1.85x | 1.53x |
| nqueens(8) | 1.507 | 0.839 | 0.807 | 1.80x | 1.04x |

Paired AOT speedup ranges are 2.12–2.37x, 1.77–1.94x and 1.62–1.85x respectively. This is one compile-time policy across all three cases, not a per-case choice of the fastest variant. Fibonacci remains slower than Python. Full ablations, raw samples, compile observations, binary sizes, library hashes and startup-inclusive wall times are in `results/fast/`.

Skipping unchanged tags, ASCII-token dispatch, lazy frame generations and direct generated patch application were also tried. They did not improve this combination consistently enough to retain their implementations. `results/fast/rejected.json` records these exploratory comparisons separately from the final experiment.

These measurements cover the supported int64/bool scalar SHM prototype, not all repository benchmarks or an integrated default-runtime compiler. The fixed mappings, exclusive writer and process-death scope in [README](README.md) still apply.

Seven modes each pass 149 checks, including 32 instruction-boundary restarts and 24 SIGKILL/restarts. Further checks cover 49 cross-method resumes, seven live-edit groups, seven partially applied call replays and three alias fixtures. Independent CPU25 checks additionally cover 26 AOT/fast-loop groups with loops inside aliasing and deep-reference callees, live KV edits and malformed states; all final six-body digests match. The codec passes 16,955,522 comparisons each under GCC `-O3` and Clang ASan/UBSan, including all depth-byte combinations, malformed row/status bytes, eight alignments and guard-page body boundaries.

## Reproduce

```sh
python3 bench/native/strategies.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /path/to/release/libkvspace.so.1 \
  --backend /path/to/backend/build --include /path/to/abi/include \
  --output bench/native/results/fast \
  --variants aot fast-commit fast-pc fast fast-slots fast-address fast-loop python
python3 bench/native/validate_strategies.py \
  --kvlang /path/to/kvlang/bin/kvlang \
  --frontend /path/to/release/libkvspace.so.1 \
  --backend /path/to/backend/build --include /path/to/abi/include \
  --binaries bench/native/results/fast/binaries \
  --output bench/native/results/fast/validation.json \
  --variants aot fast-commit fast-pc fast fast-slots fast-address fast-loop
python3 bench/native/validate_codec.py \
  --cpu 24 --output bench/native/results/fast/codec-validation.json
```

`compile.py --strategy fast-loop` compiles another supported entry. Omitting `--strategy` preserves the original AOT generator; the original strategy runner/validator defaults are preserved.
