// JIT unified ABI V1, stage-1 minimal field set (spec §一).
// Reserved fields may be omitted; sizes documented in answer-008.
// Terminology: keyto64_xvalue = XValue 64-bit address; keyto64_xvalueBody =
// keyto64_xvalue + 8; keyto64_xvaluelangtype = attr tag=1 address.
#pragma once

#include <cstdint>

namespace jitabi {

inline constexpr std::uint32_t kAbiVersionV1 = 0x0001'0000u;

enum : std::uint32_t {
    kDetailSrcNotFast = 1,
    kDetailRunaway = 2,
    kDetailNullArgument = 3,
    kDetailCommitFailed = 4,
};
inline constexpr std::uint32_t kNoPc = 0xFFFF'FFFFu;

enum class ValueTagV1 : std::uint16_t {
    Empty = 0,
    Bool,
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    RefHandle,
    ArrayHandle,
    DynamicHandle,
};

enum ValueFlagsV1 : std::uint16_t {
    kValueOwned = 1u << 0,
    kValueBorrowed = 1u << 1,
    kValueNullable = 1u << 2,
    kValueDirty = 1u << 3,
};

struct alignas(16) ValueSlotV1 {
    std::uint64_t payload_lo;   // scalar bits or handle id
    std::uint64_t payload_hi;   // reserved in stage 1
    std::uint32_t type_id;      // canonical type/shape id
    ValueTagV1 tag;
    std::uint16_t flags;
    std::uint32_t generation;   // reserved (handle guard)
    std::uint32_t reserved;
};
static_assert(sizeof(ValueSlotV1) == 32);

enum class ExitKindV1 : std::uint16_t {
    Return = 0,
    Call,
    Native,
    Handoff,
    GuardFail,
    Exception,
    Error,
    Budget,
    Yield,
    Deopt,
};

enum ExitFlagsV1 : std::uint16_t {
    kFrameMaterialized = 1u << 0,
    kEffectStarted = 1u << 1,
    kEffectCommitted = 1u << 2,
    kExternalPcNeeded = 1u << 3,
    kRetrySamePc = 1u << 4,
};

// Stage-1 minimal subset: Return/Error are the only emitted kinds.
struct alignas(16) StructuredExitV1 {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    ExitKindV1 kind;
    std::uint16_t flags;
    std::uint32_t detail_code;
    std::uint32_t pc;
    std::uint32_t resume_pc;
    std::uint32_t value_base;
    std::uint32_t value_count;
    std::uint64_t aux0;         // result or error payload
};
static_assert(sizeof(StructuredExitV1) == 48);

struct alignas(64) VmContextV1 {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    void* runtime_opaque;       // stage 1: legacy JitFn
    std::uint64_t reserved_u64[6];
};
static_assert(sizeof(VmContextV1) == 64);

struct alignas(64) BinFrameV1 {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    ValueSlotV1* slots;
    std::uint32_t slot_count;
    std::uint32_t arg_base;
    std::uint32_t return_base;
    std::uint32_t pc;           // logical instruction ordinal
    void* legacy_frame_adapter; // stage 1: LegacyFrameLink*
    std::uint64_t reserved_u64[3];
};
static_assert(sizeof(BinFrameV1) == 64);

using GeneralJitFnV1 = void (*)(VmContextV1* context,
                                BinFrameV1* frame,
                                StructuredExitV1* out) noexcept;

}  // namespace jitabi
