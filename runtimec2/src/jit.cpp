#include "jit.hpp"
#include "valueio.hpp"
#include "vmkv.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include <sys/mman.h>

// 平台无关 JIT 核心：静态判定（verify_jitable 形状）+ 进程级缓存 +
// run_frame 钩子。发射器按 arch 拆分：x86_64 → jit_x64.cpp（完整），
// ARM64 → jit_arm.cpp（框架）。emit_arch 返回空 = 该平台不可发射。

namespace kvlang::runtimec2 {
namespace {

using vmbin::Register;
using vmbin::Frame;
using vmbin::Outcome;

bool g_nojit = std::getenv("RC2_NOJIT") != nullptr;
bool g_jitdump = std::getenv("RC2_JITDUMP") != nullptr;
bool g_jitdbg = std::getenv("RC2_JITDBG") != nullptr;

constexpr std::int64_t kInt32Min = -2147483647LL - 1;
constexpr std::int64_t kInt32Max = 2147483647LL;

// ── 静态判定（M1 形状，平台无关）──────────────────────────────────

bool verify(const Segment* seg, JitShape* sh) {
    if (seg->has_rwirhandler || seg->insns.empty())
        return false;
    if (g_jitdbg) {
        for (std::size_t i = 0; i < seg->regs.size(); i++) {
            std::fprintf(stderr, "[jitdbg reg %zu kind=%d lit=%d v=%lld pk=%d]\n",
                         i, (int)seg->regs[i].kind,
                         (int)(i < seg->lit.size() && seg->lit[i].is_i64),
                         (long long)(i < seg->lit.size() ? seg->lit[i].v : 0),
                         seg->regs[i].param_k);
        }
        for (std::size_t i = 0; i < seg->insns.size(); i++) {
            const CInsn& ci = seg->insns[i];
            std::fprintf(stderr,
                         "[jitdbg insn %zu s0=%d op=%s opfn=%d nrd=%u nwr=%u "
                         "rd=%d,%d,%d wr=%d,%d,%d]\n",
                         i, ci.s0, ci.name.c_str(), (int)ci.opfn, ci.nrd, ci.nwr,
                         ci.rd[0], ci.rd[1], ci.rd[2], ci.wr[0], ci.wr[1],
                         ci.wr[2]);
        }
    }
    const int n = static_cast<int>(seg->insns.size());
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        const RegDesc& d = seg->regs[i];
        if (d.kind == Operand::Kind::Literal) {
            if (i >= seg->lit.size() || !seg->lit[i].is_i64 ||
                seg->lit[i].v < kInt32Min || seg->lit[i].v > kInt32Max)
                return false;
            continue;
        }
        if (d.kind != Operand::Kind::Var)
            return false;
        if (d.param_k == -1) {
            if (sh->src_idx >= 0)
                return false;
            sh->src_idx = static_cast<int>(i);
        } else if (d.param_k == 1) {
            if (sh->dst_idx >= 0)
                return false;
            sh->dst_idx = static_cast<int>(i);
        } else if (d.name.empty() || d.name[0] == '*' || d.name[0] == '/' ||
                   d.name.find("\xC2\xB7") != std::string::npos) {
            return false;
        }
    }
    if (sh->src_idx < 0 || sh->dst_idx < 0)
        return false;
    // 入口第一条必须无条件写 dst 参数（回写在 JIT 侧恒成立）
    {
        const CInsn& c0 = seg->insns[0];
        bool wr0 = false;
        for (int k = 0; k < c0.nwr; k++)
            if (c0.wr[k] == sh->dst_idx)
                wr0 = true;
        if (!wr0)
            return false;
    }
    for (std::size_t ii = 0; ii < seg->insns.size(); ii++) {
        const CInsn& ci = seg->insns[ii];
        if (ci.op == rwir::Op::Return) {
            if (ci.nrd != 0 || ci.nwr != 0 || ii + 1 != seg->insns.size())
                return false;
            continue;
        }
        if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
            const bool br = ci.op == rwir::Op::Br;
            if (ci.impl != rwir::Impl::Control || (br && ci.nrd != 3) ||
                (!br && ci.nrd != 1) || ci.nwr != 0)
                return false;
            // rd[0]=cond（Var）；目标 rd[1]/rd[2] 必须是字面量且在 [1,n]
            if (br) {
                if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)
                    return false;
            } else {
                if (seg->regs[ci.rd[0]].kind != Operand::Kind::Literal)
                    return false;
            }
            const int t1 = ci.rd[br ? 1 : 0];
            const int t2 = br ? ci.rd[2] : -1;
            if (seg->regs[t1].kind != Operand::Kind::Literal)
                return false;
            const std::int64_t tv1 = seg->lit[t1].v;
            if (tv1 < 1 || tv1 > n)
                return false;
            if (br) {
                if (seg->regs[t2].kind != Operand::Kind::Literal)
                    return false;
                const std::int64_t tv2 = seg->lit[t2].v;
                if (tv2 < 1 || tv2 > n)
                    return false;
            }
            continue;
        }
        if (ci.op == rwir::Op::Copy) {
            if (ci.nrd != 1 || ci.nwr != 1)
                return false;  // Copy 的 opcode 是 `==`（Impl::Dynamic），不判 impl
        } else if (ci.fast_i64) {
            const bool is_cmp = ci.opfn >= 13 && ci.opfn <= 18;
            if (ci.opfn != 1 && !is_cmp)  // M1 只发射 add + 比较；其余回退解释路径
                return false;
            if (ci.nrd != 2 || ci.nwr != 1)
                return false;
            if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)  // 字面量作第一操作数未测
                return false;
            if (seg->regs[ci.wr[0]].kind != Operand::Kind::Var)
                return false;
        } else {
            return false;
        }
        for (int k = 0; k < ci.nwr; k++) {
            if (ci.wr[k] == sh->src_idx ||  // 读参不可写（写 RDI 会毁 arg）
                seg->regs[ci.wr[k]].kind == Operand::Kind::Literal)
                return false;
        }
    }
    return true;
}

std::unordered_map<std::string, JitFn>& jit_cache() {
    static std::unordered_map<std::string, JitFn> c;
    return c;
}

}  // namespace

bool jit_enabled() { return !g_nojit; }

void jit_clear_cache() { jit_cache().clear(); }

const void* jit_compile(const std::string& func_dir, const Segment* seg) {
    auto& cache = jit_cache();
    auto it = cache.find(func_dir);
    if (it != cache.end())
        return reinterpret_cast<const void*>(it->second);
    JitShape sh;
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg verify %s]\n", func_dir.c_str());
    if (!verify(seg, &sh)) {
        if (g_jitdbg)
            std::fprintf(stderr, "[jitdbg verify FAIL %s]\n", func_dir.c_str());
        cache[func_dir] = nullptr;
        return nullptr;
    }
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg emit %s src=%d dst=%d]\n", func_dir.c_str(),
                     sh.src_idx, sh.dst_idx);
    std::vector<std::uint8_t> bytes = emit_arch(seg, sh);
    if (bytes.empty()) {
        cache[func_dir] = nullptr;
        return nullptr;
    }
    const std::size_t cap = bytes.size();
    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        cache[func_dir] = nullptr;
        return nullptr;
    }
    std::memcpy(mem, bytes.data(), cap);
    if (mprotect(mem, cap, PROT_READ | PROT_EXEC) != 0) {
        munmap(mem, cap);
        cache[func_dir] = nullptr;
        return nullptr;
    }
    cache[func_dir] = reinterpret_cast<JitFn>(mem);
    return reinterpret_cast<const void*>(mem);
}

bool jit_try_run(Frame& f, const Segment* seg, int start_s0,
                 Outcome* out, std::string* stop_pc, std::string* err) {
    *out = Outcome::Ok;
    if (!jit_enabled())
        return false;
    if (start_s0 != 1)
        return false;
    const void* code = jit_compile(f.func_dir, seg);
    if (!code)
        return false;
    int src_idx = -1, dst_idx = -1;
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        if (seg->regs[i].kind == Operand::Kind::Var && seg->regs[i].param_k == -1)
            src_idx = static_cast<int>(i);
        if (seg->regs[i].kind == Operand::Kind::Var && seg->regs[i].param_k == 1)
            dst_idx = static_cast<int>(i);
    }
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg run %s src=%d dst=%d]\n",
                     f.func_dir.c_str(), src_idx, dst_idx);
    if (src_idx < 0 || dst_idx < 0)
        return false;
    // 读参装载（镜像 read_i64 的 src_ptr 继承）
    Register& sr0 = f.regs[src_idx];
    Register* src = sr0.src_ptr ? sr0.src_ptr : &sr0;
    if (g_jitdbg)
        std::fprintf(stderr,
                     "[jitdbg run %s src_ptr=%p spfast=%d self=%p fast=%d v=%lld "
                     "ansrc=%d]\n",
                     f.func_dir.c_str(), (void*)sr0.src_ptr,
                     (int)(sr0.src_ptr ? sr0.src_ptr->fast_i64 : -1),
                     (void*)&sr0, (int)sr0.fast_i64, (long long)sr0.fv,
                     f.arg_src_n);
    if (!src->fast_i64)
        return false;
    const std::int64_t arg = src->fv;
    std::int64_t errf = 0;
    view_cache_idle_all();  // native code never acks: hold no view tokens across it
    const std::int64_t res =
        reinterpret_cast<JitFn>(const_cast<void*>(code))(arg, &errf);
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg res %s arg=%lld res=%lld]\n",
                     f.func_dir.c_str(), (long long)arg, (long long)res);
    if (errf) {
        *err = "runaway loop (jit): " + f.func_dir;
        *out = Outcome::Error;
        return true;
    }
    // 结果回写（镜像 write_reg 的 dst_ptr 分支：fast 缓存 + 已有槽直写）。
    // 沿链贯通到最终调用方 reg——只挂一层会让 depth≥2 的中间帧 fast 缓存随帧消亡。
    Register& dr0 = f.regs[dst_idx];
    Register* dst = dr0.dst_ptr ? dr0.dst_ptr : &dr0;
    while (dst->dst_ptr)
        dst = dst->dst_ptr;
    dst->fv = res;
    dst->fast_i64 = true;
    if (dst->view_ok && dst->body)
        (void)g_vmkv.store_i64_slot(f, *dst, res);
    dr0.fv = res;
    dr0.fast_i64 = true;
    if (stop_pc)
        stop_pc->clear();
    *out = f.depth <= 1 ? Outcome::Ok : Outcome::Returned;
    return true;
}

}  // namespace kvlang::runtimec2
