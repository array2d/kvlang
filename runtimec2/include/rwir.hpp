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
// 执行载体（与 policy 正交）：Typed/Control/Dynamic 由引擎内联执行，
// Handoff 交扩展；实现载体为 rwir handler 的条目 op=None、impl=None、
// handler != None（vmkv_rwirhandler.cpp）。
enum class Impl : std::uint8_t { None = 0, Typed, Control, Dynamic, Handoff };

// rwir handler 表的唯一下标（vmkv_rwirhandler.cpp 按此 id 直接取函数，
// 不做任何字符串查找）。每条 rwir = 名字 + 语义 + 参数布局，实现载体
// 只有两种：引擎内联（handler=None）与 handler 表项（handler != None）。
enum class RwirHandlerId : std::uint16_t {
    None = 0,
    TimeNow,
    TimeArithmetic,
    DurationScale,
    DurationAs,
    RandomNumber,
    ArrayNew,
    ArrayFill,
    ArrayCopy,
    ArrayNewSlice,
    ArrayMetadata,
    ArrayReshape,
    XvalueMetadata,
    ArrayElementAccess,
    ScatterNew,
    ScatterLength,
    ScatterAt,
    ScatterSet,
    ScatterNewCompact,
    ScatterNewScatter,
    ScatterAppend,
    ScatterSlice,
    StringCharacter,
    StringCompare,
    StringConcatenate,
    StringFind,
    StringFormatInt,
    StringFormatUint,
    StringLength,
    StringOrdinal,
    StringParseFloat,
    StringParseInt,
    StringParseUint,
    StringSet,
    StringSlice,
    ObjectMap,
    StructNew,
    Absolute,
    VthreadCreate,
    VthreadRun,
    VthreadCall,
    VthreadSleep,
    VthreadSetStatus,
    KvspaceGet,
    KvspaceSet,
    KvspaceDelete,
    KvspaceDeleteTree,
    KvspaceMakeIndex,
    KvspaceList,
    KvspaceCopy,
    KvspaceExternalIndex,
    KvspaceRemoveExternalIndex,
    Count,
};

struct Entry {
    const char* name;
    Op op;
    Kind kind;
    std::uint8_t width;
    Class cls;
    Impl impl;
    RwirHandlerId handler;
};

// = C runtime 的 myrwircaps（能力表，opcode→处理器）。
extern const Entry myrwircaps[];
extern const int kMyrwircapsN;

// 名字 → 表项；未命中返回 nullptr（非本 runtime 的 rwir）。
const Entry* find_myrwircap(const char* name, std::uint32_t len) noexcept;

}  // namespace kvlang::runtimec2::rwir
