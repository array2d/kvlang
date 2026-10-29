#include "jit.hpp"

#include <vector>

// ARM64 JIT 发射器（框架阶段，M5 双平台收口时补全）。
// 当前返回空 = 该平台不可发射，jit_compile 缓存 nullptr、回退 fast kernel 解释
// 路径——与 x86_64 未命中同一语义（jitable 判定通过但发射失败）。
// 由 CMake 按 arch 选择编译（emit_arch 单一定义）；ARM64 机器上才编译本文件。
//
// 实现时对齐 JitShape：src_idx（读参，X0）/ dst_idx（写参，结果），
// guard 计数寄存器 + err_out 指针（X1），add/比较/copy/br/return 的 AArch64
// 指令编码（32 位定长），rel 跳转（B.cond/B）。

namespace kv::rt {

std::vector<std::uint8_t> emit_arch(const Segment* seg, const JitShape& sh) {
    (void)seg;
    (void)sh;
    // TODO(M5): ARM64 发射器——AArch64 32 位定长指令 + rel 跳转。
    return {};
}

}  // namespace kv::rt
