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
