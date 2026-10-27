#include "vmkv.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <sys/random.h>
#include <ctime>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "model.hpp"
#include "vmbin.hpp"
#include "valueio.hpp"

namespace kv::rt {
// 二进制栈类型（FReg/FrameCtx/RunResult 等）归 vmbin 命名空间，执行层直接使用。
using bin::FReg;
using bin::FrameCtx;
using bin::RunResult;
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

// fast kernel A/B 开关（对拍定位用）：RC2_NOFAST=1 关连续执行，回主循环单步。
const bool g_no_fast = std::getenv("RC2_NOFAST") != nullptr;

// warm frame：FReg 块池（线程局部，按容量复用）。递归帧每层借一块、返回还池，
// 免每次 call 的 vector 堆分配 + FReg string 构造（fib 递归 setup 大头）。
thread_local std::vector<std::vector<FReg>> g_freg_free;

struct FregPoolGuard {
    std::vector<FReg>* blk = nullptr;
    ~FregPoolGuard() {
        if (blk && !blk->empty())
            g_freg_free.push_back(std::move(*blk));
    }
};

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

// 时间探针：x86 rdtsc；ARM64 用 mach_absolute_time（macOS 跨架构移植）。
#if defined(__x86_64__)
unsigned long long now() { return __builtin_ia32_rdtsc(); }
#else
#include <mach/mach_time.h>
unsigned long long now() { return static_cast<unsigned long long>(mach_absolute_time()); }
#endif

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
        // 绝对路径可能带类型后缀（`/tmp/up [int64]·int64`）：剥空格后段。
        const auto sp = name.find(' ');
        *key = name.substr(0, sp);
        return true;
    }
    if (name[0] != '*') {
        // 描述符可能带类型声明（`arr [int64]·int64`）：变量名取空格前段。
        const auto sp = name.find(' ');
        *key = f.frame + "/" + name.substr(0, sp);
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
        if (v_ptr_target(aw, &t2)) {
            *key = t2;  // 写参 Ptr 链：arg 槽 Ptr → 调用方槽
        } else {
            *key = target;  // 读参传值：arg 槽即值槽（本帧，view 缓存）
        }
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

// 槽值缓存：None 值用显式 XValue 表示（kvspace-c 对 None 返回 NULL，C runtime 对齐语义）。
thread_local std::uint8_t g_none_wire[32];
thread_local std::uint8_t* g_none_ptr = nullptr;
thread_local std::uint32_t g_none_len = 0;

// 惰性解析：Ptr 描述符在首次读/写时经 resolve_desc 补 key（target 由运行时写，
// 帧入口解析会过早失败）。读写共用同一入口。
static inline bool ensure_reg_key(FrameCtx& f, FReg& r) {
    if (r.key.empty()) {
        // 普通变量（desc_ref）：key = frame + "/" + name（一次轻拼接，SSO）。
        if (r.desc_ref) {
            // 绝对路径变量（`/lib/...` 顶层写槽）：直接作 key，不拼 f.frame。
            if (!r.desc_ref->name.empty() && r.desc_ref->name[0] == '/') {
                r.key = r.desc_ref->name;
                return true;
            }
            r.key.reserve(f.frame.size() + 1 + r.desc_ref->name.size());
            r.key = f.frame;
            r.key += "/";
            r.key += r.desc_ref->name;
            return true;
        }
        if (!r.desc.empty()) {
            std::string ek, ed;
            if (!resolve_desc(f, r.desc, &ek, &ed))
                return false;
            r.key = std::move(ek);
        }
    }
    return true;
}

bool read_reg(FrameCtx& f, std::uint8_t idx, Wire* out) {
    FReg& r = f.regs[idx];
    // 内存别名读参：转发读调用方 reg 的 fast 缓存（src_ptr 链已在帧入口挂好）。
    FReg* fr = r.src_ptr ? r.src_ptr : &r;
    if (!r.src_ptr && !ensure_reg_key(f, r))
        return false;
    // 纯内存帧局部变量（write_reg 内存化、fast_i64 已设、无 kvspace 槽）：直构 int64 wire。
    // 别名 reg（src_ptr）key 为空，走 fast 缓存构造。
    const std::string& rk = r.key;
    const bool flk =
        r.src_ptr || (!rk.empty() && rk.compare(0, f.frame.size() + 1,
                                                f.frame + "/") == 0 &&
                      rk.find("/[0,") == std::string::npos &&
                      rk.find("\xE2\x80\xA5") == std::string::npos);
    if (std::getenv("RC2_TRACE") && rk == f.frame + "/t1")
        std::fprintf(stderr, "[readreg t1 fast=%d flk=%d src=%p]\n",
                     (int)fr->fast_i64, (int)flk, (void*)r.src_ptr);
    if (fr->fast_i64 && flk) {
        thread_local std::uint8_t s_buf[64];
        thread_local std::uint32_t s_len = 0;
        std::uint8_t* buf = nullptr;
        std::uint32_t len = 0;
        if (kvspaceXvalueEncode("int64",
                                reinterpret_cast<const std::uint8_t*>(&fr->fv), 8,
                                nullptr, 0, &buf, &len) == 0 &&
            buf && len <= sizeof s_buf) {
            std::memcpy(s_buf, buf, len);
            std::free(buf);
            s_len = len;
            out->d = s_buf;
            out->n = s_len;
            return true;
        }
        std::free(buf);
    }
    if (ensure_ref(f, &r)) {
        std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        if (kvspaceGetByRef(f.h, &r.ref, r.key.c_str(), &d, &n) == 0 && d && n) {
            out->d = d;
            out->n = n;
            return true;
        }
        // d=NULL（None 值）→ 返回显式 None XValue，不是失败。
        if (!g_none_len) {
            std::uint64_t nn = 0;
            g_none_ptr = g_none_wire;
            if (kvspaceXhNewNone(&g_none_ptr, &nn) == 0)
                g_none_len = static_cast<std::uint32_t>(nn);
        }
        if (g_none_len) {
            out->d = g_none_ptr;
            out->n = g_none_len;
            return true;
        }
    }
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(f.h, r.key, &d, &n)) {
        // read_wire 也失败：同 None 语义（key 缺失/空值 → None）。
        if (!g_none_len) {
            std::uint64_t nn = 0;
            g_none_ptr = g_none_wire;
            if (kvspaceXhNewNone(&g_none_ptr, &nn) == 0)
                g_none_len = static_cast<std::uint32_t>(nn);
        }
        if (g_none_len) {
            out->d = g_none_ptr;
            out->n = g_none_len;
            return true;
        }
        return false;
    }
    out->d = d;
    out->n = n;
    return true;
}

// 读标量：slot view 直读；kind/width 每帧解析一次后缓存。
// 帧局部/内存别名的 fast 缓存直取（纯内存变量无明文槽，view 恒无 →
// 此前每步 fallback 到 kvspaceXvalueEncode，fib 的 `+` 3003 cycles/次 主源）。
bool read_scalar(FrameCtx& f, std::uint8_t idx, Scalar* out) {
    FReg& r = f.regs[idx];
    FReg& rf = r.src_ptr ? *r.src_ptr : r;
    if (rf.fast_i64) {
        out->k = Scalar::K::Int;
        out->width = 8;
        out->i = rf.fv;
        out->u = static_cast<std::uint64_t>(rf.fv);
        return true;
    }
    if (rf.fast_bool) {
        out->k = Scalar::K::Bool;
        out->width = 1;
        out->u = rf.fb ? 1 : 0;
        return true;
    }
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

// 帧局部普通变量 key（非参数槽、非系统槽）：纯内存化候选。
static inline bool is_frame_local(const FrameCtx& f, const std::string& rk) {
    return rk.size() > f.frame.size() + 1 &&
           rk.compare(0, f.frame.size() + 1, f.frame + "/") == 0 &&
           rk.find("/[0,") == std::string::npos &&
           rk.find("\xE2\x80\xA5") == std::string::npos;
}

// 冲刷帧局部纯内存变量落明文（Stop/native 边界前；纯内存子帧无 native 则免）。
// 仅对 fast 缓存有效但无 kvspace 槽的变量落一次明文，后续 read 走明文/新 view。
void flush_frame_locals(FrameCtx& f) {
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    for (FReg& r : f.regs) {
        if (r.view_ok || r.src_ptr || r.dst_ptr)
            continue;
        // 惰性 key：普通变量（desc_ref 或 desc 非 Ptr/绝对路径）帧入口未解析 key，
        // flush 边界须补解析（native/handoff 前落明文，println 读帧局部）。
        if (r.key.empty() &&
            (r.desc_ref ||
             (!r.desc.empty() && r.desc[0] != '*' && r.desc[0] != '/')))
            ensure_reg_key(f, r);
        if (r.key.empty())
            continue;
        if (r.fast_i64 && is_frame_local(f, r.key)) {
            if (kvspaceXvalueEncode("int64",
                                    reinterpret_cast<const std::uint8_t*>(&r.fv),
                                    8, nullptr, 0, &buf, &len) != 0 || !buf)
                continue;
            char err[192] = {};
            if (kvspaceSetValue(f.h, r.key.c_str(), buf, len, 0, 0, err,
                                sizeof err) == 0 &&
                kvspaceResolveRef(f.h, r.key.c_str(), &r.ref) == 0)
                r.resolved = true;
            std::free(buf);
            buf = nullptr;
        } else if (r.fast_bool && is_frame_local(f, r.key)) {
            const std::uint8_t b = r.fb ? 1 : 0;
            if (kvspaceXvalueEncode("bool", &b, 1, nullptr, 0, &buf, &len) != 0 ||
                !buf)
                continue;
            char err[192] = {};
            if (kvspaceSetValue(f.h, r.key.c_str(), buf, len, 0, 0, err,
                                sizeof err) == 0 &&
                kvspaceResolveRef(f.h, r.key.c_str(), &r.ref) == 0)
                r.resolved = true;
            std::free(buf);
            buf = nullptr;
        }
    }
}

bool write_reg(FrameCtx& f, std::uint8_t idx, const std::uint8_t* data,
               std::uint32_t len) {
    FReg& r = f.regs[idx];
    // 内存别名写参：直接写调用方 reg（fast 缓存 + 已有槽直写），零 kvspace。
    if (r.dst_ptr) {
        FReg& dr = *r.dst_ptr;
        r.fast_i64 = false;
        r.fast_bool = false;
        if (len >= 8) {
            kvspaceXh xh{};
            if (kvspaceXhDecode(data, len, &xh) == 0 && xh.body &&
                xh.content_len == 8 &&
                std::strcmp(reinterpret_cast<const char*>(xh.langtype),
                            "int64") == 0) {
                std::int64_t v = 0;
                std::memcpy(&v, xh.body, 8);
                dr.fv = v;
                dr.fast_i64 = true;
                if (dr.view_ok && dr.body)
                    std::memcpy(dr.body, &v, 8);
                r.fv = v;
                r.fast_i64 = true;
                return true;
            }
        }
        // 非 int64 写参：转发落 kvspace（别名调用不常见，回退安全路径）。
        if (dr.view_ok) {
            dr.view_ok = false;
        }
        char err[192] = {};
        return kvspaceSetValue(f.h, dr.key.c_str(), data, len, 0, 0, err,
                               sizeof err) == 0;
    }
    if (!ensure_reg_key(f, r))
        return false;
    r.view_ok = false;
    r.sc_ok = false;
    r.fast_i64 = false;
    r.fast_bool = false;
    // 帧局部 int64 变量：内存化写（FReg.fv + 槽直写若已有 view），层内读走 fast
    // 缓存，免每 op kvspace 往返（fib/binary_search 热点）。参数/绝对路径/系统槽照旧。
    const std::string& rk = r.key;
    const bool frame_local = is_frame_local(f, rk);
    if (std::getenv("RC2_TRACE") && rk == f.frame + "/t1")
        std::fprintf(stderr, "[writereg t1 fl=%d len=%u]\n", (int)frame_local,
                     len);
    if (frame_local && len >= 8) {
        kvspaceXh xh{};
        if (std::getenv("RC2_TRACE") && rk == f.frame + "/t1" &&
            kvspaceXhDecode(data, len, &xh) == 0)
            std::fprintf(stderr,
                         "[writereg t1 dec body=%p clen=%lu]\n",
                         (void*)xh.body,
                         (unsigned long)xh.content_len);
        if (kvspaceXhDecode(data, len, &xh) == 0 && xh.body &&
            xh.content_len == 8 &&
            std::strcmp(reinterpret_cast<const char*>(xh.langtype), "int64") == 0) {
            std::int64_t v = 0;
            std::memcpy(&v, xh.body, 8);
            r.fv = v;
            r.fast_i64 = true;
            if (r.view_ok && r.body) {
                std::memcpy(r.body, &v, 8);  // 已有槽：直写同步明文
                return true;
            }
            // 纯内存：不建 kvspace 槽。帧局部 int64 变量层内读走 fast 缓存；
            // 调用边界经 arg_src/arg_dst 内存别名或读参传值，无需明文存在。
            return true;
        }
    }
    if (ensure_ref(f, &r)) {
        char err[192] = {};
        if (kvspaceSetValueByRef(f.h, &r.ref, r.key.c_str(), data, len, 0, 0, err,
                                 sizeof err) == 0)
            return true;
        r.resolved = false;
    }
    char err[192] = {};
    if (kvspaceSetValue(f.h, r.key.c_str(), data, len, 0, 0, err, sizeof err) != 0) {
        if (std::getenv("RC2_TRACE"))
            std::fprintf(stderr, "[setvalue fail key=%s err='%s' len=%u]\n",
                         r.key.c_str(), err, len);
        return false;
    }
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
    // 帧局部变量（含首次写，fast_i64 未设）：无 view 也直写 fast 缓存 return
    // （免 kvspace）。非帧局部（参数/容器/系统槽）照旧落明文。
    if (is_frame_local(f, fr.key)) {
        fr.fv = v;
        fr.fast_i64 = true;
        if (fr.view_ok && fr.body)
            std::memcpy(fr.body, &v, 8);
        if (g_prof_on)
            g_prof.w_fast++;
        return true;
    }
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
    // 帧局部纯内存变量：无 view 也直写 fast 缓存 return（免 kvspace）。
    if (fr.fast_bool && is_frame_local(f, fr.key)) {
        fr.fb = b;
        if (fr.view_ok && fr.body)
            *fr.body = b ? 1 : 0;
        if (g_prof_on)
            g_prof.w_fast++;
        return true;
    }
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
    if constexpr (O == ops::Op::Shl) {
        // SGN 时 i 有效（u 可能未同步）；无符号用 u。移位量取低 6 位。
        r->i = (std::int64_t)((SGN ? (std::uint64_t)a.i : a.u)
                              << (SGN ? (std::uint64_t)b.i & 63 : b.u & 63));
        return true;
    }
    if constexpr (O == ops::Op::Shr) {
        r->i = SGN ? (a.i >> (b.i & 63)) : (std::int64_t)(a.u >> (b.u & 63));
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
    arith_i<ops::Op::Return, false>, arith_i<ops::Op::Cast, false>,
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
    arith_f<ops::Op::Return>, arith_f<ops::Op::Cast>,
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
    cmp_sgn<ops::Op::Return>, cmp_sgn<ops::Op::Cast>,
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
    cmp_uns<ops::Op::Return>, cmp_uns<ops::Op::Cast>,
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
    cmp_f<ops::Op::Return>, cmp_f<ops::Op::Cast>,
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
    // warm frame：从池借一块容量匹配的 FReg 块（复用 string capacity 免分配）。
    FregPoolGuard freg_guard;
    if (f.regs.empty()) {
        const std::size_t need = static_cast<std::size_t>(seg->regs.size());
        for (auto it = g_freg_free.begin(); it != g_freg_free.end(); ++it) {
            if (it->capacity() >= need) {
                f.regs.swap(*it);
                g_freg_free.erase(it);
                break;
            }
        }
    }
    freg_guard.blk = &f.regs;
    // 逐 reg 热字段重置（不清 string capacity——池复用的 key/desc 保留缓冲，
    // fib 每帧省 24 次 string 构造/析构；语义同 assign 全清零）。
    f.regs.resize(seg->regs.size());
    for (size_t i = 0; i < seg->regs.size(); i++) {
        FReg& rr = f.regs[i];
        rr.ref = kvspaceRef_t{};
        rr.resolved = false;
        rr.view = kvspaceSlotView_t{};
        rr.view_ok = false;
        rr.body = nullptr;
        rr.cap = 0;
        rr.sc_ok = false;
        rr.sck = Scalar::K::None;
        rr.scw = 0;
        rr.fast_i64 = false;
        rr.fv = 0;
        rr.fast_bool = false;
        rr.fb = false;
        rr.src_ptr = nullptr;
        rr.dst_ptr = nullptr;
        rr.arr_esz = 0;
        rr.arr_dims[0] = rr.arr_dims[1] = rr.arr_dims[2] = rr.arr_dims[3] = 0;
        rr.arr_dims[4] = rr.arr_dims[5] = rr.arr_dims[6] = rr.arr_dims[7] = 0;
        rr.arr_ndim = 0;
        rr.arr_kind = 0;
        rr.key.clear();
        rr.desc.clear();
        rr.desc_ref = nullptr;
        const RegDesc& d = seg->regs[i];
            if (d.kind == Operand::Kind::Literal) {
                f.regs[i].key = d.slot_key;
                // 字面量值恒定：段级快值缓存（build 期读槽一次）→ 帧入口零 kvspace。
                if (i < seg->lit.size() && seg->lit[i].is_i64) {
                    f.regs[i].fv = seg->lit[i].v;
                    f.regs[i].fast_i64 = true;
                } else {
                    Scalar sc{};
                    std::uint8_t* lb = nullptr;
                    std::uint64_t lc = 0;
                    if (reg_body(f, static_cast<std::uint8_t>(i), &lb, &lc) &&
                        v_scalar_view(f.regs[i].view.head + 18,
                                      f.regs[i].view.headlen - 18, lb, lc, &sc) &&
                        sc.k == Scalar::K::Int && sc.width == 8) {
                        f.regs[i].fv = sc.i;
                        f.regs[i].fast_i64 = true;
                    }
                }
            } else if (d.kind == Operand::Kind::Var) {
                // Ptr 描述符（`*` 开头）惰性解析：target 由运行时写（如 &x 产 Ptr），
                // 帧入口解析会过早失败。非 Ptr 的 Var/字面量帧入口一次建 key。
                if (d.name[0] == '*' && d.name.rfind("*[0,", 0) == 0) {
                    // 参数 Ptr（`*[0,±k]`）：call 时已绑定。内存别名子帧命中
                    // arg_src/arg_dst 表 → 挂跨帧指针（读参 src / 写参 dst），
                    // 零 kvspace；未命中（容器/扩展参数）回退 kvspace 原路径。
                    // 槽号 build 已预解析（d.param_k），帧入口零 substr/atoi
                    // （fib 递归每帧 12+ reg，字符串解析是 setup 大头）。
                    const int k = d.param_k;
                    FReg* ap = nullptr;
                    if (k < 0 && static_cast<std::size_t>(-k) <=
                                     static_cast<std::size_t>(f.arg_src_n))
                        ap = f.arg_src[-k - 1];
                    else if (k > 0 &&
                             static_cast<std::size_t>(k) <=
                                 static_cast<std::size_t>(f.arg_dst_n))
                        ap = f.arg_dst[k - 1];
                    if (ap) {
                        if (k < 0) {
                            // 读参内存别名：值恒等于调用方读参 → 继承 fast 缓存
                            // （递归参数链逐层继承，hot 判据/read_scalar 免追 src_ptr）。
                            f.regs[i].src_ptr = ap;
                            f.regs[i].fast_i64 = ap->fast_i64;
                            f.regs[i].fv = ap->fv;
                            f.regs[i].fast_bool = ap->fast_bool;
                            f.regs[i].fb = ap->fb;
                        } else {
                            f.regs[i].dst_ptr = ap;
                        }
                        f.regs[i].key.clear();
                        if (std::getenv("RC2_TRACE"))
                            std::fprintf(stderr, "[argmem d=%s k=%d %s]\n",
                                         d.name.c_str(), k,
                                         k < 0 ? "src" : "dst");
                    } else if (!resolve_desc(f, d.name, &f.regs[i].key, err)) {
                        if (std::getenv("RC2_TRACE"))
                            std::fprintf(stderr, "[argkv d=%s key=%s]\n",
                                         d.name.c_str(), f.regs[i].key.c_str());
                        return RunResult::Error;
                    }
                } else if (d.name[0] == '*') {
                    // 普通 Ptr 描述符（`*p`）：target 由运行时写，惰性解析。
                    f.regs[i].key.clear();
                    f.regs[i].desc = d.name;
                } else {
                    // 普通变量：无类型声明（纯名 a/b/x/y）key 惰性（fast 路径零构造，
                    // ensure_reg_key 才拼 frame/name）；带类型声明（容器/类型化，如
                    // `A [N,N]float64`）成员访问需 key，立即 resolve。
                    if (d.name.find(' ') == std::string::npos) {
                        f.regs[i].key.clear();
                        f.regs[i].desc.clear();
                        f.regs[i].desc_ref = &d;
                    } else if (!resolve_desc(f, d.name, &f.regs[i].key, err)) {
                        return RunResult::Error;
                    }
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
    if (g_prof_on)
        g_prof.frames++;
    // 四基址常驻：指令段/FReg 表基址提局部，热路径免 vector 链访问。
    const CInsn* ci = seg->insns.data();
    FReg* regs = f.regs.data();
    // runaway 防护：局部计数，函数末尾合并进全局（热路径无全局 inc）。
    unsigned long long run_steps = 0;
    for (; idx < n; idx++) {
        const unsigned long long t0 = g_prof_on ? now() : 0;
        const CInsn& ins = ci[idx];
        // runaway 防护：每 16M 步抽检一次（热路径无每步大数比较）。
        if ((++run_steps & 0xFFFFFFULL) == 0 && run_steps > 50000000ULL) {
            *err = "runaway loop: " + f.func_dir + " s0=" + std::to_string(ins.s0) +
                   " op=" + ins.name;
            return RunResult::Error;
        }
        if (g_trace_on && run_steps < g_trace_n)
            std::fprintf(stderr, "[%s d%d s0=%d op=%s impl=%d idx=%d]\n",
                         f.func_dir.c_str(), f.depth, ins.s0, ins.name.c_str(),
                         (int)ins.impl, idx);
        // ── fast kernel：typed 连续执行（RC2_NOFAST=1 关闭）──────────────────
        // 连续执行 fast 指令（int64 算术/比较、wr_local copy、goto/br），直到
        // 不可连续指令（call/return/native/handoff/非 fast）。每步写值（fv/fb）
        // 与推进 idx（内存 PC，主循环边界落盘——与主循环 PC 语义等价，崩溃恢复
        // 不破）；runaway 防护每 16M 步抽检。不可连续时退出走主循环。
        // 进入预检：读参/目标 fast 才连续（否则主循环 cold 处理，防 idx-- 补偿
        // 死循环——prime_sieve 的 limit 参数非 mem 帧不 fast）。
        bool fk_enter = false;
        if (!g_no_fast) {
            if (ins.fast_i64) {
                FReg& r0 = regs[ins.rd[0]], &r1 = regs[ins.rd[1]];
                FReg& ra = r0.src_ptr ? *r0.src_ptr : r0;
                FReg& rb = r1.src_ptr ? *r1.src_ptr : r1;
                fk_enter = ra.fast_i64 && rb.fast_i64;
            } else if (ins.op == ops::Op::Goto || ins.op == ops::Op::Br) {
                FReg& tr = regs[ins.rd[ins.op == ops::Op::Br ? 1 : 0]];
                FReg& tra = tr.src_ptr ? *tr.src_ptr : tr;
                fk_enter = tra.fast_i64;
                if (ins.op == ops::Op::Br) {
                    // cond 也要 fast（bool/int64）：否则 fk 内 cond exit → 主循环 br
                    // 又进 fk → 死循环（is_prime bool 落明文无 fast_bool）。
                    FReg& cr = regs[ins.rd[0]];
                    FReg& cra = cr.src_ptr ? *cr.src_ptr : cr;
                    fk_enter = fk_enter && (cra.fast_bool || cra.fast_i64);
                }
            } else if (ins.op == ops::Op::Copy && ins.wr_local && ins.nrd == 1) {
                FReg& sr = regs[ins.rd[0]];
                FReg& rf = sr.src_ptr ? *sr.src_ptr : sr;
                fk_enter = rf.fast_i64 || rf.fast_bool;
            }
        }
        if (fk_enter) {
            unsigned long long fk_n = 0;
            goto fk_next;
        fk_next:
            if (idx >= n)
                return RunResult::Ok;
            {
                const CInsn& ik = ci[idx];
                if ((++fk_n & 0xFFFFFFULL) == 0 && fk_n > 50000000ULL) {
                    *err = "runaway loop (fast): " + f.func_dir + " s0=" +
                           std::to_string(ik.s0);
                    return RunResult::Error;
                }
                if (ik.fast_i64) {
                    FReg &r0 = regs[ik.rd[0]], &r1 = regs[ik.rd[1]],
                         &w = regs[ik.wr[0]];
                    FReg& ra = r0.src_ptr ? *r0.src_ptr : r0;
                    FReg& rb = r1.src_ptr ? *r1.src_ptr : r1;
                    if (ra.fast_i64 && rb.fast_i64) {
                        const std::int64_t a = ra.fv, b = rb.fv;
                        std::int64_t rv;
                        switch (ik.opfn) {
#define C2_FK_CASE(O) case O: rv = fast_i64<O>(a, b); break
                        C2_FK_CASE(1); C2_FK_CASE(2); C2_FK_CASE(3);
                        C2_FK_CASE(4); C2_FK_CASE(5); C2_FK_CASE(6);
                        C2_FK_CASE(7); C2_FK_CASE(8); C2_FK_CASE(9);
                        C2_FK_CASE(10); C2_FK_CASE(11); C2_FK_CASE(12);
                        C2_FK_CASE(13); C2_FK_CASE(14); C2_FK_CASE(15);
                        C2_FK_CASE(16); C2_FK_CASE(17); C2_FK_CASE(18);
                        default: rv = 0; break;
                        }
                        if (ik.opfn == 4 || ik.opfn == 5) {
                            if (b == 0) {
                                *err = "division by zero: " + ik.name;
                                return RunResult::Error;
                            }
                        }
                        w.fast_bool = false;
                        if (ik.opfn <= 12) {
                            if (ik.wr_local && !w.view_ok) {
                                w.fv = rv;
                                w.fast_i64 = true;
                                w.sc_ok = false;
                            } else if (w.fast_i64 && w.view_ok && w.body) {
                                w.fv = rv;
                                std::memcpy(w.body, &rv, 8);
                                w.fast_i64 = true;
                                w.sc_ok = false;
                            } else {
                                (void)slot_write_i64(f, ik.wr[0], rv);
                                w.fast_i64 = true;
                                w.sc_ok = false;
                            }
                        } else {
                            const bool rb2 = rv != 0;
                            if (ik.wr_local && !w.view_ok) {
                                w.fb = rb2;
                                w.fast_bool = true;
                                w.sc_ok = false;
                            } else {
                                const bool body_is_bool =
                                    w.view_ok && w.body &&
                                    (w.fast_bool ||
                                     (w.sc_ok && w.sck == Scalar::K::Bool &&
                                      w.scw == 1));
                                if (body_is_bool) {
                                    *w.body = rb2 ? 1 : 0;
                                    w.fb = rb2;
                                    w.fast_bool = true;
                                } else {
                                    (void)slot_write_bool(f, ik.wr[0], rb2);
                                }
                                w.fast_i64 = false;
                                w.sc_ok = false;
                            }
                        }
                        idx++;
                        goto fk_next;
                    }
                    idx--;
                    goto fk_exit;  // cold 读参：主循环处理（含冷 fallback）
                }
                if (ik.op == ops::Op::Goto || ik.op == ops::Op::Br) {
                    FReg& tr = regs[ik.rd[ik.op == ops::Op::Br ? 1 : 0]];
                    FReg& tra = tr.src_ptr ? *tr.src_ptr : tr;
                    if (!tra.fast_i64) {
                        idx--;
                        goto fk_exit;
                    }
                    std::int64_t targ = tra.fv;
                    if (ik.op == ops::Op::Br) {
                        FReg& cr = regs[ik.rd[0]];
                        FReg& cra = cr.src_ptr ? *cr.src_ptr : cr;
                        bool cond = false;
                        if (cra.fast_bool)
                            cond = cra.fb;
                        else if (cra.fast_i64)
                            cond = cra.fv != 0;
                        else {
                            idx--;
                            goto fk_exit;
                        }
                        if (!cond) {
                            FReg& fr = regs[ik.rd[2]];
                            FReg& fra = fr.src_ptr ? *fr.src_ptr : fr;
                            if (!fra.fast_i64) {
                                idx--;
                                goto fk_exit;
                            }
                            targ = fra.fv;
                        }
                    }
                    if (targ < 1 || targ > n + 1) {
                        *err = "control: target out of range (fast): " + ik.name;
                        return RunResult::Error;
                    }
                    idx = static_cast<int>(targ) - 1;  // fk 内无 for 补偿
                    goto fk_next;
                }
                if (ik.op == ops::Op::Copy && ik.wr_local && ik.nrd == 1) {
                    FReg& sr = regs[ik.rd[0]];
                    FReg& rf = sr.src_ptr ? *sr.src_ptr : sr;
                    FReg& w = regs[ik.wr[0]];
                    if (rf.fast_i64) {
                        w.fast_bool = false;
                        if (ik.wr_local && !w.view_ok) {
                            w.fv = rf.fv;
                            w.fast_i64 = true;
                            w.sc_ok = false;
                        } else if (w.view_ok && w.body) {
                            w.fv = rf.fv;
                            std::memcpy(w.body, &rf.fv, 8);
                            w.fast_i64 = true;
                            w.sc_ok = false;
                        } else {
                            (void)slot_write_i64(f, ik.wr[0], rf.fv);
                            w.fast_i64 = true;
                            w.sc_ok = false;
                        }
                    } else if (rf.fast_bool) {
                        w.fast_i64 = false;
                        if (ik.wr_local && !w.view_ok) {
                            w.fb = rf.fb;
                            w.fast_bool = true;
                            w.sc_ok = false;
                        } else if (w.view_ok && w.body) {
                            *w.body = rf.fb ? 1 : 0;
                            w.fb = rf.fb;
                            w.fast_bool = true;
                        } else {
                            (void)slot_write_bool(f, ik.wr[0], rf.fb);
                            w.fast_bool = true;
                            w.sc_ok = false;
                        }
                    } else {
                        idx--;
                        goto fk_exit;
                    }
                    idx++;
                    goto fk_next;
                }
                idx--;
            fk_exit:
                if (g_prof_on) {
                    g_prof.handler += now() - t0;
                    g_prof.steps += fk_n;
                }
                continue;
            }
        }
        // 快路径前置：fast op 纯算术（int64 二目），不可能是 handoff/
        // goto/br/native/call，提前到通用检查之前（iops 热循环主力）。
        if (ins.fast_i64) {
            FReg &r0 = regs[ins.rd[0]], &r1 = regs[ins.rd[1]];
            FReg &w = regs[ins.wr[0]];
            // 稳态恒 fast：双读参直接取缓存值（mem 别名参数追 src_ptr，fib/iops
            // 参数读参也走 hot）；未命中（首次/失效）落冷路径 read_i64 兜底。
            // opfn 1-18 → goto 跳表（编译期 label 数组）。
            FReg& ra = r0.src_ptr ? *r0.src_ptr : r0;
            FReg& rb = r1.src_ptr ? *r1.src_ptr : r1;
            if (ra.fast_i64 && rb.fast_i64) {
                const std::int64_t a = ra.fv, b = rb.fv;
                std::int64_t rv;
                static const void* const kJmp[] = {
                    &&L1, &&L2, &&L3, &&L4, &&L5, &&L6,
                    &&L7, &&L8, &&L9, &&L10, &&L11, &&L12,
                    &&L13, &&L14, &&L15, &&L16, &&L17, &&L18,
                };
                goto *kJmp[ins.opfn - 1];
            L1:  rv = a + b; goto LWR;
            L2:  rv = a - b; goto LWR;
            L3:  rv = a * b; goto LWR;
            L4:  rv = b ? a / b : 0; goto LWR;
            L5:  rv = b ? a % b : 0; goto LWR;
            L6:  rv = a > b ? a : b; goto LWR;
            L7:  rv = a < b ? a : b; goto LWR;
            L8:  rv = a & b; goto LWR;
            L9:  rv = a | b; goto LWR;
            L10: rv = a ^ b; goto LWR;
            L11: rv = (std::int64_t)((std::uint64_t)a << (b & 63)); goto LWR;
            L12: rv = a >> (b & 63); goto LWR;
            L13: rv = a == b ? 1 : 0; goto LWR;
            L14: rv = a != b ? 1 : 0; goto LWR;
            L15: rv = a < b ? 1 : 0; goto LWR;
            L16: rv = a <= b ? 1 : 0; goto LWR;
            L17: rv = a > b ? 1 : 0; goto LWR;
            L18: rv = a >= b ? 1 : 0; goto LWR;
            LWR:
                if (ins.opfn == 4 || ins.opfn == 5) {
                    if (b == 0) {
                        *err = "division by zero: " + ins.name;
                        return RunResult::Error;
                    }
                }
                if (ins.opfn <= 12) {
                    w.fast_bool = false;
                    // 帧局部普通变量（build 预标记 wr_local）：纯 fv 写零 string 比较
                    // （iops 单变量读改写热路径）；有明文槽（view）才 memcpy 同步。
                    if (ins.wr_local && !w.view_ok) {
                        w.fv = rv;
                        w.fast_i64 = true;
                        w.sc_ok = false;
                    } else if (w.fast_i64 && w.view_ok && w.body) {
                        w.fv = rv;
                        std::memcpy(w.body, &rv, 8);  // body 直写：明文槽同步（shm 即 mmap）
                        w.fast_i64 = true;
                        w.sc_ok = false;
                    } else {
                        (void)slot_write_i64(f, ins.wr[0], rv);
                        w.fast_i64 = true;
                        w.sc_ok = false;
                    }
                } else {
                    // 比较 opfn 13-18：结果 bool。槽已是 bool 才直写 body，
                    // 否则 slot_write_bool 产完整 bool XValue（含 head）。
                    const bool rb = rv != 0;
                    // 帧局部临时 bool（wr_local）：纯 fb 写免 slot_write_bool
                    // （iops 的 le 结果 `_5`，每轮一次）。
                    if (ins.wr_local && !w.view_ok) {
                        w.fb = rb;
                        w.fast_bool = true;
                        w.sc_ok = false;
                    } else {
                        const bool body_is_bool =
                            w.view_ok && w.body &&
                            (w.fast_bool ||
                             (w.sc_ok && w.sck == Scalar::K::Bool && w.scw == 1));
                        if (body_is_bool) {
                            *w.body = rb ? 1 : 0;
                            w.fb = rb;
                            w.fast_bool = true;
                        } else {
                            (void)slot_write_bool(f, ins.wr[0], rb);
                        }
                        w.fast_i64 = false;
                        w.sc_ok = false;
                    }
                }
                if (g_prof_on) {
                    const std::uint64_t dh = now() - t0;
                    g_prof.handler += dh;
                    g_prof.steps++;
                    prof_tick(ins.prof_id, dh, 0);
                }
                continue;
            }
            // 冷路径：fast 缓存未命中（读参首次/失效），read_i64 兜底。
            std::int64_t a = 0, b = 0;
            if (read_i64(f, ins.rd[0], &a) && read_i64(f, ins.rd[1], &b)) {
                std::int64_t rv = 0;
                bool ok2 = true;
                switch (ins.opfn) {
#define C2_FAST_CASE2(O) case O: rv = fast_i64<O>(a, b); break
                C2_FAST_CASE2(1);  C2_FAST_CASE2(2);  C2_FAST_CASE2(3);
                C2_FAST_CASE2(4);  C2_FAST_CASE2(5);  C2_FAST_CASE2(6);
                C2_FAST_CASE2(7);  C2_FAST_CASE2(8);  C2_FAST_CASE2(9);
                C2_FAST_CASE2(10); C2_FAST_CASE2(11); C2_FAST_CASE2(12);
                C2_FAST_CASE2(13); C2_FAST_CASE2(14); C2_FAST_CASE2(15);
                C2_FAST_CASE2(16); C2_FAST_CASE2(17); C2_FAST_CASE2(18);
                default: break;
                }
                if (ins.opfn <= 12) {
                    if (ins.opfn == 4 || ins.opfn == 5) {
                        if (b == 0) {
                            *err = "division by zero: " + ins.name;
                            return RunResult::Error;
                        }
                    }
                    w.fv = rv;
                    w.fast_bool = false;
                    if (w.fast_i64 && w.view_ok && w.body) {
                        std::memcpy(w.body, &rv, 8);
                    } else {
                        ok2 = slot_write_i64(f, ins.wr[0], rv);
                    }
                    w.fast_i64 = true;
                    w.sc_ok = false;
                } else {
                    const bool rb = rv != 0;
                    const bool body_is_bool =
                        w.view_ok && w.body &&
                        (w.fast_bool ||
                         (w.sc_ok && w.sck == Scalar::K::Bool && w.scw == 1));
                    if (body_is_bool) {
                        *w.body = rb ? 1 : 0;
                        w.fb = rb;
                        w.fast_bool = true;
                    } else {
                        ok2 = slot_write_bool(f, ins.wr[0], rb);
                    }
                    w.fast_i64 = false;
                    w.sc_ok = false;
                }
                if (g_prof_on) {
                    const std::uint64_t dh = now() - t0;
                    g_prof.handler += dh;
                    g_prof.steps++;
                    prof_tick(ins.prof_id, dh, 0);
                }
                continue;
            }
            // cold 读参失败：回退到通用路径（下述）。
        }
        if (ins.op == ops::Op::Goto || ins.op == ops::Op::Br) {
            std::int64_t target_v = 0;
            FReg &tr = regs[ins.rd[ins.op == ops::Op::Br ? 1 : 0]];
            if (!(tr.fast_i64 ? (target_v = tr.fv, true)
                              : read_i64(f, ins.rd[ins.op == ops::Op::Br ? 1 : 0],
                                         &target_v))) {
                *err = "control: bad target";
                return RunResult::Error;
            }
            int dest = static_cast<int>(target_v);
            if (ins.op == ops::Op::Br) {
                bool cond = false;
                FReg &cr = regs[ins.rd[0]];
                if (!(cr.fast_bool ? (cond = cr.fb, true)
                                   : read_bool(f, ins.rd[0], &cond))) {
                    *err = "br: bad cond";
                    return RunResult::Error;
                }
                if (!cond) {
                    FReg &fr = regs[ins.rd[2]];
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
        if (ins.impl == ops::Impl::Handoff || ins.impl == ops::Impl::None) {
            if (g_trace_on)
                std::fprintf(stderr, "[stop %s d%d s0=%d %s]\n",
                             f.func_dir.c_str(), f.depth, ins.s0, ins.name.c_str());
            // Stop 边界（handoff/外部引擎接管）：冲刷帧局部纯内存变量落明文，
            // resume 后的新 FrameCtx 无 fast 缓存，须从 kvspace 重建。
            flush_frame_locals(f);
            if (stop_pc)
                *stop_pc = param_key(f.frame, ins.s0, 0);
            return RunResult::Stop;
        }
        if (ins.op == ops::Op::Return) {
            if (g_prof_on) {
                g_prof.loop += now() - t0;
                g_prof.steps++;
            }
            if (g_prof_on)
                prof_tick(ins.prof_id, 0, 0);
            // return 边界：本段含 native/handoff 才冲刷（纯算术/递归帧免，
            // 保持零 kvspace）。入口帧 return 交给外层引擎收尾（同样冲刷）。
            if (seg->has_native)
                flush_frame_locals(f);
            if (f.depth <= 1)
                return RunResult::Ok;  // 入口帧（depth=1）return = vthread done
            // 纯内存递归帧：无 returnpc 明文（call 时省略）→ 直接 Returned，
            // stop_pc 留空；Call 分支捕获后 continue 到调用方下一条指令。
            if (f.mem_frame) {
                if (stop_pc)
                    stop_pc->clear();
                return RunResult::Returned;
            }
            std::string ret;
            if (!read_text(f.h, f.frame + "\xE2\x80\xA5" "returnpc", &ret) ||
                ret.empty()) {
                *err = "return: missing returnpc in " + f.frame;
                return RunResult::Error;
            }
            // 帧不立即 DelTree：残留延迟到下次同 depth 的 call 建帧时清理，
            // 每层 return 省 1 次 kvspace 往返（纯内存递归帧无外部可观察面）。
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
                    xh.content_len != 8) {
                    if (std::getenv("RC2_TRACE"))
                        std::fprintf(stderr, "[ai64fail i=%d d=%p n=%u]\n", i,
                                     (void*)in[i].d, in[i].n);
                    return false;
                }
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
#if defined(__APPLE__)
                arc4random_buf(&r, sizeof r);
#else
                if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) {
                    *err = "native: getrandom failed";
                    return RunResult::Error;
                }
#endif
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
            } else if (fn == "array") {
                // 数组字面量 `[v...]`：读参[0..n-1]=元素，写槽描述符 `a <langtype>`
                // 提供 elem kind（`[]int64`/`[N]T`），打包为 [n]elem compact XValue。
                std::string lt;
                const std::string wkey =
                    f.func_dir + "/[" + std::to_string(ins.s0) + ",1]";
                const std::uint8_t* wd = nullptr;
                std::uint32_t wn = 0;
                if (read_wire_raw(f.h, wkey, &wd, &wn) && wd) {
                    kvspaceXh wxh{};
                    if (kvspaceXhDecode(wd, wn, &wxh) == 0 && wxh.body) {
                        const std::uint8_t* b = wxh.body;
                        const std::size_t bl = wxh.content_len;
                        if (bl > 5) {
                            const char* p = reinterpret_cast<const char*>(b + 5);
                            const std::size_t cap = bl - 5;
                            std::size_t i = 0;
                            while (i < cap && p[i] != '\0') i++;
                            if (i < cap)
                                lt.assign(p + i + 1, cap - i - 1);
                        }
                    }
                }
                // 解析 `[N,M]kind` → 剥 dims 得裸 elem kind（数组字面量长度=读参数）。
                std::uint32_t esz = 0;
                if (!lt.empty() && lt[0] == '[') {
                    const std::size_t close = lt.find(']');
                    if (close != std::string::npos)
                        lt = lt.substr(close + 1);
                }
                if (lt.rfind("float64", 0) == 0) esz = 8;
                else if (lt.rfind("float32", 0) == 0) esz = 4;
                else if (lt.rfind("int64", 0) == 0 || lt.rfind("uint64", 0) == 0) esz = 8;
                else if (lt.rfind("int32", 0) == 0 || lt.rfind("uint32", 0) == 0) esz = 4;
                else if (lt.rfind("int16", 0) == 0 || lt.rfind("uint16", 0) == 0) esz = 2;
                else if (lt.rfind("int8", 0) == 0 || lt.rfind("uint8", 0) == 0 ||
                         lt.rfind("bool", 0) == 0) esz = 1;
                if (esz == 0 || ins.nrd < 1) {
                    *err = "array: bad type/elements " + ins.name;
                    return RunResult::Error;
                }
                const std::int32_t nd = static_cast<std::int32_t>(ins.nrd);
                std::vector<std::uint8_t> raw(static_cast<std::uint64_t>(nd) * esz, 0);
                for (int i = 0; i < ins.nrd; i++) {
                    Wire ew{};
                    Scalar es{};
                    if (!read_reg(f, ins.rd[i], &ew) || !v_scalar(ew, &es)) {
                        *err = "array: bad element " + std::to_string(i);
                        return RunResult::Error;
                    }
                    const std::uint64_t off = (std::uint64_t)i * esz;
                    if (esz == 8) {
                        if (lt.rfind("float", 0) == 0) std::memcpy(raw.data() + off, &es.f, 8);
                        else { std::uint64_t u = es.k == Scalar::K::Int ? (std::uint64_t)es.i : es.u;
                               std::memcpy(raw.data() + off, &u, 8); }
                    } else if (esz == 4) {
                        if (lt.rfind("float", 0) == 0) { float f = (float)es.f; std::memcpy(raw.data() + off, &f, 4); }
                        else { std::uint32_t u = es.k == Scalar::K::Int ? (std::uint32_t)es.i : (std::uint32_t)es.u;
                               std::memcpy(raw.data() + off, &u, 4); }
                    } else if (esz == 2) {
                        std::uint16_t u = (std::uint16_t)(es.k == Scalar::K::Int ? es.i : (std::int64_t)es.u);
                        std::memcpy(raw.data() + off, &u, 2);
                    } else {
                        std::uint8_t b = es.k == Scalar::K::Int ? (std::uint8_t)es.i : (std::uint8_t)es.u;
                        std::memset(raw.data() + off, b, 1);
                    }
                }
                const std::int32_t adims[1] = {nd};
                std::uint8_t* arr = nullptr;
                std::uint32_t al = 0;
                ok = kvspaceXvalueEncode(
                         lt.c_str(), raw.data(), static_cast<std::uint32_t>(raw.size()),
                         adims, 1, &arr, &al) == 0 &&
                     arr && write_reg(f, ins.wr[0], arr, al);
                std::free(arr);
            } else if (fn == "array/fill") {
                // array·fill(langtype[, value])：按定长数组类型一次成型 compact XValue。
                // langtype 形如 `[4,4]float64`：解析 dims + 裸 elem kind，填 value（缺省 0）。
                Wire lw{};
                std::string lt;
                if (ins.nrd < 1 || !read_reg(f, ins.rd[0], &lw) ||
                    !wire_text(lw.d, lw.n, &lt)) {
                    *err = "array·fill: bad langtype";
                    return RunResult::Error;
                }
                // 解析 `[N,M]kind`。
                std::int32_t dims[8];
                std::uint32_t ndim = 0;
                std::size_t pos = 0;
                if (!lt.empty() && lt[0] == '[') {
                    // `[N,M]kind`：括号内逗号分隔多个 dim。
                    const std::size_t close = lt.find(']');
                    if (close != std::string::npos) {
                        const std::string seg = lt.substr(1, close - 1);
                        std::size_t i = 0;
                        while (i <= seg.size() && ndim < 8) {
                            const std::size_t comma = seg.find(',', i);
                            const std::string one =
                                comma == std::string::npos
                                    ? seg.substr(i)
                                    : seg.substr(i, comma - i);
                            char* end = nullptr;
                            std::int64_t dv = std::strtoll(one.c_str(), &end, 10);
                            dims[ndim++] = static_cast<std::int32_t>(dv);
                            if (comma == std::string::npos)
                                break;
                            i = comma + 1;
                        }
                        lt = lt.substr(close + 1);  // 剩裸 kind（如 float64）
                    }
                }
                // 每元素字节宽。
                std::uint32_t esz = 0;
                if (lt.rfind("float64", 0) == 0) esz = 8;
                else if (lt.rfind("float32", 0) == 0) esz = 4;
                else if (lt.rfind("int64", 0) == 0 || lt.rfind("uint64", 0) == 0) esz = 8;
                else if (lt.rfind("int32", 0) == 0 || lt.rfind("uint32", 0) == 0) esz = 4;
                else if (lt.rfind("int16", 0) == 0 || lt.rfind("uint16", 0) == 0) esz = 2;
                else if (lt.rfind("int8", 0) == 0 || lt.rfind("uint8", 0) == 0 ||
                         lt.rfind("bool", 0) == 0) esz = 1;
                if (esz == 0 || ndim == 0) {
                    *err = "array·fill: bad type " + lt;
                    return RunResult::Error;
                }
                std::uint64_t total = 1;
                for (std::uint32_t i = 0; i < ndim; i++)
                    total *= static_cast<std::uint64_t>(dims[i]);
                total *= esz;
                std::vector<std::uint8_t> raw(total ? total : 1, 0);
                // value 缺省 0；第二读参为填充值。
                if (ins.nrd >= 2) {
                    Wire vw{};
                    Scalar vs{};
                    if (read_reg(f, ins.rd[1], &vw) && v_scalar(vw, &vs)) {
                        for (std::uint64_t i = 0; i < total / esz; i++) {
                            const std::uint64_t off = i * esz;
                            if (lt.rfind("float64", 0) == 0)
                                std::memcpy(raw.data() + off, &vs.f, esz);
                            else if (lt.rfind("float32", 0) == 0) {
                                float f = (float)vs.f;
                                std::memcpy(raw.data() + off, &f, esz);
                            } else if (lt.rfind("int64", 0) == 0 ||
                                       lt.rfind("uint64", 0) == 0) {
                                std::uint64_t u = vs.k == Scalar::K::Int
                                                      ? (std::uint64_t)vs.i
                                                      : vs.u;
                                std::memcpy(raw.data() + off, &u, esz);
                            } else if (lt.rfind("int32", 0) == 0 ||
                                       lt.rfind("uint32", 0) == 0) {
                                std::uint32_t u32 = (std::uint32_t)(vs.k == Scalar::K::Int
                                                            ? vs.i : (std::int64_t)vs.u);
                                std::memcpy(raw.data() + off, &u32, esz);
                            } else {
                                std::uint8_t b = vs.k == Scalar::K::Int
                                                    ? (std::uint8_t)vs.i : (std::uint8_t)vs.u;
                                std::memset(raw.data() + off, b, esz);
                            }
                        }
                    }
                }
                std::uint8_t* arr = nullptr;
                std::uint32_t al = 0;
                ok = kvspaceXvalueEncode(
                         lt.c_str(), raw.data(), static_cast<std::uint32_t>(raw.size()),
                         dims, ndim, &arr, &al) == 0 &&
                     arr && write_reg(f, ins.wr[0], arr, al);
                std::free(arr);
            } else if (fn == "string/char") {
                // string·char(s, idx)：取第 idx 个码点，返回 char/utf32 单字符。
                Wire sw{};
                std::int64_t idx = 0;
                if (ins.nrd < 2 || !read_reg(f, ins.rd[0], &sw) ||
                    !read_i64(f, ins.rd[1], &idx)) {
                    *err = "string·char: bad input";
                    return RunResult::Error;
                }
                kvspaceXh sh{};
                std::uint32_t cp = 0;
                if (kvspaceXhDecode(sw.d, sw.n, &sh) == 0 && sh.body) {
                    std::string sl(reinterpret_cast<const char*>(sh.langtype),
                                   sh.langtype_len);
                    if (sl.find("utf32") != std::string::npos) {
                        const std::uint32_t n =
                            static_cast<std::uint32_t>(sh.content_len / 4);
                        if (idx >= 0 && (std::uint64_t)idx < n)
                            std::memcpy(&cp, sh.body + (std::uint64_t)idx * 4, 4);
                        else {
                            *err = "string·char: index out of bounds";
                            return RunResult::Error;
                        }
                    } else {
                        // utf8：逐码点数到 idx。
                        const std::uint8_t* b = sh.body;
                        std::int64_t rune_i = 0;
                        for (std::uint64_t i = 0; i < sh.content_len && rune_i <= idx;) {
                            const std::uint8_t c = b[i];
                            std::uint32_t r = 0, take = 1;
                            if (c < 0x80) { r = c; }
                            else if ((c >> 5) == 0x6) { r = c & 0x1F; take = 2; }
                            else if ((c >> 4) == 0xE) { r = c & 0x0F; take = 3; }
                            else if ((c >> 3) == 0x1E) { r = c & 0x07; take = 4; }
                            for (std::uint32_t k = 1; k < take && i + k < sh.content_len; k++)
                                r = (r << 6) | (b[i + k] & 0x3F);
                            if (rune_i == idx) { cp = r; break; }
                            rune_i++;
                            i += take;
                        }
                        if (rune_i < idx) {
                            *err = "string·char: index out of bounds";
                            return RunResult::Error;
                        }
                    }
                }
                const std::uint32_t u32 = cp;
                std::uint8_t* cb = nullptr;
                std::uint32_t cl = 0;
                const std::int32_t cdims[1] = {1};  // 单码点 = [1]char/utf32
                ok = kvspaceXvalueEncode(
                         "char/utf32",
                         reinterpret_cast<const std::uint8_t*>(&u32), 4,
                         cdims, 1, &cb, &cl) == 0 &&
                     cb && write_reg(f, ins.wr[0], cb, cl);
                std::free(cb);
            } else if (fn == "string/len") {
                // string·len：读字符串，返回字符数（utf8 数 rune，utf32 数组长）。
                Wire sw{};
                if (ins.nrd < 1 || !read_reg(f, ins.rd[0], &sw)) {
                    *err = "string·len: bad input";
                    return RunResult::Error;
                }
                kvspaceXh sh{};
                std::int64_t len = 0;
                if (kvspaceXhDecode(sw.d, sw.n, &sh) == 0 && sh.body) {
                    std::string sl(reinterpret_cast<const char*>(sh.langtype),
                                   sh.langtype_len);
                    if (sl.find("utf32") != std::string::npos) {
                        len = static_cast<std::int64_t>(sh.content_len / 4);
                    } else if (sl.find("ascii") != std::string::npos) {
                        len = static_cast<std::int64_t>(sh.content_len);
                    } else {
                        // utf8：数码点（跳过续字节 10xxxxxx）。
                        const std::uint8_t* b = sh.body;
                        for (std::uint64_t i = 0; i < sh.content_len; i++)
                            if ((b[i] & 0xC0) != 0x80)
                                len++;
                    }
                }
                Scalar outv{};
                outv.k = Scalar::K::Int;
                outv.width = 8;
                outv.i = len;
                ok = slot_write_i64(f, ins.wr[0], len);
            } else if (fn == "string/ord") {
                // string·ord：返回字符串首码点（空串 -1）。utf32 直取第 0 个，
                // utf8/ascii 解码首 rune。对齐 C runtime。
                Wire sw{};
                if (ins.nrd < 1 || !read_reg(f, ins.rd[0], &sw)) {
                    *err = "string·ord: bad input";
                    return RunResult::Error;
                }
                kvspaceXh sh{};
                std::int64_t cp = -1;
                if (kvspaceXhDecode(sw.d, sw.n, &sh) == 0 && sh.body &&
                    sh.content_len) {
                    std::string sl(reinterpret_cast<const char*>(sh.langtype),
                                   sh.langtype_len);
                    const std::uint8_t* b = sh.body;
                    if (sl.find("utf32") != std::string::npos) {
                        std::uint32_t u = 0;
                        std::memcpy(&u, b, 4);
                        cp = u;
                    } else {
                        std::uint32_t r = 0, take = 1;
                        const std::uint8_t c = b[0];
                        if (c < 0x80) { r = c; }
                        else if ((c >> 5) == 0x6) { r = c & 0x1F; take = 2; }
                        else if ((c >> 4) == 0xE) { r = c & 0x0F; take = 3; }
                        else if ((c >> 3) == 0x1E) { r = c & 0x07; take = 4; }
                        for (std::uint32_t k = 1; k < take && k < sh.content_len; k++)
                            r = (r << 6) | (b[k] & 0x3F);
                        cp = r;
                    }
                }
                ok = slot_write_i64(f, ins.wr[0], cp);
            } else if (fn == "ndarray/numel" || fn == "ndarray/dim" ||
                       fn == "ndarray/shape") {
                // ndarray·numel/dim/shape：从 XValue head langtype 取总元素/维数/dims。
                // map（langtype 含 `·`）numel=成员数（kvspaceListLen）。
                Wire aw{};
                if (ins.nrd < 1 || !read_reg(f, ins.rd[0], &aw)) {
                    *err = "ndarray: bad array";
                    return RunResult::Error;
                }
                kvspaceXh axh{};
                if (kvspaceXhDecode(aw.d, aw.n, &axh) != 0) {
                    *err = "ndarray: not a compact array";
                    return RunResult::Error;
                }
                std::string alt(reinterpret_cast<const char*>(axh.langtype),
                                axh.langtype_len);
                std::int32_t adims[8];
                std::uint32_t andim = 0;
                const bool is_map = alt.find("\xC2\xB7") != std::string::npos;
                if (!alt.empty() && alt[0] == '[') {
                    const std::size_t close = alt.find(']');
                    if (close != std::string::npos) {
                        const std::string seg = alt.substr(1, close - 1);
                        std::size_t i = 0;
                        while (i <= seg.size() && andim < 8) {
                            const std::size_t comma = seg.find(',', i);
                            const std::string one =
                                comma == std::string::npos
                                    ? seg.substr(i)
                                    : seg.substr(i, comma - i);
                            char* end = nullptr;
                            adims[andim++] = (std::int32_t)std::strtoll(one.c_str(), &end, 10);
                            if (comma == std::string::npos) break;
                            i = comma + 1;
                        }
                    }
                }
                if (fn == "ndarray/numel") {
                    if (is_map) {
                        std::string base_key;
                        if (f.regs[ins.rd[0]].key.size() > 5)
                            base_key = f.regs[ins.rd[0]].key;
                        std::int32_t cnt = 0;
                        ok = !base_key.empty() &&
                             kvspaceListLen(f.h, (base_key + "/").c_str(), 0, 0,
                                            &cnt) == 0;
                        if (ok)
                            ok = slot_write_i64(f, ins.wr[0], cnt);
                    } else {
                        std::int64_t total = 1;
                        for (std::uint32_t i = 0; i < andim; i++)
                            total *= adims[i];
                        ok = slot_write_i64(f, ins.wr[0], total);
                    }
                } else if (fn == "ndarray/dim") {
                    ok = slot_write_i64(f, ins.wr[0], andim);
                } else {
                    // shape：`[n]int64` 数组，dims 打包。
                    std::uint8_t raw[64];
                    for (std::uint32_t i = 0; i < andim && i < 8; i++) {
                        const std::int64_t d = adims[i];
                        for (int j = 0; j < 8; j++)
                            raw[i * 8 + j] = (d >> (j * 8)) & 0xFF;
                    }
                    const std::int32_t sd[1] = {static_cast<std::int32_t>(andim)};
                    std::uint8_t* sb = nullptr;
                    std::uint32_t sl = 0;
                    ok = kvspaceXvalueEncode("int64", raw,
                                             andim * 8, sd, 1, &sb, &sl) == 0 &&
                         sb && write_reg(f, ins.wr[0], sb, sl);
                    std::free(sb);
                }
            } else if (fn == "xv/at" || fn == "xv/set") {
                // compact 数组元素读/写（[N,M]kind，body 连续打包）。
                // 读参[0]=数组；下标读参[1..n-1]；xv·set 最后读参=值，写回数组槽。
                const bool is_set = fn == "xv/set";
                // view 直读：数组 body 一次取得（零 kvspaceGetByRef）；布局缓存命中
                // 零 decode（langtype 枚举化后热路径零 string 判断）。
                FReg& ar0 = f.regs[ins.rd[0]];
                std::uint8_t* ab0 = nullptr;
                std::uint64_t acap0 = 0;
                if (!reg_body(f, ins.rd[0], &ab0, &acap0) || !ab0) {
                    *err = "xv: bad array";
                    return RunResult::Error;
                }
                kvspaceXh xh{};
                xh.body = ab0;
                std::int32_t dims[8];
                std::uint32_t ndim;
                std::uint32_t esz;
                Scalar::K ekind;
                bool is_char;
                if (ar0.arr_ndim) {
                    ndim = ar0.arr_ndim;
                    esz = ar0.arr_esz;
                    ekind = static_cast<Scalar::K>(ar0.arr_kind & 0x7F);
                    is_char = (ar0.arr_kind & 0x80) != 0;
                    std::memcpy(dims, ar0.arr_dims,
                                sizeof(std::int32_t) * ndim);
                } else {
                    // 首次：view.head langtype 原位取（head+18），零 decode 拷贝。
                    const std::uint8_t* hd = ar0.view.head;
                    const std::size_t hcap =
                        hd ? static_cast<std::size_t>(ar0.view.headlen > 18
                                                         ? ar0.view.headlen - 18
                                                         : 0)
                           : 0;
                    const std::size_t hlen =
                        hcap ? ::strnlen(reinterpret_cast<const char*>(hd + 18),
                                         hcap)
                             : 0;
                    // 首次：stackbin langtype 枚举化（零 string rfind 链——
                    // 双栈原则；dims 解析保留，kindexpr→LangType 查映射表一次）。
                    const char* lt = reinterpret_cast<const char*>(hd + 18);
                    std::size_t ln = hlen;
                    ndim = 0;
                    if (ln && *lt == '[') {
                        const std::size_t close = std::string(lt, ln).find(']');
                        if (close != std::string::npos) {
                            const std::string seg(lt + 1, close - 1);
                            std::size_t i = 0;
                            while (i <= seg.size() && ndim < 8) {
                                const std::size_t comma = seg.find(',', i);
                                const std::string one =
                                    comma == std::string::npos
                                        ? seg.substr(i)
                                        : seg.substr(i, comma - i);
                                char* end = nullptr;
                                std::int64_t dv =
                                    std::strtoll(one.c_str(), &end, 10);
                                dims[ndim++] = static_cast<std::int32_t>(dv);
                                if (comma == std::string::npos) break;
                                i = comma + 1;
                            }
                            const std::size_t koff = close + 1;
                            lt += koff;
                            ln -= koff;
                        }
                    }
                    const bin::LangType bt = bin::langtype_of(lt, ln);
                    is_char = bt == bin::LangType::CharUtf8 ||
                              bt == bin::LangType::CharUtf32 ||
                              bt == bin::LangType::CharAscii;
                    esz = bin::elem_width_of(bt);
                    ekind = bt == bin::LangType::Float32 ||
                                    bt == bin::LangType::Float64
                                ? Scalar::K::Float
                                : bt == bin::LangType::Int8 ||
                                          bt == bin::LangType::Int16 ||
                                          bt == bin::LangType::Int32 ||
                                          bt == bin::LangType::Int64
                                      ? Scalar::K::Int
                                      : bt == bin::LangType::Uint8 ||
                                                bt == bin::LangType::Uint16 ||
                                                bt == bin::LangType::Uint32 ||
                                                bt == bin::LangType::Uint64
                                            ? Scalar::K::Uint
                                            : Scalar::K::Bool;
                    // 缓存（dims/esz/kind 恒定；数组重建/搬迁时 view 失效，布局仍对）。
                    ar0.arr_esz = esz;
                    ar0.arr_ndim = ndim;
                    ar0.arr_kind =
                        static_cast<std::uint8_t>(ekind) | (is_char ? 0x80 : 0);
                    std::memcpy(ar0.arr_dims, dims, sizeof(std::int32_t) * ndim);
                }
                const int nidx = is_set ? ins.nrd - 2 : ins.nrd - 1;
                if (esz == 0 || ndim == 0 || nidx != (int)ndim) {
                    *err = "xv: bad type/dims";
                    return RunResult::Error;
                }
                std::int64_t flat = 0;
                for (int d = 0; d < nidx; d++) {
                    std::int64_t idxv = 0;
                    if (!read_i64(f, ins.rd[d + 1], &idxv)) {
                        *err = "xv: bad index";
                        return RunResult::Error;
                    }
                    flat = flat * dims[d] + idxv;
                }
                // xh.body 已指向 head 之后（view body 即元素连续区，含 head 偏移）。
                // 元素偏移 = flat * esz，**不含** head 长度。
                const std::uint64_t off = (std::uint64_t)flat * esz;
                if ((std::uint64_t)flat * esz + esz > acap0) {
                    *err = "xv: index out of range";
                    return RunResult::Error;
                }
                if (!is_set) {
                    // 读元素：按 elem kind 编码（缓存路径 ekind，无需 lt）。
                    Scalar ev{};
                    ev.k = ekind;
                    ev.width = static_cast<std::uint8_t>(esz);
                    if (ev.k == Scalar::K::Float) {
                        if (esz == 8) std::memcpy(&ev.f, xh.body + off, esz);
                        else {
                            float f;
                            std::memcpy(&f, xh.body + off, esz);
                            ev.f = f;
                        }
                    } else if (ev.k == Scalar::K::Int) {
                        std::int64_t v = 0;
                        std::memcpy(&v, xh.body + off, esz);
                        ev.i = v;
                        ev.u = (std::uint64_t)v;
                    } else {
                        std::uint64_t v = 0;
                        std::memcpy(&v, xh.body + off, esz);
                        ev.u = v;
                        ev.i = (std::int64_t)v;
                    }
                    if (is_char) {
                        // char 元素：单码点 = `[1]char/utf32`（对齐 string·char/C write_char32）。
                        std::uint8_t u32[4] = {0};
                        std::memcpy(u32, xh.body + off, esz < 4 ? esz : 4);
                        std::uint8_t* eb = nullptr;
                        std::uint32_t el = 0;
                        const std::int32_t cdims[1] = {1};
                        ok = kvspaceXvalueEncode(
                                 esz == 4 ? "char/utf32" : "char/utf8", u32, esz,
                                 cdims, 1, &eb, &el) == 0 &&
                             eb && write_reg(f, ins.wr[0], eb, el);
                        std::free(eb);
                        continue;
                    }
                    // int64 定宽元素直写 fast（零 encode——二进制栈原则）；
                    // 其余（float/窄/char）回退 v_encode。
                    if (ev.k == Scalar::K::Int && esz == 8) {
                        ok = slot_write_i64(f, ins.wr[0], ev.i);
                    } else {
                        std::uint8_t* eb = nullptr;
                        std::uint32_t el = 0;
                        const char* kn = kind_name(
                            ekind, static_cast<std::uint8_t>(esz));
                        ok = kn && v_encode(kn, esz, ev, &eb, &el) && eb &&
                             write_reg(f, ins.wr[0], eb, el);
                        std::free(eb);
                    }
                } else {
                    // 写元素：值读参最后，body 直写（shm 槽 body 即 mmap，写即持久）。
                    Wire vw{};
                    if (!read_reg(f, ins.rd[ins.nrd - 1], &vw)) {
                        *err = "xv: bad value";
                        return RunResult::Error;
                    }
                    FReg& ar = f.regs[ins.rd[0]];
                    std::uint8_t* ab = nullptr;
                    std::uint64_t acap = 0;
                    if (reg_body(f, ins.rd[0], &ab, &acap) && ab &&
                        (std::uint64_t)flat * esz + esz <= acap) {
                        ar.body = ab;
                        ar.cap = acap;
                        const std::uint64_t body_off = (std::uint64_t)flat * esz;
                        // 值 XValue：取 body 前 esz 字节（标量 body 即数值）。
                        std::uint8_t tmp[8] = {0};
                        kvspaceXh vh{};
                        if (kvspaceXhDecode(vw.d, vw.n, &vh) == 0 && vh.body) {
                            const std::uint32_t take =
                                vh.content_len < esz ? vh.content_len : esz;
                            std::memcpy(tmp, vh.body, take);
                        } else if (vw.n <= 8) {
                            std::memcpy(tmp, vw.d, vw.n);
                        }
                        std::memcpy(ab + body_off, tmp, esz);
                        ar.fast_i64 = false;
                        ar.fast_bool = false;
                        ok = true;
                    } else {
                        // 回退：SetPartByRef（view 不可用）。
                        char e[192] = {};
                        ok = kvspaceSetPartByRef(f.h, &ar.ref, ar.key.c_str(),
                                                 static_cast<std::uint32_t>(off),
                                                 vw.d, vw.n, e, sizeof e) == 0;
                        ar.view_ok = false;
                    }
                }
            } else if (fn == "obj" || fn == "map") {
                // 容器创建：obj=空 `{}`，map=带元素 `{v...}`。写槽描述符 `arr <langtype>`
                // 取容器类型，写 marker XValue（body 空）+ 建成员目录；map 逐元素写 `base/i`。
                // 对齐 C kvlangBuiltinMap：容器值是索引型（成员=子键）。
                std::string lt;
                // 写槽描述符 body = `name\0<langtype>`（NUL 分隔两段）。
                const std::string wkey =
                    f.func_dir + "/[" + std::to_string(ins.s0) + ",1]";
                const std::uint8_t* wd = nullptr;
                std::uint32_t wn = 0;
                if (read_wire_raw(f.h, wkey, &wd, &wn) && wd) {
                    kvspaceXh xh{};
                    if (kvspaceXhDecode(wd, wn, &xh) == 0 && xh.body) {
                        // body 前 5 字节计数头 + 名字 + NUL + 类型。
                        const std::uint8_t* b = xh.body;
                        const std::size_t bl = xh.content_len;
                        if (bl > 5) {
                            const char* p = reinterpret_cast<const char*>(b + 5);
                            const std::size_t cap = bl - 5;
                            std::size_t i = 0;
                            while (i < cap && p[i] != '\0') i++;
                            if (i < cap)
                                lt.assign(p + i + 1, cap - i - 1);
                        }
                    }
                }
                if (lt.empty())
                    lt = "map";
                // 容器 marker：langtype 含 `·`（map 类型），用 XhNewShort 直存。
                std::uint8_t* mark = nullptr;
                std::uint64_t ml = 0;
                const std::string ck = f.regs[ins.wr[0]].key;
                ok = kvspaceXhNewShort(lt.c_str(), nullptr, 0, &mark, &ml) == 0 &&
                     mark &&
                     write_reg(f, ins.wr[0], mark,
                               static_cast<std::uint32_t>(ml));
                std::free(mark);
                if (ok) {
                    // 建成员目录（容器子键空间）。
                    char e[192] = {};
                    const std::string dir = ck + "/";
                    ok = kvspaceMkindex(f.h, dir.c_str(), 0, e, sizeof e) == 0;
                }
                // map：逐元素写 `base/[i]`（散 key 数组，坐标键对齐 C kvlangBuiltinScatterKey
                // 的 `base·[i]`；c2 用 `/` 作成员分隔）。`·*"[i]"` 动态键读同键匹配。
                // obj：命名成员字面量 `{n=42; x=7}` 读参成对（键,值,键,值）。
                if (ok && (fn == "map" || ins.nrd >= 2)) {
                    const std::string base_key = f.regs[ins.wr[0]].key;
                    char e[192] = {};
                    if (fn == "map") {
                        for (int i = 0; i < ins.nrd; i++) {
                            Wire ew{};
                            if (!read_reg(f, ins.rd[i], &ew)) {
                                *err = "map: bad element " + std::to_string(i);
                                return RunResult::Error;
                            }
                            const std::string mk =
                                base_key + "/[" + std::to_string(i) + "]";
                            if (kvspaceSetValue(f.h, mk.c_str(), ew.d, ew.n, 0,
                                                0, e, sizeof e) != 0) {
                                *err = "map: set failed: " + ins.name;
                                return RunResult::Error;
                            }
                        }
                    } else {
                        // obj 命名成员：读参 [0]=键名,[1]=值,[2]=键名,[3]=值,…
                        for (int i = 0; i + 1 < ins.nrd; i += 2) {
                            Wire kw{};
                            Wire vw{};
                            std::string kname;
                            if (!read_reg(f, ins.rd[i], &kw) ||
                                !wire_text(kw.d, kw.n, &kname) ||
                                kname.empty() ||
                                !read_reg(f, ins.rd[i + 1], &vw)) {
                                *err = "obj: bad member pair " + ins.name;
                                return RunResult::Error;
                            }
                            const std::string mk = base_key + "/" + kname;
                            if (kvspaceSetValue(f.h, mk.c_str(), vw.d, vw.n, 0,
                                                0, e, sizeof e) != 0) {
                                *err = "obj: set failed: " + ins.name;
                                return RunResult::Error;
                            }
                        }
                    }
                }
            } else if (fn == "kvlang/abs") {
                // &x ≡ kvlang·abs(x)：读参是变量名/字面量路径 → 产 Ptr（langtype=目标 kindexpr，body=绝对路径）。
                std::string k0;
                const RegDesc& ad = seg->regs[ins.rd[0]];
                if (ad.kind == Operand::Kind::Literal) {
                    ok = atext(0, &k0);
                } else if (ad.name[0] == '/') {
                    k0 = ad.name.substr(0, ad.name.find(' '));  // 剥类型后缀
                } else {
                    k0 = f.regs[ins.rd[0]].key;  // 变量已解析的帧 key
                }
                if (!ok || k0.empty()) {
                    *err = "native: bad path operand: " + ins.name;
                    return RunResult::Error;
                }
                const std::uint8_t* td = nullptr;
                std::uint32_t tn = 0;
                std::string lt;
                if (read_wire(f.h, k0, &td, &tn))
                    (void)v_langtype(Wire{td, tn}, &lt);
                std::uint8_t* ptr = nullptr;
                std::uint32_t pl = 0;
                ok = kvspaceNewPtr(lt.c_str(), k0.c_str(), &ptr, &pl) == 0 && ptr &&
                     write_reg(f, ins.wr[0], ptr, pl);
                std::free(ptr);
            } else if (fn == "kvspace/del" || fn == "kvspace/deltree" ||
                       fn == "kvspace/mkindex" || fn == "kvspace/listlen" ||
                       fn == "kvspace/listat" || fn == "kvspace/cp" ||
                       fn == "kvspace/cpdir" || fn == "kvspace/cplist" ||
                       fn == "kvspace/extindex" || fn == "kvspace/rmindexext") {
                // 路径读参：变量/参数取 FReg.key（已解析的 Ptr target 或帧路径），
                // 字面量取值文本。读参值本身（容器 marker）不含路径。
                std::string k0, k1;
                const RegDesc& d0 = seg->regs[ins.rd[0]];
                if (d0.kind == Operand::Kind::Literal) {
                    ok = atext(0, &k0);
                } else if (d0.name[0] == '/') {
                    k0 = d0.name.substr(0, d0.name.find(' '));  // 剥类型后缀
                } else {
                    k0 = f.regs[ins.rd[0]].key;
                }
                if (ins.nrd >= 2) {
                    const RegDesc& d1 = seg->regs[ins.rd[1]];
                    if (d1.kind == Operand::Kind::Literal) {
                        ok = ok && atext(1, &k1);
                    } else if (d1.name[0] == '/') {
                        k1 = d1.name.substr(0, d1.name.find(' '));  // 剥类型后缀
                    } else {
                        k1 = f.regs[ins.rd[1]].key;
                    }
                }
                if (!ok) {
                    *err = "native: bad path operand: " + ins.name;
                    return RunResult::Error;
                }
                // kvspace·get/set 已死刑（清理方案）：成员读写走 `=`/`base·*key`，
                // 单路径读写走 `=`。c2 不再实现 get/set。
                if (false) {
                    // 保留编译期可达性：永不命中（get/set 不在 ops 表）。
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
                } else if (fn == "kvspace/listlen" || fn == "kvspace/listat") {
                    // 容器成员前缀：kvspace 的 bad_dir_prefix 要求尾 `/` 或 `·`；
                    // c2 容器成员以 `/` 组织（base_key + "/" + kstr），补尾斜杠。
                    std::string dkey = k0;
                    if (!dkey.empty() && dkey.back() != '/' &&
                        !(dkey.size() >= 2 &&
                          (unsigned char)dkey[dkey.size() - 2] == 0xC2 &&
                          (unsigned char)dkey.back() == 0xB7))
                        dkey += "/";
                    if (fn == "kvspace/listlen") {
                        std::int32_t cnt = 0;
                        ok = kvspaceListLen(f.h, dkey.c_str(), 0, 0, &cnt) == 0 &&
                             write_named_i64(f.h, ins.wr[0], f, "int64", cnt);
                    } else {
                        std::int64_t idx = 0;
                        std::uint8_t name[512];
                        std::uint32_t nn = 0;
                        ok = ai64(1, &idx) &&
                             kvspaceListAt(f.h, dkey.c_str(), 0, 0,
                                           (std::int32_t)idx, name, sizeof name,
                                           &nn) == 0;
                        if (ok)
                            ok = write_text(
                                f.h, f.regs[ins.wr[0]].key,
                                std::string_view(
                                    reinterpret_cast<const char*>(name), nn));
                    }
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
            // callee 已在 build_segment 预解析为最终 /lib/ 路径（裸名 pkg 探测一次，
            // 运行期零 read_text/kvspaceGet）。fib 递归每帧省 2 次 kvspace 往返。
            const std::string& callee = ins.callee;
            if (callee.empty()) {
                *err = "call: empty callee";
                return RunResult::Error;
            }
            const std::string nf = frame_root(f.vid, f.depth + 1);
            char e[192] = {};
            // 纯 runtime rwfunc 调用（callee 有代码段可解释）：帧局部 int64/bool
            // 参数走内存别名，免建 kvspace arg 槽/写参槽（fib 递归热点）。
            // 扩展/native/容器/float 参数照旧 kvspace 传值/Ptr。
            const Segment* cseg = cached_segment(f.h, callee);
            // 纯内存递归帧：callee 段无 native/handoff → 无观察边界 → 帧系统变量
            // （returnpc/lib）与生命周期（DelTree/Mkindex/ext 索引）全部省略，
            // 每帧省 ~17k cycles kvspace（fib 递归最大头）。
            const bool mem_frame = cseg && !cseg->has_native;
            // 非纯内存帧：清残留 + 建目录 + 系统变量落明文（明文栈唯一事实）。
            if (!mem_frame) {
                const std::string nstack = nf + "/";
                kvspaceDelTree(f.h, nf.c_str(), e, sizeof e);
                // Mkindex 删除：shm ART SetValue 隐式建目录（实测无回归）。
                // 显式 Mkindex 使 SetValue 便宜 ~2k，但省掉 Mkindex 本身净省 ~11k/帧。
                if (kvspaceMkindex(f.h, nstack.c_str(), 0, e, sizeof e) != 0) {
                    *err = std::string("call: frame init failed: ") + e;
                    return RunResult::Error;
                }
                // 帧目录的 callee ext 索引：仅供他人 runtime 从 pc 找函数（handoff 路由）。
                const std::string callee_dir = callee + "/";
                kvspaceMkindexExt(f.h, nstack.c_str(), callee_dir.c_str(), e,
                                  sizeof e);
                const std::string retpc = param_key(f.frame, ins.s0 + 1, 0);
                // callpc 已删：运行期零读取者（仅 dump 观测），每帧省 1 次 SetValue。
                if (!write_text(f.h, nf + "\xE2\x80\xA5" "returnpc", retpc) ||
                    !write_text(f.h, nstack + "\xE2\x80\xA5" "lib", callee)) {
                    *err = "call: system vars failed";
                    return RunResult::Error;
                }
            }
            if (std::getenv("RC2_TRACE")) {
                std::fprintf(stderr, "[memcall callee=%s mem=%d]\n",
                             callee.c_str(), (int)(cseg != nullptr));
            }
            // 内存别名子帧：arg_src/arg_dst 从调用方 FReg 建立（含首层 test→rwfunc）。
            // 定长栈数组（段 nr/nw ≤ 16，免 vector 堆分配）：槽号索引
            // 读参槽 *[0,-k]（第 k 参数 → 槽 -k，顺序）、写参槽 *[0,+k]。
            // 容器参数不走别名（空位），子帧入口按 -k-1/k-1 取表。
            FReg* mem_src[16] = {};
            FReg* mem_dst[16] = {};
            int mem_src_n = 0, mem_dst_n = 0;
            // 帧目录标记：非 mem_frame 已在初始化块建好；mem_frame 纯标量调用
            // 不建（零 kvspace），首个需要 kvspace 槽的参数（容器/float/复杂）
            // 出现时才补建（SetValue 到不存在的父目录会失败——写侧不穿透）。
            bool mk_done = !mem_frame;
            auto bind_arg = [&](int x, FReg* caller_reg,
                                const std::string& value_key,
                                const std::string& typ,
                                const Wire& src) -> bool {
                char be[192] = {};
                // 容器参数（typ 含 `·`）必须 Ptr：成员在调用方容器下（传值只复制
                // marker，callee 读不到成员）。
                const bool container_arg =
                    typ.find("\xC2\xB7") != std::string::npos;
                // 内存别名：callee 可解释 + 标量（int64/bool）+ 非字面量别名源？
                // 读参任意来源（含字面量 fast 缓存）；写参目标为调用方 Var。
                const bool scalar_arg = !container_arg &&
                    (typ == "int64" || typ == "bool");
                // 内存别名只对纯内存帧（callee 段无 native/handoff）：段含 Handoff
                // 时执行中途 Stop、帧会被 execute_vthread 重启（arg_src 丢失），
                // 标量参数必须建 kvspace 槽保证重启后可解析（prime_sieve println 坑）。
                if (mem_frame && scalar_arg) {
                    if (x < 0) {
                        const std::size_t ix = static_cast<std::size_t>(-x - 1);
                        mem_src[ix] = caller_reg;
                        if (ix + 1 > static_cast<std::size_t>(mem_src_n))
                            mem_src_n = static_cast<int>(ix + 1);
                    } else {
                        const std::size_t ix = static_cast<std::size_t>(x - 1);
                        mem_dst[ix] = caller_reg;
                        if (ix + 1 > static_cast<std::size_t>(mem_dst_n))
                            mem_dst_n = static_cast<int>(ix + 1);
                    }
                    return true;
                }
                // 非 mem 路径才需要 arg_key/slot 字符串（fib 纯标量走 mem 别名，
                // 免每参数 3 次 string 构造/转换）。
                const std::string xs = std::to_string(x);
                const std::string arg_key = nf + "/\xE2\x80\xA5" "arg[0," + xs + "]";
                const std::string slot = nf + "/[0," + xs + "]";
                if (std::getenv("RC2_TRACE") && callee.find("bsearch") != std::string::npos)
                    std::fprintf(stderr, "[bindkv x=%d typ=%s memcall=%d scalar=%d]\n",
                                 x, typ.c_str(), (int)(cseg != nullptr),
                                 (int)scalar_arg);
                // 首个需要 kvspace 槽的参数（非标量绑定路径）：补建帧目录。
                if (!mk_done) {
                    const std::string ns = nf + "/";
                    if (kvspaceMkindex(f.h, ns.c_str(), 0, be, sizeof be) != 0) {
                        *err = std::string("bind: frame dir failed: ") + be;
                        return false;
                    }
                    mk_done = true;
                }
                if (x < 0 && !container_arg) {
                    // 读参传值：调用方值复制到本帧 arg 槽（callee 只读，本帧
                    // view 缓存后免跨帧 kvspace 读）。slot 仍 Ptr→arg 槽。
                    // 值用调用方寄存器 wire（fast 缓存命中时帧局部无 kvspace 槽）。
                    const std::uint8_t* vd = src.d;
                    std::uint32_t vn = src.n;
                    if (!vd || !vn) {
                        *err = "bind: read arg value failed: " + value_key;
                        return false;
                    }
                    if (kvspaceSetValue(f.h, arg_key.c_str(), vd, vn, 0, 0, be,
                                        sizeof be) != 0) {
                        *err = std::string("bind: set arg value failed: ") + be;
                        return false;
                    }
                    std::uint8_t* sp = nullptr;
                    std::uint32_t sn = 0;
                    bool bok =
                        kvspaceNewPtr(typ.c_str(), arg_key.c_str(), &sp, &sn) == 0;
                    if (!bok) {
                        *err = "bind: NewPtr(slot) failed";
                        return false;
                    }
                    bok = kvspaceSetValue(f.h, slot.c_str(), sp, sn, 0, 0, be,
                                          sizeof be) == 0;
                    std::free(sp);
                    if (!bok)
                        *err = std::string("bind: set slot failed: ") + be;
                    return bok;
                }
                std::uint8_t* a = nullptr;
                std::uint8_t* sp = nullptr;
                std::uint32_t an = 0, sn = 0;
                bool bok =
                    kvspaceNewPtr(typ.c_str(), value_key.c_str(), &a, &an) == 0;
                if (!bok) {
                    *err = "bind: NewPtr(value) failed typ=" + typ;
                    return false;
                }
                bok = kvspaceNewPtr(typ.c_str(), arg_key.c_str(), &sp, &sn) == 0;
                if (!bok) {
                    *err = "bind: NewPtr(arg) failed";
                    std::free(a);
                    return false;
                }
                bok = kvspaceSetValue(f.h, arg_key.c_str(), a, an, 0, 0, be, sizeof be) == 0 &&
                      kvspaceSetValue(f.h, slot.c_str(), sp, sn, 0, 0, be, sizeof be) == 0;
                if (!bok)
                    *err = std::string("bind: set failed: ") + be;
                std::free(a);
                std::free(sp);
                return bok;
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
                // typ 优先取描述符类型段（`arr [int64]·int64` 空格后），免每 call
                // v_langtype wire 解析（fib 参数 int64 每 call 2 次 decode）；无则回退。
                typ.clear();
                const std::size_t tsp = rd.name.find(' ');
                if (tsp != std::string::npos)
                    typ = rd.name.substr(tsp + 1);
                if (typ.empty() && (!v_langtype(vw, &typ) || typ.empty()))
                    typ = "int64";
                if (!bind_arg(-i, &f.regs[ins.rd[i]], vk, typ, vw))
                    return RunResult::Error;
            }
            for (int i = 1; i <= ins.nwr; i++) {
                // 返回参数：目标槽调用前无值，Ptr langtype 标注用写参描述符类型；
                // 读不读取现有值（会得到 None/空 typ）。
                const std::string vk = f.regs[ins.wr[i - 1]].key;
                std::string typ = "int64";
                const RegDesc& wd = seg->regs[ins.wr[i - 1]];
                const auto sp = wd.name.find(' ');
                if (sp != std::string::npos) {
                    const std::string t = wd.name.substr(sp + 1);
                    if (!t.empty()) typ = t;
                }
                if (!bind_arg(i, &f.regs[ins.wr[i - 1]], vk, typ, Wire{}))
                    return RunResult::Error;
            }
            FrameCtx nfc;
            nfc.h = f.h;
            nfc.vid = f.vid;
            nfc.depth = f.depth + 1;
            // bind 已结束（fib mem 路径早 return，nf 未被非 mem 路径再用）：move 免拷贝
            nfc.frame = std::move(nf);
            nfc.func_dir = callee;
            // 按实参数拷贝（fib 3 指针 vs 全 16 指针，省 232B memcpy/call）。
            if (mem_src_n)
                std::memcpy(nfc.arg_src, mem_src,
                            static_cast<std::size_t>(mem_src_n) * sizeof(FReg*));
            if (mem_dst_n)
                std::memcpy(nfc.arg_dst, mem_dst,
                            static_cast<std::size_t>(mem_dst_n) * sizeof(FReg*));
            nfc.arg_src_n = mem_src_n;
            nfc.arg_dst_n = mem_dst_n;
            nfc.mem_frame = mem_frame;
            RunResult rr = run_frame(nfc, 1, stop_pc, err);
            if (rr != RunResult::Ok && rr != RunResult::Returned)
                return rr;
            if (g_prof_on)
                g_prof.steps++;  // 嵌套帧自身已计时，勿重复累计
            continue;
        }
        // 值算子
        Wire w[4]{};
        Scalar s[4]{};
        bool non_scalar = false;
        bool none0 = false, none1 = false;
        if (ins.op != ops::Op::Copy) {
            for (int i = 0; i < ins.nrd; i++) {
                if (read_scalar(f, ins.rd[i], &s[i]))
                    continue;
                if (!read_reg(f, ins.rd[i], &w[i])) {
                    *err = "operand " + std::to_string(i) + " is missing";
                    return RunResult::Error;
                }
                if (v_is_none(w[i])) {
                    if (i == 0) none0 = true;
                    else if (i == 1) none1 = true;
                } else
                    non_scalar = true;
            }
        }
        bool ok = true;
        // None 参与比较：对齐 C 语义（Eq：双 None=true；Neq：双 None=false；其余 → TypeError）。
        if ((none0 || none1) && is_cmp(ins.op)) {
            if (ins.op == ops::Op::Eq)
                ok = slot_write_bool(f, ins.wr[0], none0 && none1);
            else if (ins.op == ops::Op::Neq)
                ok = slot_write_bool(f, ins.wr[0], !(none0 && none1));
            else {
                *err = "TypeError: None in comparison: " + ins.name;
                return RunResult::Error;
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
            continue;
        }
        if (non_scalar) {
            // char 族转码（char/utf8|utf32|ascii 互转）优先于 `+` 拼接。
            // 简化：统一按 utf8 文本读入，目标编码经 kvspaceNewChar 产 char XValue。
            if (ins.op == ops::Op::Cast && ins.nrd == 1 &&
                ins.name.rfind("char/", 0) == 0) {
                std::string txt;
                if (!wire_text(w[0].d, w[0].n, &txt)) {
                    *err = "char cast: bad input";
                    return RunResult::Error;
                }
                std::uint8_t* cb = nullptr;
                std::uint32_t cl = 0;
                const int ncr = kvspaceNewChar(
                    reinterpret_cast<const std::uint8_t*>(txt.data()),
                    static_cast<std::uint32_t>(txt.size()), &cb, &cl);
                ok = ncr == 0 && cb &&
                     write_reg(f, ins.wr[0], cb, cl);
                std::free(cb);
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
            } else {
                // 动态算子按运行时类型分派：`+` 对 char 族即字符串拼接；
                // `==`/`!=` 对 char 族按文本比较产 bool。
                std::string a, b;
                if (ins.nrd == 2 && (ins.op == ops::Op::Eq || ins.op == ops::Op::Neq) &&
                    wire_text(w[0].d, w[0].n, &a) &&
                    wire_text(w[1].d, w[1].n, &b)) {
                    const bool eq = a == b;
                    ok = slot_write_bool(f, ins.wr[0], ins.op == ops::Op::Eq ? eq : !eq);
                    if (g_prof_on) {
                        const std::uint64_t dh = now() - t0;
                        g_prof.handler += dh;
                        g_prof.steps++;
                        prof_tick(ins.prof_id, dh, 0);
                    }
                    continue;
                }
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
        }
        // 动态键：Ptr 参数先解引用取 target 槽值（链最多 2 跳）；int64/标量 →
        // 十进制字符串；char 族 → 文本。
        auto key_str = [&](const Wire& kw0, std::string* out) -> bool {
            Wire cur = kw0;
            for (int hop = 0; hop < 2; hop++) {
                std::string target;
                if (!v_ptr_target(cur, &target))
                    break;
                const std::uint8_t* td = nullptr;
                std::uint32_t tn = 0;
                if (!read_wire(f.h, target, &td, &tn) || !td || !tn)
                    break;
                cur.d = td;
                cur.n = tn;
            }
            Scalar sc{};
            if (v_scalar(cur, &sc)) {
                char b[64];
                switch (sc.k) {
                case Scalar::K::Int:
                    std::snprintf(b, sizeof b, "%lld", (long long)sc.i);
                    *out = b;
                    return true;
                case Scalar::K::Uint:
                    std::snprintf(b, sizeof b, "%llu", (unsigned long long)sc.u);
                    *out = b;
                    return true;
                default:
                    break;
                }
            }
            return wire_text(cur.d, cur.n, out);
        };
        if (ins.op == ops::Op::Copy) {
            // 单参普通赋值（`a -> b`，非成员读写）：src fast 缓存 → dst 直传，
            // 免 read_reg 的 kvspaceXvalueEncode + write_reg 的整套 wire 路径。
            // dst 为写参别名（dst_ptr）转发调用方 reg；非别名且非帧局部才落明文。
            {
                const RegDesc& wdesc0 = seg->regs[ins.wr[0]];
                const bool mw = wdesc0.name.find("\xC2\xB7") != std::string::npos;
                if (ins.nrd == 1 && !mw) {
                    FReg& sr = regs[ins.rd[0]];
                    FReg& rf = sr.src_ptr ? *sr.src_ptr : sr;
                    if (rf.fast_i64 || rf.fast_bool) {
                        FReg& wr = regs[ins.wr[0]];
                        FReg& target = wr.dst_ptr ? *wr.dst_ptr : wr;
                        target.fast_bool = false;
                        if (rf.fast_i64) {
                            target.fv = rf.fv;
                            target.fast_i64 = true;
                            if (target.view_ok && target.body)
                                std::memcpy(target.body, &rf.fv, 8);
                            if (wr.dst_ptr) {  // 写参链：同步当前帧 reg（对齐 write_reg）
                                wr.fv = rf.fv;
                                wr.fast_i64 = true;
                            }
                        } else {
                            target.fb = rf.fb;
                            target.fast_bool = true;
                            if (target.view_ok && target.body)
                                *target.body = rf.fb ? 1 : 0;
                            if (wr.dst_ptr) {
                                wr.fb = rf.fb;
                                wr.fast_bool = true;
                            }
                        }
                        if (g_prof_on) {
                            const std::uint64_t dh = now() - t0;
                            g_prof.handler += dh;
                            g_prof.steps++;
                            prof_tick(ins.prof_id, dh, 0);
                        }
                        // 非别名写参/绝对路径槽：fast 直写只进缓存，须补落明文
                        // （内存别名由边界冲刷兜底，见 flush_frame_locals）。
                        if (!wr.dst_ptr && !is_frame_local(f, wr.key)) {
                            bool ok2 = rf.fast_i64
                                           ? slot_write_i64(f, ins.wr[0], rf.fv)
                                           : slot_write_bool(f, ins.wr[0], rf.fb);
                            if (!ok2) {
                                *err = "value op failed: " + ins.name;
                                return RunResult::Error;
                            }
                        }
                        continue;
                    }
                }
            }
            // 成员路径（layout MEMBER_READ_OP="="）：nrd=2 = 成员读（base+key），
            // 写槽描述符含 `·*` = 成员写。目标 key = 容器槽 + "/" + 键值。
            const RegDesc& wdesc = seg->regs[ins.wr[0]];
            const bool member_write =
                wdesc.name.find("\xC2\xB7") != std::string::npos;
            if (ins.nrd == 2 && !member_write) {
                // 成员读：base（读参0 的值或 Ptr 目标 key）+ key（读参1 的值）。
                Wire bw{};
                std::string base_key;
                if (read_reg(f, ins.rd[0], &bw)) {
                    std::string bt;
                    if (v_ptr_target(bw, &bt)) {
                        base_key = bt;
                    } else if (wire_text(bw.d, bw.n, &bt) && !bt.empty() &&
                               bt[0] == '/') {
                        // 字面量路径 base（`char/utf32:/tmp/up`）→ 直接取文本。
                        base_key = bt;
                    } else if (f.regs[ins.rd[0]].key.size() > 5) {
                        base_key = f.regs[ins.rd[0]].key;
                    }
                }
                Wire kw{};
                std::string kstr;
                if (!base_key.empty() && read_reg(f, ins.rd[1], &kw) &&
                    key_str(kw, &kstr) && !kstr.empty()) {
                    const std::string mkey = base_key + "/" + kstr;
                    const std::uint8_t* md = nullptr;
                    std::uint32_t mn = 0;
                    if (read_wire(f.h, mkey, &md, &mn) && md && mn) {
                        ok = write_reg(f, ins.wr[0], md, mn);
                    } else {
                        // 缺失成员 → None（容器成员可能未建）。
                        std::uint8_t* none = nullptr;
                        std::uint64_t nn = 0;
                        ok = kvspaceXhNewNone(&none, &nn) == 0 && none &&
                             write_reg(f, ins.wr[0], none,
                                       static_cast<std::uint32_t>(nn));
                        std::free(none);
                    }
                } else {
                    *err = "member read: bad base/key: " + ins.name;
                    return RunResult::Error;
                }
            } else if (member_write) {
                // 成员写 `v -> base·name` / `v -> base·*key` / `v -> base·*"lit"`。
                // 写槽 `base·rest`：rest 以 `*` 开头 = 动态键（读 `*` 后变量/字面量的值），
                // 否则 rest 是静态键文本。值 = 读参（v），保留完整 XValue 类型。
                const std::string& wname = wdesc.name;
                const auto mid = wname.find("\xC2\xB7");
                const std::string base_part = wname.substr(0, mid);
                // `·` 是 2 字节 UTF-8；`·*` 后是动态键，`·name` 后是静态键名。
                const std::size_t after_dot = mid + 2;
                const bool dyn = after_dot < wname.size() && wname[after_dot] == '*';
                const std::string rest =
                    wname.substr(after_dot + (dyn ? 1 : 0));
                std::string kstr;
                if (dyn) {
                    const std::string keyref = rest;
                    if (!keyref.empty() && keyref[0] == '"') {
                        kstr = keyref.substr(1, keyref.size() - 2);  // `·*"a"` → 文本 a
                    } else {
                        // 动态键变量：读帧成员值（纯内存帧局部变量走 fast 缓存）。
                        Wire kvw{};
                        const std::string keyreg =
                            std::string("v:") + keyref;
                        std::uint8_t kridx = 255;
                        for (std::size_t kr = 0; kr < f.regs.size(); kr++) {
                            // 惰性 key 普通变量 desc=name（帧局部），与 key 等价匹配。
                            if (f.regs[kr].key == f.frame + "/" + keyref ||
                                f.regs[kr].desc == keyref ||
                                (f.regs[kr].desc_ref &&
                                 f.regs[kr].desc_ref->name == keyref)) {
                                kridx = static_cast<std::uint8_t>(kr);
                                break;
                            }
                        }
                        if (kridx == 255 ||
                            !read_reg(f, kridx, &kvw) ||
                            !key_str(kvw, &kstr) || kstr.empty()) {
                            *err = "member write: bad key var: " + wname;
                            return RunResult::Error;
                        }
                    }
                } else {
                    kstr = rest;  // 静态键 `d·a` → 键 a
                }
                // base 槽 key = 帧局部变量名（arr 是帧变量，key=frame/arr）；
                // `/` 开头 = 绝对路径字面量 base（如 /tmp/two_sum·*x），直接用。
                const std::string base_key =
                    !base_part.empty() && base_part[0] == '/'
                        ? base_part
                        : f.frame + "/" + base_part;
                if (base_key.empty()) {
                    *err = "member write: no base: " + ins.name;
                    return RunResult::Error;
                }
                const std::string mkey = base_key + "/" + kstr;
                char e[192] = {};
                Wire kw{};
                if (!read_reg(f, ins.rd[0], &kw)) {
                    *err = "member write: no value: " + ins.name;
                    return RunResult::Error;
                }
                ok = kvspaceSetValue(f.h, mkey.c_str(), kw.d, kw.n, 0, 0, e,
                                     sizeof e) == 0;
            } else {
                Wire src{};
                if (read_reg(f, ins.rd[0], &src)) {
                    ok = write_reg(f, ins.wr[0], src.d, src.n);
                } else {
                    // 未赋值变量读作 None：整体写 None XValue，不是错误。
                    std::uint8_t* none = nullptr;
                    std::uint64_t nn = 0;
                    ok = kvspaceXhNewNone(&none, &nn) == 0 && none &&
                         write_reg(f, ins.wr[0], none,
                                   static_cast<std::uint32_t>(nn));
                    std::free(none);
                }
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
