#pragma once
#include <cstdint>

namespace kvlang::runtimec2::rwir {

enum class Op : std::uint8_t {
    None = 0, Copy, Bool, Br, Call, Goto, Return, Cast, Abs, Add, Sub, Mul,
    Div, Mod, Max, Min, Neg, Sign, BitAnd, BitOr, BitXor, Shl, Shr, Eq, Neq, Lt, Le,
    Gt, Ge, Sqrt, Exp, Log, Pow, And, Or, Not
};

enum class Kind : std::uint8_t { None = 0, Int, Uint, Float, Any };
enum class Class : std::uint8_t { Base = 0, Complex = 1 };
// 执行方式（与 policy 正交）：Typed/Control/Dynamic 由本 runtime 执行，
// Handoff 交扩展（M2 起逐步实现）。
enum class Impl : std::uint8_t { None = 0, Typed, Control, Dynamic, Native, Handoff };

struct Entry {
    const char* name;
    Op op;
    Kind kind;
    std::uint8_t width;
    Class cls;
    Impl impl;
};

// = C runtime 的 myrwircaps（能力表，opcode→处理器）。
extern const Entry myrwircaps[];
extern const int kMyrwircapsN;

// 名字 → 表项；未命中返回 nullptr（非本 runtime 的 rwir）。
const Entry* find_myrwircap(const char* name, std::uint32_t len) noexcept;

}  // namespace kvlang::runtimec2::rwir
