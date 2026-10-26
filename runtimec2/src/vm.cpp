#include "vm.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <sys/random.h>
#include <ctime>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "model.hpp"
#include "valueio.hpp"

namespace kv::rt {
namespace {

struct OpStat {
    unsigned long long n = 0, handler = 0, loop = 0;
};

struct Prof {
    unsigned long long handler = 0, loop = 0, steps = 0;
    unsigned long long pre = 0, loads = 0, frames = 0, ctrl = 0, fb = 0;
    unsigned long long f_slot = 0, f_raw = 0, w_fast = 0, w_fb = 0;
    unsigned long long rdtsc = 0;  // 单次 now() 的探针成本（校准值）
    std::vector<OpStat> ops;
};

Prof g_prof;
// 探针以编译期宏摘除：非 RC2_PROF 构建中 g_prof_on 恒 false，热路径零开销。
#ifdef RC2_PROF
constexpr bool g_prof_on = true;
#else
constexpr bool g_prof_on = false;
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
    (void)id; (void)h; (void)l;
#endif
}
bool g_trace_on = false;
unsigned long long g_trace_n = 200;

unsigned long long now() { return __builtin_ia32_rdtsc(); }

void prof_report() {
    if (!g_prof_on || g_prof.steps == 0)
        return;
    std::fprintf(stderr,
                 "RC2PROF steps=%llu frames=%llu handler=%.1f loop=%.1f "
                 "ctrl=%.1f fb=%llu wfast=%llu wfb=%llu total=%.1f\n",
                 g_prof.steps, g_prof.frames,
                 (double)g_prof.handler / g_prof.steps,
                 (double)g_prof.loop / g_prof.steps,
                 (double)g_prof.ctrl / g_prof.steps, g_prof.fb, g_prof.w_fast,
                 g_prof.w_fb,
                 (double)(g_prof.handler + g_prof.loop + g_prof.ctrl) /
                     g_prof.steps);
    std::fprintf(stderr, "RC2PROF rdtsc_cost=%.1f cycles\n", (double)g_prof.rdtsc);
    const double cal = (double)g_prof.rdtsc;
    for (std::size_t i = 0; i < g_prof.ops.size(); i++) {
        const OpStat& st = g_prof.ops[i];
        if (st.n == 0)
            continue;
        std::fprintf(stderr,
                     "RC2PROFOP %-24s n=%-7llu handler=%.1f loop=%.1f "
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
#ifdef RC2_PROF
    g_trace_on = std::getenv("RC2_TRACE") != nullptr;
    {
        const char* e = std::getenv("RC2_TRACE_N");
        g_trace_n = e ? std::strtoull(e, nullptr, 10) : 200ULL;
    }
    // 探针校准：一次 now() 往返的真实成本（取 256 次最小值）。
    unsigned long long best = ~0ULL;
    for (int i = 0; i < 256; i++) {
        const unsigned long long a = now();
        const unsigned long long b = now();
        if (b - a < best)
            best = b - a;
    }
    g_prof.rdtsc = best;
    std::atexit(prof_report);
#else
    g_trace_on = std::getenv("RC2_TRACE") != nullptr;
    {
        const char* e = std::getenv("RC2_TRACE_N");
        g_trace_n = e ? std::strtoull(e, nullptr, 10) : 200ULL;
    }
#endif
}

// 变量描述符 → 具体 key（帧入口一次；Ptr 链在此解析完）。
bool resolve_desc(FrameCtx& f, const std::string& name, std::string* key,
                  std::string* err) {
    if (name.empty()) {
        *err = "operand: empty var";
        return false;
    }
    if (name[0] == '/') {
        *key = name;
        return true;
    }
    if (name[0] != '*') {
        *key = f.frame + "/" + name;
        return true;
    }
    std::string member = name.substr(1);
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    auto member_raw = [&](const std::string& m) {
        return read_wire_raw(f.h, f.frame + "/" + m, &d, &n);
    };
    bool ok = member_raw(member);
    if (!ok) {
        auto sp = member.find(' ');
        if (sp != std::string::npos) {
            member = member.substr(0, sp);
            ok = member_raw(member);
        }
    }
    if (!ok) {
        *err = "operand: member missing: " + member;
        return false;
    }
    Wire w{d, n};
    std::string target;
    if (!v_ptr_target(w, &target)) {
        *err = "operand: member is not Ptr: " + member;
        return false;
    }
    if (member.rfind("[0,", 0) == 0) {
        const std::uint8_t* ad = nullptr;
        std::uint32_t an = 0;
        if (!read_wire_raw(f.h, target, &ad, &an)) {
            *err = "operand: arg Ptr missing: " + target;
            return false;
        }
        Wire aw{ad, an};
        std::string t2;
        if (!v_ptr_target(aw, &t2)) {
            *err = "operand: arg Ptr chain broken: " + target;
            return false;
        }
        *key = t2;
        return true;
    }
    *key = target;
    return true;
}

bool ensure_ref(FrameCtx& f, FReg* r) {
    if (r->resolved)
        return true;
    if (r->key.empty())
        return false;
    if (kvspaceResolveRef(f.h, r->key.c_str(), &r->ref) != 0)
        return false;
    r->resolved = true;
    return true;
}

// shm slot view：head/body 直接指针（帧内缓存，写后失效）。
bool reg_slot(FrameCtx& f, std::uint8_t idx, kvspaceSlotView_t* out) {
    FReg& r = f.regs[idx];
    if (r.view_ok) {
        *out = r.view;
        return true;
    }
    if (!ensure_ref(f, &r))
        return false;
    if (kvspaceSlotView(f.h, &r.ref, r.key.c_str(), &r.view) != 0)
        return false;
    r.view_ok = true;
    r.body = r.view.body;
    r.cap = r.view.body_cap;
    *out = r.view;
    return true;
}

// 只取 body/cap（热路径不搬 40 字节 view 结构）。
bool reg_body(FrameCtx& f, std::uint8_t idx, std::uint8_t** body,
              std::uint64_t* cap) {
    FReg& r = f.regs[idx];
    if (!r.view_ok) {
        if (!ensure_ref(f, &r))
            return false;
        if (kvspaceSlotView(f.h, &r.ref, r.key.c_str(), &r.view) != 0)
            return false;
        r.view_ok = true;
        r.body = r.view.body;
        r.cap = r.view.body_cap;
    }
    *body = r.body;
    *cap = r.cap;
    return true;
}

bool slot_langtype(const kvspaceSlotView_t& v, std::string* lt) {
    if (!v.head || v.headlen <= 18)
        return false;
    const char* p = reinterpret_cast<const char*>(v.head + 18);
    std::size_t cap = v.headlen - 18, n = 0;
    while (n < cap && p[n] != '\0')
        n++;
    lt->assign(p, n);
    return !lt->empty();
}

// 从 view 直读标量（不 decode head、不 ABI）。
bool slot_read_scalar(const kvspaceSlotView_t& v, Scalar* out) {
    if (!v.head || v.headlen <= 18)
        return false;
    return v_scalar_view(v.head + 18, v.headlen - 18, v.body, v.body_cap, out);
}

bool read_reg(FrameCtx& f, std::uint8_t idx, Wire* out) {
    FReg& r = f.regs[idx];
    if (ensure_ref(f, &r)) {
        std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        if (kvspaceGetByRef(f.h, &r.ref, r.key.c_str(), &d, &n) == 0 && d && n) {
            out->d = d;
            out->n = n;
            return true;
        }
    }
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(f.h, r.key, &d, &n))
        return false;
    out->d = d;
    out->n = n;
    return true;
}

// 读标量：slot view 直读；kind/width 每帧解析一次后缓存。
bool read_scalar(FrameCtx& f, std::uint8_t idx, Scalar* out) {
    FReg& r = f.regs[idx];
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    const bool have_view = reg_body(f, idx, &body, &cap);
    if (g_prof_on && !have_view)
        g_prof.f_slot++;
    if (have_view) {
        if (r.sc_ok) {
            if (v_scalar_raw(r.sck, r.scw, body, cap, out))
                return true;
            if (g_prof_on)
                g_prof.f_raw++;
        } else if (slot_read_scalar(r.view, out)) {
            r.sck = out->k;
            r.scw = out->width;
            r.sc_ok = true;
            return true;
        }
    }
    if (g_prof_on)
        g_prof.fb++;
    Wire w{};
    return read_reg(f, idx, &w) && v_scalar(w, out);
}

bool write_reg(FrameCtx& f, std::uint8_t idx, const std::uint8_t* data,
               std::uint32_t len) {
    FReg& r = f.regs[idx];
    r.view_ok = false;
    r.sc_ok = false;
    r.fast_i64 = false;
    r.fast_bool = false;
    if (ensure_ref(f, &r)) {
        char err[192] = {};
        if (kvspaceSetValueByRef(f.h, &r.ref, r.key.c_str(), data, len, 0, 0, err,
                                 sizeof err) == 0)
            return true;
        r.resolved = false;
    }
    char err[192] = {};
    if (kvspaceSetValue(f.h, r.key.c_str(), data, len, 0, 0, err, sizeof err) != 0)
        return false;
    // 新键：补一次绑定，后续走 ByRef。
    if (kvspaceResolveRef(f.h, r.key.c_str(), &r.ref) == 0)
        r.resolved = true;
    return true;
}

// 直取 int64 槽值（不经 Scalar 结构）。快缓存命中：零判定、零拷贝。
bool read_i64(FrameCtx& f, std::uint8_t idx, std::int64_t* out) {
    FReg& r = f.regs[idx];
    if (r.fast_i64) {
        *out = r.fv;
        return true;
    }
    Scalar sc{};
    if (!read_scalar(f, idx, &sc) || sc.k != Scalar::K::Int)
        return false;
    *out = sc.i;
    if (sc.width == 8) {
        r.fv = sc.i;
        r.fast_i64 = true;
    }
    return true;
}

bool truthy(const Scalar& s);

// 直取 bool 槽值（比较/br/条件）。
bool read_bool(FrameCtx& f, std::uint8_t idx, bool* out) {
    FReg& r = f.regs[idx];
    if (r.fast_bool) {
        *out = r.fb;
        return true;
    }
    Scalar sc{};
    if (!read_scalar(f, idx, &sc))
        return false;
    *out = truthy(sc);
    if (sc.k == Scalar::K::Bool && sc.width == 1) {
        r.fb = *out;
        r.fast_bool = true;
    }
    return true;
}

const char* kind_name(Scalar::K k, std::uint8_t width);
bool slot_write_scalar(FrameCtx& f, std::uint8_t idx, const Scalar& s,
                       std::uint8_t width);

// 专用直写 int64：缓存命中（Int/8）时免 Scalar 装箱、免 kind 判定，纯 8 字节 memcpy。
bool slot_write_i64(FrameCtx& f, std::uint8_t idx, std::int64_t v) {
    FReg& fr = f.regs[idx];
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    if ((fr.fast_i64 || (fr.sc_ok && fr.sck == Scalar::K::Int && fr.scw == 8)) &&
        reg_body(f, idx, &body, &cap) && body && cap >= 8) {
        std::memcpy(body, &v, 8);
        fr.fv = v;
        fr.fast_i64 = true;
        if (g_prof_on)
            g_prof.w_fast++;
        return true;
    }
    Scalar s{};
    s.k = Scalar::K::Int;
    s.width = 8;
    s.i = v;
    s.u = (std::uint64_t)v;
    bool ok = slot_write_scalar(f, idx, s, 8);
    if (ok) {
        fr.fv = v;
        fr.fast_i64 = true;
    }
    return ok;
}

// 专用直写 bool：同理，1 字节。
bool slot_write_bool(FrameCtx& f, std::uint8_t idx, bool b) {
    FReg& fr = f.regs[idx];
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    if ((fr.fast_bool || (fr.sc_ok && fr.sck == Scalar::K::Bool && fr.scw == 1)) &&
        reg_body(f, idx, &body, &cap) && body && cap >= 1) {
        *body = b ? 1 : 0;
        fr.fb = b;
        fr.fast_bool = true;
        if (g_prof_on)
            g_prof.w_fast++;
        return true;
    }
    Scalar s{};
    s.k = Scalar::K::Bool;
    s.width = 1;
    s.u = b ? 1 : 0;
    bool ok = slot_write_scalar(f, idx, s, 1);
    if (ok) {
        fr.fb = b;
        fr.fast_bool = true;
    }
    return ok;
}

bool slot_write_scalar(FrameCtx& f, std::uint8_t idx, const Scalar& s,
                       std::uint8_t width) {
    std::uint8_t* body = nullptr;
    std::uint64_t body_cap = 0;
    const char* kn = kind_name(s.k, width);
    if (!kn)
        return false;
    FReg& fr = f.regs[idx];
    bool match = fr.sc_ok && fr.sck == s.k && fr.scw == width;
    if (reg_body(f, idx, &body, &body_cap) && body && body_cap >= width) {
        if (!match && fr.view.head && fr.view.headlen > 18) {
            const char* lt = reinterpret_cast<const char*>(fr.view.head + 18);
            const std::size_t cap = fr.view.headlen - 18;
            const std::size_t n = ::strnlen(lt, cap);
            match = n == std::strlen(kn) && std::memcmp(lt, kn, n) == 0;
        }
        if (match) {
            std::uint64_t u = 0;
            switch (s.k) {
            case Scalar::K::Float:
                if (width == 4) {
                    float fl = (float)s.f;
                    std::uint32_t u32;
                    std::memcpy(&u32, &fl, 4);
                    u = u32;
                } else {
                    std::memcpy(&u, &s.f, 8);
                }
                break;
            case Scalar::K::Int:
                u = (std::uint64_t)s.i;
                break;
            default:
                u = s.u;
                break;
            }
            std::memcpy(body, &u, width);
            fr.sck = s.k;
            fr.scw = width;
            fr.sc_ok = true;
            if (g_prof_on)
                g_prof.w_fast++;
            return true;
        }
    }
    // 回退：编码 + ByRef（槽 langtype/容量不匹配时走这里）。
    if (g_prof_on)
        g_prof.w_fb++;
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    if (!v_encode(kn, width, s, &buf, &len))
        return false;
    bool ok = write_reg(f, idx, buf, len);
    std::free(buf);
    return ok;
}

// 写后 fast 缓存同步（Int/8 → fv；Bool/1 → fb；其余失效）。
void sync_fast_after_write(FrameCtx& f, std::uint8_t idx, const Scalar& s) {
    FReg& r = f.regs[idx];
    r.fast_i64 = false;
    r.fast_bool = false;
    if (s.k == Scalar::K::Int && s.width == 8) {
        r.fv = s.i;
        r.fast_i64 = true;
    } else if (s.k == Scalar::K::Bool && s.width == 1) {
        r.fb = s.u != 0;
        r.fast_bool = true;
    }
}

// ── 值计算（与之前相同）────────────────────────────────────────────

bool truthy(const Scalar& s) {
    switch (s.k) {
    case Scalar::K::Int: return s.i != 0;
    case Scalar::K::Uint:
    case Scalar::K::Bool: return s.u != 0;
    case Scalar::K::Float: return s.f != 0.0;
    default: return false;
    }
}

bool same_class(Scalar::K a, Scalar::K b) {
    if (a == Scalar::K::Bool)
        a = Scalar::K::Uint;
    if (b == Scalar::K::Bool)
        b = Scalar::K::Uint;
    return a == b && (a == Scalar::K::Int || a == Scalar::K::Uint ||
                      a == Scalar::K::Float);
}

const char* kind_name(Scalar::K k, std::uint8_t width) {
    switch (k) {
    case Scalar::K::Int:
        return width == 1 ? "int8" : width == 2 ? "int16" : width == 4 ? "int32" : "int64";
    case Scalar::K::Uint:
        return width == 1 ? "uint8" : width == 2 ? "uint16" : width == 4 ? "uint32" : "uint64";
    case Scalar::K::Float:
        return width == 4 ? "float32" : "float64";
    case Scalar::K::Bool:
        return "bool";
    default:
        return nullptr;
    }
}

// ── int64 快路径：模板特化（编译期内联单指令，无函数指针间接跳）。
template <std::uint8_t O>
static inline std::int64_t fast_i64(std::int64_t a, std::int64_t b) {
    if constexpr (O == 1)  return a + b;                        // Add
    if constexpr (O == 2)  return a - b;                        // Sub
    if constexpr (O == 3)  return a * b;                        // Mul
    if constexpr (O == 4)  return b ? a / b : 0;                // Div（0 哨兵）
    if constexpr (O == 5)  return b ? a % b : 0;                // Mod
    if constexpr (O == 6)  return a > b ? a : b;                // Max
    if constexpr (O == 7)  return a < b ? a : b;                // Min
    if constexpr (O == 8)  return a & b;                        // BitAnd
    if constexpr (O == 9)  return a | b;                        // BitOr
    if constexpr (O == 10) return a ^ b;                        // BitXor
    if constexpr (O == 11) return (std::int64_t)((std::uint64_t)a << (b & 63)); // Shl
    if constexpr (O == 12) return a >> (b & 63);                // Shr
    if constexpr (O == 13) return a == b ? 1 : 0;               // Eq
    if constexpr (O == 14) return a != b ? 1 : 0;               // Neq
    if constexpr (O == 15) return a < b ? 1 : 0;                // Lt
    if constexpr (O == 16) return a <= b ? 1 : 0;               // Le
    if constexpr (O == 17) return a > b ? 1 : 0;                // Gt
    if constexpr (O == 18) return a >= b ? 1 : 0;               // Ge
    return 0;
}

// ── 模板化值计算：switch(Op) → template<Op> constexpr（编译期内联单指令）。
// 返回 false = 该 op 不支持此操作数（Zero 检查失败等）。

template <ops::Op O, bool SGN>
constexpr bool arith_i(const Scalar& a, const Scalar& b, Scalar* r) {
    if constexpr (O == ops::Op::Add) { r->i = a.i + b.i; return true; }
    if constexpr (O == ops::Op::Sub) { r->i = a.i - b.i; return true; }
    if constexpr (O == ops::Op::Mul) { r->i = a.i * b.i; return true; }
    if constexpr (O == ops::Op::Div) {
        if (SGN) { if (b.i == 0) return false; r->i = a.i / b.i; }
        else     { if (b.u == 0) return false; r->u = a.u / b.u; }
        return true;
    }
    if constexpr (O == ops::Op::Mod) {
        if (SGN) { if (b.i == 0) return false; r->i = a.i % b.i; }
        else     { if (b.u == 0) return false; r->u = a.u % b.u; }
        return true;
    }
    if constexpr (O == ops::Op::Max) {
        r->i = SGN ? (a.i > b.i ? a.i : b.i) : (std::int64_t)(a.u > b.u ? a.u : b.u);
        return true;
    }
    if constexpr (O == ops::Op::Min) {
        r->i = SGN ? (a.i < b.i ? a.i : b.i) : (std::int64_t)(a.u < b.u ? a.u : b.u);
        return true;
    }
    if constexpr (O == ops::Op::Neg) { r->i = -a.i; return true; }
    if constexpr (O == ops::Op::Abs) { r->i = a.i < 0 ? -a.i : a.i; return true; }
    if constexpr (O == ops::Op::Sign) { r->i = a.i > 0 ? 1 : a.i < 0 ? -1 : 0; return true; }
    if constexpr (O == ops::Op::BitAnd) { r->i = a.i & b.i; return true; }
    if constexpr (O == ops::Op::BitOr)  { r->i = a.i | b.i; return true; }
    if constexpr (O == ops::Op::BitXor) { r->i = a.i ^ b.i; return true; }
    if constexpr (O == ops::Op::Shl) { r->i = (std::int64_t)(a.u << (b.u & 63)); return true; }
    if constexpr (O == ops::Op::Shr) {
        r->i = SGN ? (a.i >> (b.u & 63)) : (std::int64_t)(a.u >> (b.u & 63));
        return true;
    }
    return false;
}

template <ops::Op O>
constexpr bool arith_f(const Scalar& a, const Scalar& b, Scalar* r) {
    if constexpr (O == ops::Op::Add) { r->f = a.f + b.f; return true; }
    if constexpr (O == ops::Op::Sub) { r->f = a.f - b.f; return true; }
    if constexpr (O == ops::Op::Mul) { r->f = a.f * b.f; return true; }
    if constexpr (O == ops::Op::Div) { r->f = a.f / b.f; return true; }
    if constexpr (O == ops::Op::Max) { r->f = a.f > b.f ? a.f : b.f; return true; }
    if constexpr (O == ops::Op::Min) { r->f = a.f < b.f ? a.f : b.f; return true; }
    if constexpr (O == ops::Op::Neg) { r->f = -a.f; return true; }
    if constexpr (O == ops::Op::Abs) { r->f = a.f < 0 ? -a.f : a.f; return true; }
    if constexpr (O == ops::Op::Sign) { r->f = a.f > 0 ? 1 : a.f < 0 ? -1 : 0; return true; }
    if constexpr (O == ops::Op::Sqrt) { r->f = __builtin_sqrt(a.f); return true; }
    if constexpr (O == ops::Op::Exp) { r->f = __builtin_exp(a.f); return true; }
    if constexpr (O == ops::Op::Log) { r->f = __builtin_log(a.f); return true; }
    if constexpr (O == ops::Op::Pow) { r->f = __builtin_pow(a.f, b.f); return true; }
    return false;
}

template <ops::Op O>
constexpr bool cmp_f(const Scalar& a, const Scalar& b) {
    if constexpr (O == ops::Op::Eq)  return a.f == b.f;
    if constexpr (O == ops::Op::Neq) return a.f != b.f;
    if constexpr (O == ops::Op::Lt)  return a.f < b.f;
    if constexpr (O == ops::Op::Le)  return a.f <= b.f;
    if constexpr (O == ops::Op::Gt)  return a.f > b.f;
    if constexpr (O == ops::Op::Ge)  return a.f >= b.f;
    return false;
}

template <ops::Op O>
constexpr bool cmp_sgn(const Scalar& a, const Scalar& b) {
    if constexpr (O == ops::Op::Eq)  return a.i == b.i;
    if constexpr (O == ops::Op::Neq) return a.i != b.i;
    if constexpr (O == ops::Op::Lt)  return a.i < b.i;
    if constexpr (O == ops::Op::Le)  return a.i <= b.i;
    if constexpr (O == ops::Op::Gt)  return a.i > b.i;
    if constexpr (O == ops::Op::Ge)  return a.i >= b.i;
    return false;
}

template <ops::Op O>
constexpr bool cmp_uns(const Scalar& a, const Scalar& b) {
    if constexpr (O == ops::Op::Eq)  return a.u == b.u;
    if constexpr (O == ops::Op::Neq) return a.u != b.u;
    if constexpr (O == ops::Op::Lt)  return a.u < b.u;
    if constexpr (O == ops::Op::Le)  return a.u <= b.u;
    if constexpr (O == ops::Op::Gt)  return a.u > b.u;
    if constexpr (O == ops::Op::Ge)  return a.u >= b.u;
    return false;
}

// 分派表：Op → 三套模板实例化的函数指针（编译期查表，运行期直呼）。
// 顺序与 ops.hpp 的 Op 枚举一致。
typedef bool (*IntFn)(const Scalar&, const Scalar&, Scalar*);
typedef bool (*FloatFn)(const Scalar&, const Scalar&, Scalar*);
typedef bool (*CmpFn)(const Scalar&, const Scalar&);
static constexpr IntFn kIntTbl[] = {
    arith_i<ops::Op::None, false>, arith_i<ops::Op::Copy, false>, arith_i<ops::Op::Bool, false>,
    arith_i<ops::Op::Br, false>, arith_i<ops::Op::Call, false>, arith_i<ops::Op::Goto, false>,
    arith_i<ops::Op::Debugger, false>, arith_i<ops::Op::Return, false>, arith_i<ops::Op::Cast, false>,
    arith_i<ops::Op::Abs, true>, arith_i<ops::Op::Add, true>, arith_i<ops::Op::Sub, true>,
    arith_i<ops::Op::Mul, true>, arith_i<ops::Op::Div, true>, arith_i<ops::Op::Mod, true>,
    arith_i<ops::Op::Max, true>, arith_i<ops::Op::Min, true>, arith_i<ops::Op::Neg, true>,
    arith_i<ops::Op::Sign, true>, arith_i<ops::Op::BitAnd, true>, arith_i<ops::Op::BitOr, true>,
    arith_i<ops::Op::BitXor, true>, arith_i<ops::Op::Shl, true>, arith_i<ops::Op::Shr, true>,
    arith_i<ops::Op::Eq, true>, arith_i<ops::Op::Neq, true>, arith_i<ops::Op::Lt, true>,
    arith_i<ops::Op::Le, true>, arith_i<ops::Op::Gt, true>, arith_i<ops::Op::Ge, true>,
    arith_i<ops::Op::Sqrt, true>, arith_i<ops::Op::Exp, true>, arith_i<ops::Op::Log, true>,
    arith_i<ops::Op::Pow, true>, arith_i<ops::Op::And, true>, arith_i<ops::Op::Or, true>,
    arith_i<ops::Op::Not, true>,
};
static constexpr FloatFn kFloatTbl[] = {
    arith_f<ops::Op::None>, arith_f<ops::Op::Copy>, arith_f<ops::Op::Bool>,
    arith_f<ops::Op::Br>, arith_f<ops::Op::Call>, arith_f<ops::Op::Goto>,
    arith_f<ops::Op::Debugger>, arith_f<ops::Op::Return>, arith_f<ops::Op::Cast>,
    arith_f<ops::Op::Abs>, arith_f<ops::Op::Add>, arith_f<ops::Op::Sub>,
    arith_f<ops::Op::Mul>, arith_f<ops::Op::Div>, arith_f<ops::Op::Mod>,
    arith_f<ops::Op::Max>, arith_f<ops::Op::Min>, arith_f<ops::Op::Neg>,
    arith_f<ops::Op::Sign>, arith_f<ops::Op::BitAnd>, arith_f<ops::Op::BitOr>,
    arith_f<ops::Op::BitXor>, arith_f<ops::Op::Shl>, arith_f<ops::Op::Shr>,
    arith_f<ops::Op::Eq>, arith_f<ops::Op::Neq>, arith_f<ops::Op::Lt>,
    arith_f<ops::Op::Le>, arith_f<ops::Op::Gt>, arith_f<ops::Op::Ge>,
    arith_f<ops::Op::Sqrt>, arith_f<ops::Op::Exp>, arith_f<ops::Op::Log>,
    arith_f<ops::Op::Pow>, arith_f<ops::Op::And>, arith_f<ops::Op::Or>,
    arith_f<ops::Op::Not>,
};
static constexpr CmpFn kCmpSgnTbl[] = {
    cmp_sgn<ops::Op::None>, cmp_sgn<ops::Op::Copy>, cmp_sgn<ops::Op::Bool>,
    cmp_sgn<ops::Op::Br>, cmp_sgn<ops::Op::Call>, cmp_sgn<ops::Op::Goto>,
    cmp_sgn<ops::Op::Debugger>, cmp_sgn<ops::Op::Return>, cmp_sgn<ops::Op::Cast>,
    cmp_sgn<ops::Op::Abs>, cmp_sgn<ops::Op::Add>, cmp_sgn<ops::Op::Sub>,
    cmp_sgn<ops::Op::Mul>, cmp_sgn<ops::Op::Div>, cmp_sgn<ops::Op::Mod>,
    cmp_sgn<ops::Op::Max>, cmp_sgn<ops::Op::Min>, cmp_sgn<ops::Op::Neg>,
    cmp_sgn<ops::Op::Sign>, cmp_sgn<ops::Op::BitAnd>, cmp_sgn<ops::Op::BitOr>,
    cmp_sgn<ops::Op::BitXor>, cmp_sgn<ops::Op::Shl>, cmp_sgn<ops::Op::Shr>,
    cmp_sgn<ops::Op::Eq>, cmp_sgn<ops::Op::Neq>, cmp_sgn<ops::Op::Lt>,
    cmp_sgn<ops::Op::Le>, cmp_sgn<ops::Op::Gt>, cmp_sgn<ops::Op::Ge>,
    cmp_sgn<ops::Op::Sqrt>, cmp_sgn<ops::Op::Exp>, cmp_sgn<ops::Op::Log>,
    cmp_sgn<ops::Op::Pow>, cmp_sgn<ops::Op::And>, cmp_sgn<ops::Op::Or>,
    cmp_sgn<ops::Op::Not>,
};
static constexpr CmpFn kCmpUnsTbl[] = {
    cmp_uns<ops::Op::None>, cmp_uns<ops::Op::Copy>, cmp_uns<ops::Op::Bool>,
    cmp_uns<ops::Op::Br>, cmp_uns<ops::Op::Call>, cmp_uns<ops::Op::Goto>,
    cmp_uns<ops::Op::Debugger>, cmp_uns<ops::Op::Return>, cmp_uns<ops::Op::Cast>,
    cmp_uns<ops::Op::Abs>, cmp_uns<ops::Op::Add>, cmp_uns<ops::Op::Sub>,
    cmp_uns<ops::Op::Mul>, cmp_uns<ops::Op::Div>, cmp_uns<ops::Op::Mod>,
    cmp_uns<ops::Op::Max>, cmp_uns<ops::Op::Min>, cmp_uns<ops::Op::Neg>,
    cmp_uns<ops::Op::Sign>, cmp_uns<ops::Op::BitAnd>, cmp_uns<ops::Op::BitOr>,
    cmp_uns<ops::Op::BitXor>, cmp_uns<ops::Op::Shl>, cmp_uns<ops::Op::Shr>,
    cmp_uns<ops::Op::Eq>, cmp_uns<ops::Op::Neq>, cmp_uns<ops::Op::Lt>,
    cmp_uns<ops::Op::Le>, cmp_uns<ops::Op::Gt>, cmp_uns<ops::Op::Ge>,
    cmp_uns<ops::Op::Sqrt>, cmp_uns<ops::Op::Exp>, cmp_uns<ops::Op::Log>,
    cmp_uns<ops::Op::Pow>, cmp_uns<ops::Op::And>, cmp_uns<ops::Op::Or>,
    cmp_uns<ops::Op::Not>,
};
static constexpr CmpFn kCmpFloatTbl[] = {
    cmp_f<ops::Op::None>, cmp_f<ops::Op::Copy>, cmp_f<ops::Op::Bool>,
    cmp_f<ops::Op::Br>, cmp_f<ops::Op::Call>, cmp_f<ops::Op::Goto>,
    cmp_f<ops::Op::Debugger>, cmp_f<ops::Op::Return>, cmp_f<ops::Op::Cast>,
    cmp_f<ops::Op::Abs>, cmp_f<ops::Op::Add>, cmp_f<ops::Op::Sub>,
    cmp_f<ops::Op::Mul>, cmp_f<ops::Op::Div>, cmp_f<ops::Op::Mod>,
    cmp_f<ops::Op::Max>, cmp_f<ops::Op::Min>, cmp_f<ops::Op::Neg>,
    cmp_f<ops::Op::Sign>, cmp_f<ops::Op::BitAnd>, cmp_f<ops::Op::BitOr>,
    cmp_f<ops::Op::BitXor>, cmp_f<ops::Op::Shl>, cmp_f<ops::Op::Shr>,
    cmp_f<ops::Op::Eq>, cmp_f<ops::Op::Neq>, cmp_f<ops::Op::Lt>,
    cmp_f<ops::Op::Le>, cmp_f<ops::Op::Gt>, cmp_f<ops::Op::Ge>,
    cmp_f<ops::Op::Sqrt>, cmp_f<ops::Op::Exp>, cmp_f<ops::Op::Log>,
    cmp_f<ops::Op::Pow>, cmp_f<ops::Op::And>, cmp_f<ops::Op::Or>,
    cmp_f<ops::Op::Not>,
};

// 通用数值计算入口：查表直呼（不再 switch）。
bool compute_int(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b, Scalar* r) {
    const auto o = static_cast<std::size_t>(op);
    if (o >= sizeof kIntTbl / sizeof kIntTbl[0])
        return false;
    *r = a;
    if (k == Scalar::K::Int) {
        if (!kIntTbl[o](a, b, r))
            return false;
        r->u = (std::uint64_t)r->i;
        return true;
    }
    if (!kIntTbl[o](a, b, r))
        return false;
    r->u = (std::uint64_t)r->i;
    r->i = (std::int64_t)r->u;
    return true;
}

bool compute_float(ops::Op op, double a, double b, double* r) {
    Scalar x, y;
    x.f = a;
    y.f = b;
    Scalar out;
    const auto o = static_cast<std::size_t>(op);
    if (o >= sizeof kFloatTbl / sizeof kFloatTbl[0])
        return false;
    if (!kFloatTbl[o](x, y, &out))
        return false;
    *r = out.f;
    return true;
}

bool compare(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b, bool* out) {
    const auto o = static_cast<std::size_t>(op);
    if (o >= sizeof kCmpSgnTbl / sizeof kCmpSgnTbl[0])
        return false;
    switch (k) {
    case Scalar::K::Int:  *out = kCmpSgnTbl[o](a, b); return true;
    case Scalar::K::Uint: case Scalar::K::Bool: *out = kCmpUnsTbl[o](a, b); return true;
    case Scalar::K::Float: *out = kCmpFloatTbl[o](a, b); return true;
    default: return false;
    }
}

bool is_cmp(ops::Op op) {
    return op == ops::Op::Eq || op == ops::Op::Neq || op == ops::Op::Lt ||
           op == ops::Op::Le || op == ops::Op::Gt || op == ops::Op::Ge;
}

bool raw_i64(Wire w, std::int64_t* out) {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0 || !xh.body || xh.content_len < 8)
        return false;
    std::memcpy(out, xh.body, 8);
    return true;
}

bool write_named_i64(void* h, std::uint8_t idx, FrameCtx& f, const char* kind,
                     std::int64_t v) {
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    if (kvspaceXvalueEncode(kind, raw, 8, nullptr, 0, &buf, &len) != 0 || !buf)
        return false;
    bool ok = write_reg(f, idx, buf, len);
    std::free(buf);
    return ok;
}

}  // namespace

RunResult run_frame(FrameCtx& f, int start_s0, std::string* stop_pc,
                    std::string* err) {
    prof_init();
    if (stop_pc)
        stop_pc->clear();
    const Segment* seg = cached_segment(f.h, f.func_dir);
    if (!seg) {
        *err = "build_segment failed: " + f.func_dir;
        return RunResult::Error;
    }
    if (f.regs.size() != seg->regs.size()) {
        f.regs.assign(seg->regs.size(), FReg{});
        for (size_t i = 0; i < seg->regs.size(); i++) {
            const RegDesc& d = seg->regs[i];
            if (d.kind == Operand::Kind::Literal) {
                f.regs[i].key = d.slot_key;
            } else if (d.kind == Operand::Kind::Var) {
                if (!resolve_desc(f, d.name, &f.regs[i].key, err))
                    return RunResult::Error;
            }
        }
    }
    const int n = static_cast<int>(seg->insns.size());
    int idx = start_s0 > 0 ? start_s0 - 1 : 0;
    if (idx < 0 || idx >= n) {
        if (idx == n)
            return RunResult::Ok;
        *err = "start s0 out of segment";
        return RunResult::Error;
    }
    static unsigned long long g_run_steps = 0;
    if (g_prof_on)
        g_prof.frames++;
    for (; idx < n; idx++) {
        const unsigned long long t0 = g_prof_on ? now() : 0;
        const CInsn& ins = seg->insns[idx];
        if (++g_run_steps > 50000000ULL) {
            *err = "runaway loop: " + f.func_dir + " s0=" + std::to_string(ins.s0) +
                   " op=" + ins.name;
            return RunResult::Error;
        }
        if (g_trace_on && g_run_steps < g_trace_n)
            std::fprintf(stderr, "[%s d%d s0=%d op=%s impl=%d]\n",
                         f.func_dir.c_str(), f.depth, ins.s0, ins.name.c_str(),
                         (int)ins.impl);
        if (ins.impl == ops::Impl::Handoff || ins.impl == ops::Impl::None) {
            if (g_trace_on)
                std::fprintf(stderr, "[stop %s d%d s0=%d %s]\n",
                             f.func_dir.c_str(), f.depth, ins.s0, ins.name.c_str());
            if (stop_pc)
                *stop_pc = param_key(f.frame, ins.s0, 0);
            return RunResult::Stop;
        }
        if (ins.op == ops::Op::Goto || ins.op == ops::Op::Br) {
            std::int64_t target_v = 0;
            FReg &tr = f.regs[ins.rd[ins.op == ops::Op::Br ? 1 : 0]];
            if (!(tr.fast_i64 ? (target_v = tr.fv, true)
                              : read_i64(f, ins.rd[ins.op == ops::Op::Br ? 1 : 0],
                                         &target_v))) {
                *err = "control: bad target";
                return RunResult::Error;
            }
            int dest = static_cast<int>(target_v);
            if (ins.op == ops::Op::Br) {
                bool cond = false;
                FReg &cr = f.regs[ins.rd[0]];
                if (!(cr.fast_bool ? (cond = cr.fb, true)
                                   : read_bool(f, ins.rd[0], &cond))) {
                    *err = "br: bad cond";
                    return RunResult::Error;
                }
                if (!cond) {
                    FReg &fr = f.regs[ins.rd[2]];
                    if (!(fr.fast_i64 ? (target_v = fr.fv, true)
                                      : read_i64(f, ins.rd[2], &target_v))) {
                        *err = "br: bad false target";
                        return RunResult::Error;
                    }
                    dest = static_cast<int>(target_v);
                }
            }
            if (dest < 1 || dest > n + 1) {
                *err = "control: target out of range";
                return RunResult::Error;
            }
            idx = dest - 2;
            if (g_prof_on) {
                const std::uint64_t d = now() - t0;
                g_prof.ctrl += d;
                g_prof.steps++;
                prof_tick(ins.prof_id, 0, d);
            }
            continue;
        }
        if (ins.op == ops::Op::Return) {
            if (g_prof_on) {
                g_prof.loop += now() - t0;
                g_prof.steps++;
            }
            if (g_prof_on)
                prof_tick(ins.prof_id, 0, 0);
            if (f.depth <= 1)
                return RunResult::Ok;  // 入口帧（depth=1）return = vthread done
            std::string ret;
            if (!read_text(f.h, f.frame + "\xE2\x80\xA5" "returnpc", &ret) ||
                ret.empty()) {
                *err = "return: missing returnpc in " + f.frame;
                return RunResult::Error;
            }
            char e[192] = {};
            kvspaceDelTree(f.h, f.frame.c_str(), e, sizeof e);
            if (stop_pc)
                *stop_pc = ret;
            return RunResult::Returned;
        }
        if (ins.impl == ops::Impl::Native) {
            Wire in[2]{};
            for (int i = 0; i < ins.nrd && i < 2; i++) {
                if (!read_reg(f, ins.rd[i], &in[i])) {
                    *err = "native: missing operand " + std::to_string(i) + ": " +
                           ins.name;
                    return RunResult::Error;
                }
            }
            auto ai64 = [&](int i, std::int64_t* v) {
                Scalar sc{};
                if (v_scalar(in[i], &sc)) {
                    *v = sc.k == Scalar::K::Int     ? sc.i
                         : sc.k == Scalar::K::Float ? (std::int64_t)sc.f
                                                    : (std::int64_t)sc.u;
                    return true;
                }
                // time/duration 等非数值 langtype：body 即 8 字节整数。
                kvspaceXh xh{};
                if (kvspaceXhDecode(in[i].d, in[i].n, &xh) != 0 ||
                    xh.content_len != 8)
                    return false;
                std::memcpy(v, xh.body, 8);
                return true;
            };
            auto atext = [&](int i, std::string* out) {
                return wire_text(in[i].d, in[i].n, out);
            };
            auto wbool = [&](bool b) {
                Scalar sc{};
                sc.k = Scalar::K::Bool;
                sc.width = 1;
                sc.u = b ? 1 : 0;
                return slot_write_scalar(f, ins.wr[0], sc, 1);
            };
            // `·`(U+00B7)→`/` 后按族名分派（与 opcode 一一对应）。
            std::string fn;
            fn.reserve(ins.name.size());
            for (std::size_t i = 0; i < ins.name.size();) {
                if (i + 2 < ins.name.size() &&
                    static_cast<unsigned char>(ins.name[i]) == 0xC2 &&
                    static_cast<unsigned char>(ins.name[i + 1]) == 0xB7) {
                    fn += '/';
                    i += 2;
                } else {
                    fn += ins.name[i++];
                }
            }
            std::int64_t v0 = 0, v1 = 0;
            bool ok = true;
            char errbuf[256] = {};
            if (fn == "time/now") {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                const std::int64_t ns =
                    (std::int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
                ok = write_named_i64(f.h, ins.wr[0], f, "time", ns);
            } else if (fn == "time/add" || fn == "time/sub" ||
                       fn == "time/before" || fn == "time/after" ||
                       fn == "time/duration/add" || fn == "time/duration/sub" ||
                       fn == "time/duration/before" || fn == "time/duration/after") {
                ok = ai64(0, &v0) && ai64(1, &v1);
                if (!ok) {
                    *err = "native: bad operands: " + ins.name;
                    return RunResult::Error;
                }
                if (fn == "time/before" || fn == "time/duration/before")
                    ok = wbool(v0 < v1);
                else if (fn == "time/after" || fn == "time/duration/after")
                    ok = wbool(v0 > v1);
                else if (fn == "time/add")
                    ok = write_named_i64(f.h, ins.wr[0], f, "time", v0 + v1);
                else
                    ok = write_named_i64(f.h, ins.wr[0], f, "duration", v0 - v1);
            } else if (fn == "time/duration/nanos" || fn == "time/duration/millis" ||
                       fn == "time/duration/seconds" ||
                       fn == "time/duration/minutes" ||
                       fn == "time/duration/hours") {
                ok = ai64(0, &v0);
                std::int64_t mul = 1;
                if (fn == "time/duration/millis") mul = 1000000;
                else if (fn == "time/duration/seconds") mul = 1000000000;
                else if (fn == "time/duration/minutes") mul = 60000000000LL;
                else if (fn == "time/duration/hours") mul = 3600000000000LL;
                ok = ok && write_named_i64(f.h, ins.wr[0], f, "duration", v0 * mul);
            } else if (fn.rfind("time/duration/as_", 0) == 0) {
                ok = ai64(0, &v0);
                std::int64_t div = 1;
                if (fn.find("millis") != std::string::npos) div = 1000000;
                else if (fn.find("seconds") != std::string::npos) div = 1000000000;
                else if (fn.find("minutes") != std::string::npos) div = 60000000000LL;
                else if (fn.find("hours") != std::string::npos) div = 3600000000000LL;
                Scalar outv{};
                outv.k = Scalar::K::Int;
                outv.width = 8;
                outv.i = v0 / div;
                ok = ok && slot_write_scalar(f, ins.wr[0], outv, 8);
            } else if (fn == "random/int63" || fn == "random/uint64" ||
                       fn == "random/intn") {
                std::uint64_t r = 0;
                if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) {
                    *err = "native: getrandom failed";
                    return RunResult::Error;
                }
                if (fn == "random/intn") {
                    ok = ai64(0, &v0);
                    const std::uint64_t m = (std::uint64_t)v0;
                    r = m == 0 ? 0 : r % m;
                    ok = ok && write_named_i64(f.h, ins.wr[0], f, "uint64",
                                               (std::int64_t)r);
                } else if (fn == "random/int63") {
                    ok = write_named_i64(f.h, ins.wr[0], f, "int64",
                                         (std::int64_t)(r >> 1));
                } else {
                    ok = write_named_i64(f.h, ins.wr[0], f, "uint64",
                                         (std::int64_t)r);
                }
            } else if (fn == "kvspace/set" || fn == "kvspace/get" ||
                       fn == "kvspace/del" || fn == "kvspace/deltree" ||
                       fn == "kvspace/mkindex" || fn == "kvspace/listlen" ||
                       fn == "kvspace/listat" || fn == "kvspace/cp" ||
                       fn == "kvspace/cpdir" || fn == "kvspace/cplist" ||
                       fn == "kvspace/extindex" || fn == "kvspace/rmindexext") {
                std::string k0, k1;
                ok = ins.nrd >= 1 && atext(0, &k0) && (ins.nrd < 2 || atext(1, &k1));
                if (!ok) {
                    *err = "native: bad path operand: " + ins.name;
                    return RunResult::Error;
                }
                if (fn == "kvspace/set") {
                    ok = ins.nrd == 2 &&
                         kvspaceSetValue(f.h, k0.c_str(), in[1].d, in[1].n, 0, 0,
                                         errbuf, sizeof errbuf) == 0;
                } else if (fn == "kvspace/get") {
                    const std::uint8_t* d = nullptr;
                    std::uint32_t n = 0;
                    ok = read_wire(f.h, k0, &d, &n) &&
                         write_reg(f, ins.wr[0], d, n);
                } else if (fn == "kvspace/del") {
                    const char* keys[1] = {k0.c_str()};
                    ok = kvspaceDel(f.h, keys, 1, errbuf, sizeof errbuf) == 0;
                } else if (fn == "kvspace/deltree") {
                    ok = kvspaceDelTree(f.h, k0.c_str(), errbuf, sizeof errbuf) == 0;
                } else if (fn == "kvspace/mkindex") {
                    std::uint32_t cap = 0;
                    if (ins.nrd >= 2) {
                        std::int64_t c = 0;
                        ok = ai64(1, &c);
                        cap = (std::uint32_t)c;
                    }
                    ok = ok && kvspaceMkindex(f.h, k0.c_str(), cap, errbuf,
                                              sizeof errbuf) == 0;
                } else if (fn == "kvspace/listlen") {
                    std::int32_t cnt = 0;
                    ok = kvspaceListLen(f.h, k0.c_str(), 0, 0, &cnt) == 0 &&
                         write_named_i64(f.h, ins.wr[0], f, "int64", cnt);
                } else if (fn == "kvspace/listat") {
                    std::int64_t idx = 0;
                    std::uint8_t name[512];
                    std::uint32_t nn = 0;
                    ok = ai64(1, &idx) &&
                         kvspaceListAt(f.h, k0.c_str(), 0, 0, (std::int32_t)idx,
                                       name, sizeof name, &nn) == 0;
                    if (ok)
                        ok = write_text(
                            f.h, f.regs[ins.wr[0]].key,
                            std::string_view(reinterpret_cast<const char*>(name), nn));
                } else if (fn == "kvspace/cp" || fn == "kvspace/cpdir" ||
                           fn == "kvspace/cplist") {
                    int rc = fn == "kvspace/cp"
                                 ? kvspaceCp(f.h, k0.c_str(), k1.c_str(), errbuf,
                                             sizeof errbuf)
                                 : fn == "kvspace/cpdir"
                                       ? kvspaceCpTree(f.h, k0.c_str(), k1.c_str(),
                                                       errbuf, sizeof errbuf)
                                       : kvspaceCpList(f.h, k0.c_str(), k1.c_str(),
                                                       errbuf, sizeof errbuf);
                    ok = rc == 0;
                } else if (fn == "kvspace/extindex") {
                    ok = kvspaceMkindexExt(f.h, k0.c_str(), k1.c_str(), errbuf,
                                           sizeof errbuf) == 0;
                } else if (fn == "kvspace/rmindexext") {
                    ok = kvspaceRmindexExt(f.h, k0.c_str(), errbuf,
                                           sizeof errbuf) == 0;
                } else {
                    ok = false;
                    *err = "native op not implemented: " + ins.name;
                    return RunResult::Error;
                }
                if (!ok && errbuf[0])
                    *err = std::string(ins.name) + ": " + errbuf;
            } else {
                *err = "native op not implemented: " + ins.name;
                return RunResult::Error;
            }
            if (!ok) {
                if (err->empty())
                    *err = "native: write failed: " + ins.name;
                return RunResult::Error;
            }
            if (g_prof_on) {
                const std::uint64_t d = now() - t0;
                g_prof.handler += d;
                g_prof.steps++;
                prof_tick(ins.prof_id, d, 0);
            }
            continue;
        }
        if (ins.op == ops::Op::Call) {
            const std::string fn = ins.callee;
            if (fn.empty()) {
                *err = "call: empty callee";
                return RunResult::Error;
            }
            const std::string callee = fn.rfind("/lib/", 0) == 0 ? fn : "/lib/" + fold_op(fn);
            const std::string callee_dir = callee + "/";
            const std::string nf = frame_root(f.vid, f.depth + 1);
            const std::string nstack = nf + "/";
            const std::string entry = nf + "/[1,0]";
            char e[192] = {};
            kvspaceDelTree(f.h, nf.c_str(), e, sizeof e);
            if (kvspaceMkindex(f.h, nstack.c_str(), 0, e, sizeof e) != 0 ||
                kvspaceMkindexExt(f.h, nstack.c_str(), callee_dir.c_str(), e, sizeof e) != 0) {
                *err = std::string("call: frame init failed: ") + e;
                return RunResult::Error;
            }
            const std::string retpc = param_key(f.frame, ins.s0 + 1, 0);
            if (!write_text(f.h, nf + "\xE2\x80\xA5" "returnpc", retpc) ||
                !write_text(f.h, nf + "\xE2\x80\xA5" "callpc", entry) ||
                !write_text(f.h, nstack + "\xE2\x80\xA5" "lib", callee)) {
                *err = "call: system vars failed";
                return RunResult::Error;
            }
            auto bind_arg = [&](int x, const std::string& value_key,
                                const std::string& typ) -> bool {
                const std::string xs = std::to_string(x);
                const std::string arg_key = nf + "/\xE2\x80\xA5" "arg[0," + xs + "]";
                const std::string slot = nf + "/[0," + xs + "]";
                std::uint8_t* a = nullptr;
                std::uint8_t* sp = nullptr;
                std::uint32_t an = 0, sn = 0;
                bool ok = kvspaceNewPtr(typ.c_str(), value_key.c_str(), &a, &an) == 0;
                if (!ok) {
                    *err = "bind: NewPtr(value) failed typ=" + typ;
                    return false;
                }
                ok = kvspaceNewPtr(typ.c_str(), arg_key.c_str(), &sp, &sn) == 0;
                if (!ok) {
                    *err = "bind: NewPtr(arg) failed";
                    std::free(a);
                    return false;
                }
                char be[192] = {};
                ok = kvspaceSetValue(f.h, arg_key.c_str(), a, an, 0, 0, be, sizeof be) == 0 &&
                     kvspaceSetValue(f.h, slot.c_str(), sp, sn, 0, 0, be, sizeof be) == 0;
                if (!ok)
                    *err = std::string("bind: set failed: ") + be;
                std::free(a);
                std::free(sp);
                return ok;
            };
            for (int i = 1; i < ins.nrd; i++) {
                Wire vw{};
                std::string vk, typ;
                if (!read_reg(f, ins.rd[i], &vw)) {
                    *err = "call: bad arg " + std::to_string(i);
                    return RunResult::Error;
                }
                const RegDesc& rd = seg->regs[ins.rd[i]];
                if (rd.kind == Operand::Kind::Literal) {
                    vk = nf + "/._lit" + std::to_string(i);
                    char be[192] = {};
                    if (kvspaceSetValue(f.h, vk.c_str(), vw.d, vw.n, 0, 0, be, sizeof be) != 0) {
                        *err = std::string("call: literal arg failed: ") + be;
                        return RunResult::Error;
                    }
                } else {
                    vk = f.regs[ins.rd[i]].key;
                }
                if (!v_langtype(vw, &typ) || typ.empty())
                    typ = "int64";
                if (!bind_arg(-i, vk, typ))
                    return RunResult::Error;
            }
            for (int i = 1; i <= ins.nwr; i++) {
                const std::string vk = f.regs[ins.wr[i - 1]].key;
                Wire vw{};
                std::string typ = "int64";
                if (read_reg(f, ins.wr[i - 1], &vw))
                    (void)v_langtype(vw, &typ);
                if (!bind_arg(i, vk, typ))
                    return RunResult::Error;
            }
            FrameCtx nfc;
            nfc.h = f.h;
            nfc.vid = f.vid;
            nfc.depth = f.depth + 1;
            nfc.frame = nf;
            nfc.func_dir = callee;
            RunResult rr = run_frame(nfc, 1, stop_pc, err);
            if (rr != RunResult::Ok && rr != RunResult::Returned)
                return rr;
            if (g_prof_on)
                g_prof.steps++;  // 嵌套帧自身已计时，勿重复累计
            continue;
        }
        // 快路径：int64×int64（iops 热循环主力），不经 Scalar/通用 switch。
        // fast_i64 由 build_segment 预标，每步只判一位。
        if (ins.fast_i64) {
            // 直取两操作数：fast 缓存命中（热循环稳态）零判定；未命中回退 read_i64。
            FReg &r0 = f.regs[ins.rd[0]], &r1 = f.regs[ins.rd[1]];
            std::int64_t a = 0, b = 0;
            bool got =
                (r0.fast_i64 ? (a = r0.fv, true)
                             : read_i64(f, ins.rd[0], &a)) &&
                (r1.fast_i64 ? (b = r1.fv, true)
                             : read_i64(f, ins.rd[1], &b));
            if (got) {
                std::int64_t rv = 0;
                bool ok2 = true;
                // 模板分派：每个 case 内联成单指令（除 Div/Mod 的 0 哨兵）。
                switch (ins.opfn) {
#define C2_FAST_CASE(O) case O: rv = fast_i64<O>(a, b); break
                C2_FAST_CASE(1);  C2_FAST_CASE(2);  C2_FAST_CASE(3);
                C2_FAST_CASE(4);  C2_FAST_CASE(5);  C2_FAST_CASE(6);
                C2_FAST_CASE(7);  C2_FAST_CASE(8);  C2_FAST_CASE(9);
                C2_FAST_CASE(10); C2_FAST_CASE(11); C2_FAST_CASE(12);
                default: break;
                }
                if (ins.opfn <= 12) {
                    if (ins.opfn == 4 || ins.opfn == 5) {
                        if (b == 0) {
                            *err = "division by zero: " + ins.name;
                            return RunResult::Error;
                        }
                    }
                    FReg &w = f.regs[ins.wr[0]];
                    if (w.fast_i64) {
                        std::uint8_t *wb = nullptr;
                        std::uint64_t wcap = 0;
                        if (reg_body(f, ins.wr[0], &wb, &wcap) && wb && wcap >= 8) {
                            std::memcpy(wb, &rv, 8);
                            w.fv = rv;
                        } else {
                            w.fast_i64 = false;
                            ok2 = slot_write_i64(f, ins.wr[0], rv);
                        }
                    } else {
                        ok2 = slot_write_i64(f, ins.wr[0], rv);
                    }
                } else {
                    switch (ins.opfn) {
                    C2_FAST_CASE(13); C2_FAST_CASE(14); C2_FAST_CASE(15);
                    C2_FAST_CASE(16); C2_FAST_CASE(17); C2_FAST_CASE(18);
                    default: break;
                    }
                    // 比较结果直写 bool 槽：fast_bool 命中时内联 memcpy（免函数调用）。
                    const bool rb = rv != 0;
                    FReg &wb = f.regs[ins.wr[0]];
                    if (wb.fast_bool ||
                        (wb.sc_ok && wb.sck == Scalar::K::Bool && wb.scw == 1)) {
                        std::uint8_t *bb = nullptr;
                        std::uint64_t bcap = 0;
                        if (reg_body(f, ins.wr[0], &bb, &bcap) && bb && bcap >= 1) {
                            *bb = rb ? 1 : 0;
                            wb.fb = rb;
                            wb.fast_bool = true;
                            ok2 = true;
                        } else {
                            wb.fast_bool = false;
                            ok2 = slot_write_bool(f, ins.wr[0], rb);
                        }
                    } else {
                        ok2 = slot_write_bool(f, ins.wr[0], rb);
                    }
                }
#undef C2_FAST_CASE
                if (g_prof_on) {
                    const std::uint64_t dh = now() - t0;
                    g_prof.handler += dh;
                    g_prof.steps++;
                    prof_tick(ins.prof_id, dh, 0);
                }
                if (!ok2) {
                    *err = "value op failed: " + ins.name;
                    return RunResult::Error;
                }
                continue;
            }
        }
        // 值算子
        Wire w[4]{};
        Scalar s[4]{};
        bool non_scalar = false;
        if (ins.op != ops::Op::Copy) {
            for (int i = 0; i < ins.nrd; i++) {
                if (read_scalar(f, ins.rd[i], &s[i]))
                    continue;
                if (!read_reg(f, ins.rd[i], &w[i])) {
                    *err = "operand " + std::to_string(i) + " is missing";
                    return RunResult::Error;
                }
                non_scalar = true;
            }
        }
        bool ok = true;
        if (non_scalar) {
            // 动态算子按运行时类型分派：`+` 对 char 族即字符串拼接。
            std::string a, b;
            if (ins.op != ops::Op::Add || ins.nrd != 2 ||
                !wire_text(w[0].d, w[0].n, &a) ||
                !wire_text(w[1].d, w[1].n, &b)) {
                *err = "non-scalar operand: " + ins.name;
                return RunResult::Error;
            }
            const std::string out_s = a + b;
            const std::int32_t dims[1] = {static_cast<std::int32_t>(out_s.size())};
            std::uint8_t* buf = nullptr;
            std::uint32_t len = 0;
            ok = kvspaceXvalueEncode(
                     "char/utf8",
                     reinterpret_cast<const std::uint8_t*>(out_s.data()),
                     static_cast<std::uint32_t>(out_s.size()), dims, 1, &buf,
                     &len) == 0 &&
                 buf && write_reg(f, ins.wr[0], buf, len);
            std::free(buf);
            if (g_prof_on) {
                const std::uint64_t dh = now() - t0;
                g_prof.handler += dh;
                g_prof.steps++;
                prof_tick(ins.prof_id, dh, 0);
            }
            if (!ok) {
                *err = "value op failed: " + ins.name;
                return RunResult::Error;
            }
            continue;
        }
        if (ins.op == ops::Op::Copy) {
            Wire src{};
            if (read_reg(f, ins.rd[0], &src)) {
                ok = write_reg(f, ins.wr[0], src.d, src.n);
            } else {
                // 未赋值变量读作 None：整体写 None XValue，不是错误。
                std::uint8_t* none = nullptr;
                std::uint64_t nn = 0;
                ok = kvspaceXhNewNone(&none, &nn) == 0 && none &&
                     write_reg(f, ins.wr[0], none, static_cast<std::uint32_t>(nn));
                std::free(none);
            }
        } else if (ins.op == ops::Op::Bool) {
            Scalar r{};
            r.k = Scalar::K::Bool;
            r.width = 1;
            r.u = truthy(s[0]) ? 1 : 0;
            ok = slot_write_scalar(f, ins.wr[0], r, 1);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], r);
        } else if (ins.op == ops::Op::Cast) {
            Scalar r{};
            r.k = ins.kind == ops::Kind::Int     ? Scalar::K::Int
                  : ins.kind == ops::Kind::Uint  ? Scalar::K::Uint
                  : ins.kind == ops::Kind::Float ? Scalar::K::Float
                                                 : Scalar::K::None;
            r.width = ins.width;
            const Scalar& src = s[0];
            if (r.k == Scalar::K::Float) {
                r.f = src.k == Scalar::K::Float   ? src.f
                      : src.k == Scalar::K::Uint  ? (double)src.u
                                                  : (double)src.i;
            } else if (r.k == Scalar::K::Uint) {
                r.u = src.k == Scalar::K::Float  ? (std::uint64_t)(std::int64_t)src.f
                      : src.k == Scalar::K::Uint ? src.u
                                                 : (std::uint64_t)src.i;
                r.i = (std::int64_t)r.u;
            } else {
                r.i = src.k == Scalar::K::Float  ? (std::int64_t)src.f
                      : src.k == Scalar::K::Uint ? (std::int64_t)src.u
                                                 : src.i;
                r.u = (std::uint64_t)r.i;
            }
            ok = slot_write_scalar(f, ins.wr[0], r, r.width);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], r);
        } else if (ins.op == ops::Op::And || ins.op == ops::Op::Or ||
                   ins.op == ops::Op::Not) {
            bool a = truthy(s[0]);
            bool b = ins.nrd > 1 ? truthy(s[1]) : false;
            bool r = ins.op == ops::Op::And ? (a && b)
                     : ins.op == ops::Op::Or ? (a || b)
                                             : !a;
            Scalar out{};
            out.k = Scalar::K::Bool;
            out.width = 1;
            out.u = r ? 1 : 0;
            ok = slot_write_scalar(f, ins.wr[0], out, 1);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], out);
        } else if (is_cmp(ins.op)) {
            bool r = false;
            Scalar a = s[0], b = s[1];
            if ((a.k == Scalar::K::Uint && b.k == Scalar::K::Int) ||
                (a.k == Scalar::K::Int && b.k == Scalar::K::Uint)) {
                // 有符号/无符号混合：按 C 提升语义统一到能容纳的一侧。
                const bool a_u = a.k == Scalar::K::Uint;
                const std::uint64_t uv = a_u ? a.u : b.u;
                const std::int64_t iv = a_u ? b.i : a.i;
                if (iv < 0 || uv > (std::uint64_t)INT64_MAX) {
                    a.k = b.k = Scalar::K::Uint;
                    a.u = a_u ? a.u : (std::uint64_t)iv;
                    b.u = a_u ? (std::uint64_t)iv : b.u;
                } else {
                    a.k = b.k = Scalar::K::Int;
                    a.i = a_u ? (std::int64_t)uv : a.i;
                    b.i = a_u ? b.i : (std::int64_t)uv;
                }
            } else if (a.k != Scalar::K::Float && b.k == Scalar::K::Float) {
                a.k = Scalar::K::Float;
                a.f = (double)(s[0].k == Scalar::K::Uint ? (double)s[0].u
                                                        : (double)s[0].i);
            } else if (b.k != Scalar::K::Float && a.k == Scalar::K::Float) {
                b.k = Scalar::K::Float;
                b.f = (double)(s[1].k == Scalar::K::Uint ? (double)s[1].u
                                                        : (double)s[1].i);
            }
            ok = same_class(a.k, b.k) && compare(ins.op, a.k, a, b, &r);
            Scalar out{};
            out.k = Scalar::K::Bool;
            out.width = 1;
            out.u = r ? 1 : 0;
            ok = ok && slot_write_scalar(f, ins.wr[0], out, 1);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], out);
        } else {
            // 一元 `-` 与二元 Sub 同 opcode：按读参数区分。
            const ops::Op op =
                ins.nrd == 1 && ins.op == ops::Op::Sub ? ops::Op::Neg : ins.op;
            Scalar::K k = s[0].k;
            const Scalar::K k1 = ins.nrd > 1 ? s[1].k : k;
            Scalar out = s[0];
            const bool to_float = op == ops::Op::Pow || op == ops::Op::Sqrt ||
                                  op == ops::Op::Exp || op == ops::Op::Log ||
                                  k == Scalar::K::Float ||
                                  (ins.nrd > 1 && k1 == Scalar::K::Float);
            auto as_d = [](const Scalar& v) {
                return v.k == Scalar::K::Float  ? v.f
                       : v.k == Scalar::K::Uint ? (double)v.u
                                                : (double)v.i;
            };
            ok = same_class(k, k1) || to_float;
            // 结果宽度 = 两操作数中更宽者（窄化回输入位宽）。
            const std::uint8_t wmax =
                std::max(s[0].width, ins.nrd > 1 ? s[1].width : s[0].width);
            if (ok && to_float) {
                out.k = Scalar::K::Float;
                out.width = k == Scalar::K::Float ? wmax : 8;
                ok = compute_float(op, as_d(s[0]), as_d(s[1]), &out.f);
            } else if (ok && k == Scalar::K::Float)
                ok = compute_float(op, s[0].f, s[1].f, &out.f);
            else if (ok && (k == Scalar::K::Int || k == Scalar::K::Uint ||
                            k == Scalar::K::Bool)) {
                if (k == Scalar::K::Bool)
                    k = Scalar::K::Uint;
                ok = compute_int(op, k, s[0], s[1], &out);
                out.width = wmax;
            } else
                ok = false;
            if (ok)
                ok = slot_write_scalar(f, ins.wr[0], out,
                                       out.width ? out.width : 8);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], out);
        }
        if (g_prof_on) {
            const std::uint64_t dh = now() - t0;
            g_prof.handler += dh;
            g_prof.steps++;
            prof_tick(ins.prof_id, dh, 0);
        }
        if (!ok) {
            *err = "value op failed: " + ins.name;
            return RunResult::Error;
        }
    }
    return RunResult::Ok;
}

}  // namespace kv::rt
