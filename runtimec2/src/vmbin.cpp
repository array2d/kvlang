#include "vmbin.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "codeseg.hpp"

// 双栈映射实现：langtype 枚举 ↔ 明文 kindexpr 串的映射表、槽 suffix 预编码、
// 帧基址拼接。全部 init 期一次性构建，热路径零 string 判断、零 malloc。
//
// 明文栈契约保持：kvspace 落盘仍 string 形态（kindexpr / /vthread/... key）；
// stackbin 侧以枚举 + 偏移量直寻址，仅在边界（首次/落盘）经映射表转换一次。

namespace kv::rt::bin {
namespace {

// kindexpr 串 → LangType 映射（init 期查表，此后常数）。
struct TypeMapEntry {
    const char* kindexpr;
    LangType type;
};

// 覆盖标量 + 裸容器；带 dims 的（`[N,M]int64`）剥 `[dims]` 前缀后匹配裸 kind。
// 与 xv/ndarray builtin 的 esz/kind 判定对齐（vm.cpp 首次解析的枚举化）。
constexpr TypeMapEntry kTypeMap[] = {
    {"int8", LangType::Int8},      {"int16", LangType::Int16},
    {"int32", LangType::Int32},    {"int64", LangType::Int64},
    {"uint8", LangType::Uint8},    {"uint16", LangType::Uint16},
    {"uint32", LangType::Uint32},  {"uint64", LangType::Uint64},
    {"float32", LangType::Float32},{"float64", LangType::Float64},
    {"bool", LangType::Bool},
    {"char/utf8", LangType::CharUtf8},
    {"char/utf32", LangType::CharUtf32},
    {"char/ascii", LangType::CharAscii},
};

// 元素字节宽（对齐 kind_name/esz 判定）。
std::uint8_t elem_width(LangType t) {
    switch (t) {
    case LangType::Int8: case LangType::Uint8: case LangType::Bool:
    case LangType::CharUtf8: case LangType::CharAscii:
        return 1;
    case LangType::Int16: case LangType::Uint16:
        return 2;
    case LangType::Int32: case LangType::Uint32: case LangType::Float32:
    case LangType::CharUtf32:
        return 4;
    case LangType::Int64: case LangType::Uint64: case LangType::Float64:
        return 8;
    default:
        return 0;
    }
}

}  // namespace

// kindexpr 串 → 枚举（剥 dims 前缀；命中映射表）。返回 None 表示未知/复合。
LangType langtype_of(const char* kindexpr, std::size_t len) {
    const char* p = kindexpr;
    std::size_t n = len;
    // 剥 `[N,M]` dims 前缀（容器/数组）。
    if (n && *p == '[') {
        while (n && *p != ']') {
            p++;
            n--;
        }
        if (n) {
            p++;
            n--;
        }
    }
    for (const auto& e : kTypeMap) {
        const std::size_t el = std::strlen(e.kindexpr);
        if (n == el && std::memcmp(p, e.kindexpr, el) == 0)
            return e.type;
    }
    // `·` 结尾 = stringkeymap；`@` 开头 = 扩展世界。
    if (n && p[n - 1] == '\xC2\xB7')
        return LangType::StringKeyMap;
    if (n && *p == '@')
        return LangType::Ext;
    return LangType::None;
}

std::uint8_t elem_width_of(LangType t) { return elem_width(t); }

bool is_scalar(LangType t) {
    return t != LangType::None && t != LangType::Array &&
           t != LangType::StringKeyMap && t != LangType::StructRef &&
           t != LangType::Ext;
}

// ── 性能统计（RC2_PROF 构建探针；热路径零开销）──────────────────────

Prof g_prof;
#ifdef RC2_PROF
constexpr bool g_prof_on = true;
#else
constexpr bool g_prof_on = false;
#endif

bool g_trace_on = false;
unsigned long long g_trace_n = 200;

// 时间探针：x86 rdtsc；ARM64 用 mach_absolute_time（macOS 跨架构移植）。
#if defined(__x86_64__)
unsigned long long now() { return __builtin_ia32_rdtsc(); }
#else
#include <mach/mach_time.h>
unsigned long long now() {
    return static_cast<unsigned long long>(mach_absolute_time());
}
#endif

void prof_tick(std::uint16_t id, std::uint64_t h, std::uint64_t l) {
#ifdef RC2_PROF
    if (g_prof.ops.size() < op_name_count())
        g_prof.ops.resize(op_name_count());
    OpStat& st = g_prof.ops[id];
    st.n++;
    st.handler += h;
    st.loop += l;
#else
    (void)id;
    (void)h;
    (void)l;
#endif
}

void prof_report() {
    if (!g_prof_on || g_prof.steps == 0)
        return;
    std::fprintf(stderr,
                 "RC2PROF steps=%llu frames=%llu handler=%.1f loop=%.1f "
                 "ctrl=%.1f fb=%llu wfast=%llu wfb=%llu total=%.1f\n",
                 g_prof.steps, g_prof.frames, (double)g_prof.handler / g_prof.steps,
                 (double)g_prof.loop / g_prof.steps,
                 (double)g_prof.ctrl / g_prof.steps, g_prof.fb, g_prof.w_fast,
                 g_prof.w_fb,
                 (double)(g_prof.handler + g_prof.loop + g_prof.ctrl) /
                     g_prof.steps);
    std::fprintf(stderr, "RC2PROF rdtsc_cost=%.1f cycles\n",
                 (double)g_prof.rdtsc);
    const double cal = (double)g_prof.rdtsc;
    for (std::size_t i = 0; i < g_prof.ops.size(); i++) {
        const OpStat& st = g_prof.ops[i];
        if (st.n == 0)
            continue;
        std::fprintf(stderr, "RC2PROFOP %-24s n=%-7llu handler=%.1f loop=%.1f "
                             "(扣探针 %.1f)\n",
                     op_name(static_cast<std::uint16_t>(i)).c_str(), st.n,
                     (double)st.handler / st.n, (double)st.loop / st.n,
                     (double)(st.handler + st.loop) / st.n - cal);
    }
}

void prof_init() {
    static bool inited = false;
    if (inited)
        return;
    inited = true;
    g_trace_on = std::getenv("RC2_TRACE") != nullptr;
    const char* e = std::getenv("RC2_TRACE_N");
    g_trace_n = e ? std::strtoull(e, nullptr, 10) : 200ULL;
#ifdef RC2_PROF
    unsigned long long best = ~0ULL;
    for (int i = 0; i < 256; i++) {
        const unsigned long long a = now();
        const unsigned long long b = now();
        if (b - a < best)
            best = b - a;
    }
    g_prof.rdtsc = best;
    std::atexit(prof_report);
#endif
}

}  // namespace kv::rt::bin
