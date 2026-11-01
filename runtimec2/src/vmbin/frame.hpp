#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <kvspace/kvspace.h>

#include "value.hpp"
#include "vmbin/program.hpp"

namespace kvlang::runtimec2::vmbin {

struct PcBin {
    std::uint32_t frame_id;
    std::uint32_t s0;
};

struct SlotOff {
    std::int32_t s0;
    std::int8_t axis;
};

struct FrameKey {
    char base[64];
    std::uint16_t base_len;
    std::uint32_t vid;
    std::uint32_t depth;
};

struct Register {
    std::string key;
    std::string desc;
    const RegisterDesc* desc_ref = nullptr;
    kvspaceRef_t ref{};
    bool resolved = false;
    bool noslot = false;
    kvspaceXvalueBodyView_t view{};
    bool view_ok = false;
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    bool sc_ok = false;
    Scalar::K sck = Scalar::K::None;
    std::uint8_t scw = 0;
    bool fast_i64 = false;
    std::int64_t fv = 0;
    bool fast_bool = false;
    bool fb = false;
    Register* src_ptr = nullptr;
    Register* dst_ptr = nullptr;
    bool base_ptr = false;
    bool base_ptr_known = false;
    // 显式槽状态（成员读/写快路径用，避免每写一次 read_langtype）：
    //   Absent  = 纯内存写（int64 快路径，kvspace 无槽）；
    //   Present = 对该 key 的 kvspace 写成功过；
    //   Unknown = 默认，保守查询。key 变更 / 帧复用 / 外部失效必须回 Unknown。
    // 落进两个 bool 之后的既有 padding：不增大 Register（帧数组内存敏感）。
    enum class SlotState : std::uint8_t { Unknown = 0, Absent, Present };
    SlotState slot_state = SlotState::Unknown;
    std::uint32_t arr_esz = 0;
    std::int32_t arr_dims[8] = {};
    std::uint32_t arr_ndim = 0;
    std::uint8_t arr_kind = 0;
    std::uint64_t arr_bodylen = 0;
    std::string arr_elem_kind;
};

// handler 直写 kvspace 后同步槽状态：写的正是本寄存器自己的 key → Present
// （槽已真实存在，后续写须查类型）；派生 key（成员/动态键）不动寄存器自身状态。
inline void note_slot_write(Register& w, const char* key) noexcept {
    if (w.slot_state != Register::SlotState::Absent)
        return;
    if (key && *key && w.key == key)
        w.slot_state = Register::SlotState::Present;
}

struct Frame {
    Frame* parent = nullptr;
    void* h = nullptr;
    std::string vid;
    int depth = 0;
    std::string frame;
    std::string func_dir;
    std::string frame_dir_hint;
    std::vector<Register> regs;
    Register* arg_src[16] = {};
    int arg_src_n = 0;
    Register* arg_dst[16] = {};
    int arg_dst_n = 0;
    bool mem_frame = false;
    const Program* seg_hint = nullptr;
    const void* jit_hint = nullptr;
    bool jit_known = false;
    bool observable = false;
    bool skip_enter_frame = false;  // JIT Native 出口后从 pc 续跑，不重置帧
    const void* jit2_entry = nullptr;  // callee 的 JIT2 产物（CallSite 缓存）
};

template <std::uint8_t O>
inline std::int64_t fast_i64(std::int64_t a, std::int64_t b) {
    if constexpr (O == 1) return a + b;
    if constexpr (O == 2) return a - b;
    if constexpr (O == 3) return a * b;
    if constexpr (O == 4) return b ? a / b : 0;
    if constexpr (O == 5) return b ? a % b : 0;
    if constexpr (O == 6) return a > b ? a : b;
    if constexpr (O == 7) return a < b ? a : b;
    if constexpr (O == 8) return a & b;
    if constexpr (O == 9) return a | b;
    if constexpr (O == 10) return a ^ b;
    if constexpr (O == 11) return static_cast<std::int64_t>(
        static_cast<std::uint64_t>(a) << (b & 63));
    if constexpr (O == 12) return a >> (b & 63);
    if constexpr (O == 13) return a == b ? 1 : 0;
    if constexpr (O == 14) return a != b ? 1 : 0;
    if constexpr (O == 15) return a < b ? 1 : 0;
    if constexpr (O == 16) return a <= b ? 1 : 0;
    if constexpr (O == 17) return a > b ? 1 : 0;
    if constexpr (O == 18) return a >= b ? 1 : 0;
    return 0;
}

}  // namespace kvlang::runtimec2::vmbin
