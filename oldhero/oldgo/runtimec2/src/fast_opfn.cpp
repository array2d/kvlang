#include "fast_opfn.hpp"

namespace kvlang::runtimec2::rwir {

std::uint8_t int64_fast_opfn(Op op) noexcept {
    switch (op) {
    case Op::Add: return 1;
    case Op::Sub: return 2;
    case Op::Mul: return 3;
    case Op::Div: return 4;
    case Op::Mod: return 5;
    case Op::Max: return 6;
    case Op::Min: return 7;
    case Op::BitAnd: return 8;
    case Op::BitOr: return 9;
    case Op::BitXor: return 10;
    case Op::Shl: return 11;
    case Op::Shr: return 12;
    case Op::Eq: return 13;
    case Op::Neq: return 14;
    case Op::Lt: return 15;
    case Op::Le: return 16;
    case Op::Gt: return 17;
    case Op::Ge: return 18;
    default: return 0;
    }
}

}  // namespace kvlang::runtimec2::rwir
