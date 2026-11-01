#pragma once

#include "codeseg.hpp"

namespace kvlang::runtimec2 {

// B2：为段内 Impl::Dynamic 指令派生 int64 快路径描述（Segment::derived）。
// 判据与 typed 行一致：二元 1 写，读参静态类型全为 int64（字面量 is_i64 或
// 槽 langtype=Int64/width=8），算子属于 int64_fast_opfn 的 1..18。
// 任何一条不满足 → opfn=0（执行侧继续走通用动态路径）。
void derive_dispatch(Segment* seg);

}  // namespace kvlang::runtimec2
