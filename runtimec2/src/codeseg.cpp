#include "codeseg.hpp"

#include <string>

#include "binircompiler/compiler.hpp"
#include "jit.hpp"

// vmkv 面向的段接口：全部转发到 binircompiler 编译模块。
// 编译逻辑（建段、binir 编码/解码视图、段缓存）只在 binircompiler；
// 运行时段缓存与 JIT 缓存在此一并清理（JIT 属执行侧，不迁进编译模块）。
namespace kvlang::runtimec2 {

bool build_segment(void* h, const std::string& func_dir, Segment* out) {
    binircompiler::CompileError error;
    return binircompiler::compile(h, func_dir, binircompiler::CompileOptions{},
                                  out, &error);
}

const Segment* cached_segment(void* h, const std::string& func_dir) {
    return binircompiler::cached(h, func_dir);
}

bool cached_insn(const std::string& func_dir, int s0, const CInsn** out,
                 const Segment** seg_out) {
    const Segment* seg = binircompiler::lookup(func_dir);
    if (!seg)
        return false;  // 未缓存：交给调用方走 cached_segment 补建
    const auto i = s0 - 1;
    if (i < 0 || i >= static_cast<int>(seg->insns.size()))
        return false;
    if (out)
        *out = &seg->insns[static_cast<std::size_t>(i)];
    if (seg_out)
        *seg_out = seg;
    return true;
}

void clear_segment_cache() {
    binircompiler::clear_cache();
    jit_clear_cache();
}

const char* op_name(std::uint16_t id) {
    if (id < static_cast<std::uint16_t>(rwir::kMyrwircapsN))
        return rwir::myrwircaps[id].name;
    return id == static_cast<std::uint16_t>(rwir::kMyrwircapsN) ? "<ext>"
                                                                : "<unknown>";
}

std::size_t op_name_count() { return static_cast<std::size_t>(rwir::kMyrwircapsN) + 1; }

}  // namespace kvlang::runtimec2
