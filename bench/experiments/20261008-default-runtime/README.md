# Default runtime comparison

All 10 repository workloads and their 30 frozen sweep points, with 9 alternating baseline/candidate/Python runs per point (810 samples). Every functional output matches. CPU 24, Xeon Gold 6418H, GCC 14.3, Python 3.10.12, fresh SHM stores. Both runtimes use Release `-O3 -DNDEBUG`; both SHM backends use `-O3 -g -DNDEBUG` and the same allocator dependency. The CLI, layout library, and Release dispatch frontend are identical between variants.

The baseline is the previous optimized runtime `ddac99ab` with SHM backend `703797b9`. The candidate adds key references and the optional full-value write API. This measures the default execution path; no standalone native prototype is used.

| Measurement | Geometric mean speedup |
|---|---:|
| Repository algorithm timers | 1.207x |
| Entire CLI invocation, including startup and layout | 1.021x |

The 30 algorithm speedups range from 1.013x to 1.452x. Python algorithm timers remain about 85–1670 times faster; this change does not establish Python parity or a large whole-command gain.

`samples.csv` contains every measurement and functional-output digest. `summary.json` contains all 30 medians, including whole-command times. `metadata.json` records source hashes, compiler flags, baseline heads, tracked-diff hashes, command arguments, and the libraries actually loaded. Absolute paths document this run; supply local equivalents to repeat it:

```sh
python3 bench/experiments/20261008-default-runtime/run.py \
  --binary PATH_TO_COMMON_CLI \
  --baseline PATH_TO_BASELINE_RUNTIME \
  --candidate PATH_TO_CANDIDATE_RUNTIME \
  --frontend PATH_TO_RELEASE_FRONTEND \
  --baseline-backend PATH_TO_BASELINE_BACKEND_DIRECTORY \
  --backend PATH_TO_CANDIDATE_BACKEND_DIRECTORY \
  --output PATH_TO_RESULTS --pairs 9 --cpu 24
```

The runner imports the repository's unchanged `benchmark/run.py` sweep and checks output equality, timer availability, actual runtime loading, and binary stability. These results apply to SHM; FS receives correctness coverage, and Redis performance is untested.
