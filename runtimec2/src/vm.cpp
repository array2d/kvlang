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

struct Prof {
    unsigned long long handler = 0, loop = 0, steps = 0;
    unsigned long long pre = 0, loads = 0, frames = 0, ctrl = 0, fb = 0;
    unsigned long long f_slot = 0, f_raw = 0, w_fast = 0, w_fb = 0;
};
Prof g_prof;
bool g_prof_on = false;
bool g_trace_on = false;
unsigned long long g_trace_n = 200;

unsigned long long now() { return __builtin_ia32_rdtsc(); }

void prof_report() {
    if (!g_prof_on || g_prof.steps == 0)
        return;
    std::fprintf(stderr,
                 "RC2PROF steps=%llu frames=%llu handler=%.1f loop=%.1f "
                 "pre=%.1f loads=%.1f ctrl=%.1f fb=%llu fslot=%llu fraw=%llu "
                 "wfast=%llu wfb=%llu total=%.1f\n",
                 g_prof.steps, g_prof.frames,
                 (double)g_prof.handler / g_prof.steps,
                 (double)g_prof.loop / g_prof.steps,
                 (double)g_prof.pre / g_prof.steps,
                 (double)g_prof.loads / g_prof.steps,
                 (double)g_prof.ctrl / g_prof.steps, g_prof.fb, g_prof.f_slot,
                 g_prof.f_raw, g_prof.w_fast, g_prof.w_fb,
                 (double)(g_prof.handler + g_prof.loop) / g_prof.steps);
}

void prof_init() {
    static bool inited = false;
    if (inited)
        return;
    inited = true;
    g_prof_on = std::getenv("RC2_PROF") != nullptr;
    g_trace_on = std::getenv("RC2_TRACE") != nullptr;
    {
        const char* e = std::getenv("RC2_TRACE_N");
        g_trace_n = e ? std::strtoull(e, nullptr, 10) : 200ULL;
    }
    if (g_prof_on)
        std::atexit(prof_report);
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
    *out = r.view;
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
    kvspaceSlotView_t v{};
    bool have_view = reg_slot(f, idx, &v);
    if (g_prof_on && !have_view)
        g_prof.f_slot++;
    if (have_view) {
        if (r.sc_ok) {
            if (v_scalar_raw(r.sck, r.scw, v.body, v.body_cap, out))
                return true;
            if (g_prof_on)
                g_prof.f_raw++;
        } else if (slot_read_scalar(v, out)) {
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

const char* kind_name(Scalar::K k, std::uint8_t width);

bool slot_write_scalar(FrameCtx& f, std::uint8_t idx, const Scalar& s,
                       std::uint8_t width) {
    kvspaceSlotView_t v{};
    const char* kn = kind_name(s.k, width);
    if (!kn)
        return false;
    if (reg_slot(f, idx, &v) && v.head && v.headlen > 18 && v.body &&
        v.body_cap >= width) {
        const char* lt = reinterpret_cast<const char*>(v.head + 18);
        const std::size_t cap = v.headlen - 18;
        const std::size_t n = ::strnlen(lt, cap);
        if (n == std::strlen(kn) && std::memcmp(lt, kn, n) == 0) {
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
            std::memcpy(v.body, &u, width);
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

bool compute_int(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b, Scalar* r) {
    const bool sgn = k == Scalar::K::Int;
    const std::int64_t ai = a.i, bi = b.i;
    const std::uint64_t au = a.u, bu = b.u;
    std::int64_t ri = 0;
    switch (op) {
    case ops::Op::Add: ri = ai + bi; break;
    case ops::Op::Sub: ri = ai - bi; break;
    case ops::Op::Mul: ri = ai * bi; break;
    case ops::Op::Div:
        if ((sgn && bi == 0) || (!sgn && bu == 0)) return false;
        ri = sgn ? ai / bi : (std::int64_t)(au / bu);
        break;
    case ops::Op::Mod:
        if ((sgn && bi == 0) || (!sgn && bu == 0)) return false;
        ri = sgn ? ai % bi : (std::int64_t)(au % bu);
        break;
    case ops::Op::Max: ri = sgn ? (ai > bi ? ai : bi) : (std::int64_t)(au > bu ? au : bu); break;
    case ops::Op::Min: ri = sgn ? (ai < bi ? ai : bi) : (std::int64_t)(au < bu ? au : bu); break;
    case ops::Op::Neg: ri = -ai; break;
    case ops::Op::Abs: ri = ai < 0 ? -ai : ai; break;
    case ops::Op::Sign: ri = ai > 0 ? 1 : ai < 0 ? -1 : 0; break;
    case ops::Op::BitAnd: ri = ai & bi; break;
    case ops::Op::BitOr: ri = ai | bi; break;
    case ops::Op::BitXor: ri = ai ^ bi; break;
    case ops::Op::Shl: ri = (std::int64_t)(au << (bu & 63)); break;
    case ops::Op::Shr: ri = sgn ? (ai >> (bu & 63)) : (std::int64_t)(au >> (bu & 63)); break;
    default: return false;
    }
    *r = a;
    if (sgn) {
        r->i = ri;
        r->u = (std::uint64_t)ri;
    } else {
        r->u = (std::uint64_t)ri;
        r->i = (std::int64_t)r->u;
    }
    return true;
}

bool compute_float(ops::Op op, double a, double b, double* r) {
    switch (op) {
    case ops::Op::Add: *r = a + b; return true;
    case ops::Op::Sub: *r = a - b; return true;
    case ops::Op::Mul: *r = a * b; return true;
    case ops::Op::Div: *r = a / b; return true;
    case ops::Op::Max: *r = a > b ? a : b; return true;
    case ops::Op::Min: *r = a < b ? a : b; return true;
    case ops::Op::Neg: *r = -a; return true;
    case ops::Op::Abs: *r = a < 0 ? -a : a; return true;
    case ops::Op::Sign: *r = a > 0 ? 1 : a < 0 ? -1 : 0; return true;
    case ops::Op::Sqrt: *r = __builtin_sqrt(a); return true;
    case ops::Op::Exp: *r = __builtin_exp(a); return true;
    case ops::Op::Log: *r = __builtin_log(a); return true;
    case ops::Op::Pow: *r = __builtin_pow(a, b); return true;
    default: return false;
    }
}

bool compare(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b, bool* out) {
    if (k == Scalar::K::Int) {
        switch (op) {
        case ops::Op::Eq: *out = a.i == b.i; return true;
        case ops::Op::Neq: *out = a.i != b.i; return true;
        case ops::Op::Lt: *out = a.i < b.i; return true;
        case ops::Op::Le: *out = a.i <= b.i; return true;
        case ops::Op::Gt: *out = a.i > b.i; return true;
        case ops::Op::Ge: *out = a.i >= b.i; return true;
        default: return false;
        }
    }
    if (k == Scalar::K::Uint || k == Scalar::K::Bool) {
        switch (op) {
        case ops::Op::Eq: *out = a.u == b.u; return true;
        case ops::Op::Neq: *out = a.u != b.u; return true;
        case ops::Op::Lt: *out = a.u < b.u; return true;
        case ops::Op::Le: *out = a.u <= b.u; return true;
        case ops::Op::Gt: *out = a.u > b.u; return true;
        case ops::Op::Ge: *out = a.u >= b.u; return true;
        default: return false;
        }
    }
    if (k == Scalar::K::Float) {
        switch (op) {
        case ops::Op::Eq: *out = a.f == b.f; return true;
        case ops::Op::Neq: *out = a.f != b.f; return true;
        case ops::Op::Lt: *out = a.f < b.f; return true;
        case ops::Op::Le: *out = a.f <= b.f; return true;
        case ops::Op::Gt: *out = a.f > b.f; return true;
        case ops::Op::Ge: *out = a.f >= b.f; return true;
        default: return false;
        }
    }
    return false;
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
        const unsigned long long t_start = g_prof_on ? now() : 0;
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
        unsigned long long t0 = g_prof_on ? now() : 0;
        if (ins.op == ops::Op::Goto || ins.op == ops::Op::Br) {
            Scalar target{};
            if (!read_scalar(f, ins.rd[ins.op == ops::Op::Br ? 1 : 0], &target)) {
                *err = "control: bad target";
                return RunResult::Error;
            }
            int dest = static_cast<int>(target.i);
            if (ins.op == ops::Op::Br) {
                Scalar cond{};
                if (!read_scalar(f, ins.rd[0], &cond)) {
                    *err = "br: bad cond";
                    return RunResult::Error;
                }
                if (!truthy(cond)) {
                    if (!read_scalar(f, ins.rd[2], &target)) {
                        *err = "br: bad false target";
                        return RunResult::Error;
                    }
                    dest = static_cast<int>(target.i);
                }
            }
            if (dest < 1 || dest > n + 1) {
                *err = "control: target out of range";
                return RunResult::Error;
            }
            idx = dest - 2;
            if (g_prof_on) {
                g_prof.ctrl += now() - t0;
                g_prof.steps++;
            }
            continue;
        }
        if (ins.op == ops::Op::Return) {
            if (g_prof_on) {
                g_prof.loop += now() - t0;
                g_prof.steps++;
            }
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
                g_prof.handler += now() - t0;
                g_prof.steps++;
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
        unsigned long long th = g_prof_on ? now() : 0;
        if (g_prof_on) {
            g_prof.pre += t0 - t_start;
            g_prof.loads += th - t0;
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
                g_prof.handler += now() - th;
                g_prof.steps++;
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
        }
        if (g_prof_on) {
            g_prof.handler += now() - th;
            g_prof.loop += (th - t0);
            g_prof.steps++;
        }
        if (!ok) {
            *err = "value op failed: " + ins.name;
            return RunResult::Error;
        }
    }
    return RunResult::Ok;
}

}  // namespace kv::rt
