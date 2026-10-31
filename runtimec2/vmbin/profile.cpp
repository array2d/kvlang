#include "vmbin/profile.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "codeseg.hpp"

namespace kvlang::runtimec2::vmbin {
namespace {

State* g_profile_state = nullptr;

#ifdef RUNTIMEC2_VMBIN_PROFILE
void prof_report_at_exit() {
    if (g_profile_state)
        g_profile_state->prof_report();
}
#endif

const char* environment_value(const char* canonical, const char* legacy) {
    const char* value = std::getenv(canonical);
    return value ? value : std::getenv(legacy);
}

struct TypeMapEntry {
    const char* kindexpr;
    LangType type;
};

constexpr TypeMapEntry kTypeMap[] = {
    {"int8", LangType::Int8},       {"int16", LangType::Int16},
    {"int32", LangType::Int32},     {"int64", LangType::Int64},
    {"uint8", LangType::Uint8},     {"uint16", LangType::Uint16},
    {"uint32", LangType::Uint32},   {"uint64", LangType::Uint64},
    {"float32", LangType::Float32}, {"float64", LangType::Float64},
    {"bool", LangType::Bool},
    {"char/utf8", LangType::CharUtf8},
    {"char/utf32", LangType::CharUtf32},
    {"char/ascii", LangType::CharAscii},
};

constexpr char kStringKeyMapSuffix[] = "\xC2\xB7";

std::uint8_t elem_width(LangType type) {
    switch (type) {
    case LangType::Int8:
    case LangType::Uint8:
    case LangType::Bool:
    case LangType::CharUtf8:
    case LangType::CharAscii:
        return 1;
    case LangType::Int16:
    case LangType::Uint16:
        return 2;
    case LangType::Int32:
    case LangType::Uint32:
    case LangType::Float32:
    case LangType::CharUtf32:
        return 4;
    case LangType::Int64:
    case LangType::Uint64:
    case LangType::Float64:
        return 8;
    default:
        return 0;
    }
}

}  // namespace

bool trace_enabled() {
    return environment_value("RUNTIMEC2_TRACE", "RC2_TRACE") != nullptr;
}

unsigned long long trace_limit() {
    const char* value =
        environment_value("RUNTIMEC2_VMBIN_TRACE_LIMIT", "RC2_TRACE_N");
    return value ? std::strtoull(value, nullptr, 10) : 200ULL;
}

LangType State::langtype_of(const char* kindexpr, std::size_t len) {
    const char* current = kindexpr;
    std::size_t remaining = len;
    if (remaining && *current == '[') {
        while (remaining && *current != ']') {
            current++;
            remaining--;
        }
        if (remaining) {
            current++;
            remaining--;
        }
    }
    for (const auto& entry : kTypeMap) {
        const std::size_t entry_len = std::strlen(entry.kindexpr);
        if (remaining == entry_len &&
            std::memcmp(current, entry.kindexpr, entry_len) == 0)
            return entry.type;
    }
    constexpr std::size_t suffix_len = sizeof(kStringKeyMapSuffix) - 1;
    if (remaining >= suffix_len &&
        std::memcmp(current + remaining - suffix_len, kStringKeyMapSuffix,
                    suffix_len) == 0)
        return LangType::StringKeyMap;
    if (remaining && *current == '@')
        return LangType::Ext;
    return LangType::None;
}

std::uint8_t State::elem_width_of(LangType type) { return elem_width(type); }

bool State::is_scalar(LangType type) {
    return type != LangType::None && type != LangType::Array &&
           type != LangType::StringKeyMap && type != LangType::StructRef &&
           type != LangType::Ext;
}

#if defined(__x86_64__)
unsigned long long State::now() { return __builtin_ia32_rdtsc(); }
#else
#include <mach/mach_time.h>
unsigned long long State::now() {
    return static_cast<unsigned long long>(mach_absolute_time());
}
#endif

void State::prof_tick(std::uint16_t id, std::uint64_t handler,
                      std::uint64_t loop) {
#ifdef RUNTIMEC2_VMBIN_PROFILE
    if (prof.ops.size() < op_name_count())
        prof.ops.resize(op_name_count());
    OpStat& stat = prof.ops[id];
    stat.n++;
    stat.handler += handler;
    stat.loop += loop;
#else
    (void)id;
    (void)handler;
    (void)loop;
#endif
}

void State::prof_report() {
    if (!prof_on || prof.steps == 0)
        return;
    std::fprintf(stderr,
                 "RC2PROF steps=%llu frames=%llu handler=%.1f loop=%.1f "
                 "ctrl=%.1f fb=%llu wfast=%llu wfb=%llu total=%.1f\n",
                 prof.steps, prof.frames, static_cast<double>(prof.handler) / prof.steps,
                 static_cast<double>(prof.loop) / prof.steps,
                 static_cast<double>(prof.ctrl) / prof.steps, prof.fb, prof.w_fast,
                 prof.w_fb,
                 static_cast<double>(prof.handler + prof.loop + prof.ctrl) / prof.steps);
    std::fprintf(stderr, "RC2PROF rdtsc_cost=%.1f cycles\n",
                 static_cast<double>(prof.rdtsc));
    const double calibration = static_cast<double>(prof.rdtsc);
    for (std::size_t index = 0; index < prof.ops.size(); index++) {
        const OpStat& stat = prof.ops[index];
        if (stat.n == 0)
            continue;
        std::fprintf(stderr,
                     "RC2PROFOP %-24s n=%-7llu handler=%.1f loop=%.1f "
                     "(扣探针 %.1f)\n",
                     op_name(static_cast<std::uint16_t>(index)).c_str(), stat.n,
                     static_cast<double>(stat.handler) / stat.n,
                     static_cast<double>(stat.loop) / stat.n,
                     static_cast<double>(stat.handler + stat.loop) / stat.n -
                         calibration);
    }
}

void State::prof_init() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    g_profile_state = this;
    trace_on = trace_enabled();
    trace_n = trace_limit();
#ifdef RUNTIMEC2_VMBIN_PROFILE
    unsigned long long best = ~0ULL;
    for (int index = 0; index < 256; index++) {
        const unsigned long long begin = now();
        const unsigned long long end = now();
        if (end - begin < best)
            best = end - begin;
    }
    prof.rdtsc = best;
    std::atexit(prof_report_at_exit);
#endif
}

}  // namespace kvlang::runtimec2::vmbin
