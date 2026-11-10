#include "codeseg.hpp"

#include <string>

#include "binircompiler/compiler.hpp"
#include "jit.hpp"

namespace kvlang::runtimec2 {

bool build_segment(void* h, const std::string& func_dir, Segment* out) {
    binircompiler::CompileError error;
    return binircompiler::compile(h, func_dir, binircompiler::CompileOptions{},
                                  out, &error);
}

const Segment* cached_segment(void* h, const std::string& func_dir) {
    const std::uint64_t before = binircompiler::relayout_epoch();
    const Segment* seg = binircompiler::cached(h, func_dir);
    if (binircompiler::relayout_epoch() != before)
        jit_clear_cache();
    return seg;
}

bool cached_insn(const std::string& func_dir, int s0, const CInsn** out,
                 const Segment** seg_out) {
    const Segment* seg = binircompiler::lookup(func_dir);
    if (!seg)
        return false;
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

std::string take_last_segment_error() {
    return binircompiler::take_last_error();
}

void bump_segment_epoch() noexcept {
    binircompiler::bump_epoch();
}

std::uint64_t current_segment_epoch() noexcept {
    return binircompiler::current_epoch();
}

bool segment_may_stop(void* h, const Segment* seg) {
    return binircompiler::segment_may_stop(h, seg);
}

const char* op_name(std::uint16_t id) {
    if (id < static_cast<std::uint16_t>(rwir::kMyrwircapsN))
        return rwir::myrwircaps[id].name;
    return id == static_cast<std::uint16_t>(rwir::kMyrwircapsN) ? "<ext>"
                                                                : "<unknown>";
}

std::size_t op_name_count() { return static_cast<std::size_t>(rwir::kMyrwircapsN) + 1; }

}  // namespace kvlang::runtimec2
