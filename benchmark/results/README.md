# benchmark/results — 发布基准快照

## 采集口径（从 v0.3.30 起）

- **一律由 `benchmark/run.py` 生成**（`--backends shm --repeat 3`）。
- `input` 列取**用例自报标签**（用例源码里的 `__bench_input:` 输出），例如
  `N=32,seed=1`、`depth=4`、`n=8`、`rep=3`。
- `harness_rev` 列标记用例结构版本：历史数据视为 1，v0.3.30 起为 2。
  **跨 `harness_rev` 的数字只能看趋势，不能做精确回归判定。**

## v0.3.29 及更早

- 由**临时脚本**产出，schema 还带 `kvlang_over_c` 列，仓库里没有脚本写这个表头。
- `input` 列是脚本自己拼的**合成标签** `N=<scale>`。

## 跨版本比较要注意

标签口径不同：`binary_trees`（`N=4` vs `depth=4`）、`fib`（`N=8` vs `n=8`）、
`k_nucleotide`（`N=3` vs `rep=3`）、`quicksort`（`N=32` vs `N=32,seed=1`）。

**按 `(case, input)` 直接连接会静默丢行**（v0.3.29 → v0.3.30 的 30 行只会连上 18 行）。
比较时请**按行内数字取规模**（从 `input` 里解析出数字），不要按字符串相等连接。
