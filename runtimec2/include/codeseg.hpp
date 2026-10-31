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
void clear_segment_cache();

const std::string& op_name(std::uint16_t id);
std::size_t op_name_count();

bool cached_insn(const std::string& func_dir, int s0,
                 const vmbin::Instruction** out,
                 const vmbin::Program** program_out);

}  // namespace kvlang::runtimec2
