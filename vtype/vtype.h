#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <functional>

// Value type system — identical API to vtype/vtype.go and vtype/vtype.rs.

namespace kvlang::vtype {

class VThread; // forward decl

// ── VType interface ──────────────────────────────────

class VType {
public:
    virtual ~VType() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string opcode() const = 0;

    // Execute an op on this value type
    virtual std::string execute(
        std::string_view op_name,
        const std::vector<std::string>& reads,
        std::string_view write,
        VThread& vt
    ) = 0;
};

// ── Registry ─────────────────────────────────────────

using VTypeFactory = std::function<std::unique_ptr<VType>()>;

void register_vtype(std::string_view name, VTypeFactory factory);
[[nodiscard]] VType* lookup(std::string_view opcode);
[[nodiscard]] std::string op_name(std::string_view opcode);

} // namespace kvlang::vtype
