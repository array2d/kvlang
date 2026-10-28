#include <cstdint>
#include <cstdio>
#include <new>

extern "C" {
#include "kvlang_runtime.h"
#include "kvlang_vthread.h"
}

#include <cstring>
#include <string>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "vmkv.hpp"
#include "runtime.hpp"

#define KV2_EXPORT extern "C" __attribute__((visibility("default")))

namespace {
using kv::rt::Runtime;
kv::rt::Runtime* rt_of(void* rt) { return static_cast<Runtime*>(rt); }
}  // namespace

namespace kv::rt {
char* params_impl(void* h, const char* pc);
char* resolve_read_impl(void* h, const char* pc, int idx);
char* resolve_read_path_impl(void* h, const char* pc, int idx);
char* resolve_write_impl(void* h, const char* pc, int idx);
char* next_pc_impl(const char* pc);
}  // namespace kv::rt

KV2_EXPORT kvlangRuntime_t* kvlangRuntimeConnect(const char* dsn) {
    if (!dsn || !*dsn)
        return nullptr;
    auto* rt = new (std::nothrow) Runtime();
    if (!rt)
        return nullptr;
    if (!rt->kv.open(dsn)) {
        delete rt;
        return nullptr;
    }
    return reinterpret_cast<kvlangRuntime_t*>(rt);
}

KV2_EXPORT void kvlangRuntimeDisconnect(kvlangRuntime_t* rt) {
    delete rt_of(rt);
}

KV2_EXPORT void* kvlangRuntimeKvspaceHandle(kvlangRuntime_t* rt) {
    auto* r = rt_of(rt);
    return r ? r->kv.handle() : nullptr;
}

KV2_EXPORT char* kvlangRuntimeBootstrap(kvlangRuntime_t* rt,
                                        const char* funcname,
                                        const char* const* args, int nargs) {
    (void)args;
    (void)nargs;
    return kv::rt::bootstrap_impl(rt_of(rt), funcname);
}

KV2_EXPORT int kvlangRuntimeExecuteVthread(kvlangRuntime_t* rt, const char* vid,
                                           char** out_pc) {
    return kv::rt::execute_vthread_impl(rt_of(rt), vid, out_pc);
}

KV2_EXPORT int kvlangRuntimeExecutePc(kvlangRuntime_t* rt, const char* pc) {
    (void)rt;
    (void)pc;
    return -1;  // M2
}

KV2_EXPORT int kvlangRuntimeExecute(kvlangRuntime_t* rt, const char* funcname,
                                    const char* const* args, int nargs,
                                    char** ret, char* err, uint32_t err_cap) {
    (void)rt;
    (void)funcname;
    (void)args;
    (void)nargs;
    (void)ret;
    if (err && err_cap)
        std::snprintf(err, err_cap, "runtimec2: Execute not implemented (M2)");
    return -1;
}

namespace {

bool set_wire(void* h, const std::string& key, const char* kind,
              const std::uint8_t* raw, std::uint32_t len) {
    std::uint8_t* buf = nullptr;
    std::uint32_t n = 0;
    if (kvspaceXvalueEncode(kind, raw, len, nullptr, 0, &buf, &n) != 0 || !buf)
        return false;
    char err[192] = {};
    int rc = kvspaceSetValue(h, key.c_str(), buf, n, 1, 0, err, sizeof err);
    std::free(buf);
    return rc == 0;
}

bool write_sig(void* h, const std::string& base, int x, const char* lt,
               std::size_t len) {
    const std::string key = base + "/[0," + std::to_string(x) + "]";
    return set_wire(h, key, "def langtype",
                    reinterpret_cast<const std::uint8_t*>(lt),
                    static_cast<std::uint32_t>(len));
}

}  // namespace

KV2_EXPORT int kvlangDefRwir(void* kvspace, const char* opcode,
                             const char* const* rp, int32_t nr,
                             const char* const* wp, int32_t nw) {
    if (!kvspace || !opcode || !*opcode)
        return -1;
    void* h = kvspace;
    const std::string base = std::string("/lib/") + opcode;
    // 新架构：签名声明同时落 /‥isa/<folded-op>（layout 的 isa_op 路径），
    // 供 layout 对扩展/term op（如 println）按 Bridge 放行。
    const std::string isa_base = [&] {
        std::string f;
        for (const char* p = opcode; *p; p++) {
            const unsigned char c = (unsigned char)*p;
            if (c == 0xC2 && (unsigned char)p[1] == 0xB7) {
                f += '/';
                p++;
            } else if (c == '/') {
                f += '/';
            } else {
                f += c;
            }
        }
        return "/â¥isa/" + f;
    }();
    int dynamic = 0;
    std::size_t last_len = nr > 0 && rp[nr - 1] ? std::strlen(rp[nr - 1]) : 0;
    if (nr > 0 && last_len >= 3 &&
        std::memcmp(rp[nr - 1] + last_len - 3, "...", 3) == 0) {
        dynamic = 1;
        last_len -= 3;
    }
    const std::uint8_t body[5] = {
        static_cast<std::uint8_t>(nr & 0xff),
        static_cast<std::uint8_t>((nr >> 8) & 0xff),
        static_cast<std::uint8_t>(nw & 0xff),
        static_cast<std::uint8_t>((nw >> 8) & 0xff),
        static_cast<std::uint8_t>(dynamic)};
    // /lib/<op> 供调用路由；/‥isa/<op> 供 layout 签名查询（def rwir）。
    if (!set_wire(h, base, "def rwir", body, sizeof body) ||
        !set_wire(h, isa_base, "def rwir", body, sizeof body))
        return -1;
    auto write_sigs = [&](const std::string& b) {
        for (int32_t i = 0; i < nr; i++)
            if (!write_sig(h, b, -(i + 1), rp[i],
                           (i == nr - 1) ? last_len : std::strlen(rp[i])))
                return false;
        for (int32_t i = 0; i < nw; i++)
            if (!write_sig(h, b, i + 1, wp[i], std::strlen(wp[i])))
                return false;
        return true;
    };
    return write_sigs(base) && write_sigs(isa_base) ? 0 : -1;
}

KV2_EXPORT int kvlangRwirextHandoff(void* kvspace, const char* vtid,
                                    const char* pc) {
    (void)kvspace;
    (void)vtid;
    (void)pc;
    return -1;  // M1：RETURN 模式不 handoff
}

KV2_EXPORT char* kvlangRwirextParams(void* kvspace, const char* pc) {
    return kv::rt::params_impl(kvspace, pc);
}

KV2_EXPORT char* kvlangRwirextNextPc(const char* pc) {
    return kv::rt::next_pc_impl(pc);
}

KV2_EXPORT char* kvlangRwirextResolveRead(void* kvspace, const char* pc,
                                          int idx) {
    return kv::rt::resolve_read_impl(kvspace, pc, idx);
}

KV2_EXPORT char* kvlangRwirextResolveReadPath(void* kvspace, const char* pc,
                                              int idx) {
    return kv::rt::resolve_read_path_impl(kvspace, pc, idx);
}

KV2_EXPORT char* kvlangRwirextResolveWrite(void* kvspace, const char* pc,
                                           int idx) {
    return kv::rt::resolve_write_impl(kvspace, pc, idx);
}

KV2_EXPORT bool kvlangLangtypeValid(const char* expr) {
    (void)expr;
    return false;  // M2
}

KV2_EXPORT bool kvlangLangtypeMatch(const char* expr, const char* kind,
                                    int32_t ndim, const int32_t* dims) {
    (void)expr;
    (void)kind;
    (void)ndim;
    (void)dims;
    return false;  // M2
}
