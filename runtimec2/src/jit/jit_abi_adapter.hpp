// Stage-1 migration adapter interface (three-pointer JIT ABI).
#pragma once

#include "jit_abi_v1.hpp"

#include "vmbin/frame.hpp"
#include "vmbin/program.hpp"

#include <cstdint>

namespace kvlang::runtimec2 {

// Stage-1 bridge to the legacy vmbin::Frame.
struct JitAbiLink {
    vmbin::Frame* frame = nullptr;
    const vmbin::Program* program = nullptr;  // segment of frame (E-layer RegDesc)
    std::uint8_t src_idx = 0;
    std::uint8_t dst_idx = 0;
};

// RC2_JIT_ABI=1 enables the adapter; unset keeps the legacy path byte-identical.
bool jit_abi_enabled();

// Dispatcher gate: the legacy src must resolve to a fast int64.
bool jit_abi_eligible(const JitAbiLink& link) noexcept;

// Runs the legacy JIT behind the three-pointer ABI.  Preconditions are the
// same as jit_abi_eligible; the function does not re-check them.
void jit_abi_run(const JitAbiLink& link, const void* jit_code,
                 jitabi::StructuredExitV1* out) noexcept;

}  // namespace kvlang::runtimec2
