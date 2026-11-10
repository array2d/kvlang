#pragma once

#include <cstdint>

#include "rwir.hpp"

// 语义算子的参数/结果 union 签名（阶段 A）。
//
// 唯一书面定义：stdlib/kvlang/spec/05-runtime语义/11-指令派发与特化.kv
//   §语义算子参数 union / §数值 promotion。
// 本表是它的唯一实现转录：layout 的 specialize、binircompiler 的编译期校验、
// 标量矩阵、解释器提升与即时编译都按这张表；任一侧不得有第二份规则。
namespace kvlang::runtimec2::rwir {

enum class ScalarClass : std::uint8_t {
    None = 0,
    SI8, SI16, SI32, SI64,
    UI8, UI16, UI32, UI64,
    F32, F64,
    Bool,
    Char,
    Count,
};

using TypeSet = std::uint16_t;

constexpr TypeSet type_bit(ScalarClass c) {
    return static_cast<TypeSet>(1u << static_cast<unsigned>(c));
}

constexpr TypeSet kSignedInts =
    type_bit(ScalarClass::SI8) | type_bit(ScalarClass::SI16) |
    type_bit(ScalarClass::SI32) | type_bit(ScalarClass::SI64);
constexpr TypeSet kUnsignedInts =
    type_bit(ScalarClass::UI8) | type_bit(ScalarClass::UI16) |
    type_bit(ScalarClass::UI32) | type_bit(ScalarClass::UI64);
constexpr TypeSet kFloats = type_bit(ScalarClass::F32) | type_bit(ScalarClass::F64);
constexpr TypeSet kInts = kSignedInts | kUnsignedInts;
constexpr TypeSet kNumeric = kInts | kFloats;
constexpr TypeSet kAny = kNumeric | type_bit(ScalarClass::Bool) |
                         type_bit(ScalarClass::Char);

struct OpSignature {
    TypeSet operands = 0;      // 每个操作数都按此集合校验
    std::uint8_t min_arity = 2;
    std::uint8_t max_arity = 2;  // 0 = 变长
    enum class Result : std::uint8_t {
        Promoted,  // 按 §数值 promotion
        Bool,
        Float64,   // sqrt/exp/log/pow 恒 float64
        Same,      // 同操作数 class
        Target,    // cast：由目标 kind 决定
    };
    Result result = Result::Promoted;
};

extern const OpSignature kSigArith;          // add/mul/div/max/min，2 参
extern const OpSignature kSigArithOrNeg;     // sub，1..2 参（一元即 neg）
extern const OpSignature kSigMod;            // 整型
extern const OpSignature kSigBit;            // 位/移位，仅 64 位
extern const OpSignature kSigNegAbs;         // 有符号整数 + 浮点
extern const OpSignature kSigSign;           // SI64 / F64
extern const OpSignature kSigFloat64Unary;   // sqrt/exp/log
extern const OpSignature kSigFloat64Binary;  // pow
extern const OpSignature kSigCompare;        // 比较，结果 bool
extern const OpSignature kSigBoolUnary;      // not
extern const OpSignature kSigBoolBinary;     // and/or
extern const OpSignature kSigCast;           // 类型构造器

// 按 Op 查族签名（typed 行同样适用）；控制流/无签名算子返回 nullptr。
const OpSignature* signature_of(Op op) noexcept;

}  // namespace kvlang::runtimec2::rwir
