#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
};

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
    std::string name;
    std::string callee;
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
    bool has_rwirhandler = false;
};

}  // namespace kvlang::runtimec2::vmbin
