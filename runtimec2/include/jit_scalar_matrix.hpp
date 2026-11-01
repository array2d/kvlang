// Scalar JIT matrix: typed kind + op family -> abstract machine sequence.
//
// 来源：feat/583-stage7 存档提交 45add7a3（jit_scalar_matrix.{hpp,cpp} 与单测）。
// 当前只入库、未接线：vmkv / jit_stage2 都不调用本表。阶段 B 才接线
// （解释器按 (opcode, RegDesc.langtype) 选实现，JIT 用同一张表发射）。
//
// Dispatch contract: MicroOp enum values are the only emission key.
// MicroOpInfo.mnemonic is documentation/diagnostics only; no emission path may
// switch or match on the mnemonic string (that would be a hidden branch).
//
// This file is standalone: it does not touch jit_stage2.cpp.
#pragma once

#include <cstdint>

#include "rwir_signature.hpp"

namespace kvlang::runtimec2::jit2 {

// 与 rwir 的 ScalarClass 是同一个类型（单一枚举，不复制）。
using ScalarTypeClass = rwir::ScalarClass;

enum class KernelOp : std::uint8_t {
    Add = 0, Sub, Mul, Div, Mod, Max, Min, Abs, Neg, Sign,
    BitAnd, BitOr, BitXor, Shl, Shr,
    Eq, Neq, Lt, Le, Gt, Ge,
    Cast,
    Count,
};

enum class MicroOp : std::uint8_t {
    LoadS8, LoadS16, LoadS32, LoadS64,
    LoadZ8, LoadZ16, LoadZ32, LoadZ64,
    Store8, Store16, Store32, Store64,
    Add64, Sub64, Mul64,
    // 除法/取模组是 x86 专属：Cqo/IDiv64/XorEdx/Div64/MovRdxToRax 直接操作
    // RAX/RDX。AArch64 需要独立的 ScalarPlan（sdiv/udiv + msub 求余），
    // 不得把这张表原样套到 ARM 发射器；当前唯一活的发射器是 x86。
    GuardDivZero, GuardMinNeg1, Cqo, IDiv64, XorEdx, Div64, MovRdxToRax,
    CmpSigned, CmpUnsigned, SetCC, Cmov,
    And64, Or64, Xor64, Shl64, Sar64, Shr64,
    Neg64, Abs64, Sign64,
    LoadF32, LoadF64, StoreF32, StoreF64,
    AddSS, SubSS, MulSS, DivSS, AddSD, SubSD, MulSD, DivSD,
    MaxSS, MinSS, MaxSD, MinSD,
    AndFloatSignMask, XorFloatSignBit, SignFloat, CmpFloatMask, AndOne,
    ConvertSignedInt64ToFloat32, ConvertSignedInt64ToFloat64,
    ConvertUnsignedInt32ToFloat32, ConvertUnsignedInt32ToFloat64,
    ConvertUnsignedInt64ToFloat32, ConvertUnsignedInt64ToFloat64,
    ConvertFloat32ToFloat64, ConvertFloat64ToFloat32,
    // float→int 一律先按 64 位截断（cvttss2si/cvttsd2si r64），再由
    // store_int(result) 按目标宽度截断写回。不要用 32 位目标截断：
    // 越界/NaN 时 cvttsd2si r32 得 INT32_MIN，而解释器是 INT64_MIN 的低
    // 32 位（0），si32 目标会不一致。
    ConvertFloat32ToInt64Truncate, ConvertFloat64ToInt64Truncate,
    Count,
};

struct MicroOpInfo {
    MicroOp op;
    const char* mnemonic;
    std::uint8_t bytes_min;
    std::uint8_t bytes_max;
};

struct ScalarPlan {
    const MicroOp* ops = nullptr;
    std::uint8_t count = 0;
    const char* leaf = "";
    std::uint8_t bytes_min = 0;
    std::uint8_t bytes_max = 0;
    bool ok = false;
    const char* reject_reason = nullptr;
};

// kindexpr string ("int64", "float32", ...) -> type class.  Bool maps to UI8
// for comparisons/bit ops; arithmetic callers must reject bool themselves.
ScalarTypeClass parse_kindexpr(const char* kindexpr) noexcept;

// 数值 promotion：唯一书面定义在
// stdlib/kvlang/spec/05-runtime语义/11-指令派发与特化.kv §数值 promotion。
// int×uint 混合不特化（返回 None，运行时直接报错）；结果宽度与操作数顺序无关。
// bool is not a numeric kind: returns None for any bool arithmetic operand.
ScalarTypeClass promote(ScalarTypeClass lhs, ScalarTypeClass rhs) noexcept;

// Build the abstract sequence for a typed variant.  `lhs`/`rhs` are the
// operand type classes; `result` is the write-slot class (used for casts and
// for the store width).  Rejected cells return ok=false with a reason.
ScalarPlan scalar_plan(KernelOp op, ScalarTypeClass lhs, ScalarTypeClass rhs,
                       ScalarTypeClass result) noexcept;

// Unique leaf name / sequence table (for the unit test and diagnostics).
std::size_t leaf_count() noexcept;
const char* leaf_name(std::size_t index) noexcept;

// Coverage helper: does the myrwircap have this typed variant?  Mirrors the
// 166 Impl::Typed rows in rwir_gen.cpp (casts are handled separately).
bool typed_variant_exists(KernelOp op, ScalarTypeClass type) noexcept;

}  // namespace kvlang::runtimec2::jit2
