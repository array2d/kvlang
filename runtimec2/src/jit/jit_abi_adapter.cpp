// Stage-1 migration adapter: legacy jit_try_run load/writeback behind the
// three-pointer ABI.  See answer-008 for the measured costs and the
// keyto64_xvalue / keyto64_xvalueBody / keyto64_xvaluelangtype terminology.

#include "jit_abi_adapter.hpp"

#include "jit.hpp"
#include "valueio.hpp"
#include "vmkv.hpp"

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace kvlang::runtimec2 {
namespace {

using vmbin::Frame;
using vmbin::LangType;
using vmbin::Register;
using vmbin::RegisterDesc;

// Fast plaintext commit for the direct-dst case, using the E-layer static
// type (RegDesc.langtype/width).  `view.head` is the keyto64_xvalue address;
// `view.body` is the keyto64_xvalueBody address; the langtype would be
// reached as keyto64_xvaluelangtype (= keyto64_xvalue + 8 + body_len, attr
// tag=1) but the E-layer hint makes that read unnecessary.
__attribute__((always_inline)) inline bool commit_direct_i64(Register& r,
                                                             const RegisterDesc& d,
                                                             std::int64_t v) {
    if (d.langtype != LangType::Int64 || d.width != 8)
        return false;
    const kvspaceXvalueBodyView_t& w = r.view;
    if (!(w.head && w.headlen == KVSPACE_XVALUE_HEAD_LEN && w.body_cap >= 8))
        return false;
    // Scalar storetype only: never memcpy into a slack/array body.
    if (w.head[6] != KVSPACE_XVALUE_ST_SHORT)
        return false;
    std::memcpy(r.body, &v, 8);
    return true;
}

}  // namespace

namespace {
// Load-time initialisation: no function-local static guard on the hot path.
// RC2_JIT_ABI=1 (or any non-empty non-"0" value) enables; unset/"0" is off.
bool jit_abi_env_enabled() noexcept {
    const char* value = std::getenv("RC2_JIT_ABI");
    return value && *value && std::strcmp(value, "0") != 0;
}
const bool g_jit_abi_enabled = jit_abi_env_enabled();
}  // namespace

bool jit_abi_enabled() { return g_jit_abi_enabled; }

bool jit_abi_eligible(const JitAbiLink& link) noexcept {
    if (!link.frame || link.src_idx >= link.frame->regs.size())
        return false;
    const Register& sr0 = link.frame->regs[link.src_idx];
    const Register* src = sr0.src_ptr ? sr0.src_ptr : &sr0;
    return src->fast_i64;
}

void jit_abi_run(const JitAbiLink& link, const void* jit_code,
                 jitabi::StructuredExitV1* out) noexcept {
    out->abi_version = jitabi::kAbiVersionV1;
    out->struct_size = sizeof(jitabi::StructuredExitV1);
    out->kind = jitabi::ExitKindV1::Return;
    out->flags = 0;
    out->detail_code = 0;
    out->pc = 0;
    out->resume_pc = jitabi::kNoPc;
    out->value_base = 0;
    out->value_count = 0;
    out->aux0 = 0;

    Frame& f = *link.frame;
    Register& sr0 = f.regs[link.src_idx];
    Register* src = sr0.src_ptr ? sr0.src_ptr : &sr0;
    const std::int64_t arg = src->fv;

    view_cache_idle_all();  // native code never acks: hold no views across it
    std::int64_t err = 0;
    const std::int64_t result =
        reinterpret_cast<JitFn>(const_cast<void*>(jit_code))(arg, &err);
    if (err) {
        out->kind = jitabi::ExitKindV1::Error;
        out->detail_code = jitabi::kDetailRunaway;
        return;
    }

    Register& dr0 = f.regs[link.dst_idx];
    Register* dst = dr0.dst_ptr ? dr0.dst_ptr : &dr0;
    while (dst->dst_ptr)
        dst = dst->dst_ptr;
    dst->fv = result;
    dst->fast_i64 = true;
    if (dst->view_ok && dst->body) {
        bool committed =
            dst == &dr0 && link.program &&
            link.dst_idx < link.program->regs.size() &&
            commit_direct_i64(*dst, link.program->regs[link.dst_idx], result);
        if (!committed)
            committed = g_vmkv.store_i64_slot(f, *dst, result);
        if (!committed) {
            out->kind = jitabi::ExitKindV1::Error;
            out->detail_code = jitabi::kDetailCommitFailed;
            return;
        }
    }
    dr0.fv = result;
    dr0.fast_i64 = true;

    out->value_count = 1;
    out->aux0 = static_cast<std::uint64_t>(result);
}

}  // namespace kvlang::runtimec2
