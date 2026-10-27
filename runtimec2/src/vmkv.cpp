#include "vmkv.hpp"

#include <cstdlib>
#include <cstring>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "model.hpp"
#include "value.hpp"
#include "valueio.hpp"

namespace kv::rt {
using bin::g_prof;
using bin::g_prof_on;
// 明文栈操作（装载/槽解析/回写）。FReg/FrameCtx 来自 vmbin（bin 命名空间）。
using bin::FReg;
using bin::FrameCtx;

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

// shm xvaluebodyview：head/body 直接指针（帧内缓存，写后失效）。
bool reg_slot(FrameCtx& f, std::uint8_t idx, kvspaceXvalueBodyView_t* out) {
    FReg& r = f.regs[idx];
    if (r.view_ok) {
        *out = r.view;
        return true;
    }
    if (!ensure_ref(f, &r))
        return false;
    if (kvspaceXvalueBodyView(f.h, &r.ref, r.key.c_str(), &r.view) != 0)
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
        if (kvspaceXvalueBodyView(f.h, &r.ref, r.key.c_str(), &r.view) != 0)
            return false;
        r.view_ok = true;
        r.body = r.view.body;
        r.cap = r.view.body_cap;
    }
    *body = r.body;
    *cap = r.cap;
    return true;
}

bool slot_langtype(const kvspaceXvalueBodyView_t& v, std::string* lt) {
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
bool slot_read_scalar(const kvspaceXvalueBodyView_t& v, Scalar* out) {
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
bool ensure_reg_key(FrameCtx& f, FReg& r) {
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

// 读标量：xvaluebodyview 直读；kind/width 每帧解析一次后缓存。
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
bool is_frame_local(const FrameCtx& f, const std::string& rk) {
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
}  // namespace kv::rt
