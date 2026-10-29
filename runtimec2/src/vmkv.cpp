#include "vmkv.hpp"
#include "vmkv_native.hpp"
#include "jit.hpp"

#include <cstdlib>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <memory>
#include <sys/random.h>
#include <ctime>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "value.hpp"
#include "valueio.hpp"

namespace kvlang::runtimec2 {
// 明文栈操作（装载/槽解析/回写）。FReg/FrameCtx 来自 vmbin（bin 命名空间）。
using bin::FReg;
using bin::FrameCtx;
using bin::RunResult;
using bin::fast_i64;

namespace {

// fast kernel A/B 开关（对拍定位用）：RC2_NOFAST=1 关连续执行，回主循环单步。
const bool g_no_fast = std::getenv("RC2_NOFAST") != nullptr;
const bool g_trace = std::getenv("RC2_TRACE") != nullptr;

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

// Callee FrameCtx free list: reused frames keep their string/array capacity.
thread_local std::vector<std::unique_ptr<FrameCtx>> g_fctx_free;

struct FctxLease {
    FrameCtx* p;
    FctxLease() {
        if (g_fctx_free.empty()) {
            p = new FrameCtx;
        } else {
            p = g_fctx_free.back().release();
            g_fctx_free.pop_back();
            FrameCtx& c = *p;
            c.regs.clear();
            c.arg_src_n = c.arg_dst_n = 0;
            c.mem_frame = false;
            c.seg_hint = nullptr;
            c.jit_hint = nullptr;
            c.jit_known = false;
            c.frame_dir_hint.clear();
        }
    }
    ~FctxLease() { g_fctx_free.emplace_back(p); }
    FctxLease(const FctxLease&) = delete;
    FctxLease& operator=(const FctxLease&) = delete;
};


// `/vthread/<vid>/[<d>]/<name>` with a plain name (no `/`, `[`, `‥`).
bool is_any_frame_local(std::string_view k) {
    constexpr std::string_view pre = "/vthread/";
    if (k.substr(0, pre.size()) != pre)
        return false;
    const std::size_t v = k.find('/', pre.size());
    if (v == std::string_view::npos || v + 2 >= k.size() || k[v + 1] != '[')
        return false;
    const std::size_t e = k.find("]/", v + 2);
    if (e == std::string_view::npos)
        return false;
    for (std::size_t i = v + 2; i < e; i++)
        if (k[i] < '0' || k[i] > '9')
            return false;
    const std::string_view name = k.substr(e + 2);
    return !name.empty() && name[0] != '[' &&
           name.find('/') == std::string_view::npos &&
           name.find("\xE2\x80\xA5") == std::string_view::npos;
}

// Whether r, or the caller reg it read-aliases, caches a scalar value.
bool holds_value(const FReg* r) {
    while (!(r->fast_i64 || r->fast_bool) && r->src_ptr)
        r = r->src_ptr;
    return r->fast_i64 || r->fast_bool;
}

// First Var reg whose name (type suffix stripped) is `name`; -1 if none.
int reg_by_name(const Segment* seg, std::string_view name) {
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        const RegDesc& d = seg->regs[i];
        if (d.kind != Operand::Kind::Var)
            continue;
        const std::string_view n = d.name;
        if (n == name || n.substr(0, n.find(' ')) == name)
            return static_cast<int>(i);
    }
    return -1;
}

// "/vthread/<vid>/[<depth>]" per depth; deque keeps references stable.
const std::string& child_frame_root(const std::string& vid, int depth) {
    thread_local std::deque<std::pair<std::string, std::string>> c;
    const std::size_t d = depth < 0 ? 0 : static_cast<std::size_t>(depth);
    if (d >= c.size())
        c.resize(d + 1);
    auto& e = c[d];
    if (e.second.empty() || e.first != vid) {
        e.first = vid;
        e.second = g_vmkv.frame_root(vid, depth);
    }
    return e.second;
}

}  // namespace

// 变量描述符 → 具体 key（帧入口一次；Ptr 链在此解析完）。
bool Vmkv::resolve_desc(FrameCtx& f, const std::string& name, std::string* key,
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
            // arg 槽缺失 = 空指针（None 在 shm 即无键）：key 指回 arg 槽，
            // 读取自然返回 None，不是错误（`inorder(None)` 传空指针参数）。
            *key = target;
            return true;
        }
        // 读参（`*[0,-k]`）取 arg 槽本体（Ptr→调用方槽，一跳）：`head -> cur`
        // 复制的是指针；两跳会落到调用方实例。写参（`*[0,k]`）才两跳取地址。
        const bool read_arg = member.size() >= 4 && member[3] == '-';
        Wire aw{ad, an};
        std::string t2;
        if (!read_arg && v_ptr_target(aw, &t2)) {
            *key = t2;  // 写参 Ptr 链：arg 槽 Ptr → 调用方槽
        } else {
            *key = target;  // 读参：arg 槽即 Ptr/值槽（本帧，view 缓存）
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
    if (kvspaceResolveRef(f.h, r->key.c_str(), &r->ref) != 0) {
        r->noslot = true;
        return false;
    }
    r->resolved = true;
    r->noslot = false;
    return true;
}

// shm xvaluebodyview：head/body 直接指针（帧内缓存，写后失效）。
bool Vmkv::reg_slot(FrameCtx& f, std::uint8_t idx, kvspaceXvalueBodyView_t* out) {
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
bool Vmkv::reg_body(FrameCtx& f, std::uint8_t idx, std::uint8_t** body,
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

bool Vmkv::slot_langtype(const kvspaceXvalueBodyView_t& v, std::string* lt) {
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
bool Vmkv::slot_read_scalar(const kvspaceXvalueBodyView_t& v, Scalar* out) {
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
bool Vmkv::ensure_reg_key(FrameCtx& f, FReg& r) {
    if (r.key.empty()) {
        // 内存别名写参（dst_ptr 链）：key = 调用方 reg 最终槽（纯内存帧无 arg 槽，
        // `p·x = v` 的 p 写参直接落在调用方实例成员上）。
        FReg* ap = &r;
        while (ap->dst_ptr)
            ap = ap->dst_ptr;
        if (ap != &r && !ap->key.empty()) {
            r.key = ap->key;
            return true;
        }
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

bool Vmkv::read_reg(FrameCtx& f, std::uint8_t idx, Wire* out) {
    FReg& r = f.regs[idx];
    // 内存别名读参：转发读调用方 reg 的 fast 缓存（src_ptr 链已在帧入口挂好）。
    FReg* fr = r.src_ptr ? r.src_ptr : &r;
    if (!r.src_ptr && !ensure_reg_key(f, r))
        return false;
    // 纯内存帧局部变量（write_reg 内存化、fast_i64 已设、无 kvspace 槽）：直构 int64 wire。
    // 别名 reg（src_ptr）key 为空，走 fast 缓存构造。
    const std::string& rk = r.key;
    const std::size_t fs = f.frame.size();
    const bool flk =
        r.src_ptr || (rk.size() >= fs + 1 && rk.compare(0, fs, f.frame) == 0 &&
                      rk[fs] == '/' &&
                      rk.find("/[0,") == std::string::npos &&
                      rk.find("\xE2\x80\xA5") == std::string::npos);
    if (g_trace && rk == f.frame + "/t1")
        std::fprintf(stderr, "[readreg t1 fast=%d flk=%d src=%p]\n",
                     (int)fr->fast_i64, (int)flk, (void*)r.src_ptr);
    if (fr->fast_i64 && flk) {
        // The int64 wire is fixed-shape: encode once, then patch the body.
        thread_local std::uint8_t s_buf[64];
        thread_local std::uint32_t s_len = 0;
        if (!s_len) {
            std::uint8_t* buf = nullptr;
            std::uint32_t len = 0;
            const std::int64_t zero = 0;
            if (kvspaceXvalueEncode("int64",
                                    reinterpret_cast<const std::uint8_t*>(&zero), 8,
                                    nullptr, 0, &buf, &len) == 0 &&
                buf && len >= 8 && len <= sizeof s_buf) {
                std::memcpy(s_buf, buf, len);
                s_len = len;
            }
            std::free(buf);
        }
        if (s_len) {
            std::memcpy(s_buf + s_len - 8, &fr->fv, 8);
            out->d = s_buf;
            out->n = s_len;
            return true;
        }
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
bool Vmkv::read_scalar(FrameCtx& f, std::uint8_t idx, Scalar* out) {
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
    if (bin.prof_on && !have_view)
        bin.prof.f_slot++;
    if (have_view) {
        if (r.sc_ok) {
            if (v_scalar_raw(r.sck, r.scw, body, cap, out))
                return true;
            if (bin.prof_on)
                bin.prof.f_raw++;
        } else if (slot_read_scalar(r.view, out)) {
            r.sck = out->k;
            r.scw = out->width;
            r.sc_ok = true;
            return true;
        }
    }
    if (bin.prof_on)
        bin.prof.fb++;
    Wire w{};
    return read_reg(f, idx, &w) && v_scalar(w, out);
}

// 帧局部普通变量 key（非参数槽、非系统槽）：纯内存化候选。
bool Vmkv::is_frame_local(const FrameCtx& f, const std::string& rk) {
    const std::size_t fs = f.frame.size();
    return rk.size() > fs + 1 && rk.compare(0, fs, f.frame) == 0 && rk[fs] == '/' &&
           rk.find("/[0,") == std::string::npos &&
           rk.find("\xE2\x80\xA5") == std::string::npos;
}

// 冲刷帧局部纯内存变量落明文（Stop/native 边界前；纯内存子帧无 native 则免）。
// 仅对 fast 缓存有效但无 kvspace 槽的变量落一次明文，后续 read 走明文/新 view。
void Vmkv::flush_frame_locals(FrameCtx& f) {
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

bool Vmkv::write_reg(FrameCtx& f, std::uint8_t idx, const std::uint8_t* data,
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
                if (dr.view_ok && dr.body && !store_i64_slot(f, dr, v))
                    return false;
                r.fv = v;
                r.fast_i64 = true;
                return true;
            }
        }
        // 非 int64 写参：转发落 kvspace（别名调用不常见，回退安全路径）。
        if (dr.view_ok) {
            dr.view_ok = false;
        }
        dr.base_ptr_known = false;
        char err[192] = {};
        return kvspaceSetValue(f.h, dr.key.c_str(), data, len, 0, 0, err,
                               sizeof err) == 0;
    }
    // 成员写槽（desc 含 `·`，如 `p·x`）：base 是帧变量/参数（可能 dst_ptr 别名
    // 指向调用方实例），key = 最终 base 槽 + `·` + 成员名（`p·x = v` 就地改
    // 调用方对象，见 ptr-param-mutate）。绝对路径 base（`/tmp/x·y`）直接用。
    const std::string& wdsc = !r.desc.empty() ? r.desc
                             : (r.desc_ref ? r.desc_ref->name : r.desc);
    if (wdsc.find("\xC2\xB7") != std::string::npos && wdsc[0] != '/') {
        const auto mid = wdsc.find("\xC2\xB7");
        const std::string base_part = wdsc.substr(0, mid);
        const std::string mname = wdsc.substr(mid + 2);
        // base 对应的 reg：desc 名匹配（`p` 参数/变量）→ 沿 src/dst 别名链到最终槽
        // （`p·x` 的 p 写参 dst_ptr→调用方 a）。
        FReg* bp = &r;
        for (FReg& rr : f.regs) {
            const std::string rn = !rr.desc.empty() ? rr.desc
                                   : (rr.desc_ref ? rr.desc_ref->name : std::string());
            if (rn == base_part || rr.key == f.frame + "/" + base_part) {
                bp = &rr;
                break;
            }
        }
        while (bp->src_ptr || bp->dst_ptr)
            bp = bp->src_ptr ? bp->src_ptr : bp->dst_ptr;
        // base 名未匹配（写槽 `p·x` 的 p 是 `*[0,1]` 写参描述符）：取已解析到
        // 调用方实例的写参 reg 链作 base（成员写就地改调用方对象）。
        if (bp == &r || bp->key.empty()) {
            for (FReg& rr : f.regs) {
                const std::string rn = !rr.desc.empty() ? rr.desc
                                       : (rr.desc_ref ? rr.desc_ref->name
                                                      : std::string());
                if (rn.rfind("*[0,", 0) == 0) {
                    FReg* rp = &rr;
                    while (rp->src_ptr || rp->dst_ptr)
                        rp = rp->src_ptr ? rp->src_ptr : rp->dst_ptr;
                    if (rp->key.empty() && !ensure_reg_key(f, *rp))
                        continue;
                    if (!rp->key.empty()) {
                        bp = rp;
                        break;
                    }
                }
            }
        }
        if (bp->key.empty() && !ensure_reg_key(f, *bp))
            return false;
        // base 槽是 Ptr（`cur->next = v` 的 cur 存指针）：成员写落在 target 上。
        // The base reg keeps its own key: rebinding it would aim later writes to
        // the variable itself (`cur·next -> cur`) at the old target.
        std::string bkey = bp->key;
        if (!bkey.empty()) {
            const std::uint8_t* bd = nullptr;
            std::uint32_t bn = 0;
            std::string bt;
            if (read_wire_raw(f.h, bkey, &bd, &bn) &&
                v_ptr_target(Wire{bd, bn}, &bt) && !bt.empty())
                bkey = bt;
        }
        if (!bkey.empty() && !mname.empty() && mname[0] != '*') {
            r.key = bkey + "\xC2\xB7" + mname;
            r.view_ok = false;
            r.sc_ok = false;
            r.fast_i64 = false;
            r.fast_bool = false;
            char errb[192] = {};
            return kvspaceSetValue(f.h, r.key.c_str(), data, len, 0, 0, errb,
                                   sizeof errb) == 0;
        }
    }
    if (!ensure_reg_key(f, r))
        return false;
    r.view_ok = false;
    r.sc_ok = false;
    r.fast_i64 = false;
    r.fast_bool = false;
    r.base_ptr_known = false;  // 槽值可能变 Ptr/非 Ptr，成员 base 缓存失效
    // 帧局部 int64 变量：内存化写（FReg.fv + 槽直写若已有 view），层内读走 fast
    // 缓存，免每 op kvspace 往返（fib/binary_search 热点）。参数/绝对路径/系统槽照旧。
    const std::string& rk = r.key;
    const bool frame_local = is_frame_local(f, rk);
    if (g_trace && rk == f.frame + "/t1")
        std::fprintf(stderr, "[writereg t1 fl=%d len=%u]\n", (int)frame_local,
                     len);
    if (frame_local && len >= 8) {
        kvspaceXh xh{};
        if (g_trace && rk == f.frame + "/t1" &&
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
            if (r.view_ok && r.body)
                return store_i64_slot(f, r, v);  // 已有槽：直写同步明文
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
        if (g_trace)
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
bool Vmkv::read_i64(FrameCtx& f, std::uint8_t idx, std::int64_t* out) {
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

// 直取 bool 槽值（比较/br/条件）。
bool Vmkv::read_bool(FrameCtx& f, std::uint8_t idx, bool* out) {
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

// 专用直写 int64：缓存命中（Int/8）时免 Scalar 装箱、免 kind 判定，纯 8 字节 memcpy。
// Store v into r's existing slot: in place if it is int64, else re-encode it.
bool Vmkv::store_i64_slot(FrameCtx& f, FReg& r, std::int64_t v) {
    const kvspaceXvalueBodyView_t& w = r.view;
    if (w.head && w.headlen > 18 && w.body_cap >= 8 &&
        std::memcmp(w.head + 18, "int64", 6) == 0) {
        std::memcpy(r.body, &v, 8);
        return true;
    }
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    char err[192] = {};
    const bool ok =
        !r.key.empty() &&
        kvspaceXvalueEncode("int64", reinterpret_cast<const std::uint8_t*>(&v), 8,
                            nullptr, 0, &buf, &len) == 0 &&
        buf && kvspaceSetValue(f.h, r.key.c_str(), buf, len, 0, 0, err, sizeof err) == 0;
    std::free(buf);
    r.view_ok = false;
    r.body = nullptr;
    r.resolved = false;
    r.noslot = false;
    r.sc_ok = false;
    r.fast_bool = false;
    return ok;
}

bool Vmkv::write_member_scalar(FrameCtx& f, std::uint8_t idx, const Scalar& s,
                               std::uint8_t width) {
    const char* kn = kind_name(s.k, width);
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    const bool ok = kn && v_encode(kn, width, s, &buf, &len) &&
                    write_reg(f, idx, buf, len);
    std::free(buf);
    return ok;
}

bool Vmkv::slot_write_i64(FrameCtx& f, std::uint8_t idx, std::int64_t v) {
    FReg& fr = f.regs[idx];
    if (fr.desc_ref && fr.desc_ref->member) {
        Scalar s{};
        s.k = Scalar::K::Int;
        s.width = 8;
        s.i = v;
        s.u = (std::uint64_t)v;
        if (!write_member_scalar(f, idx, s, 8))
            return false;
        fr.fv = v;  // callers mark the cache valid; keep it equal to what was written
        fr.fast_i64 = true;
        return true;
    }
    // 内存别名写参（mem_frame 递归/纯内存调用的返回槽）：直写调用方 reg
    // （镜像 write_reg 的 dst_ptr 分支），免每迭代 v_encode+malloc 回退。
    if (fr.dst_ptr) {
        FReg& dr = *fr.dst_ptr;
        if (dr.view_ok && dr.body && !store_i64_slot(f, dr, v))
            return false;
        dr.fv = v;
        dr.fast_i64 = true;
        fr.fv = v;
        fr.fast_i64 = true;
        return true;
    }
    // 帧局部变量（含首次写，fast_i64 未设）：无 view 也直写 fast 缓存 return
    // （免 kvspace）。非帧局部（参数/容器/系统槽）照旧落明文。
    if (is_frame_local(f, fr.key)) {
        if (fr.view_ok && fr.body && !store_i64_slot(f, fr, v))
            return false;
        fr.fv = v;
        fr.fast_i64 = true;
        if (bin.prof_on)
            bin.prof.w_fast++;
        return true;
    }
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    if ((fr.fast_i64 || (fr.sc_ok && fr.sck == Scalar::K::Int && fr.scw == 8)) &&
        reg_body(f, idx, &body, &cap) && body && cap >= 8) {
        if (!store_i64_slot(f, fr, v))
            return false;
        fr.fv = v;
        fr.fast_i64 = true;
        if (bin.prof_on)
            bin.prof.w_fast++;
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
bool Vmkv::slot_write_bool(FrameCtx& f, std::uint8_t idx, bool b) {
    FReg& fr = f.regs[idx];
    if (fr.desc_ref && fr.desc_ref->member) {
        Scalar s{};
        s.k = Scalar::K::Bool;
        s.width = 1;
        s.u = b ? 1 : 0;
        if (!write_member_scalar(f, idx, s, 1))
            return false;
        fr.fb = b;
        fr.fast_bool = true;
        return true;
    }
    // 内存别名写参：直写调用方 reg（对齐 slot_write_i64 的 dst_ptr 快路径）。
    if (fr.dst_ptr) {
        FReg& dr = *fr.dst_ptr;
        dr.fb = b;
        dr.fast_bool = true;
        if (dr.view_ok && dr.body)
            *dr.body = b ? 1 : 0;
        fr.fb = b;
        fr.fast_bool = true;
        return true;
    }
    // 帧局部纯内存变量：无 view 也直写 fast 缓存 return（免 kvspace）。
    if (fr.fast_bool && is_frame_local(f, fr.key)) {
        fr.fb = b;
        if (fr.view_ok && fr.body)
            *fr.body = b ? 1 : 0;
        if (bin.prof_on)
            bin.prof.w_fast++;
        return true;
    }
    std::uint8_t* body = nullptr;
    std::uint64_t cap = 0;
    if ((fr.fast_bool || (fr.sc_ok && fr.sck == Scalar::K::Bool && fr.scw == 1)) &&
        reg_body(f, idx, &body, &cap) && body && cap >= 1) {
        *body = b ? 1 : 0;
        fr.fb = b;
        fr.fast_bool = true;
        if (bin.prof_on)
            bin.prof.w_fast++;
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

bool Vmkv::slot_write_scalar(FrameCtx& f, std::uint8_t idx, const Scalar& s,
                       std::uint8_t width) {
    std::uint8_t* body = nullptr;
    std::uint64_t body_cap = 0;
    const char* kn = kind_name(s.k, width);
    if (!kn)
        return false;
    FReg& fr = f.regs[idx];
    if (fr.desc_ref && fr.desc_ref->member)
        return write_member_scalar(f, idx, s, width);
    // Aliased int64 write param into a caller frame local: the caller's cache is
    // the value there, so skip the encode. Other targets keep the kvspace write.
    if (fr.dst_ptr && s.k == Scalar::K::Int && width == 8) {
        const FReg* dr = fr.dst_ptr;
        while (dr->dst_ptr)
            dr = dr->dst_ptr;
        if (dr->key.empty() || is_any_frame_local(dr->key))
            return slot_write_i64(f, idx, s.i);
    }
    // Slotless frame-local int64 (a ResolveRef already missed in this frame):
    // slot_write_i64 keeps it in the cache, as it always has, without another miss.
    // Plain names only (the build-time wr_local rule): `p·x` is a member write.
    if (!fr.dst_ptr && !fr.src_ptr && !fr.view_ok && !fr.resolved && fr.noslot &&
        s.k == Scalar::K::Int && width == 8 && fr.desc_ref &&
        fr.desc_ref->vclass == RegDesc::VarClass::Plain &&
        fr.desc_ref->name.find("\xC2\xB7") == std::string::npos &&
        !fr.desc_ref->name.empty() && fr.desc_ref->name[0] != '/' &&
        ensure_reg_key(f, fr) && is_frame_local(f, fr.key))
        return slot_write_i64(f, idx, s.i);
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
            if (bin.prof_on)
                bin.prof.w_fast++;
            return true;
        }
    }
    // 回退：编码 + ByRef（槽 langtype/容量不匹配时走这里）。
    if (bin.prof_on)
        bin.prof.w_fb++;
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    if (!v_encode(kn, width, s, &buf, &len))
        return false;
    bool ok = write_reg(f, idx, buf, len);
    std::free(buf);
    return ok;
}

// 写后 fast 缓存同步（Int/8 → fv；Bool/1 → fb；其余失效）。
void Vmkv::sync_fast_after_write(FrameCtx& f, std::uint8_t idx, const Scalar& s) {
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

bool Vmkv::truthy(const Scalar& s) {
    switch (s.k) {
    case Scalar::K::Int: return s.i != 0;
    case Scalar::K::Uint:
    case Scalar::K::Bool: return s.u != 0;
    case Scalar::K::Float: return s.f != 0.0;
    default: return false;
    }
}

bool Vmkv::same_class(Scalar::K a, Scalar::K b) {
    if (a == Scalar::K::Bool)
        a = Scalar::K::Uint;
    if (b == Scalar::K::Bool)
        b = Scalar::K::Uint;
    return a == b && (a == Scalar::K::Int || a == Scalar::K::Uint ||
                      a == Scalar::K::Float);
}

const char* Vmkv::kind_name(Scalar::K k, std::uint8_t width) {
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

template <rwir::Op O, bool SGN>
constexpr bool arith_i(const Scalar& a, const Scalar& b, Scalar* r) {
    if constexpr (O == rwir::Op::Add) { r->i = a.i + b.i; return true; }
    if constexpr (O == rwir::Op::Sub) { r->i = a.i - b.i; return true; }
    if constexpr (O == rwir::Op::Mul) { r->i = a.i * b.i; return true; }
    if constexpr (O == rwir::Op::Div) {
        if (SGN) { if (b.i == 0) return false; r->i = a.i / b.i; }
        else     { if (b.u == 0) return false; r->u = a.u / b.u; }
        return true;
    }
    if constexpr (O == rwir::Op::Mod) {
        if (SGN) { if (b.i == 0) return false; r->i = a.i % b.i; }
        else     { if (b.u == 0) return false; r->u = a.u % b.u; }
        return true;
    }
    if constexpr (O == rwir::Op::Max) {
        r->i = SGN ? (a.i > b.i ? a.i : b.i) : (std::int64_t)(a.u > b.u ? a.u : b.u);
        return true;
    }
    if constexpr (O == rwir::Op::Min) {
        r->i = SGN ? (a.i < b.i ? a.i : b.i) : (std::int64_t)(a.u < b.u ? a.u : b.u);
        return true;
    }
    if constexpr (O == rwir::Op::Neg) { r->i = -a.i; return true; }
    if constexpr (O == rwir::Op::Abs) { r->i = a.i < 0 ? -a.i : a.i; return true; }
    if constexpr (O == rwir::Op::Sign) { r->i = a.i > 0 ? 1 : a.i < 0 ? -1 : 0; return true; }
    if constexpr (O == rwir::Op::BitAnd) { r->i = a.i & b.i; return true; }
    if constexpr (O == rwir::Op::BitOr)  { r->i = a.i | b.i; return true; }
    if constexpr (O == rwir::Op::BitXor) { r->i = a.i ^ b.i; return true; }
    if constexpr (O == rwir::Op::Shl) {
        // SGN 时 i 有效（u 可能未同步）；无符号用 u。移位量取低 6 位。
        r->i = (std::int64_t)((SGN ? (std::uint64_t)a.i : a.u)
                              << (SGN ? (std::uint64_t)b.i & 63 : b.u & 63));
        return true;
    }
    if constexpr (O == rwir::Op::Shr) {
        r->i = SGN ? (a.i >> (b.i & 63)) : (std::int64_t)(a.u >> (b.u & 63));
        return true;
    }
    return false;
}

template <rwir::Op O>
constexpr bool arith_f(const Scalar& a, const Scalar& b, Scalar* r) {
    if constexpr (O == rwir::Op::Add) { r->f = a.f + b.f; return true; }
    if constexpr (O == rwir::Op::Sub) { r->f = a.f - b.f; return true; }
    if constexpr (O == rwir::Op::Mul) { r->f = a.f * b.f; return true; }
    if constexpr (O == rwir::Op::Div) { r->f = a.f / b.f; return true; }
    if constexpr (O == rwir::Op::Max) { r->f = a.f > b.f ? a.f : b.f; return true; }
    if constexpr (O == rwir::Op::Min) { r->f = a.f < b.f ? a.f : b.f; return true; }
    if constexpr (O == rwir::Op::Neg) { r->f = -a.f; return true; }
    if constexpr (O == rwir::Op::Abs) { r->f = a.f < 0 ? -a.f : a.f; return true; }
    if constexpr (O == rwir::Op::Sign) { r->f = a.f > 0 ? 1 : a.f < 0 ? -1 : 0; return true; }
    if constexpr (O == rwir::Op::Sqrt) { r->f = __builtin_sqrt(a.f); return true; }
    if constexpr (O == rwir::Op::Exp) { r->f = __builtin_exp(a.f); return true; }
    if constexpr (O == rwir::Op::Log) { r->f = __builtin_log(a.f); return true; }
    if constexpr (O == rwir::Op::Pow) { r->f = __builtin_pow(a.f, b.f); return true; }
    return false;
}

template <rwir::Op O>
constexpr bool cmp_f(const Scalar& a, const Scalar& b) {
    if constexpr (O == rwir::Op::Eq)  return a.f == b.f;
    if constexpr (O == rwir::Op::Neq) return a.f != b.f;
    if constexpr (O == rwir::Op::Lt)  return a.f < b.f;
    if constexpr (O == rwir::Op::Le)  return a.f <= b.f;
    if constexpr (O == rwir::Op::Gt)  return a.f > b.f;
    if constexpr (O == rwir::Op::Ge)  return a.f >= b.f;
    return false;
}

template <rwir::Op O>
constexpr bool cmp_sgn(const Scalar& a, const Scalar& b) {
    if constexpr (O == rwir::Op::Eq)  return a.i == b.i;
    if constexpr (O == rwir::Op::Neq) return a.i != b.i;
    if constexpr (O == rwir::Op::Lt)  return a.i < b.i;
    if constexpr (O == rwir::Op::Le)  return a.i <= b.i;
    if constexpr (O == rwir::Op::Gt)  return a.i > b.i;
    if constexpr (O == rwir::Op::Ge)  return a.i >= b.i;
    return false;
}

template <rwir::Op O>
constexpr bool cmp_uns(const Scalar& a, const Scalar& b) {
    if constexpr (O == rwir::Op::Eq)  return a.u == b.u;
    if constexpr (O == rwir::Op::Neq) return a.u != b.u;
    if constexpr (O == rwir::Op::Lt)  return a.u < b.u;
    if constexpr (O == rwir::Op::Le)  return a.u <= b.u;
    if constexpr (O == rwir::Op::Gt)  return a.u > b.u;
    if constexpr (O == rwir::Op::Ge)  return a.u >= b.u;
    return false;
}

// 分派表：Op → 三套模板实例化的函数指针（编译期查表，运行期直呼）。
// 顺序与 ops.hpp 的 Op 枚举一致。
typedef bool (*IntFn)(const Scalar&, const Scalar&, Scalar*);
typedef bool (*FloatFn)(const Scalar&, const Scalar&, Scalar*);
typedef bool (*CmpFn)(const Scalar&, const Scalar&);
static constexpr IntFn kIntTbl[] = {
    arith_i<rwir::Op::None, false>, arith_i<rwir::Op::Copy, false>, arith_i<rwir::Op::Bool, false>,
    arith_i<rwir::Op::Br, false>, arith_i<rwir::Op::Call, false>, arith_i<rwir::Op::Goto, false>,
    arith_i<rwir::Op::Return, false>, arith_i<rwir::Op::Cast, false>,
    arith_i<rwir::Op::Abs, true>, arith_i<rwir::Op::Add, true>, arith_i<rwir::Op::Sub, true>,
    arith_i<rwir::Op::Mul, true>, arith_i<rwir::Op::Div, true>, arith_i<rwir::Op::Mod, true>,
    arith_i<rwir::Op::Max, true>, arith_i<rwir::Op::Min, true>, arith_i<rwir::Op::Neg, true>,
    arith_i<rwir::Op::Sign, true>, arith_i<rwir::Op::BitAnd, true>, arith_i<rwir::Op::BitOr, true>,
    arith_i<rwir::Op::BitXor, true>, arith_i<rwir::Op::Shl, true>, arith_i<rwir::Op::Shr, true>,
    arith_i<rwir::Op::Eq, true>, arith_i<rwir::Op::Neq, true>, arith_i<rwir::Op::Lt, true>,
    arith_i<rwir::Op::Le, true>, arith_i<rwir::Op::Gt, true>, arith_i<rwir::Op::Ge, true>,
    arith_i<rwir::Op::Sqrt, true>, arith_i<rwir::Op::Exp, true>, arith_i<rwir::Op::Log, true>,
    arith_i<rwir::Op::Pow, true>, arith_i<rwir::Op::And, true>, arith_i<rwir::Op::Or, true>,
    arith_i<rwir::Op::Not, true>,
};
static constexpr FloatFn kFloatTbl[] = {
    arith_f<rwir::Op::None>, arith_f<rwir::Op::Copy>, arith_f<rwir::Op::Bool>,
    arith_f<rwir::Op::Br>, arith_f<rwir::Op::Call>, arith_f<rwir::Op::Goto>,
    arith_f<rwir::Op::Return>, arith_f<rwir::Op::Cast>,
    arith_f<rwir::Op::Abs>, arith_f<rwir::Op::Add>, arith_f<rwir::Op::Sub>,
    arith_f<rwir::Op::Mul>, arith_f<rwir::Op::Div>, arith_f<rwir::Op::Mod>,
    arith_f<rwir::Op::Max>, arith_f<rwir::Op::Min>, arith_f<rwir::Op::Neg>,
    arith_f<rwir::Op::Sign>, arith_f<rwir::Op::BitAnd>, arith_f<rwir::Op::BitOr>,
    arith_f<rwir::Op::BitXor>, arith_f<rwir::Op::Shl>, arith_f<rwir::Op::Shr>,
    arith_f<rwir::Op::Eq>, arith_f<rwir::Op::Neq>, arith_f<rwir::Op::Lt>,
    arith_f<rwir::Op::Le>, arith_f<rwir::Op::Gt>, arith_f<rwir::Op::Ge>,
    arith_f<rwir::Op::Sqrt>, arith_f<rwir::Op::Exp>, arith_f<rwir::Op::Log>,
    arith_f<rwir::Op::Pow>, arith_f<rwir::Op::And>, arith_f<rwir::Op::Or>,
    arith_f<rwir::Op::Not>,
};
static constexpr CmpFn kCmpSgnTbl[] = {
    cmp_sgn<rwir::Op::None>, cmp_sgn<rwir::Op::Copy>, cmp_sgn<rwir::Op::Bool>,
    cmp_sgn<rwir::Op::Br>, cmp_sgn<rwir::Op::Call>, cmp_sgn<rwir::Op::Goto>,
    cmp_sgn<rwir::Op::Return>, cmp_sgn<rwir::Op::Cast>,
    cmp_sgn<rwir::Op::Abs>, cmp_sgn<rwir::Op::Add>, cmp_sgn<rwir::Op::Sub>,
    cmp_sgn<rwir::Op::Mul>, cmp_sgn<rwir::Op::Div>, cmp_sgn<rwir::Op::Mod>,
    cmp_sgn<rwir::Op::Max>, cmp_sgn<rwir::Op::Min>, cmp_sgn<rwir::Op::Neg>,
    cmp_sgn<rwir::Op::Sign>, cmp_sgn<rwir::Op::BitAnd>, cmp_sgn<rwir::Op::BitOr>,
    cmp_sgn<rwir::Op::BitXor>, cmp_sgn<rwir::Op::Shl>, cmp_sgn<rwir::Op::Shr>,
    cmp_sgn<rwir::Op::Eq>, cmp_sgn<rwir::Op::Neq>, cmp_sgn<rwir::Op::Lt>,
    cmp_sgn<rwir::Op::Le>, cmp_sgn<rwir::Op::Gt>, cmp_sgn<rwir::Op::Ge>,
    cmp_sgn<rwir::Op::Sqrt>, cmp_sgn<rwir::Op::Exp>, cmp_sgn<rwir::Op::Log>,
    cmp_sgn<rwir::Op::Pow>, cmp_sgn<rwir::Op::And>, cmp_sgn<rwir::Op::Or>,
    cmp_sgn<rwir::Op::Not>,
};
static constexpr CmpFn kCmpUnsTbl[] = {
    cmp_uns<rwir::Op::None>, cmp_uns<rwir::Op::Copy>, cmp_uns<rwir::Op::Bool>,
    cmp_uns<rwir::Op::Br>, cmp_uns<rwir::Op::Call>, cmp_uns<rwir::Op::Goto>,
    cmp_uns<rwir::Op::Return>, cmp_uns<rwir::Op::Cast>,
    cmp_uns<rwir::Op::Abs>, cmp_uns<rwir::Op::Add>, cmp_uns<rwir::Op::Sub>,
    cmp_uns<rwir::Op::Mul>, cmp_uns<rwir::Op::Div>, cmp_uns<rwir::Op::Mod>,
    cmp_uns<rwir::Op::Max>, cmp_uns<rwir::Op::Min>, cmp_uns<rwir::Op::Neg>,
    cmp_uns<rwir::Op::Sign>, cmp_uns<rwir::Op::BitAnd>, cmp_uns<rwir::Op::BitOr>,
    cmp_uns<rwir::Op::BitXor>, cmp_uns<rwir::Op::Shl>, cmp_uns<rwir::Op::Shr>,
    cmp_uns<rwir::Op::Eq>, cmp_uns<rwir::Op::Neq>, cmp_uns<rwir::Op::Lt>,
    cmp_uns<rwir::Op::Le>, cmp_uns<rwir::Op::Gt>, cmp_uns<rwir::Op::Ge>,
    cmp_uns<rwir::Op::Sqrt>, cmp_uns<rwir::Op::Exp>, cmp_uns<rwir::Op::Log>,
    cmp_uns<rwir::Op::Pow>, cmp_uns<rwir::Op::And>, cmp_uns<rwir::Op::Or>,
    cmp_uns<rwir::Op::Not>,
};
static constexpr CmpFn kCmpFloatTbl[] = {
    cmp_f<rwir::Op::None>, cmp_f<rwir::Op::Copy>, cmp_f<rwir::Op::Bool>,
    cmp_f<rwir::Op::Br>, cmp_f<rwir::Op::Call>, cmp_f<rwir::Op::Goto>,
    cmp_f<rwir::Op::Return>, cmp_f<rwir::Op::Cast>,
    cmp_f<rwir::Op::Abs>, cmp_f<rwir::Op::Add>, cmp_f<rwir::Op::Sub>,
    cmp_f<rwir::Op::Mul>, cmp_f<rwir::Op::Div>, cmp_f<rwir::Op::Mod>,
    cmp_f<rwir::Op::Max>, cmp_f<rwir::Op::Min>, cmp_f<rwir::Op::Neg>,
    cmp_f<rwir::Op::Sign>, cmp_f<rwir::Op::BitAnd>, cmp_f<rwir::Op::BitOr>,
    cmp_f<rwir::Op::BitXor>, cmp_f<rwir::Op::Shl>, cmp_f<rwir::Op::Shr>,
    cmp_f<rwir::Op::Eq>, cmp_f<rwir::Op::Neq>, cmp_f<rwir::Op::Lt>,
    cmp_f<rwir::Op::Le>, cmp_f<rwir::Op::Gt>, cmp_f<rwir::Op::Ge>,
    cmp_f<rwir::Op::Sqrt>, cmp_f<rwir::Op::Exp>, cmp_f<rwir::Op::Log>,
    cmp_f<rwir::Op::Pow>, cmp_f<rwir::Op::And>, cmp_f<rwir::Op::Or>,
    cmp_f<rwir::Op::Not>,
};

// 通用数值计算入口：查表直呼（不再 switch）。
bool Vmkv::compute_int(rwir::Op op, Scalar::K k, const Scalar& a, const Scalar& b, Scalar* r) {
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

bool Vmkv::compute_float(rwir::Op op, double a, double b, double* r) {
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

bool Vmkv::compare(rwir::Op op, Scalar::K k, const Scalar& a, const Scalar& b, bool* out) {
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

bool Vmkv::is_cmp(rwir::Op op) {
    return op == rwir::Op::Eq || op == rwir::Op::Neq || op == rwir::Op::Lt ||
           op == rwir::Op::Le || op == rwir::Op::Gt || op == rwir::Op::Ge;
}

bool raw_i64(Wire w, std::int64_t* out) {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0 || !xh.body || xh.content_len < 8)
        return false;
    std::memcpy(out, xh.body, 8);
    return true;
}

bool Vmkv::write_named_i64(void* h, std::uint8_t idx, FrameCtx& f, const char* kind,
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

// ── 执行（原 vm.cpp run_frame 方法化）：明文栈 ↔ 二进制栈的执行边界 ──────
// 帧入口：逐 reg 热字段重置 + 字面量快值缓存 + Ptr 参数内存别名绑定 + 描述符解析。
// 每 call 唯一的 kvspace 冷路径，与指令派发正交。
bool Vmkv::enter_frame(FrameCtx& f, const Segment* seg, std::string* err) {
    // 逐 reg 热字段重置（不清 string capacity——池复用的 key/desc 保留缓冲，
    // fib 每帧省 24 次 string 构造/析构；语义同 assign 全清零）。
    f.regs.resize(seg->regs.size());
    for (size_t i = 0; i < seg->regs.size(); i++) {
        FReg& rr = f.regs[i];
        rr.ref = kvspaceRef_t{};
        rr.resolved = false;
        rr.noslot = false;
        rr.view = kvspaceXvalueBodyView_t{};
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
        rr.base_ptr = false;
        rr.base_ptr_known = false;
        rr.key.clear();
        rr.desc.clear();
        rr.desc_ref = nullptr;
        const RegDesc& d = seg->regs[i];
            if (d.kind == Operand::Kind::Literal) {
                // 绝对路径字面量（operand_desc 预解析，name=路径本体）：key 直接是路径，
                // 读=路径读、写=路径写（对齐 C is_absolute_key）。
                f.regs[i].key =
                    !d.name.empty() && d.name[0] == '/' ? d.name : d.slot_key;
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
                using VC = RegDesc::VarClass;
                if (d.vclass == VC::ParamPtr) {
                    // 参数 Ptr（`*[0,±k]`）：call 时已绑定。内存别名子帧命中
                    // arg_src/arg_dst 表 → 挂跨帧指针（读参 src / 写参 dst），
                    // 零 kvspace；未命中（容器/扩展参数）回退 kvspace 原路径。
                    // 槽号 build 已预解析（d.param_k），帧入口零 substr/atoi
                    // （fib 递归每帧 12+ reg，字符串解析是 setup 大头）。
                    f.regs[i].desc_ref = &d;  // 成员写 base 匹配（`p·x` 的 p→`*[0,1]`）
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
                        // 写参别名：key 直接继承调用方实例槽（成员写 `p·x = v` 的
                        // base p 经 f.regs 遍历能取到最终 key）。读参别名 key 留空
                        // （值转发走 src_ptr，读穿透到目标）。
                        if (k > 0 && ap && !ap->key.empty())
                            f.regs[i].key = ap->key;
                        else
                            f.regs[i].key.clear();
                        if (g_trace)
                            std::fprintf(stderr, "[argmem d=%s k=%d %s]\n",
                                         d.name.c_str(), k,
                                         k < 0 ? "src" : "dst");
                    } else if (!resolve_desc(f, d.name, &f.regs[i].key, err)) {
                        if (g_trace)
                            std::fprintf(stderr, "[argkv d=%s key=%s]\n",
                                         d.name.c_str(), f.regs[i].key.c_str());
                        return false;
                    }
                } else if (d.vclass == VC::Ptr) {
                    // 普通 Ptr 描述符（`*p`）：target 由运行时写，惰性解析。
                    f.regs[i].key.clear();
                    f.regs[i].desc = d.name;
                } else {
                    // 普通变量：无类型声明（纯名 a/b/x/y）key 惰性（fast 路径零构造，
                    // ensure_reg_key 才拼 frame/name）；带类型声明（容器/类型化，如
                    // `A [N,N]float64`）成员访问需 key，立即 resolve。
                    if (d.vclass == VC::Plain) {
                        f.regs[i].key.clear();
                        f.regs[i].desc.clear();
                        f.regs[i].desc_ref = &d;
                    } else if (!resolve_desc(f, d.name, &f.regs[i].key, err)) {
                        return false;
                    }
                }
            }
    }
    if (g_trace)
        for (std::size_t ri = 0; ri < f.regs.size() && ri < 12; ri++)
            std::fprintf(stderr, "[reginit %zu kind=%d name='%s' key='%s']\n",
                         ri, (int)seg->regs[ri].kind,
                         seg->regs[ri].name.c_str(), f.regs[ri].key.c_str());
    return true;
}

RunResult Vmkv::run_frame(FrameCtx& f, int start_s0,
                         std::string* stop_pc,
                         std::string* err) {
    bin.prof_init();
    if (stop_pc)
        stop_pc->clear();
    const Segment* seg =
        f.seg_hint ? f.seg_hint : cached_segment(f.h, f.func_dir);
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
    if (!enter_frame(f, seg, err))
        return RunResult::Error;
    // JIT 钩子（M1）：命中即原生执行返回；否则走 fast kernel。
    {
        bin::RunResult jr = bin::RunResult::Ok;
        if ((!f.jit_known || f.jit_hint) &&
            jit_try_run(f, seg, start_s0, &jr, stop_pc, err))
            return jr;
    }
    const int n = static_cast<int>(seg->insns.size());
    int idx = start_s0 > 0 ? start_s0 - 1 : 0;
    if (idx < 0 || idx >= n) {
        if (idx == n)
            return RunResult::Ok;
        *err = "start s0 out of segment";
        return RunResult::Error;
    }
    if (bin.prof_on)
        bin.prof.frames++;
    // 四基址常驻：指令段/FReg 表基址提局部，热路径免 vector 链访问。
    const CInsn* ci = seg->insns.data();
    FReg* regs = f.regs.data();
    // runaway 防护：局部计数，函数末尾合并进全局（热路径无全局 inc）。
    unsigned long long run_steps = 0;
    for (; idx < n; idx++) {
        const unsigned long long t0 = bin.prof_on ? bin.now() : 0;
        const CInsn& ins = ci[idx];
        // runaway 防护：每 16M 步抽检一次（热路径无每步大数比较）。
        if (((run_steps + 1) & 63u) == 0)
            view_cache_tick();  // instruction boundary: no borrowed wires held
        if ((++run_steps & 0xFFFFFFULL) == 0 && run_steps > 50000000ULL) {
            *err = "runaway loop: " + f.func_dir + " s0=" + std::to_string(ins.s0) +
                   " op=" + ins.name;
            return RunResult::Error;
        }
        if (bin.trace_on && run_steps < bin.trace_n)
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
            } else if (ins.op == rwir::Op::Goto || ins.op == rwir::Op::Br) {
                FReg& tr = regs[ins.rd[ins.op == rwir::Op::Br ? 1 : 0]];
                FReg& tra = tr.src_ptr ? *tr.src_ptr : tr;
                fk_enter = tra.fast_i64;
                if (ins.op == rwir::Op::Br) {
                    // cond 也要 fast（bool/int64）：否则 fk 内 cond exit → 主循环 br
                    // 又进 fk → 死循环（is_prime bool 落明文无 fast_bool）。
                    FReg& cr = regs[ins.rd[0]];
                    FReg& cra = cr.src_ptr ? *cr.src_ptr : cr;
                    fk_enter = fk_enter && (cra.fast_bool || cra.fast_i64);
                }
            } else if (ins.op == rwir::Op::Copy && ins.wr_local && ins.nrd == 1) {
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
                if ((++fk_n & 4095u) == 0)
                    view_cache_tick();
                if ((fk_n & 0xFFFFFFULL) == 0 && fk_n > 50000000ULL) {
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
                                if (stop_pc)
                                    *stop_pc = param_key(f.frame, ik.s0, 0);
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
                                (void)store_i64_slot(f, w, rv);
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
                if (ik.op == rwir::Op::Goto || ik.op == rwir::Op::Br) {
                    FReg& tr = regs[ik.rd[ik.op == rwir::Op::Br ? 1 : 0]];
                    FReg& tra = tr.src_ptr ? *tr.src_ptr : tr;
                    if (!tra.fast_i64) {
                        idx--;
                        goto fk_exit;
                    }
                    std::int64_t targ = tra.fv;
                    if (ik.op == rwir::Op::Br) {
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
                if (ik.op == rwir::Op::Copy && ik.wr_local && ik.nrd == 1) {
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
                            (void)store_i64_slot(f, w, rf.fv);
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
                if (bin.prof_on) {
                    bin.prof.handler += bin.now() - t0;
                    bin.prof.steps += fk_n;
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
                        if (stop_pc)
                            *stop_pc = param_key(f.frame, ins.s0, 0);  // 出错指令行
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
                        (void)store_i64_slot(f, w, rv);  // body 直写：明文槽同步（shm 即 mmap）
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
                if (bin.prof_on) {
                    const std::uint64_t dh = bin.now() - t0;
                    bin.prof.handler += dh;
                    bin.prof.steps++;
                    bin.prof_tick(ins.prof_id, dh, 0);
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
                        ok2 = store_i64_slot(f, w, rv);
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
                if (bin.prof_on) {
                    const std::uint64_t dh = bin.now() - t0;
                    bin.prof.handler += dh;
                    bin.prof.steps++;
                    bin.prof_tick(ins.prof_id, dh, 0);
                }
                continue;
            }
            // cold 读参失败：回退到通用路径（下述）。
        }
        if (ins.op == rwir::Op::Goto || ins.op == rwir::Op::Br) {
            std::int64_t target_v = 0;
            FReg &tr = regs[ins.rd[ins.op == rwir::Op::Br ? 1 : 0]];
            if (!(tr.fast_i64 ? (target_v = tr.fv, true)
                              : read_i64(f, ins.rd[ins.op == rwir::Op::Br ? 1 : 0],
                                         &target_v))) {
                *err = "control: bad target";
                return RunResult::Error;
            }
            int dest = static_cast<int>(target_v);
            if (ins.op == rwir::Op::Br) {
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
            if (bin.prof_on) {
                const std::uint64_t d = bin.now() - t0;
                bin.prof.ctrl += d;
                bin.prof.steps++;
                bin.prof_tick(ins.prof_id, 0, d);
            }
            continue;
        }
        if (ins.impl == rwir::Impl::Handoff || ins.impl == rwir::Impl::None) {
            if (bin.trace_on)
                std::fprintf(stderr, "[stop %s d%d s0=%d %s]\n",
                             f.func_dir.c_str(), f.depth, ins.s0, ins.name.c_str());
            // Stop 边界（handoff/外部引擎接管）：冲刷帧局部纯内存变量落明文，
            // resume 后的新 FrameCtx 无 fast 缓存，须从 kvspace 重建。
            flush_frame_locals(f);
            if (stop_pc)
                *stop_pc = param_key(f.frame, ins.s0, 0);
            return RunResult::Stop;
        }
        if (ins.op == rwir::Op::Return) {
            if (bin.prof_on) {
                bin.prof.loop += bin.now() - t0;
                bin.prof.steps++;
            }
            if (bin.prof_on)
                bin.prof_tick(ins.prof_id, 0, 0);
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
        if (ins.impl == rwir::Impl::Native) {
            // Native 分派：fold `·`→`/` 后查两级表（vmkv_native.cpp），一 op 一 handler。
            // 表未命中 / handler 失败 → error；写失败统一由调用方补 "write failed"。
            if (!ins.native_ready) {
                std::string& fo = ins.fn_fold;
                fo.clear();
                for (std::size_t i = 0; i < ins.name.size();) {
                    if (i + 2 < ins.name.size() &&
                        static_cast<unsigned char>(ins.name[i]) == 0xC2 &&
                        static_cast<unsigned char>(ins.name[i + 1]) == 0xB7) {
                        fo += '/';
                        i += 2;
                    } else {
                        fo += ins.name[i++];
                    }
                }
                ins.native = find_native(fo);
                // These handlers read their operands themselves.
                ins.native_noprefetch =
                    fo == "compactarray/at" || fo == "compactarray/set";
                ins.native_ready = true;
            }
            NativeCtx ctx{g_vmkv, f, seg, ins, {}, 0, 0, ins.fn_fold};
            for (int i = 0; !ins.native_noprefetch && i < ins.nrd && i < 2; i++) {
                if (!read_reg(f, ins.rd[i], &ctx.in[i])) {
                    *err = "native: missing operand " + std::to_string(i) + ": " +
                           ins.name;
                    return RunResult::Error;
                }
            }
            const NativeFn* h = static_cast<const NativeFn*>(ins.native);
            if (!h) {
                *err = "native op not implemented: " + ins.name;
                return RunResult::Error;
            }
            if (!(*h)(ctx, err)) {
                if (err->empty())
                    *err = "native: write failed: " + ins.name;
                return RunResult::Error;
            }
            // vthread·run 冒泡：子 vid 撞外部 rwir → 父以 Stop 让出（stop_pc=子 pc），
            // execute_vthread_impl 交驱动处理子 rwir 后重入续驱。父 pc 停在 vthread·run
            // 指令（handler 未推进），重入后继续驱动子。
            if (!ctx.yield_pc.empty()) {
                if (bin.prof_on) {
                    const std::uint64_t d = bin.now() - t0;
                    bin.prof.handler += d;
                    bin.prof.steps++;
                    bin.prof_tick(ins.prof_id, d, 0);
                }
                flush_frame_locals(f);
                if (stop_pc)
                    *stop_pc = std::move(ctx.yield_pc);
                return RunResult::Stop;
            }
            if (bin.prof_on) {
                const std::uint64_t d = bin.now() - t0;
                bin.prof.handler += d;
                bin.prof.steps++;
                bin.prof_tick(ins.prof_id, d, 0);
            }
            continue;
        }
        if (ins.op == rwir::Op::Call) {
            const RunResult rr = do_call(f, seg, ins, stop_pc, err);
            if (rr != RunResult::Ok)
                return rr;
            continue;
        }
        // 值算子
        Wire w[4]{};
        Scalar s[4]{};
        bool non_scalar = false;
        bool none0 = false, none1 = false;
        if (ins.op != rwir::Op::Copy) {
            for (int i = 0; i < ins.nrd; i++) {
                if (read_scalar(f, ins.rd[i], &s[i])) {
                    continue;
                }
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
            // A char slot reads as a Scalar of its first char; `+`/`==`/`!=` on
            // two char operands are text operations, so re-read them as wires.
            const bool text_op = ins.nrd == 2 && (ins.op == rwir::Op::Add ||
                                                  ins.op == rwir::Op::Eq ||
                                                  ins.op == rwir::Op::Neq);
            auto charish = [&](int i) {
                return w[i].d ? !v_is_none(w[i]) : s[i].k == Scalar::K::Char;
            };
            if (text_op && (s[0].k == Scalar::K::Char || s[1].k == Scalar::K::Char) &&
                charish(0) && charish(1)) {
                for (int i = 0; i < 2; i++) {
                    if (!w[i].d && !read_reg(f, ins.rd[i], &w[i])) {
                        *err = "operand " + std::to_string(i) + " is missing";
                        return RunResult::Error;
                    }
                }
                non_scalar = true;
            }
        }
        bool ok = true;
        // None 参与比较：对齐 C 语义（Eq：双 None=true；Neq：双 None=false；其余 → TypeError）。
        // Ptr vs None（Ptr 悬空=空指针）交给 non_scalar 的 Ptr 分支，不在此判。
        const bool any_ptr_cmp = (v_ptr(w[0]) || v_ptr(w[1])) && is_cmp(ins.op);
        if (!any_ptr_cmp && (none0 || none1) && is_cmp(ins.op)) {
            if (ins.op == rwir::Op::Eq)
                ok = slot_write_bool(f, ins.wr[0], none0 && none1);
            else if (ins.op == rwir::Op::Neq)
                ok = slot_write_bool(f, ins.wr[0], !(none0 && none1));
            else {
                *err = "TypeError: None in comparison: " + ins.name;
                return RunResult::Error;
            }
            if (bin.prof_on) {
                const std::uint64_t dh = bin.now() - t0;
                bin.prof.handler += dh;
                bin.prof.steps++;
                bin.prof_tick(ins.prof_id, dh, 0);
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
            if (ins.op == rwir::Op::Cast && ins.nrd == 1 &&
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
                if (bin.prof_on) {
                    const std::uint64_t dh = bin.now() - t0;
                    bin.prof.handler += dh;
                    bin.prof.steps++;
                    bin.prof_tick(ins.prof_id, dh, 0);
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
                if (ins.nrd == 2 && (ins.op == rwir::Op::Eq || ins.op == rwir::Op::Neq)) {
                    // Ptr 参与比较：指针本体 vs None（空指针检查）或 Ptr 间按 target 比
                    // （链表 `cur != None` / `h·next == &n2`）。读参 w[i] 可能已被
                    // resolve 穿透成目标值——按 Ptr target 判空与相等。
                    const bool p0 = v_ptr(w[0]), p1 = v_ptr(w[1]);
                    if (p0 || p1) {
                        // Ptr 为空 = target 槽缺失/None（`walk(None)` 的 arg 槽
                        // Ptr→nil 槽是空指针，`node != None` 须 false）。
                        auto ptr_empty = [&](const Wire& pw) -> bool {
                            std::string t;
                            if (!v_ptr_target(pw, &t) || t.empty())
                                return true;
                            const std::uint8_t* td = nullptr;
                            std::uint32_t tn = 0;
                            return !read_wire(f.h, t, &td, &tn) ||
                                   v_is_none(Wire{td, tn});
                        };
                        const bool e0 = ptr_empty(w[0]);
                        const bool e1 = ptr_empty(w[1]);
                        std::string pt0, pt1;
                        const bool h0 = v_ptr_target(w[0], &pt0);
                        const bool h1 = v_ptr_target(w[1], &pt1);
                        const bool eq = p0 && p1
                                            ? (e0 && e1) || (!e0 && !e1 && pt0 == pt1)
                                            : p0 ? e0 : e1;
                        ok = slot_write_bool(f, ins.wr[0],
                                             ins.op == rwir::Op::Eq ? eq : !eq);
                        if (bin.prof_on) {
                            const std::uint64_t d2 = bin.now() - t0;
                            bin.prof.handler += d2;
                            bin.prof.steps++;
                            bin.prof_tick(ins.prof_id, d2, 0);
                        }
                        continue;
                    }
                    if (wire_text(w[0].d, w[0].n, &a) &&
                        wire_text(w[1].d, w[1].n, &b)) {
                        const bool eq = a == b;
                        ok = slot_write_bool(f, ins.wr[0],
                                             ins.op == rwir::Op::Eq ? eq : !eq);
                        if (bin.prof_on) {
                            const std::uint64_t dh = bin.now() - t0;
                            bin.prof.handler += dh;
                            bin.prof.steps++;
                            bin.prof_tick(ins.prof_id, dh, 0);
                        }
                        continue;
                    }
                }
                // 动态算子按运行时类型分派：`+` 对 char 族即字符串拼接。
                // kvspaceNewChar 按码点计数产 `[n]char/utf8` head（#552 性能+正确）。
                {
                    if (ins.op != rwir::Op::Add || ins.nrd != 2 ||
                        !wire_text(w[0].d, w[0].n, &a) ||
                        !wire_text(w[1].d, w[1].n, &b)) {
                        *err = "non-scalar operand: " + ins.name;
                        return RunResult::Error;
                    }
                    const std::string out_s = a + b;
                    std::uint8_t* buf = nullptr;
                    std::uint32_t len = 0;
                    // kvspaceNewChar counts code points for the `[n]char/utf8` head.
                    ok = kvspaceNewChar(
                             reinterpret_cast<const std::uint8_t*>(out_s.data()),
                             static_cast<std::uint32_t>(out_s.size()), &buf, &len) == 0 &&
                         buf && write_reg(f, ins.wr[0], buf, len);
                    std::free(buf);
                    if (bin.prof_on) {
                        const std::uint64_t dh = bin.now() - t0;
                        bin.prof.handler += dh;
                        bin.prof.steps++;
                        bin.prof_tick(ins.prof_id, dh, 0);
                    }
                    if (!ok) {
                        *err = "value op failed: " + ins.name;
                        return RunResult::Error;
                    }
                    continue;
                }
                // 动态算子按运行时类型分派：`+` 对 char 族即字符串拼接（Eq/Neq
                // 已在上方处理；此处是 else 层的通用路径——Add 及其它）。
                if (ins.op != rwir::Op::Add || ins.nrd != 2 ||
                    !wire_text(w[0].d, w[0].n, &a) ||
                    !wire_text(w[1].d, w[1].n, &b)) {
                    *err = "non-scalar operand: " + ins.name;
                    return RunResult::Error;
                }
                const std::string out_s = a + b;
                const std::int32_t dims[1] = {static_cast<std::int32_t>(out_s.size())};
                std::uint8_t* buf = nullptr;
                std::uint32_t len = 0;
                const int enc_rc =
                    kvspaceXvalueEncode(
                        "char/utf8",
                        reinterpret_cast<const std::uint8_t*>(out_s.data()),
                        static_cast<std::uint32_t>(out_s.size()), dims, 1,
                        &buf, &len);
                ok = enc_rc == 0 && buf && write_reg(f, ins.wr[0], buf, len);
                std::free(buf);
                if (bin.prof_on) {
                    const std::uint64_t dh = bin.now() - t0;
                    bin.prof.handler += dh;
                    bin.prof.steps++;
                    bin.prof_tick(ins.prof_id, dh, 0);
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
        if (ins.op == rwir::Op::Copy) {
            // 单参普通赋值（`a -> b`，非成员读写）：src fast 缓存 → dst 直传，
            // 免 read_reg 的 kvspaceXvalueEncode + write_reg 的整套 wire 路径。
            // dst 为写参别名（dst_ptr）转发调用方 reg；非别名且非帧局部才落明文。
            {
                const RegDesc& wdesc0 = seg->regs[ins.wr[0]];
                const bool mw =
                    wdesc0.name.find("\xC2\xB7") != std::string::npos &&
                    wdesc0.name[0] != '/';
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
                                (void)store_i64_slot(f, target, rf.fv);
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
                        if (bin.prof_on) {
                            const std::uint64_t dh = bin.now() - t0;
                            bin.prof.handler += dh;
                            bin.prof.steps++;
                            bin.prof_tick(ins.prof_id, dh, 0);
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
            // 写槽描述符含 `·*` = 成员写。目标 key = 容器槽 + "·" + 键值。
            // `/` 开头的绝对路径（`42 -> "/tmp/kvt·data"` 路径写）不是成员写：
            // C 语义直接原样作 key（ResolveWriteSlot name[0]=='/' 分支）。
            const RegDesc& wdesc = seg->regs[ins.wr[0]];
            const bool member_write =
                wdesc.name.find("\xC2\xB7") != std::string::npos &&
                wdesc.name[0] != '/';
            if (ins.nrd == 2 && !member_write) {
                // 成员读：base（读参0 的值或 Ptr 目标 key）+ key（读参1 的值）。
                Wire bw{};
                std::string base_key;
                FReg& br0 = f.regs[ins.rd[0]];
                FReg& br0f = br0.src_ptr ? *br0.src_ptr : br0;
                if (read_reg(f, ins.rd[0], &bw)) {
                    std::string bt;
                    if (v_ptr_target(bw, &bt)) {
                        base_key = bt;
                    } else if (wire_text(bw.d, bw.n, &bt) && !bt.empty() &&
                               bt[0] == '/') {
                        // 字面量路径 base（`char/utf32:/tmp/up`）→ 直接取文本。
                        base_key = bt;
                    } else {
                        // 帧局部变量（惰性 key 未解析）：ensure 后取实例槽路径
                        // （`n3·val` 在 n3 只经 struct·new 写、无明文值时 key 仍空）。
                        if (ensure_reg_key(f, br0) && br0.key.size() > 5)
                            base_key = br0.key;
                        // Ptr 参数（key=arg 槽）：read_reg 已穿透成实例，raw 读回
                        // Ptr 本体取 target（`p·x` 的 p 是 *点 读参）。
                        if (br0f.key.size() > 5) {
                            const std::uint8_t* rd2 = nullptr;
                            std::uint32_t rn2 = 0;
                            std::string t2;
                            if (read_wire_raw(f.h, br0f.key, &rd2, &rn2) &&
                                v_ptr_target(Wire{rd2, rn2}, &t2) &&
                                !t2.empty())
                                base_key = t2;
                        }
                    }
                }
                Wire kw{};
                std::string kstr;
                if (!base_key.empty() && read_reg(f, ins.rd[1], &kw) &&
                    key_str(kw, &kstr) && !kstr.empty()) {
                    // 容器成员 key = base·name（· 中点键，对齐 C kvlangKeytreeMember）。
                    // 值读用 raw（resolve=0）：成员是 Ptr 时须取 Ptr 本体（链表
                    // `cur·next -> cur` 拿指针），穿透会得到目标值。
                    const std::string mkey = base_key + "\xC2\xB7" + kstr;
                    const std::uint8_t* md = nullptr;
                    std::uint32_t mn = 0;
                    if (read_wire_raw(f.h, mkey, &md, &mn) && md && mn) {
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
                        if (ins.mw_keyreg == -2)
                            ins.mw_keyreg = reg_by_name(seg, keyref);
                        if (ins.mw_keyreg >= 0)
                            kridx = static_cast<std::uint8_t>(ins.mw_keyreg);
                        for (std::size_t kr = 0; kridx == 255 && kr < f.regs.size(); kr++) {
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
                // 参数 Ptr 别名（`*[0,±k]` / `*p`）：取调用方 reg 链的最终 key
                // （`p·x = v` 的 p 是 src_ptr 别名 → 写调用方实例成员）。
                std::string base_key;
                if (!base_part.empty() && base_part[0] == '/') {
                    base_key = base_part;
                } else {
                    const std::string fbp = f.frame + "/" + base_part;
                    // 先精确匹配 base 名（`cur·next` 的 cur 局部变量），失败才回退
                    // 写参 Ptr（`*[0,±k]`：`r·x` 的 r 是写参）——顺序不可反。
                    // 无字符串拷贝：desc_ref 指针/name 直接比较，key 前缀匹配。
                    FReg* base_reg = nullptr;
                    if (ins.mw_basereg == -2)
                        ins.mw_basereg = reg_by_name(seg, base_part);
                    if (ins.mw_basereg >= 0)
                        base_reg = &f.regs[static_cast<std::size_t>(ins.mw_basereg)];
                    for (std::size_t ri = 0; !base_reg && ri < f.regs.size(); ri++) {
                        FReg& r = f.regs[ri];
                        if ((r.desc_ref && r.desc_ref->name == base_part) ||
                            r.key == fbp ||
                            (!r.desc.empty() && r.desc == base_part)) {
                            base_reg = &r;
                            break;
                        }
                    }
                    if (base_reg) {
                        FReg* rk = base_reg;
                        while (rk->src_ptr || rk->dst_ptr)
                            rk = rk->src_ptr ? rk->src_ptr : rk->dst_ptr;
                        // enter_frame 每 execute 重入重置 regs（惰性 key 清空）：
                        // struct·new 已建的实例槽 key 需重新解析（02-mutate `5 -> p·x`）。
                        // 仅 base_reg 自身（帧局部，desc_ref 用本帧拼 key）；链尾是
                        // 调用方 reg（跨帧）时不动——#552 的跨帧安全 + 错误检查。
                        if (rk->key.empty() && rk == base_reg &&
                            !ensure_reg_key(f, *rk))
                            return RunResult::Error;
                        if (!rk->key.empty()) {
                            // base 槽是 Ptr（`cur·next` 的 cur 存指针）：成员写落
                            // target。首查后缓存（热路径动态键容器免每写 kvspace）。
                            if (!rk->base_ptr_known) {
                                const std::uint8_t* bd = nullptr;
                                std::uint32_t bn = 0;
                                std::string bt;
                                rk->base_ptr =
                                    read_wire_raw(f.h, rk->key, &bd, &bn) &&
                                    v_ptr_target(Wire{bd, bn}, &bt) && !bt.empty();
                                rk->base_ptr_known = true;
                            }
                            if (rk->base_ptr) {
                                const std::uint8_t* bd = nullptr;
                                std::uint32_t bn = 0;
                                std::string bt;
                                if (read_wire_raw(f.h, rk->key, &bd, &bn) &&
                                    v_ptr_target(Wire{bd, bn}, &bt))
                                    base_key = bt;
                                else
                                    base_key = rk->key;
                            } else {
                                base_key = rk->key;
                            }
                        }
                    } else {
                        base_key = fbp;
                    }
                    // Write-param fallbacks apply only when no local reg names the base.
                    if (!base_reg && base_key == fbp) {
                        for (FReg& r : f.regs) {
                            const std::string_view rn =
                                !r.desc.empty() ? std::string_view(r.desc)
                                : r.desc_ref    ? std::string_view(r.desc_ref->name)
                                                : std::string_view();
                            if (rn.substr(0, 4) == "*[0," && !r.key.empty()) {
                                FReg* rk = &r;
                                while (rk->src_ptr || rk->dst_ptr)
                                    rk = rk->src_ptr ? rk->src_ptr : rk->dst_ptr;
                                if (!rk->key.empty()) {
                                    base_key = rk->key;
                                    break;
                                }
                            }
                        }
                    }
                    // 写参 base 无独立 reg（`造点(a,b) -> (r:*点)` 的 r 只出现在
                    // `r·x` 写槽，layout 不生成 `*[0,1]` reg）：读本帧写参 arg 槽
                    // （do_call 绑定 `‥arg[0,k]` 存 Ptr→调用方槽）取 target。
                    if (!base_reg && base_key == fbp) {
                        for (int k2 = 1; k2 <= 4; k2++) {
                            const std::string ak = f.frame + "/\xE2\x80\xA5" "arg[0," +
                                                   std::to_string(k2) + "]";
                            const std::uint8_t* ad2 = nullptr;
                            std::uint32_t an2 = 0;
                            std::string at;
                            if (read_wire_raw(f.h, ak, &ad2, &an2) &&
                                v_ptr_target(Wire{ad2, an2}, &at) && !at.empty()) {
                                base_key = at;
                                break;
                            }
                        }
                    }
                }
                if (base_key.empty()) {
                    *err = "member write: no base: " + ins.name;
                    return RunResult::Error;
                }
                const std::string mkey = base_key + "\xC2\xB7" + kstr;
                char e[192] = {};
                Wire kw{};
                if (!read_reg(f, ins.rd[0], &kw)) {
                    *err = "member write: no value: " + ins.name;
                    return RunResult::Error;
                }
                ok = kvspaceSetValue(f.h, mkey.c_str(), kw.d, kw.n, 0, 0, e,
                                     sizeof e) == 0;
            } else {
                // 普通单参赋值（`a -> b`）：复制**槽本体**（Ptr 不穿透——链表
                // `head -> cur` 复制指针，穿透会拿到目标实例）。普通值 raw 同值。
                Wire src{};
                FReg& sr0 = regs[ins.rd[0]];
                FReg& srf = sr0.src_ptr ? *sr0.src_ptr : sr0;
                const std::uint8_t* rawd = nullptr;
                std::uint32_t rawn = 0;
                // A write param resolves to the caller's instance; copying it must
                // alias that instance (`p -> c; v -> c·x` writes the caller), so a
                // non-scalar target copied into a plain local becomes a Ptr to it.
                const RegDesc& sd0 = seg->regs[ins.rd[0]];
                const RegDesc& dd0 = seg->regs[ins.wr[0]];
                bool aliased = false;
                if (sd0.vclass == RegDesc::VarClass::ParamPtr && sd0.param_k > 0 &&
                    dd0.vclass == RegDesc::VarClass::Plain && !dd0.member) {
                    FReg* tk = &sr0;
                    while (tk->dst_ptr)
                        tk = tk->dst_ptr;
                    std::string lt;
                    if (!tk->key.empty() &&
                        read_wire_raw(f.h, tk->key, &rawd, &rawn) && rawd &&
                        rawn > KVSPACE_XH_PREFIX && !v_ptr(Wire{rawd, rawn}) &&
                        v_langtype(Wire{rawd, rawn}, &lt)) {
                        const auto t = bin.langtype_of(lt.data(), lt.size());
                        const bool scalar = t >= bin::LangType::Int8 &&
                                            t <= bin::LangType::CharAscii;
                        std::uint8_t* pb = nullptr;
                        std::uint32_t pl = 0;
                        if (!scalar && kvspaceNewPtr(lt.c_str(), tk->key.c_str(), &pb,
                                                     &pl) == 0 && pb) {
                            ok = write_reg(f, ins.wr[0], pb, pl);
                            aliased = true;
                        }
                        std::free(pb);
                    }
                    rawd = nullptr;
                    rawn = 0;
                }
                if (aliased) {
                } else if (!srf.key.empty() &&
                    read_wire_raw(f.h, srf.key, &rawd, &rawn) && rawd && rawn) {
                    src.d = rawd;
                    src.n = rawn;
                    ok = write_reg(f, ins.wr[0], src.d, src.n);
                } else if (read_reg(f, ins.rd[0], &src)) {
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
        } else if (ins.op == rwir::Op::Bool) {
            Scalar r{};
            r.k = Scalar::K::Bool;
            r.width = 1;
            r.u = truthy(s[0]) ? 1 : 0;
            ok = slot_write_scalar(f, ins.wr[0], r, 1);
            if (ok)
                sync_fast_after_write(f, ins.wr[0], r);
        } else if (ins.op == rwir::Op::Cast) {
            Scalar r{};
            r.k = ins.kind == rwir::Kind::Int     ? Scalar::K::Int
                  : ins.kind == rwir::Kind::Uint  ? Scalar::K::Uint
                  : ins.kind == rwir::Kind::Float ? Scalar::K::Float
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
        } else if (ins.op == rwir::Op::And || ins.op == rwir::Op::Or ||
                   ins.op == rwir::Op::Not) {
            bool a = truthy(s[0]);
            bool b = ins.nrd > 1 ? truthy(s[1]) : false;
            bool r = ins.op == rwir::Op::And ? (a && b)
                     : ins.op == rwir::Op::Or ? (a || b)
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
            const rwir::Op op =
                ins.nrd == 1 && ins.op == rwir::Op::Sub ? rwir::Op::Neg : ins.op;
            Scalar::K k = s[0].k;
            const Scalar::K k1 = ins.nrd > 1 ? s[1].k : k;
            Scalar out = s[0];
            const bool to_float = op == rwir::Op::Pow || op == rwir::Op::Sqrt ||
                                  op == rwir::Op::Exp || op == rwir::Op::Log ||
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
        if (bin.prof_on) {
            const std::uint64_t dh = bin.now() - t0;
            bin.prof.handler += dh;
            bin.prof.steps++;
            bin.prof_tick(ins.prof_id, dh, 0);
        }
        if (!ok) {
            *err = "value op failed: " + ins.name;
            return RunResult::Error;
        }
    }
    return RunResult::Ok;
}

// Call 块（原 run_frame 内嵌）：callee 解析、mem_frame 判定、帧初始化、
// arg_src/arg_dst 内存别名表、递归 run_frame。Call 是天然 checkpoint 边界。
bin::RunResult Vmkv::do_call(FrameCtx& f, const Segment* seg, const CInsn& ins,
                            std::string* stop_pc, std::string* err) {
    // callee 已在 build_segment 预解析为最终 /lib/ 路径（裸名 pkg 探测一次，
    // 运行期零 read_text/kvspaceGet）。fib 递归每帧省 2 次 kvspace 往返。
    const std::string& callee = ins.callee;
    if (callee.empty()) {
        *err = "call: empty callee";
        return RunResult::Error;
    }
    const std::string& nf = child_frame_root(f.vid, f.depth + 1);
    char e[192] = {};
    // 纯 runtime rwfunc 调用（callee 有代码段可解释）：帧局部 int64/bool
    // 参数走内存别名，免建 kvspace arg 槽/写参槽（fib 递归热点）。
    // 扩展/native/容器/float 参数照旧 kvspace 传值/Ptr。
    const Segment* cseg =
        ins.call_ready ? ins.call_seg : cached_segment(f.h, callee);
    // 纯内存递归帧：callee 段无 native/handoff → 无观察边界 → 帧系统变量
    // （returnpc/lib）与生命周期（DelTree/Mkindex/ext 索引）全部省略，
    // 每帧省 ~17k cycles kvspace（fib 递归最大头）。
    const bool mem_frame = cseg && !cseg->has_native;
    // JIT 快路径判定（M1）：callee 已编译 → 形状已知（int64 单读参/写参），
    // 参数绑定直接走 mem 路径，免签名/参数 wire 的 kvspace 读取。
    const void* jcode =
        ins.call_ready ? ins.call_jit
                       : (cseg && jit_enabled() ? jit_compile(callee, cseg) : nullptr);
    // 非纯内存帧：清残留 + 建目录 + 系统变量落明文（明文栈唯一事实）。
    if (!mem_frame) {
        const std::string nstack = nf + "/";
        kvspaceDelTree(f.h, nf.c_str(), e, sizeof e);
        // Mkindex 删除：shm ART SetValue 隐式建目录（实测无回归）。
        if (kvspaceMkindex(f.h, nstack.c_str(), 0, e, sizeof e) != 0) {
            *err = std::string("call: frame init failed: ") + e;
            return RunResult::Error;
        }
        const std::string callee_dir = callee + "/";
        kvspaceMkindexExt(f.h, nstack.c_str(), callee_dir.c_str(), e, sizeof e);
        const std::string retpc = param_key(f.frame, ins.s0 + 1, 0);
        if (!write_text(f.h, nf + "\xE2\x80\xA5" "returnpc", retpc) ||
            !write_text(f.h, nstack + "\xE2\x80\xA5" "lib", callee)) {
            *err = "call: system vars failed";
            return RunResult::Error;
        }
    } else if (f.observable) {
        // 纯内存帧在**外部可观察链**（vthread·run 驱动的子链，printstack 检视）
        // 仍落最小可观察面：‥lib 标识函数 + extindex 使帧内 pc 可解析到函数指令
        // 树（05 printstack 的 frame[1] 缺 lib、cur 读 pc 键失败均由此）。
        // 顶层链（bootstrap/test）不可观察——mem_frame 保持零 kvspace（fib/iops
        // 热路径），省略 returnpc/生命周期（mem_frame return 走 Returned 直返）。
        const std::string nstack = nf + "/";
        const std::string callee_dir = callee + "/";
        if (!write_text(f.h, nf + "/\xE2\x80\xA5" "lib", callee)) {
            *err = "call: mem frame lib failed";
            return RunResult::Error;
        }
        kvspaceMkindexExt(f.h, nstack.c_str(), callee_dir.c_str(), e,
                          sizeof e);
    }
    if (g_trace) {
        std::fprintf(stderr, "[memcall callee=%s mem=%d]\n",
                     callee.c_str(), (int)(cseg != nullptr));
    }
    // 参数类型权威：callee 签名 `<func_key>.[0,±k]` 定义键 body 的 NUL 后段
    // （对齐 C param_decl_type）。Ptr langtype 必须用 callee 声明。
    auto param_decl_type = [&](int x, std::string* out) -> bool {
        out->clear();
        const std::string pk = callee + ".[0," + std::to_string(x) + "]";
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        if (!read_wire(f.h, pk, &d, &n))
            return false;
        // 槽值 = XValue（head 的 langtype 即 "def langtype"，body=名字\0类型串）。
        // 先解 head 取 body 文本，再按名字\0类型 拆。
        std::string txt;
        if (!wire_text(d, n, &txt))
            return false;
        const auto nul = txt.find('\0');
        if (nul == std::string::npos) {
            if (!txt.empty())
                *out = txt;  // 无名字：body 即类型
        } else if (nul + 1 < txt.size()) {
            *out = txt.substr(nul + 1);
        }
        return !out->empty();
    };
    // Call-site cache: callee segment, JIT entry and decl types never change
    // while the segment cache lives. Unresolved callees retry every call.
    if (!ins.call_ready && cseg) {
        ins.call_seg = cseg;
        ins.call_jit = jcode;
        ins.call_rtyp.assign(ins.nrd, std::string());
        ins.call_rtyp_ok.assign(ins.nrd, 0);
        for (int i = 1; i < ins.nrd; i++)
            ins.call_rtyp_ok[i] = param_decl_type(-i, &ins.call_rtyp[i]);
        ins.call_wtyp.assign(ins.nwr + 1, std::string());
        ins.call_wtyp_ok.assign(ins.nwr + 1, 0);
        for (int i = 1; i <= ins.nwr; i++)
            ins.call_wtyp_ok[i] = param_decl_type(i, &ins.call_wtyp[i]);
        ins.call_ready = true;
    }
    auto decl_type = [&](int x, std::string* out) -> bool {
        if (!ins.call_ready)
            return param_decl_type(x, out);
        const bool ok = x < 0 ? ins.call_rtyp_ok[-x] : ins.call_wtyp_ok[x];
        if (ok)
            *out = x < 0 ? ins.call_rtyp[-x] : ins.call_wtyp[x];
        return ok;
    };
    // 内存别名子帧：arg_src/arg_dst 从调用方 FReg 建立。定长栈数组（段 nr/nw ≤ 16）。
    FReg* mem_src[16] = {};
    FReg* mem_dst[16] = {};
    int mem_src_n = 0, mem_dst_n = 0;
    bool mk_done = !mem_frame;
    auto bind_arg = [&](int x, FReg* caller_reg, const std::string& value_key,
                        const std::string& typ, const Wire& src) -> bool {
        char be[192] = {};
        const bool container_arg = typ.find("\xC2\xB7") != std::string::npos;
        const bool scalar_arg =
            !container_arg && (typ == "int64" || typ == "bool");
        // 内存别名只对纯内存帧（callee 段无 native/handoff）：段含 Handoff
        // 时执行中途 Stop、帧会被 execute_vthread 重启（arg_src 丢失），
        // 标量参数必须建 kvspace 槽保证重启后可解析（prime_sieve println 坑）。
        // A read alias forwards the caller reg's cached value; a caller reg bound
        // through kvspace has none, so its value goes through an arg slot instead.
        if (mem_frame && scalar_arg && (x > 0 || holds_value(caller_reg))) {
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
        const std::string xs = std::to_string(x);
        const std::string arg_key = nf + "/\xE2\x80\xA5" "arg[0," + xs + "]";
        const std::string slot = nf + "/[0," + xs + "]";
        if (g_trace &&
            callee.find("bsearch") != std::string::npos)
            std::fprintf(stderr, "[bindkv x=%d typ=%s memcall=%d scalar=%d]\n",
                         x, typ.c_str(), (int)(cseg != nullptr),
                         (int)scalar_arg);
        if (!mk_done) {
            const std::string ns = nf + "/";
            if (kvspaceMkindex(f.h, ns.c_str(), 0, be, sizeof be) != 0) {
                *err = std::string("bind: frame dir failed: ") + be;
                return false;
            }
            mk_done = true;
        }
        if (x < 0 && !container_arg) {
            // Ptr 类型读参（`*T`）：arg 槽存 Ptr→调用方槽（`求和(a)` 的 a 是
            // 值实例；&n3 已 Ptr 则链 Ptr→target）。slot 恒 Ptr→arg。
            if (typ.size() >= 2 && typ[0] == '*') {
                const std::uint8_t* pd = src.d;
                std::uint32_t pn = src.n;
                std::string pt;
                const std::string pkind = typ.substr(1);
                std::uint8_t* ap = nullptr;
                std::uint32_t an = 0;
                // src 已是 Ptr（&n3 传 _3 变量）：arg 直接继承 Ptr→target。
                if (v_ptr_target(src, &pt) && !pt.empty()) {
                    bool bok = kvspaceNewPtr(pkind.c_str(), pt.c_str(), &ap,
                                             &an) == 0;
                    if (!bok) {
                        *err = "bind: NewPtr(arg chain) failed";
                        return false;
                    }
                } else if (pd && pn) {
                    // 值实例：arg 槽 = Ptr→调用方槽。
                    bool bok = value_key.empty()
                                   ? false
                                   : kvspaceNewPtr(pkind.c_str(),
                                                   value_key.c_str(), &ap,
                                                   &an) == 0;
                    if (!bok) {
                        *err = "bind: NewPtr(value) failed typ=" + typ;
                        return false;
                    }
                } else {
                    *err = "bind: read arg value failed: " + value_key;
                    return false;
                }
                bool bok2 = kvspaceSetValue(f.h, arg_key.c_str(), ap, an, 0, 0,
                                            be, sizeof be) == 0;
                std::free(ap);
                if (!bok2) {
                    *err = std::string("bind: set arg failed: ") + be;
                    return false;
                }
                std::uint8_t* sp = nullptr;
                std::uint32_t sn = 0;
                bool bok3 = kvspaceNewPtr(typ.c_str(), arg_key.c_str(), &sp,
                                          &sn) == 0;
                if (!bok3) {
                    *err = "bind: NewPtr(slot) failed";
                    return false;
                }
                bok3 = kvspaceSetValue(f.h, slot.c_str(), sp, sn, 0, 0, be,
                                       sizeof be) == 0;
                std::free(sp);
                if (!bok3)
                    *err = std::string("bind: set slot failed: ") + be;
                return bok3;
            }
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
            bool bok = kvspaceNewPtr(typ.c_str(), arg_key.c_str(), &sp, &sn) == 0;
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
        bool bok = kvspaceNewPtr(typ.c_str(), value_key.c_str(), &a, &an) == 0;
        if (!bok) {
            if (g_trace)
                std::fprintf(stderr,
                             "[bind NewPtr fail typ=%s value_key=%s]\n",
                             typ.c_str(), value_key.c_str());
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
    // The JIT binding aliases every read arg; it needs each one to hold a value.
    // 可观察链（vthread·run 子链，printstack 检视）不走 JIT 快路径——实参须落
    // _lit/arg 槽（05-printstack 的 `int64:3` 渲染依赖）。
    if (!f.observable)
        for (int i = 1; jcode && i < ins.nrd; i++)
            if (!holds_value(&f.regs[ins.rd[i]]))
                jcode = nullptr;
    if (!jcode || f.observable)
    for (int i = 1; i < ins.nrd; i++) {
        Wire vw{};
        std::string vk, typ;
        const RegDesc& rd = seg->regs[ins.rd[i]];
        // Scalar arg into a memory frame: the alias needs no wire or key.
        // 可观察链（vthread·run 子链，printstack 检视）不走——Literal 实参须落
        // _lit 槽（05-printstack 的 `int64:3` 渲染依赖）。
        if (mem_frame && !f.observable && ins.call_ready &&
            ins.call_rtyp_ok[i] &&
            (ins.call_rtyp[i] == "int64" || ins.call_rtyp[i] == "bool") &&
            !(rd.kind == Operand::Kind::Literal && !rd.name.empty() &&
              rd.name[0] == '/')) {
            FReg& cr = f.regs[ins.rd[i]];
            const FReg& ca = cr.src_ptr ? *cr.src_ptr : cr;
            if (ca.fast_i64 || ca.fast_bool) {
                if (!bind_arg(-i, &cr, vk, ins.call_rtyp[i], vw))
                    return RunResult::Error;
                continue;
            }
        }
        // Literal 绝对路径实参（`count_children("/tmp/kvt/")`）：传**路径文本**。
        if (rd.kind == Operand::Kind::Literal && !rd.name.empty() &&
            rd.name[0] == '/') {
            const std::string pt = rd.name;
            std::uint8_t* pb = nullptr;
            std::uint32_t pl = 0;
            if (kvspaceXvalueEncode(
                    "char/utf8",
                    reinterpret_cast<const std::uint8_t*>(pt.data()),
                    static_cast<std::uint32_t>(pt.size()), nullptr, 0, &pb,
                    &pl) != 0 ||
                !pb) {
                *err = "call: path arg encode failed";
                return RunResult::Error;
            }
            vk = nf + "/._lit" + std::to_string(i);
            char be[192] = {};
            const bool okk = kvspaceSetValue(f.h, vk.c_str(), pb, pl, 0, 0, be,
                                             sizeof be) == 0;
            std::free(pb);
            if (!okk) {
                *err = std::string("call: literal arg failed: ") + be;
                return RunResult::Error;
            }
            read_wire(f.h, vk, &vw.d, &vw.n);
        } else if (!read_reg(f, ins.rd[i], &vw)) {
            *err = "call: bad arg " + std::to_string(i);
            return RunResult::Error;
        }
        if (rd.kind == Operand::Kind::Literal && !rd.name.empty() &&
            rd.name[0] == '/') {
            // 上面已写 _lit 槽；vw 已设为文本。
        } else if (rd.kind == Operand::Kind::Literal) {
            vk = nf + "/._lit" + std::to_string(i);
            char be[192] = {};
            if (kvspaceSetValue(f.h, vk.c_str(), vw.d, vw.n, 0, 0, be,
                                sizeof be) != 0) {
                *err = std::string("call: literal arg failed: ") + be;
                return RunResult::Error;
            }
        } else {
            FReg& rr = f.regs[ins.rd[i]];
            if (!ensure_reg_key(f, rr))
                return RunResult::Error;
            vk = rr.key;
        }
        typ.clear();
        if (!decl_type(-i, &typ) && !v_langtype(vw, &typ))
            typ = "int64";
        if (!bind_arg(-i, &f.regs[ins.rd[i]], vk, typ, vw))
            return RunResult::Error;
    }
    if (!jcode || f.observable)
    for (int i = 1; i <= ins.nwr; i++) {
        FReg& wr = f.regs[ins.wr[i - 1]];
        // 内存别名写参（dst_ptr，key 空）转发给非纯内存 callee：值在调用方 reg，
        // 沿 dst_ptr 链取明文 key 作 NewPtr 目标（replace→replacen 转发链）。
        FReg* wrk = &wr;
        while (wrk->key.empty() && wrk->dst_ptr)
            wrk = wrk->dst_ptr;
        if (!ensure_reg_key(f, *wrk))
            return RunResult::Error;
        const std::string vk = wrk->key;
        std::string typ;
        if (!decl_type(i, &typ))
            typ = "int64";
        if (!bind_arg(i, &f.regs[ins.wr[i - 1]], vk, typ, Wire{}))
            return RunResult::Error;
    }
    FctxLease lease;
    FrameCtx& nfc = *lease.p;
    nfc.h = f.h;
    nfc.vid = f.vid;
    nfc.depth = f.depth + 1;
    nfc.frame = nf;
    nfc.func_dir = callee;
    nfc.seg_hint = cseg;
    nfc.jit_hint = jcode;
    nfc.jit_known = ins.call_ready || !jcode;
    if (mem_src_n)
        std::memcpy(nfc.arg_src, mem_src,
                    static_cast<std::size_t>(mem_src_n) * sizeof(FReg*));
    if (mem_dst_n)
        std::memcpy(nfc.arg_dst, mem_dst,
                    static_cast<std::size_t>(mem_dst_n) * sizeof(FReg*));
    nfc.arg_src_n = mem_src_n;
    nfc.arg_dst_n = mem_dst_n;
    nfc.mem_frame = mem_frame;
    nfc.observable = f.observable;  // 可观察链传播（vthread·run 子链的递归帧）
    if (jcode && !f.observable) {
        // JIT 快路径：绕过上文所有 kvspace 读取（param_decl_type/read_reg/
        // ensure_reg_key/_lit 槽），直接 mem 绑定 int64 参数。
        // 可观察链（vthread·run 子链，printstack 检视）不走此路径——字面量实参
        // 须落 _lit 槽（05-printstack 的 `int64:3` 渲染依赖）。
        for (int i = 1; i < ins.nrd; i++)
            if (!bind_arg(-i, &f.regs[ins.rd[i]], "", "int64", Wire{}))
                return RunResult::Error;
        for (int i = 1; i <= ins.nwr; i++)
            if (!bind_arg(i, &f.regs[ins.wr[i - 1]], "", "int64", Wire{}))
                return RunResult::Error;
        if (mem_src_n)
            std::memcpy(nfc.arg_src, mem_src,
                        static_cast<std::size_t>(mem_src_n) * sizeof(FReg*));
        if (mem_dst_n)
            std::memcpy(nfc.arg_dst, mem_dst,
                        static_cast<std::size_t>(mem_dst_n) * sizeof(FReg*));
        nfc.arg_src_n = mem_src_n;
        nfc.arg_dst_n = mem_dst_n;
        nfc.mem_frame = true;
    }
    RunResult rr = run_frame(nfc, 1, stop_pc, err);
    if (rr != RunResult::Ok && rr != RunResult::Returned)
        return rr;
    // 写参回写分流失效：内存别名（mem_dst）标量直写调用方 reg，缓存即值；
    // 其余经 kvspace 槽回写 → 调用方写参 fast 缓存陈旧，须逐级失效（重读槽），
    // 否则下条指令的 fk 快路径读到旧值（title 每字符 isalpha 后 br 误判）。
    for (int i = 0; i < ins.nwr; i++) {
        bool aliased = false;
        for (int j = 0; j < mem_dst_n; j++)
            if (mem_dst[j] == &f.regs[ins.wr[i]]) {
                aliased = true;
                break;
            }
        if (aliased)
            continue;
        FReg* wr = &f.regs[ins.wr[i]];
        do {
            wr->fast_i64 = false;
            wr->fast_bool = false;
            wr->sc_ok = false;
            wr->view_ok = false;
            wr->resolved = false;
            wr->noslot = false;
            wr = wr->dst_ptr;
        } while (wr);
    }
    if (bin.prof_on)
        bin.prof.steps++;  // 嵌套帧自身已计时，勿重复累计
    return RunResult::Ok;
}

namespace {

constexpr std::string_view kMemberSep = "\xE2\x80\xA5";  // ‥

}  // namespace

bool parse_pc(std::string_view pc, Pc* out) noexcept {
    // /vthread/<vid>/[d]/[s0,s1]
    constexpr std::string_view pfx = "/vthread/";
    if (pc.size() < pfx.size() || pc.substr(0, pfx.size()) != pfx)
        return false;
    std::string_view rest = pc.substr(pfx.size());
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0)
        return false;
    out->vid.assign(rest.substr(0, slash));
    rest = rest.substr(slash + 1);
    auto frame_end = rest.find(']');
    if (rest.size() < 3 || rest[0] != '[' || frame_end == std::string_view::npos)
        return false;
    if (rest.substr(1, frame_end - 1).find(',') != std::string_view::npos)
        return false;
    int depth = 0;
    auto fr = std::from_chars(rest.data() + 1, rest.data() + frame_end, depth);
    if (fr.ec != std::errc() || depth < 1)
        return false;
    rest = rest.substr(frame_end + 1);
    if (rest.size() < 3 || rest[0] != '/' || rest[1] != '[')
        return false;
    rest = rest.substr(2);
    auto close = rest.find(']');
    if (close == std::string_view::npos)
        return false;
    auto comma = rest.substr(0, close).find(',');
    if (comma == std::string_view::npos)
        return false;
    int s0 = 0, s1 = 0;
    auto a = std::from_chars(rest.data(), rest.data() + comma, s0);
    auto b = std::from_chars(rest.data() + comma + 1, rest.data() + close, s1);
    if (a.ec != std::errc() || b.ec != std::errc())
        return false;
    out->depth = depth;
    out->s0 = s0;
    out->s1 = s1;
    return true;
}

std::string Vmkv::vthread_root(std::string_view vid) {
    std::string s = "/vthread/";
    s.append(vid);
    return s;
}

std::string Vmkv::frame_root(std::string_view vid, int depth) {
    // 单次 snprintf（免多次 string 拼接的多次堆分配；等 coding3 key 零内存 demo
    // 落地后改栈缓冲复用）。
    char buf[64];
    const int n = std::snprintf(buf, sizeof buf, "/vthread/%.*s/[%d]",
                                (int)vid.size(), vid.data(), depth);
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string Vmkv::entry_pc(std::string_view vid, int depth) {
    char buf[80];
    const int n = std::snprintf(buf, sizeof buf, "/vthread/%.*s/[%d]/[1,0]",
                                (int)vid.size(), vid.data(), depth);
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string Vmkv::param_key(std::string_view fr, int s0, int s1) {
    char buf[96];
    const int n = std::snprintf(buf, sizeof buf, "%.*s/[%d,%d]", (int)fr.size(),
                                fr.data(), s0, s1);
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string Vmkv::instr_key(const Pc& pc) {
    return param_key(frame_root(pc.vid, pc.depth), pc.s0, pc.s1);
}

std::string Vmkv::pc_key_for(const Pc& p) {
    std::string s;
    s.reserve(p.vid.size() + 14);
    s += "/vthread/";
    s += p.vid;
    s += "/\xE2\x80\xA5" "pc";
    return s;
}

std::string Vmkv::next_pc(const Pc& pc) {
    return param_key(frame_root(pc.vid, pc.depth), pc.s0 + 1, 0);
}

std::string Vmkv::fold_op(std::string_view op) {
    std::string s;
    s.reserve(op.size());
    // `·` 是 UTF-8 E2 80 A5；与 `/` 一起折叠成 `/`。
    for (size_t i = 0; i < op.size();) {
        if (i + 3 <= op.size() && static_cast<unsigned char>(op[i]) == 0xE2 &&
            static_cast<unsigned char>(op[i + 1]) == 0x80 &&
            static_cast<unsigned char>(op[i + 2]) == 0xA5) {
            s += '/';
            i += 3;
        } else if (op[i] == '/') {
            s += '/';
            ++i;
        } else {
            s += op[i++];
        }
    }
    return s;
}

Vmkv g_vmkv;

}  // namespace kvlang::runtimec2
