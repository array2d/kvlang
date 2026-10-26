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
  - iops 500/1000/2000：C 523.5µs/902.6µs/1.671ms；c2 157.4µs/202.4µs/293.6µs
    → 3.3×/4.5×/5.7×
  - prime_sieve 50/75/100：C 2.629ms/3.916ms/4.948ms；c2 759.3µs/999.5µs/1.191ms
    → 3.5×/3.9×/4.2×
- **探针**：`RC2_PROF` 为 CMake 开关（默认关，热路径零开销）；开时产 profiling 二进制，
  输出每 op handler/loop cycles（rdtsc_cost 校准）。关探针的墙钟折算：
  iops ≈62 cycles/instr、prime_sieve ≈245（@2.3GHz，TSC 3.0GHz）。
- **tutorial 01–05**：22 个与 C 逐字节一致，0 个内容差异；9 个 blocked-by-extension（见上）。
- **test**：c2 ctest 5/5。
