/* myrwircap 唯一事实源：runtimec2 支持的全部 rwir。表即代码，手写维护。 */
#include "rwir.hpp"
#include "rwir_signature.hpp"

namespace kvlang::runtimec2::rwir {

// 语义算子签名：spec §语义算子参数 union 的唯一转录。
const OpSignature kSigArith{kNumeric, 2, 2, OpSignature::Result::Promoted};
const OpSignature kSigArithOrNeg{kNumeric, 1, 2, OpSignature::Result::Promoted};
const OpSignature kSigMod{kInts, 2, 2, OpSignature::Result::Promoted};
const OpSignature kSigMinMax{kNumeric, 2, 0, OpSignature::Result::Promoted};
const OpSignature kSigBit{kInts, 2, 2, OpSignature::Result::Promoted};
const OpSignature kSigNegAbs{kNumeric, 1, 1, OpSignature::Result::Same};
const OpSignature kSigSign{kNumeric, 1, 1, OpSignature::Result::Same};
const OpSignature kSigFloat64Unary{kNumeric, 1, 1, OpSignature::Result::Float64};
const OpSignature kSigFloat64Binary{kNumeric, 2, 2, OpSignature::Result::Float64};
const OpSignature kSigCompare{kNumeric | type_bit(ScalarClass::Bool), 2, 2,
                              OpSignature::Result::Bool};
const OpSignature kSigBoolUnary{type_bit(ScalarClass::Bool), 1, 1,
                                OpSignature::Result::Bool};
const OpSignature kSigBoolBinary{type_bit(ScalarClass::Bool), 2, 2,
                                 OpSignature::Result::Bool};
const OpSignature kSigCast{kAny, 1, 1, OpSignature::Result::Target};

const OpSignature* signature_of(Op op) noexcept {
    switch (op) {
    case Op::Add:
    case Op::Mul:
    case Op::Div:
        return &kSigArith;
    case Op::Sub:
        return &kSigArithOrNeg;
    case Op::Mod:
        return &kSigMod;
    case Op::Max:
    case Op::Min:
        return &kSigMinMax;
    case Op::BitAnd:
    case Op::BitOr:
    case Op::BitXor:
    case Op::Shl:
    case Op::Shr:
        return &kSigBit;
    case Op::Neg:
    case Op::Abs:
        return &kSigNegAbs;
    case Op::Sign:
        return &kSigSign;
    case Op::Sqrt:
    case Op::Exp:
    case Op::Log:
        return &kSigFloat64Unary;
    case Op::Pow:
        return &kSigFloat64Binary;
    case Op::Eq:
    case Op::Neq:
    case Op::Lt:
    case Op::Le:
    case Op::Gt:
    case Op::Ge:
        return &kSigCompare;
    case Op::Not:
        return &kSigBoolUnary;
    case Op::And:
    case Op::Or:
        return &kSigBoolBinary;
    case Op::Cast:
        return &kSigCast;
    default:
        return nullptr;
    }
}

const Entry myrwircaps[] = {
    {"!", Op::Not, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolUnary},
    {"!=", Op::Neq, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"%", Op::Mod, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigMod},
    {"&", Op::BitAnd, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"&&", Op::And, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolBinary},
    {"+", Op::Add, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"-", Op::Sub, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArithOrNeg},
    {"<", Op::Lt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"<<", Op::Shl, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"<=", Op::Le, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"=", Op::Copy, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"==", Op::Eq, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {">", Op::Gt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {">=", Op::Ge, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {">>", Op::Shr, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"^", Op::BitXor, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"abs", Op::Abs, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigNegAbs},
    {"add", Op::Add, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"and", Op::And, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolBinary},
    {"compactarray·new", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayNew},
    {"compactarray·copy", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayCopy},
    {"compactarray·newslice", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayNewSlice},
    {"scatterarray·append", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterAppend},
    {"scatterarray·newcompact", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterNewCompact},
    {"compactarray·fill", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayFill},
    {"scatterarray·newscatter", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterNewScatter},
    {"scatterarray·new", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterNew},
    {"scatterarray·len", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterLength},
    {"scatterarray·at", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterAt},
    {"scatterarray·set", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterSet},
    {"scatterarray·slice", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ScatterSlice},
    {"bitand", Op::BitAnd, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"bitor", Op::BitOr, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"bitxor", Op::BitXor, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"bool", Op::Bool, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"br", Op::Br, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"call", Op::Call, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"char/ascii", Op::Cast, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCast},
    {"char/utf32", Op::Cast, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCast},
    {"char/utf8", Op::Cast, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCast},
    {"div", Op::Div, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"eq", Op::Eq, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"exp", Op::Exp, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigFloat64Unary},
    {"float32", Op::Cast, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·abs", Op::Abs, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·add", Op::Add, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·div", Op::Div, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·eq", Op::Eq, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·ge", Op::Ge, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·gt", Op::Gt, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·le", Op::Le, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·lt", Op::Lt, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·max", Op::Max, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·min", Op::Min, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·mul", Op::Mul, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·neg", Op::Neg, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·neq", Op::Neq, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float32·sub", Op::Sub, Kind::Float, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64", Op::Cast, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·abs", Op::Abs, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·add", Op::Add, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·div", Op::Div, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·eq", Op::Eq, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·exp", Op::Exp, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·ge", Op::Ge, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·gt", Op::Gt, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·le", Op::Le, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·log", Op::Log, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·lt", Op::Lt, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·max", Op::Max, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·min", Op::Min, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·mul", Op::Mul, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·neg", Op::Neg, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·neq", Op::Neq, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·pow", Op::Pow, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·sign", Op::Sign, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·sqrt", Op::Sqrt, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"float64·sub", Op::Sub, Kind::Float, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"ge", Op::Ge, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"goto", Op::Goto, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"gt", Op::Gt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"int16", Op::Cast, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·abs", Op::Abs, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·add", Op::Add, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·div", Op::Div, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·eq", Op::Eq, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·ge", Op::Ge, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·gt", Op::Gt, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·le", Op::Le, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·lt", Op::Lt, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·max", Op::Max, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·min", Op::Min, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·mod", Op::Mod, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·mul", Op::Mul, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·neg", Op::Neg, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·neq", Op::Neq, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int16·sub", Op::Sub, Kind::Int, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32", Op::Cast, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·abs", Op::Abs, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·add", Op::Add, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·div", Op::Div, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·eq", Op::Eq, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·ge", Op::Ge, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·gt", Op::Gt, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·le", Op::Le, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·lt", Op::Lt, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·max", Op::Max, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·min", Op::Min, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·mod", Op::Mod, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·mul", Op::Mul, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·neg", Op::Neg, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·neq", Op::Neq, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int32·sub", Op::Sub, Kind::Int, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64", Op::Cast, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·abs", Op::Abs, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·add", Op::Add, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·bitand", Op::BitAnd, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·bitor", Op::BitOr, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·bitxor", Op::BitXor, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·div", Op::Div, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·eq", Op::Eq, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·ge", Op::Ge, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·gt", Op::Gt, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·le", Op::Le, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·lt", Op::Lt, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·max", Op::Max, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·min", Op::Min, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·mod", Op::Mod, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·mul", Op::Mul, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·neg", Op::Neg, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·neq", Op::Neq, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·shl", Op::Shl, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·shr", Op::Shr, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·sign", Op::Sign, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int64·sub", Op::Sub, Kind::Int, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8", Op::Cast, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·abs", Op::Abs, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·add", Op::Add, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·div", Op::Div, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·eq", Op::Eq, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·ge", Op::Ge, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·gt", Op::Gt, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·le", Op::Le, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·lt", Op::Lt, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·max", Op::Max, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·min", Op::Min, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·mod", Op::Mod, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·mul", Op::Mul, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·neg", Op::Neg, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·neq", Op::Neq, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"int8·sub", Op::Sub, Kind::Int, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"kvlang·abs", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::Absolute},
    {"kvspace·cp", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceCopy},
    {"kvspace·cpdir", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceCopy},
    {"kvspace·cplist", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceCopy},
    {"kvspace·del", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceDelete},
    {"kvspace·deltree", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceDeleteTree},
    {"kvspace·extindex", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceExternalIndex},
    {"kvspace·get", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceGet},
    {"kvspace·listat", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceList},
    {"kvspace·listlen", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceList},
    {"kvspace·mkindex", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceMakeIndex},
    {"kvspace·rmindexext", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceRemoveExternalIndex},
    {"kvspace·set", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::KvspaceSet},
    {"kvspace·watch", Op::None, Kind::None, 0, Class::Complex, Impl::Handoff, RwirHandlerId::None},
    {"le", Op::Le, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"log", Op::Log, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigFloat64Unary},
    {"lt", Op::Lt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"map·new", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ObjectMap},
    {"max", Op::Max, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigMinMax},
    {"min", Op::Min, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigMinMax},
    {"mod", Op::Mod, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigMod},
    {"mul", Op::Mul, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"compactarray·dim", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayMetadata},
    {"compactarray·numel", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayMetadata},
    {"compactarray·shape", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayMetadata},
    {"neg", Op::Neg, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigNegAbs},
    {"neq", Op::Neq, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"not", Op::Not, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolUnary},
    {"obj", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ObjectMap},
    {"or", Op::Or, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolBinary},
    {"pow", Op::Pow, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigFloat64Binary},
    {"random·int63", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::RandomNumber},
    {"random·intn", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::RandomNumber},
    {"random·uint64", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::RandomNumber},
    {"return", Op::Return, Kind::None, 0, Class::Base, Impl::Control, RwirHandlerId::None},
    {"shl", Op::Shl, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"shr", Op::Shr, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"sign", Op::Sign, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigSign},
    {"sqrt", Op::Sqrt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigFloat64Unary},
    {"string·char", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringCharacter},
    {"string·cmp", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringCompare},
    {"string·concat", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringConcatenate},
    {"string·find", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringFind},
    {"string·formatint", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringFormatInt},
    {"string·formatuint", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringFormatUint},
    {"string·len", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringLength},
    {"string·ord", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringOrdinal},
    {"string·parsefloat", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringParseFloat},
    {"string·parseint", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringParseInt},
    {"string·parseuint", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringParseUint},
    {"string·set", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringSet},
    {"string·slice", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StringSlice},
    {"struct·new", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::StructNew},
    {"sub", Op::Sub, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArithOrNeg},
    {"time/duration·add", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time/duration·after", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time/duration·as_hours", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationAs},
    {"time/duration·as_millis", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationAs},
    {"time/duration·as_minutes", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationAs},
    {"time/duration·as_nanos", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationAs},
    {"time/duration·as_seconds", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationAs},
    {"time/duration·before", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time/duration·hours", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationScale},
    {"time/duration·millis", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationScale},
    {"time/duration·minutes", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationScale},
    {"time/duration·nanos", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationScale},
    {"time/duration·seconds", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::DurationScale},
    {"time/duration·sub", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time·add", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time·after", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time·before", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"time·now", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeNow},
    {"time·sub", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::TimeArithmetic},
    {"uint16", Op::Cast, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·add", Op::Add, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·div", Op::Div, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·eq", Op::Eq, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·ge", Op::Ge, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·gt", Op::Gt, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·le", Op::Le, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·lt", Op::Lt, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·max", Op::Max, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·min", Op::Min, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·mod", Op::Mod, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·mul", Op::Mul, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·neq", Op::Neq, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint16·sub", Op::Sub, Kind::Uint, 2, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32", Op::Cast, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·add", Op::Add, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·div", Op::Div, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·eq", Op::Eq, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·ge", Op::Ge, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·gt", Op::Gt, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·le", Op::Le, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·lt", Op::Lt, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·max", Op::Max, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·min", Op::Min, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·mod", Op::Mod, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·mul", Op::Mul, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·neq", Op::Neq, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint32·sub", Op::Sub, Kind::Uint, 4, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64", Op::Cast, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·add", Op::Add, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·bitand", Op::BitAnd, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·bitor", Op::BitOr, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·bitxor", Op::BitXor, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·div", Op::Div, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·eq", Op::Eq, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·ge", Op::Ge, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·gt", Op::Gt, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·le", Op::Le, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·lt", Op::Lt, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·max", Op::Max, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·min", Op::Min, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·mod", Op::Mod, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·mul", Op::Mul, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·neq", Op::Neq, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·shl", Op::Shl, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·shr", Op::Shr, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint64·sub", Op::Sub, Kind::Uint, 8, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8", Op::Cast, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·add", Op::Add, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·div", Op::Div, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·eq", Op::Eq, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·ge", Op::Ge, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·gt", Op::Gt, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·le", Op::Le, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·lt", Op::Lt, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·max", Op::Max, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·min", Op::Min, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·mod", Op::Mod, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·mul", Op::Mul, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·neq", Op::Neq, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"uint8·sub", Op::Sub, Kind::Uint, 1, Class::Base, Impl::Typed, RwirHandlerId::None},
    {"vthread·call", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::VthreadCall},
    {"vthread·create", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::VthreadCreate},
    {"vthread·run", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::VthreadRun},
    {"vthread·setstatus", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::VthreadSetStatus},
    {"vthread·sleep", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::VthreadSleep},
    {"compactarray·at", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayElementAccess},
    {"compactarray·bodylen", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::XvalueMetadata},
    {"compactarray·langtype", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::XvalueMetadata},
    {"compactarray·parselangtype", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::XvalueMetadata},
    {"compactarray·newreinterpret", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::XvalueMetadata},
    {"compactarray·newreshape", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayReshape},
    {"compactarray·set", Op::None, Kind::None, 0, Class::Complex, Impl::None, RwirHandlerId::ArrayElementAccess},
    {"|", Op::BitOr, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBit},
    {"||", Op::Or, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigBoolBinary},
    {"×", Op::Mul, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"÷", Op::Div, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigArith},
    {"√", Op::Sqrt, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigFloat64Unary},
    {"≠", Op::Neq, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"≤", Op::Le, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
    {"≥", Op::Ge, Kind::Any, 0, Class::Complex, Impl::Dynamic, RwirHandlerId::None, &kSigCompare},
};

const int kMyrwircapsN = 305;

// binir 的 op 字段 = RwirId = 本表下标，合法区间 0..0x1FB
// （0x1FC=HANDOFF、0x1FD=BR_IF_FALSE、0x1FE=BR_IF_TRUE、0x1FF=WIDE 标记）。
static_assert(kMyrwircapsN <= 0x1FB, "binir RwirId space is 0..0x1FB");

}  // namespace kvlang::runtimec2::rwir
