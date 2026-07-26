#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <chrono>
#include <optional>

// Virtual thread management — identical API to vthread/vthread.go and vthread/vthread.rs.

namespace kvlang::vthread {

class KVSpace; // forward decl (kvspace-cpp)

// ── VThread state ────────────────────────────────────

struct VThreadState {
    std::string pc;       // current program counter (KV path)
    std::string status;   // "running" | "done" | "error" | "waiting"
};

// ── API ──────────────────────────────────────────────

// Read current PC and status from KV.
VThreadState get(KVSpace& kv, std::string_view vtid);

// Write PC and status to KV.
void set(KVSpace& kv, std::string_view vtid, std::string_view pc, std::string_view status);

// Mark vthread as done with return value.
void set_done(KVSpace& kv, std::string_view vtid, std::string_view ret_val);

// Mark vthread as error.
void set_error(KVSpace& kv, std::string_view vtid, std::string_view pc, std::string_view err_msg);

// Allocate a new vthread ID.
[[nodiscard]] std::string alloc_vtid(KVSpace& kv);

// Create a new vthread for a function call.
// Returns the new vtid.
[[nodiscard]] std::string create_vthread(
    KVSpace& kv,
    std::string_view func_name,
    const std::vector<std::string>& reads,
    const std::vector<std::string>& writes
);

// Block until vthread completes. Returns the return value, or nullopt on timeout.
[[nodiscard]] std::optional<std::string> wait_done(
    KVSpace& kv,
    std::string_view vtid,
    std::chrono::milliseconds timeout
);

} // namespace kvlang::vthread
