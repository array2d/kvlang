#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vmbin/langtype.hpp"

namespace kvlang::runtimec2::vmbin {

struct OpStat {
    unsigned long long n = 0, handler = 0, loop = 0;
};

struct Profile {
    unsigned long long handler = 0, loop = 0, steps = 0;
    unsigned long long pre = 0, loads = 0, frames = 0, ctrl = 0, fb = 0;
    unsigned long long f_slot = 0, f_raw = 0, w_fast = 0, w_fb = 0;
    unsigned long long rdtsc = 0;
    std::vector<OpStat> ops;
};

bool trace_enabled();
unsigned long long trace_limit();

struct State {
    Profile prof;
    bool trace_on = false;
    unsigned long long trace_n = 200;
    unsigned long long rdtsc_cost = 0;
#ifdef RUNTIMEC2_VMBIN_PROFILE
    static constexpr bool prof_on = true;
#else
    static constexpr bool prof_on = false;
#endif

    LangType langtype_of(const char* kindexpr, std::size_t len);
    std::uint8_t elem_width_of(LangType type);
    bool is_scalar(LangType type);

    unsigned long long now();
    void prof_tick(std::uint16_t id, std::uint64_t handler, std::uint64_t loop);
    void prof_init();
    void prof_report();
};

}  // namespace kvlang::runtimec2::vmbin
