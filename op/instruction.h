#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>

// Opcode instruction types — identical to op/instruction.go and op/instruction.rs.

namespace kvlang::op {

// ── Core instruction ─────────────────────────────────

struct Instruction {
    std::string opcode;                    // e.g. "copy", "add", "/lib/pkg.fn"
    std::vector<std::string> reads;        // read slots (KV paths)
    std::string write;                     // write slot (KV path, "" if none)
    std::string label;                     // block label
    std::string comment;                   // source comment

    [[nodiscard]] bool is_call() const;
    [[nodiscard]] bool is_return() const;
    [[nodiscard]] bool is_goto() const;
    [[nodiscard]] bool is_label() const;
    [[nodiscard]] bool is_terminator() const;
};

// ── Decode from KV store ─────────────────────────────

class KVSpace; // forward decl (kvspace-cpp)
Instruction decode(KVSpace& kv, std::string_view link_base, std::string_view pc);

// ── Constants ────────────────────────────────────────

inline constexpr std::string_view OpCopy   = "copy";
inline constexpr std::string_view OpCall   = "call";
inline constexpr std::string_view OpReturn = "return";
inline constexpr std::string_view OpGoto   = "goto";
inline constexpr std::string_view OpBr     = "br";
inline constexpr std::string_view OpNop    = "nop";

} // namespace kvlang::op
