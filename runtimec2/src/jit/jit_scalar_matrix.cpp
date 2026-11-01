// Scalar JIT matrix implementation: typed kind + op -> abstract micro-op plan.
#include "jit_scalar_matrix.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace kvlang::runtimec2::jit2 {
namespace {

// 表序必须与 MicroOp 枚举一致：static_assert 守表长，info() 守索引。
// mnemonic 仅作文档；分派键只有 MicroOp 枚举值。
constexpr MicroOpInfo kCost[] = {
    {MicroOp::LoadS8, "movsx r64, byte", 4, 4},
    {MicroOp::LoadS16, "movsx r64, word", 5, 5},
    {MicroOp::LoadS32, "movsxd r64, dword", 4, 4},
    {MicroOp::LoadS64, "mov r64, qword", 4, 4},
    {MicroOp::LoadZ8, "movzx r64, byte", 4, 4},
    {MicroOp::LoadZ16, "movzx r64, word", 5, 5},
    {MicroOp::LoadZ32, "mov r32, dword", 4, 4},
    {MicroOp::LoadZ64, "mov r64, qword", 4, 4},
    {MicroOp::Store8, "mov byte [slot], r8", 3, 3},
    {MicroOp::Store16, "mov word [slot], r16", 4, 4},
    {MicroOp::Store32, "mov dword [slot], r32", 4, 4},
    {MicroOp::Store64, "mov qword [slot], r64", 4, 4},
    {MicroOp::Add64, "add r64, r64", 3, 3},
    {MicroOp::Sub64, "sub r64, r64", 3, 3},
    {MicroOp::Mul64, "imul r64, r64", 4, 4},
    {MicroOp::GuardDivZero, "test/jz div0", 7, 7},
    {MicroOp::GuardMinNeg1, "cmp -1; cmp INT_MIN; je wrap", 10, 10},
    {MicroOp::Cqo, "cqo", 2, 2},
    {MicroOp::IDiv64, "idiv r64", 3, 3},
    {MicroOp::XorEdx, "xor edx, edx", 2, 2},
    {MicroOp::Div64, "div r64", 3, 3},
    {MicroOp::MovRdxToRax, "mov rax, rdx", 3, 3},
    {MicroOp::CmpSigned, "cmp r64, r64", 3, 3},
    {MicroOp::CmpUnsigned, "cmp r64, r64", 3, 3},
    {MicroOp::SetCC, "setcc al; movzx eax, al", 5, 5},
    {MicroOp::Cmov, "cmovg/cmovl r64, r64", 4, 4},
    {MicroOp::And64, "and r64, r64", 3, 3},
    {MicroOp::Or64, "or r64, r64", 3, 3},
    {MicroOp::Xor64, "xor r64, r64", 3, 3},
    {MicroOp::Shl64, "shl r64, cl", 3, 3},
    {MicroOp::Sar64, "sar r64, cl", 3, 3},
    {MicroOp::Shr64, "shr r64, cl", 3, 3},
    {MicroOp::Neg64, "neg r64", 3, 3},
    {MicroOp::Abs64, "abs r64 (sar/xor/sub)", 8, 8},
    {MicroOp::Sign64, "sign r64 (cmp/setg/setl/sub)", 10, 10},
    {MicroOp::LoadF32, "movss xmm, dword", 4, 5},
    {MicroOp::LoadF64, "movsd xmm, qword", 5, 5},
    {MicroOp::StoreF32, "movss dword, xmm", 4, 5},
    {MicroOp::StoreF64, "movsd qword, xmm", 5, 5},
    {MicroOp::AddSS, "addss", 4, 4},
    {MicroOp::SubSS, "subss", 4, 4},
    {MicroOp::MulSS, "mulss", 4, 4},
    {MicroOp::DivSS, "divss", 4, 4},
    {MicroOp::AddSD, "addsd", 4, 4},
    {MicroOp::SubSD, "subsd", 4, 4},
    {MicroOp::MulSD, "mulsd", 4, 4},
    {MicroOp::DivSD, "divsd", 4, 4},
    {MicroOp::MaxSS, "maxss", 4, 4},
    {MicroOp::MinSS, "minss", 4, 4},
    {MicroOp::MaxSD, "maxsd", 4, 4},
    {MicroOp::MinSD, "minsd", 4, 4},
    {MicroOp::AndFloatSignMask, "andps sign-mask (+0)", 5, 5},
    {MicroOp::XorFloatSignBit, "xorps sign-bit", 5, 5},
    {MicroOp::SignFloat, "float sign (cmp/setg/setl)", 12, 12},
    {MicroOp::CmpFloatMask, "cmpeq/cmlt... mask", 7, 7},
    {MicroOp::AndOne, "and eax, 1", 3, 3},
    {MicroOp::ConvertSignedInt64ToFloat32, "cvtsi2ss r64", 5, 5},
    {MicroOp::ConvertSignedInt64ToFloat64, "cvtsi2sd r64", 5, 5},
    {MicroOp::ConvertUnsignedInt32ToFloat32, "cvtsi2ss r64 (zero-extended u32)", 5, 5},
    {MicroOp::ConvertUnsignedInt32ToFloat64, "cvtsi2sd r64 (zero-extended u32)", 5, 5},
    {MicroOp::ConvertUnsignedInt64ToFloat32, "u64->f32 special", 12, 16},
    {MicroOp::ConvertUnsignedInt64ToFloat64, "u64->f64 special", 12, 16},
    {MicroOp::ConvertFloat32ToFloat64, "cvtss2sd", 4, 4},
    {MicroOp::ConvertFloat64ToFloat32, "cvtsd2ss", 4, 4},
    {MicroOp::ConvertFloat32ToInt64Truncate, "cvttss2si r64", 5, 5},
    {MicroOp::ConvertFloat64ToInt64Truncate, "cvttsd2si r64", 5, 5},
};

constexpr std::size_t kCostRows = sizeof(kCost) / sizeof(kCost[0]);
static_assert(kCostRows == static_cast<std::size_t>(MicroOp::Count),
              "kCost must have exactly one row per MicroOp::Count");

const MicroOpInfo& info(MicroOp op) {
    const std::size_t index = static_cast<std::size_t>(op);
    if (index >= kCostRows) {
        std::fprintf(stderr,
                     "jit_scalar_matrix: MicroOp value %zu outside kCost "
                     "(%zu rows); enum and cost table are out of sync\n",
                     index, kCostRows);
        std::abort();
    }
    return kCost[index];
}

bool is_signed(ScalarTypeClass t) {
    return t == ScalarTypeClass::SI8 || t == ScalarTypeClass::SI16 ||
           t == ScalarTypeClass::SI32 || t == ScalarTypeClass::SI64;
}

bool is_unsigned(ScalarTypeClass t) {
    return t == ScalarTypeClass::UI8 || t == ScalarTypeClass::UI16 ||
           t == ScalarTypeClass::UI32 || t == ScalarTypeClass::UI64;
}

bool is_int(ScalarTypeClass t) { return is_signed(t) || is_unsigned(t); }

bool is_float(ScalarTypeClass t) {
    return t == ScalarTypeClass::F32 || t == ScalarTypeClass::F64;
}

int width_of(ScalarTypeClass t) {
    switch (t) {
    case ScalarTypeClass::SI8: case ScalarTypeClass::UI8: return 1;
    case ScalarTypeClass::SI16: case ScalarTypeClass::UI16: return 2;
    case ScalarTypeClass::SI32: case ScalarTypeClass::UI32: return 4;
    case ScalarTypeClass::SI64: case ScalarTypeClass::UI64: return 8;
    case ScalarTypeClass::F32: return 4;
    case ScalarTypeClass::F64: return 8;
    default: return 0;
    }
}

MicroOp load_int(ScalarTypeClass t) {
    if (is_signed(t)) {
        switch (width_of(t)) {
        case 1: return MicroOp::LoadS8;
        case 2: return MicroOp::LoadS16;
        case 4: return MicroOp::LoadS32;
        default: return MicroOp::LoadS64;
        }
    }
    switch (width_of(t)) {
    case 1: return MicroOp::LoadZ8;
    case 2: return MicroOp::LoadZ16;
    case 4: return MicroOp::LoadZ32;
    default: return MicroOp::LoadZ64;
    }
}

MicroOp store_int(ScalarTypeClass t) {
    switch (width_of(t)) {
    case 1: return MicroOp::Store8;
    case 2: return MicroOp::Store16;
    case 4: return MicroOp::Store32;
    default: return MicroOp::Store64;
    }
}

MicroOp load_float(ScalarTypeClass t) {
    return t == ScalarTypeClass::F32 ? MicroOp::LoadF32 : MicroOp::LoadF64;
}

MicroOp store_float(ScalarTypeClass t) {
    return t == ScalarTypeClass::F32 ? MicroOp::StoreF32 : MicroOp::StoreF64;
}

const char* tc_name(ScalarTypeClass t) {
    switch (t) {
    case ScalarTypeClass::SI8: return "si8";
    case ScalarTypeClass::SI16: return "si16";
    case ScalarTypeClass::SI32: return "si32";
    case ScalarTypeClass::SI64: return "si64";
    case ScalarTypeClass::UI8: return "ui8";
    case ScalarTypeClass::UI16: return "ui16";
    case ScalarTypeClass::UI32: return "ui32";
    case ScalarTypeClass::UI64: return "ui64";
    case ScalarTypeClass::F32: return "f32";
    case ScalarTypeClass::F64: return "f64";
    default: return "none";
    }
}

const char* op_name(KernelOp op) {
    switch (op) {
    case KernelOp::Add: return "add";
    case KernelOp::Sub: return "sub";
    case KernelOp::Mul: return "mul";
    case KernelOp::Div: return "div";
    case KernelOp::Mod: return "mod";
    case KernelOp::Max: return "max";
    case KernelOp::Min: return "min";
    case KernelOp::Abs: return "abs";
    case KernelOp::Neg: return "neg";
    case KernelOp::Sign: return "sign";
    case KernelOp::BitAnd: return "bitand";
    case KernelOp::BitOr: return "bitor";
    case KernelOp::BitXor: return "bitxor";
    case KernelOp::Shl: return "shl";
    case KernelOp::Shr: return "shr";
    case KernelOp::Eq: return "eq";
    case KernelOp::Neq: return "neq";
    case KernelOp::Lt: return "lt";
    case KernelOp::Le: return "le";
    case KernelOp::Gt: return "gt";
    case KernelOp::Ge: return "ge";
    case KernelOp::Cast: return "cast";
    default: return "?";
    }
}

bool is_compare(KernelOp op) {
    return op == KernelOp::Eq || op == KernelOp::Neq || op == KernelOp::Lt ||
           op == KernelOp::Le || op == KernelOp::Gt || op == KernelOp::Ge;
}

bool is_arith(KernelOp op) {
    return op == KernelOp::Add || op == KernelOp::Sub || op == KernelOp::Mul ||
           op == KernelOp::Div || op == KernelOp::Mod || op == KernelOp::Max ||
           op == KernelOp::Min;
}

bool is_bit(KernelOp op) {
    return op == KernelOp::BitAnd || op == KernelOp::BitOr ||
           op == KernelOp::BitXor;
}

bool is_shift(KernelOp op) {
    return op == KernelOp::Shl || op == KernelOp::Shr;
}

bool is_unary(KernelOp op) {
    return op == KernelOp::Abs || op == KernelOp::Neg || op == KernelOp::Sign;
}

}  // namespace

ScalarTypeClass parse_kindexpr(const char* kindexpr) noexcept {
    if (!kindexpr) return ScalarTypeClass::None;
    if (std::strcmp(kindexpr, "int8") == 0) return ScalarTypeClass::SI8;
    if (std::strcmp(kindexpr, "int16") == 0) return ScalarTypeClass::SI16;
    if (std::strcmp(kindexpr, "int32") == 0) return ScalarTypeClass::SI32;
    if (std::strcmp(kindexpr, "int64") == 0) return ScalarTypeClass::SI64;
    if (std::strcmp(kindexpr, "uint8") == 0) return ScalarTypeClass::UI8;
    if (std::strcmp(kindexpr, "uint16") == 0) return ScalarTypeClass::UI16;
    if (std::strcmp(kindexpr, "uint32") == 0) return ScalarTypeClass::UI32;
    if (std::strcmp(kindexpr, "uint64") == 0) return ScalarTypeClass::UI64;
    if (std::strcmp(kindexpr, "float32") == 0) return ScalarTypeClass::F32;
    if (std::strcmp(kindexpr, "float64") == 0) return ScalarTypeClass::F64;
    if (std::strcmp(kindexpr, "bool") == 0) return ScalarTypeClass::UI8;
    return ScalarTypeClass::None;
}

ScalarTypeClass promote(ScalarTypeClass lhs, ScalarTypeClass rhs) noexcept {
    if (lhs == ScalarTypeClass::None || rhs == ScalarTypeClass::None)
        return ScalarTypeClass::None;
    if (is_float(lhs) && is_float(rhs))
        return (lhs == ScalarTypeClass::F64 || rhs == ScalarTypeClass::F64)
                   ? ScalarTypeClass::F64 : ScalarTypeClass::F32;
    if (is_float(lhs)) return lhs;   // int + float -> the float width (C)
    if (is_float(rhs)) return rhs;
    if (!is_int(lhs) || !is_int(rhs)) return ScalarTypeClass::None;
    const int aw = width_of(lhs), bw = width_of(rhs);
    if (is_signed(lhs) == is_signed(rhs))
        return aw >= bw ? lhs : rhs;
    // 混合符号（int×uint）：spec §数值 promotion 不特化，运行时直接报错。
    return ScalarTypeClass::None;
}

bool typed_variant_exists(KernelOp op, ScalarTypeClass type) noexcept {
    if (type == ScalarTypeClass::None) return false;
    if (op == KernelOp::Cast) return true;   // all 10 targets
    if (is_arith(op)) {
        if (op == KernelOp::Mod) return is_int(type);
        return is_int(type) || is_float(type);
    }
    if (is_compare(op)) return is_int(type) || is_float(type);
    if (is_bit(op) || is_shift(op))
        return type == ScalarTypeClass::SI64 || type == ScalarTypeClass::UI64;
    if (op == KernelOp::Abs || op == KernelOp::Neg)
        return is_signed(type) || is_float(type);
    if (op == KernelOp::Sign)
        return type == ScalarTypeClass::SI64 || type == ScalarTypeClass::F64;
    return false;
}

ScalarPlan scalar_plan(KernelOp op, ScalarTypeClass lhs, ScalarTypeClass rhs,
                       ScalarTypeClass result) noexcept {
    static thread_local MicroOp buf[24];
    int n = 0;
    auto push = [&](MicroOp m) { if (n < 24) buf[n++] = m; };
    auto reject = [&](const char* reason) {
        ScalarPlan p{};
        p.leaf = op_name(op);
        p.reject_reason = reason;
        return p;
    };
    if (lhs == ScalarTypeClass::None || result == ScalarTypeClass::None)
        return reject("unknown kind");
    if (op == KernelOp::Cast) {
        if (is_int(lhs) && is_int(result)) {
            push(load_int(lhs));
            push(store_int(result));
        } else if (is_int(lhs) && is_float(result)) {
            push(load_int(lhs));
            if (lhs == ScalarTypeClass::UI64)
                push(result == ScalarTypeClass::F32
                         ? MicroOp::ConvertUnsignedInt64ToFloat32
                         : MicroOp::ConvertUnsignedInt64ToFloat64);
            else if (is_unsigned(lhs))
                push(result == ScalarTypeClass::F32
                         ? MicroOp::ConvertUnsignedInt32ToFloat32
                         : MicroOp::ConvertUnsignedInt32ToFloat64);
            else
                push(result == ScalarTypeClass::F32
                         ? MicroOp::ConvertSignedInt64ToFloat32
                         : MicroOp::ConvertSignedInt64ToFloat64);
            push(store_float(result));
        } else if (is_float(lhs) && is_int(result)) {
            // Always truncate to int64, then write the target width: the
            // interpreter computes (int64_t)src.f and truncates on the store.
            // A 32-bit-target conversion would differ for out-of-range si32
            // (INT32_MIN vs the low 32 bits of INT64_MIN, i.e. 0).
            push(load_float(lhs));
            push(lhs == ScalarTypeClass::F32
                     ? MicroOp::ConvertFloat32ToInt64Truncate
                     : MicroOp::ConvertFloat64ToInt64Truncate);
            push(store_int(result));
        } else if (is_float(lhs) && is_float(result)) {
            push(load_float(lhs));
            if (lhs != result)
                push(lhs == ScalarTypeClass::F32
                         ? MicroOp::ConvertFloat32ToFloat64
                         : MicroOp::ConvertFloat64ToFloat32);
            push(store_float(result));
        } else {
            return reject("cast source not scalar");
        }
    } else {
        if (!typed_variant_exists(op, lhs) || (!is_compare(op) && lhs != rhs))
            return reject("no typed variant / operand mismatch");
        if (is_int(lhs)) {
            if (is_compare(op)) {
                push(load_int(lhs));
                push(load_int(lhs));
                push(is_signed(lhs) ? MicroOp::CmpSigned : MicroOp::CmpUnsigned);
                push(MicroOp::SetCC);
                push(MicroOp::Store8);
            } else if (op == KernelOp::Div || op == KernelOp::Mod) {
                push(load_int(lhs));
                push(load_int(lhs));
                push(MicroOp::GuardDivZero);
                if (is_signed(lhs)) {
                    push(MicroOp::GuardMinNeg1);
                    push(MicroOp::Cqo);
                    push(MicroOp::IDiv64);
                } else {
                    push(MicroOp::XorEdx);
                    push(MicroOp::Div64);
                }
                if (op == KernelOp::Mod) push(MicroOp::MovRdxToRax);
                push(store_int(result));
            } else if (is_bit(op) || is_shift(op)) {
                push(load_int(lhs));
                push(load_int(lhs));
                if (op == KernelOp::BitAnd) push(MicroOp::And64);
                else if (op == KernelOp::BitOr) push(MicroOp::Or64);
                else if (op == KernelOp::BitXor) push(MicroOp::Xor64);
                else if (op == KernelOp::Shl) push(MicroOp::Shl64);
                else push(is_signed(lhs) ? MicroOp::Sar64 : MicroOp::Shr64);
                push(store_int(result));
            } else if (is_unary(op)) {
                push(load_int(lhs));
                if (op == KernelOp::Neg) push(MicroOp::Neg64);
                else if (op == KernelOp::Abs) push(MicroOp::Abs64);
                else push(MicroOp::Sign64);
                push(store_int(result));
            } else {  // add/sub/mul/div/max/min
                push(load_int(lhs));
                push(load_int(lhs));
                if (op == KernelOp::Add) push(MicroOp::Add64);
                else if (op == KernelOp::Sub) push(MicroOp::Sub64);
                else if (op == KernelOp::Mul) push(MicroOp::Mul64);
                else {
                    // max/min 的 compare 必须按操作数符号性选；Cmov 的
                    // 条件方向（max/min + signed/unsigned）由选择层传入。
                    push(is_signed(lhs) ? MicroOp::CmpSigned
                                        : MicroOp::CmpUnsigned);
                    push(MicroOp::Cmov);
                }
                push(store_int(result));
            }
        } else if (is_float(lhs)) {
            if (is_compare(op)) {
                push(load_float(lhs));
                push(load_float(lhs));
                push(MicroOp::CmpFloatMask);
                push(MicroOp::AndOne);
                push(MicroOp::Store8);
            } else if (is_arith(op)) {
                if (op == KernelOp::Mod) return reject("float mod unsupported");
                push(load_float(lhs));
                push(load_float(lhs));
                if (op == KernelOp::Add)
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::AddSS : MicroOp::AddSD);
                else if (op == KernelOp::Sub)
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::SubSS : MicroOp::SubSD);
                else if (op == KernelOp::Mul)
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::MulSS : MicroOp::MulSD);
                else if (op == KernelOp::Div)
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::DivSS : MicroOp::DivSD);
                else if (op == KernelOp::Max)
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::MaxSS : MicroOp::MaxSD);
                else
                    push(lhs == ScalarTypeClass::F32 ? MicroOp::MinSS : MicroOp::MinSD);
                push(store_float(result));
            } else if (is_unary(op)) {
                push(load_float(lhs));
                if (op == KernelOp::Abs) push(MicroOp::AndFloatSignMask);
                else if (op == KernelOp::Neg) push(MicroOp::XorFloatSignBit);
                else push(MicroOp::SignFloat);
                push(store_float(result));
            } else {
                return reject("bit/shift unsupported on float");
            }
        }
    }
    if (n == 0) return reject("empty plan");
    ScalarPlan p{};
    p.ops = buf;
    p.count = static_cast<std::uint8_t>(n);
    p.ok = true;
    char name[64];
    std::snprintf(name, sizeof(name), "%s/%s", op_name(op), tc_name(lhs));
    // Leaf name is static per call site in the test; use a fixed pool here.
    static thread_local char leaf_name_buf[64];
    std::snprintf(leaf_name_buf, sizeof(leaf_name_buf), "%s", name);
    p.leaf = leaf_name_buf;
    for (int i = 0; i < n; i++) {
        const MicroOpInfo& mi = info(buf[i]);
        p.bytes_min = static_cast<std::uint8_t>(p.bytes_min + mi.bytes_min);
        p.bytes_max = static_cast<std::uint8_t>(p.bytes_max + mi.bytes_max);
    }
    return p;
}

std::size_t leaf_count() noexcept { return static_cast<std::size_t>(KernelOp::Count) * 10; }

const char* leaf_name(std::size_t index) noexcept {
    static const char* kTypes[] = {"si8", "si16", "si32", "si64", "ui8",
                                   "ui16", "ui32", "ui64", "f32", "f64"};
    static char buf[64];
    const std::size_t op = index / 10, t = index % 10;
    if (op >= static_cast<std::size_t>(KernelOp::Count)) return "";
    KernelOp k = static_cast<KernelOp>(op);
    std::snprintf(buf, sizeof(buf), "%s/%s", op_name(k), kTypes[t]);
    return buf;
}

}  // namespace kvlang::runtimec2::jit2
