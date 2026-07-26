#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <functional>

// Builtin operation dispatch table — identical API to op/builtin/builtin.go.

namespace kvlang::op::builtin {

class VThread; // forward decl

// Builtin handler: takes reads + write slot, returns new PC ("" = continue).
using BuiltinFunc = std::function<std::string(
    const std::vector<std::string>& reads,
    std::string_view write,
    VThread& vt
)>;

// Register a builtin by opcode name.
void register_builtin(std::string_view opcode, BuiltinFunc handler);

// Look up a builtin. Returns nullptr if not found.
[[nodiscard]] const BuiltinFunc* lookup(std::string_view opcode);

// Execute a builtin by opcode. Returns new PC, or "" if not a builtin.
[[nodiscard]] std::string dispatch(
    std::string_view opcode,
    const std::vector<std::string>& reads,
    std::string_view write,
    VThread& vt
);

} // namespace kvlang::op::builtin
