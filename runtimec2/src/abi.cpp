#include <cstdint>
#include <cstdio>
#include <new>

extern "C" {
#include "kvlang_runtime.h"
#include "kvlang_vthread.h"
}

#include "model.hpp"
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

KV2_EXPORT int kvlangDefRwir(void* kvspace, const char* opcode,
                             const char* const* rp, int32_t nr,
                             const char* const* wp, int32_t nw) {
    (void)kvspace;
    (void)opcode;
    (void)rp;
    (void)nr;
    (void)wp;
    (void)nw;
    return -1;  // M2：签名注册
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
