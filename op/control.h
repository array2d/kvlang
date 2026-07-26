#pragma once
#include "instruction.h"

// Control flow opcodes — goto, br, label, call, return.

namespace kvlang::op {

struct GotoInst {
    std::string label;
};

struct BrInst {
    std::string cond_slot;  // KV path to condition value
    std::string true_label;
    std::string false_label;
};

} // namespace kvlang::op
