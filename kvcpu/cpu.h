#pragma once
#include <string>
#include <string_view>
#include <memory>

// KV Virtual CPU — identical API to kvcpu/cpu.go and kvcpu/cpu.rs.
//
//  Usage:
//    auto cpu = kvlang::kvcpu::CPU::create(kv, vm_id);
//    cpu->execute(pc);

namespace kvlang::kvcpu {

class KVSpace; // forward decl (kvspace-cpp)

// ── CPU interface ────────────────────────────────────

class CPU {
public:
    virtual ~CPU() = default;

    // Fetch-Decode-Execute loop starting at pc.
    virtual void execute(std::string_view pc) = 0;

    // Step one instruction (for debugging).
    virtual void step(std::string_view pc) = 0;

    // Check if debugger is active for the current vthread.
    [[nodiscard]] virtual bool debugger_active() const = 0;
};

// Factory
std::unique_ptr<CPU> create_cpu(KVSpace& kv, std::string_view vm_id);

} // namespace kvlang::kvcpu
