# runtimec2（草稿，未完成）

runtime-c 的 C++20 重写（#499 新架构：明文栈 + 二进制代码段）。
**未完成、不可作为默认 runtime**；与 C 版并存，仅供 A/B 与原型。

- 构建（可选，不进入默认构建）：
  `cmake -S runtimec2 -B build/runtimec2 && cmake --build build/runtimec2 --target kvlang_runtimec2`
  产物 `bin/libkvlang_runtimec2.so`。
- 导出：与 C runtime 相同的 16 个 C ABI 符号（`-fvisibility=hidden`，其余隐藏）。
- 纪律：`-std=c++20 -fno-exceptions -fno-rtti`；热路径不用 virtual/std::function/
  variant/string/vector；复杂处用 std::pmr（待落地）。
- 测试：`ctest`（符号对齐、ops 表一致性、M1 冒烟）。M2 进行中。

## 当前 blocked（等扩展宿主）

c2 按裁决不本地实现下列 opcode 族，命中即由驱动报
"not implemented by any runtime (Handoff)"（不无限空转）：

- 族：`array` / `string·*` / `ndarray·*` / `obj` / `map` / `xv·*` / `struct·new`
- 受影响的 tutorial（01–05 共 9 个，均卡在入口第一条此类 op）：
  `01-basics/chinese_ident.kv`（ndarray·numel）、`01-basics/kv_tree.kv`（obj）、
  `01-basics/strict_types.kv`（string·cmp）、`01-basics/strings.kv`（array）、
  `02-func/accumulator.kv`（array）、`03-control/for.kv`（array）、
  `05-dict/kv_has_string_key.kv`（xv·at）、`05-dict/literal.kv`（obj）、
  `05-dict/literal_as_arg.kv`（obj）

扩展宿主就绪后按 `/lib/<op>/vids` 认领即自动解除 blocked（驱动已按 vids 分派 handoff）。

## 当前状态（#508 完成度证据）

- **A/B 与 C runtime 同轮**（benchmark/run.py --backends shm --repeat 3，min-of-3，
  输出逐字节一致）：
  - iops 500/1000/2000：C 528.7µs/910.9µs/1.671ms；c2 133.4µs/158.8µs/199.5µs
    → 4.0×/5.7×/8.4×
  - prime_sieve 50/75/100：C 2.654ms/3.910ms/4.961ms；c2 732.8µs/943.7µs/1.103ms
    → 3.6×/4.1×/4.5×
- **探针**：`RC2_PROF` 为 CMake 开关（默认关，热路径零开销）；开时产 profiling 二进制，
  输出每 op handler/loop cycles（rdtsc_cost 校准）。关探针的墙钟折算：
  iops ≈42 cycles/instr、prime_sieve ≈225（@2.3GHz，TSC 3.0GHz）。

## 目标达成标准（#499 handler 5–10，以关探针本体为准）

热 op 本体（每 op 单次 rdtsc 口径，扣探针；关探针墙钟折算同量级）：

| op | 含探针 | 扣探针本体 | 关探针墙钟折算 |
|---|---:|---:|---:|
| `int64·add` | 26.2 | **10.2** | ≈10 |
| `int64·le`  | 31.5 | **15.5** | ≈15 |
| `br`        | 23.4 | **7.4**  | ≈7 |
| `goto`      | 23.1 | **7.1**  | ≈7 |

add/br/goto 已 ≤10（达标）；le 15.5 仍差一步（bool 写 + Ptr 参数读属明文栈固有成本，
接近地板）。当四项都 ≤10 时 handler 目标宣布达成。
- **帧寄存器缓存是派生视图**（不是第二事实源）：fast u64/bool 缓存只在槽 langtype
  确认匹配后建立；直写先落 kvspace 明文槽再同步缓存；槽被删/重建/换 Ptr → view+缓存
  失效重建。bool 首次写 int64 槽走全编码更新明文 langtype，不许直写错位。
- **tutorial 01–05**：22 个与 C 逐字节一致，0 个内容差异；9 个 blocked-by-extension（见上）。
- **test**：c2 ctest 5/5。
