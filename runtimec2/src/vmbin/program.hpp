#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "langtype.hpp"
#include "rwir.hpp"

namespace kvlang::runtimec2::vmbin {

struct Operand {
    enum class Kind : std::uint8_t { None = 0, Literal, Var } kind = Kind::None;
    std::string name;
    std::string slot_key;
};

struct RegisterDesc {
    Operand::Kind kind = Operand::Kind::None;
    std::string name;
    std::string slot_key;
    int param_k = 0;
    enum class VarClass : std::uint8_t { None = 0, ParamPtr, Ptr, Plain, Typed } vclass =
        VarClass::None;
    bool member = false;
    // E 层静态类型（#583 L3）：建段期从操作数槽类型串/字面量 attr/rwfunc 参数/
    // typed op 写入；None = 未知，运行期必须回退 kvspaceGetHead。只缓存标量
    // kind 与宽度，不缓存 dims；数组/容器/指针保持 None。
    // 命名用 langtype（kind 已被 Operand::Kind 占用）。
    LangType langtype = LangType::None;
    std::uint8_t width = 0;
};

// +2B 必须落进尾部 padding、不增大结构（Linux/libstdc++ 80B；macOS/libc++ 64B）。
#if defined(__GLIBCXX__) || defined(_GLIBCXX_RELEASE)
static_assert(sizeof(RegisterDesc) == 80,
              "RegisterDesc must stay 80 bytes on libstdc++");
#elif defined(_LIBCPP_VERSION)
static_assert(sizeof(RegisterDesc) == 64,
              "RegisterDesc must stay 64 bytes on libc++");
#endif

struct Program;

struct Instruction {
    rwir::Op op = rwir::Op::None;
    rwir::Kind kind = rwir::Kind::None;
    rwir::Impl impl = rwir::Impl::None;
    rwir::RwirHandlerId handler = rwir::RwirHandlerId::None;
    std::uint8_t width = 0;
    std::uint8_t opfn = 0;
    bool fast_i64 = false;
    std::uint8_t nrd = 0, nwr = 0;
    std::uint8_t rd[16] = {};
    std::uint8_t wr[4] = {};
    bool wr_local = false;
    std::uint16_t prof_id = 0;
    int s0 = 0;
    std::uint16_t rwir_id = 0xFFFF;    // rwir::RwirId（表下标；≥0x8000 = extra 池）
    std::uint16_t callee_id = 0xFFFF;  // Program::callees 池下标（仅 call 指令）
    mutable const Program* call_seg = nullptr;
    mutable const void* call_jit = nullptr;
    mutable bool call_ready = false;
    mutable std::vector<std::string> call_rtyp;
    mutable std::vector<std::string> call_wtyp;
    mutable std::vector<std::uint8_t> call_rtyp_ok, call_wtyp_ok;
    mutable int mw_keyreg = -2, mw_basereg = -2;
};

struct FastLiteral {
    bool is_i64 = false;
    std::int64_t v = 0;
};

struct Program {
    std::vector<Instruction> insns;
    std::vector<RegisterDesc> regs;
    std::vector<FastLiteral> lit;
    std::vector<std::string> callees;          // call 目标路径池（callee_id）
    std::vector<std::string> extra_rwir_names;  // 非 myrwircaps 条目（扩展算子）
    bool has_rwirhandler = false;
};

// 扩展算子 id 段：rwir_id ≥ 本值 = extra_rwir_names 池下标（表内 id 恒 < 0x8000）。
inline constexpr std::uint16_t kRwirIdExtraBase = 0x8000;

// 指令 rwir 名：表内 id 查 myrwircaps，扩展 id 查段内 extra 池（仅诊断/分派用）。
inline const char* instruction_name(const Program& seg,
                                    const Instruction& ins) noexcept {
    if (ins.rwir_id < kRwirIdExtraBase)
        return rwir::rwir_name(ins.rwir_id);
    const std::size_t index = ins.rwir_id - kRwirIdExtraBase;
    return index < seg.extra_rwir_names.size()
               ? seg.extra_rwir_names[index].c_str()
               : "<ext>";
}

inline bool instruction_is(const Instruction& ins, rwir::RwirId id) noexcept {
    return ins.rwir_id == static_cast<std::uint16_t>(id);
}

// call 目标路径（建段期已预解析为最终 /lib/…）。
inline const std::string& instruction_callee(const Program& seg,
                                             const Instruction& ins) noexcept {
    static const std::string kEmpty;
    return ins.callee_id < seg.callees.size() ? seg.callees[ins.callee_id]
                                              : kEmpty;
}

}  // namespace kvlang::runtimec2::vmbin
