#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
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

struct Instruction {                    // 34B（align 2）
    std::uint16_t rwir_id = 0xFFFF;    // 0  rwir::RwirId（表下标；≥0x8000 = extra 池）
    rwir::RwirHandlerId handler = rwir::RwirHandlerId::None;  // 2
    std::uint16_t call_site = 0xFFFF;  // 4  Program::call_sites 下标（仅 call）
    rwir::Op op = rwir::Op::None;      // 6
    rwir::Kind kind = rwir::Kind::None;  // 7
    rwir::Impl impl = rwir::Impl::None;  // 8
    std::uint8_t width = 0;            // 9
    std::uint8_t opfn = 0;             // 10  fast kernel 跳表号
    std::uint8_t fast_i64 : 1 = 0;     // 11  int64 快路
    std::uint8_t wr_local : 1 = 0;     //     写参帧局部
    std::uint8_t nrd = 0;              // 12
    std::uint8_t nwr = 0;              // 13
    std::uint8_t rd[16] = {};          // 14..29
    std::uint8_t wr[4] = {};           // 30..33
};

static_assert(sizeof(Instruction) == 34,
              "Instruction must stay 34 bytes (slim decode view)");

struct FastLiteral {
    bool is_i64 = false;
    std::int64_t v = 0;
};

// 调用点缓存：一条 call 指令一项（其余指令不占内存）。
struct CallSite {
    std::uint16_t callee_id = 0xFFFF;  // Program::callees 池下标
    const Program* segment = nullptr;  // callee 段
    const void* jit = nullptr;         // callee JIT 入口
    bool ready = false;
    std::vector<std::string> read_types, write_types;
    std::vector<std::uint8_t> read_types_ok, write_types_ok;
};

struct Program {
    std::vector<Instruction> insns;
    std::vector<RegisterDesc> regs;
    std::vector<FastLiteral> lit;
    std::vector<std::string> callees;          // call 目标路径池（callee_id）
    std::vector<std::string> extra_rwir_names;  // 非 myrwircaps 条目（扩展算子）
    mutable std::vector<CallSite> call_sites;  // 调用点缓存（call_site 下标）
    // 成员写缓存：指令下标 → key/base 寄存器；-2 = 未解析。
    struct MemberWriteCache {
        std::int16_t key_register = -2;
        std::int16_t base_register = -2;
    };
    mutable std::unordered_map<std::uint32_t, MemberWriteCache> member_write_cache;
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

// 指令 pc = 下标 + 1：与原 s0 不变量一致（kvspace pc 路径 [s0,·]）。
inline int instruction_pc(std::size_t index) noexcept {
    return static_cast<int>(index) + 1;
}

// PROF 统计键：表内 rwir 用其 id；扩展算子归入 <ext> 桶（kMyrwircapsN）。
inline std::uint16_t instruction_prof_id(const Instruction& ins) noexcept {
    return ins.rwir_id < kRwirIdExtraBase
               ? ins.rwir_id
               : static_cast<std::uint16_t>(rwir::kMyrwircapsN);
}

inline bool instruction_is(const Instruction& ins, rwir::RwirId id) noexcept {
    return ins.rwir_id == static_cast<std::uint16_t>(id);
}

// call 目标路径（建段期已预解析为最终 /lib/…；存于调用点表）。
inline const std::string& instruction_callee(const Program& seg,
                                             const Instruction& ins) noexcept {
    static const std::string kEmpty;
    if (ins.call_site >= seg.call_sites.size())
        return kEmpty;
    const std::uint16_t callee_id = seg.call_sites[ins.call_site].callee_id;
    return callee_id < seg.callees.size() ? seg.callees[callee_id] : kEmpty;
}

}  // namespace kvlang::runtimec2::vmbin
