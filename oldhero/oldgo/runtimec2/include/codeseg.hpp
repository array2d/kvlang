#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "vmbin/program.hpp"

namespace kvlang::runtimec2 {

using Operand = vmbin::Operand;
using RegDesc = vmbin::RegisterDesc;
using CInsn = vmbin::Instruction;
using LitFast = vmbin::FastLiteral;
using Segment = vmbin::Program;

bool build_segment(void* h, const std::string& func_dir, vmbin::Program* out);
const vmbin::Program* cached_segment(void* h, const std::string& func_dir);
std::string take_last_segment_error();
void clear_segment_cache();
void bump_segment_epoch() noexcept;
std::uint64_t current_segment_epoch() noexcept;
// Reports whether `seg` (or any transitive callee) may Stop via handoff/yield.
// Pure-memory frames (`mem_frame`) require non-stopping callees because Stop reconstructs frames from kvspace.
bool segment_may_stop(void* h, const vmbin::Program* seg);

const char* op_name(std::uint16_t id);
std::size_t op_name_count();

bool cached_insn(const std::string& func_dir, int s0,
                 const vmbin::Instruction** out,
                 const vmbin::Program** program_out);

}  // namespace kvlang::runtimec2
