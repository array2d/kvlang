#pragma once
#include <string>
#include <vector>
#include <string_view>

// Op dispatch router — identical API to op/dispatch/dispatch.go and op/dispatch/router.go.

namespace kvlang::op::dispatch {

class VThread; // forward decl
class KVSpace; // forward decl

// Main dispatch: given a decoded instruction, route to the correct handler
// (builtin, vtype, control flow, or call). Returns next PC.
[[nodiscard]] std::string dispatch_inst(
    KVSpace& kv,
    std::string_view pc,
    std::string_view opcode,
    const std::vector<std::string>& reads,
    std::string_view write,
    VThread& vt
);

// Route based on opcode prefix to determine handler category.
enum class Route { builtin, vtype_op, control_flow, call, unknown };
[[nodiscard]] Route route_op(std::string_view opcode);

} // namespace kvlang::op::dispatch
