#pragma once

#include <cstdint>

#include "rwir.hpp"

namespace kvlang::runtimec2::rwir {

// int64 快内核 opfn 编号的唯一事实源：1..12 算术/位/移位，13..18 比较；
// 0 = 不属于 int64 快路径。jit_stage2 与建段期派生表共用。
std::uint8_t int64_fast_opfn(Op op) noexcept;

}  // namespace kvlang::runtimec2::rwir
