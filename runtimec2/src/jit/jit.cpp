#include "jit.hpp"
#include "vmkv_rwirhandler.hpp"
#include "jit_abi_adapter.hpp"
#include "jit_stage2.hpp"
#include "valueio.hpp"
#include "vmkv.hpp"

#include <kvspace/xvalue_head.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

// compact 数组槽的 body 逻辑长度须装下全部元素（head dims × 元素宽度），
// 否则内联寻址会越过 body 写进 attr 区。
bool array_body_fits(const Segment& seg, std::size_t r,
                     std::uint64_t cap) {
    if (r >= seg.regs.size())
        return false;
    const std::uint32_t tid = seg.regs[r].array_type_id;
    if (tid == vmbin::kArrayTypeNone || tid >= seg.array_types.size())
        return false;
    const vmbin::ArrayTypeDesc& at = seg.array_types[tid];
    return at.numel > 0 && at.width != 0 &&
           cap >= static_cast<std::uint64_t>(at.numel) * at.width;
}

bool g_nojit = std::getenv("RC2_NOJIT") != nullptr;
bool g_jitdump = std::getenv("RC2_JITDUMP") != nullptr;
bool g_jitdbg = std::getenv("RC2_JITDBG") != nullptr;

constexpr std::int64_t kInt32Min = -2147483647LL - 1;
constexpr std::int64_t kInt32Max = 2147483647LL;

// ── 静态判定（M1 形状，平台无关）──────────────────────────────────

bool verify(const Segment* seg, JitShape* sh, JitRefusal* refusal) {
    auto refuse = [&](VmjitRefusalReason reason, std::uint32_t detail) {
        refusal->refusal_reason = reason;
        refusal->detail_code = detail;
        return false;
    };
    if (seg->has_rwirhandler || seg->insns.empty())
        return refuse(VmjitRefusalReason::MalformedSegment, 0);
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
                         i, vmbin::instruction_pc(i), instruction_name(*seg, ci),
                         (int)ci.opfn, ci.nrd, ci.nwr,
                         ci.rd[0], ci.rd[1], ci.rd[2], ci.wr[0], ci.wr[1],
                         ci.wr[2]);
        }
    }
    const int n = static_cast<int>(seg->insns.size());
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        const RegDesc& d = seg->regs[i];
        if (d.kind == Operand::Kind::Literal) {
            if (i >= seg->lit.size() || !seg->lit[i].is_i64)
                return refuse(VmjitRefusalReason::TypeMismatch, i);
            if (seg->lit[i].v < kInt32Min || seg->lit[i].v > kInt32Max)
                return refuse(VmjitRefusalReason::TypeMismatch, i);
            continue;
        }
        if (d.kind != Operand::Kind::Var)
            return refuse(VmjitRefusalReason::UnsupportedOperand, i);
        if (d.param_k == -1) {
            if (sh->src_idx >= 0)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            // M1 的值模型只有 int64：bool/float 参数经 M1 会按 int64 落槽，
            // 调用方读回类型错（如 bool 结果被 println 打成 1/0）。这类形状
            // 交 JIT2 或解释器。
            if (d.langtype != vmbin::LangType::Int64 &&
                d.langtype != vmbin::LangType::Uint64 &&
                d.langtype != vmbin::LangType::None)
                return refuse(VmjitRefusalReason::TypeMismatch, i);
            sh->src_idx = static_cast<int>(i);
        } else if (d.param_k == 1) {
            if (sh->dst_idx >= 0)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            if (d.langtype != vmbin::LangType::Int64 &&
                d.langtype != vmbin::LangType::Uint64 &&
                d.langtype != vmbin::LangType::None)
                return refuse(VmjitRefusalReason::TypeMismatch, i);
            sh->dst_idx = static_cast<int>(i);
        } else if (d.param_k != 0) {
            // 多参（-2/-3/… 或 +2/…）需要参数表 ABI；当前 M1 只接受单入单出。
            return refuse(VmjitRefusalReason::BadParamLayout, i);
        } else if (d.name.empty() || d.name[0] == '*' || d.name[0] == '/' ||
                   d.name.find("\xC2\xB7") != std::string::npos) {
            return refuse(VmjitRefusalReason::UnsupportedOperand, i);
        }
    }
    if (sh->src_idx < 0 || sh->dst_idx < 0)
        return refuse(VmjitRefusalReason::BadParamLayout, 0);
    // 段结构与坐标预检：操作数下标是 uint8_t；发射器按 s0 稠密定位跳转目标。
    if (seg->regs.size() > 255)
        return refuse(VmjitRefusalReason::MalformedSegment,
                      static_cast<std::uint32_t>(seg->regs.size()));
    for (std::size_t ii = 0; ii < seg->insns.size(); ii++) {
        const CInsn& ci = seg->insns[ii];
        if (ci.nrd > 16 || ci.nwr > 4)
            return refuse(VmjitRefusalReason::MalformedSegment,
                          static_cast<std::uint32_t>(ii));
        for (int k = 0; k < ci.nrd; k++)
            if (ci.rd[k] >= seg->regs.size())
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
        for (int k = 0; k < ci.nwr; k++)
            if (ci.wr[k] >= seg->regs.size())
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
    }
    // 入口第一条必须无条件写 dst 参数（回写在 JIT 侧恒成立）
    {
        const CInsn& c0 = seg->insns[0];
        bool wr0 = false;
        for (int k = 0; k < c0.nwr; k++)
            if (c0.wr[k] == sh->dst_idx)
                wr0 = true;
        if (!wr0)
            return refuse(VmjitRefusalReason::OutputNotDefined, 0);
    }
    // 成功出口：最后一条必须是 return；否则正文 fallthrough 会跌进 guard stub。
    if (seg->insns.back().op != rwir::Op::Return)
        return refuse(VmjitRefusalReason::OutputNotDefined,
                      static_cast<std::uint32_t>(n));
    for (std::size_t ii = 0; ii < seg->insns.size(); ii++) {
        const CInsn& ci = seg->insns[ii];
        if (ci.op == rwir::Op::Return) {
            if (ci.nrd != 0 || ci.nwr != 0 || ii + 1 != seg->insns.size())
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
            continue;
        }
        if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
            const bool br = ci.op == rwir::Op::Br;
            if (ci.impl != rwir::Impl::Control)
                return refuse(VmjitRefusalReason::UnsupportedOpcode,
                              static_cast<std::uint32_t>(ii));
            if ((br && ci.nrd != 3) || (!br && ci.nrd != 1) || ci.nwr != 0)
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
            // rd[0]=cond（Var）；目标 rd[1]/rd[2] 必须是字面量且在 [1,n]
            if (br) {
                if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  static_cast<std::uint32_t>(ii));
            } else {
                if (seg->regs[ci.rd[0]].kind != Operand::Kind::Literal)
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  static_cast<std::uint32_t>(ii));
            }
            const int t1 = ci.rd[br ? 1 : 0];
            const int t2 = br ? ci.rd[2] : -1;
            if (seg->regs[t1].kind != Operand::Kind::Literal)
                return refuse(VmjitRefusalReason::UnsupportedOperand,
                              static_cast<std::uint32_t>(ii));
            const std::int64_t tv1 = seg->lit[t1].v;
            if (tv1 < 1 || tv1 > n)
                return refuse(VmjitRefusalReason::BadTarget,
                              static_cast<std::uint32_t>(ii));
            if (br) {
                if (seg->regs[t2].kind != Operand::Kind::Literal)
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  static_cast<std::uint32_t>(ii));
                const std::int64_t tv2 = seg->lit[t2].v;
                if (tv2 < 1 || tv2 > n)
                    return refuse(VmjitRefusalReason::BadTarget,
                                  static_cast<std::uint32_t>(ii));
            }
            continue;
        }
        if (ci.op == rwir::Op::Copy) {
            if (ci.nrd != 1 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
        } else if (ci.fast_i64) {
            const bool is_cmp = ci.opfn >= 13 && ci.opfn <= 18;
            if (ci.opfn != 1 && !is_cmp)  // M1 只发射 add + 比较；其余回退解释路径
                return refuse(VmjitRefusalReason::UnsupportedOpcode,
                              static_cast<std::uint32_t>(ii));
            if (ci.nrd != 2 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment,
                              static_cast<std::uint32_t>(ii));
            if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)  // 字面量作第一操作数未测
                return refuse(VmjitRefusalReason::UnsupportedOperand,
                              static_cast<std::uint32_t>(ii));
            if (seg->regs[ci.wr[0]].kind != Operand::Kind::Var)
                return refuse(VmjitRefusalReason::UnsupportedOperand,
                              static_cast<std::uint32_t>(ii));
        } else {
            return refuse(VmjitRefusalReason::UnsupportedOpcode,
                          static_cast<std::uint32_t>(ii));
        }
        for (int k = 0; k < ci.nwr; k++) {
            if (ci.wr[k] == sh->src_idx ||  // 读参不可写（写 RDI 会毁 arg）
                seg->regs[ci.wr[k]].kind == Operand::Kind::Literal)
                return refuse(VmjitRefusalReason::UnsupportedOperand,
                              static_cast<std::uint32_t>(ci.wr[k]));
        }
    }
    // 定义先于使用（definite assignment）：写参是「读-写」参数，未显式写之前
    // 读到的是调用方槽值，JIT 序言无法等价（mov RAX,0）；普通局部未定义同样
    // 不能交给机器码。沿 CFG 做「所有路径都已定义」的不动点。
    {
        const std::size_t nr = seg->regs.size();
        const std::size_t nn = seg->insns.size();
        std::vector<char> entry(nr, 0);
        for (std::size_t i = 0; i < nr; i++) {
            const RegDesc& d = seg->regs[i];
            if (d.kind == Operand::Kind::Literal)
                entry[i] = 1;
            else if (d.kind == Operand::Kind::Var && d.param_k < 0)
                entry[i] = 1;  // 读参＝调用方实参，帧入口即已定义
        }
        std::vector<std::vector<int>> succ(nn);
        for (std::size_t i = 0; i < nn; i++) {
            const CInsn& ci = seg->insns[i];
            if (ci.op == rwir::Op::Return)
                continue;
            if (ci.op == rwir::Op::Goto) {
                succ[i].push_back(static_cast<int>(seg->lit[ci.rd[0]].v) - 1);
                continue;
            }
            if (ci.op == rwir::Op::Br) {
                succ[i].push_back(static_cast<int>(seg->lit[ci.rd[1]].v) - 1);
                succ[i].push_back(static_cast<int>(seg->lit[ci.rd[2]].v) - 1);
                continue;
            }
            succ[i].push_back(static_cast<int>(i) + 1);
        }
        std::vector<std::vector<char>> in(nn), out(nn);
        std::vector<char> reach(nn, 0);
        in[0] = entry;
        reach[0] = 1;
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i < nn; i++) {
                if (!reach[i])
                    continue;
                out[i] = in[i];
                const CInsn& ci = seg->insns[i];
                for (int k = 0; k < ci.nwr; k++)
                    if (ci.wr[k] < nr)
                        out[i][ci.wr[k]] = 1;
                for (int s : succ[i]) {
                    if (s < 0 || static_cast<std::size_t>(s) >= nn)
                        continue;
                    if (!reach[s]) {
                        in[s] = out[i];
                        reach[s] = 1;
                        changed = true;
                        continue;
                    }
                    for (std::size_t r = 0; r < nr; r++)
                        if (in[s][r] && !out[i][r]) {
                            in[s][r] = 0;
                            changed = true;
                        }
                }
            }
        }
        for (std::size_t i = 0; i < nn; i++) {
            if (!reach[i])
                continue;
            const CInsn& ci = seg->insns[i];
            for (int k = 0; k < ci.nrd; k++) {
                const int idx = ci.rd[k];
                if (seg->regs[idx].kind != Operand::Kind::Var)
                    continue;
                if (!in[i][idx])
                    return refuse(VmjitRefusalReason::UninitializedRead,
                                  static_cast<std::uint32_t>(idx));
            }
            // 每个可达 return 都必须已定义写参；否则回写的是序言初值而非程序值。
            if (ci.op == rwir::Op::Return && !out[i][sh->dst_idx])
                return refuse(VmjitRefusalReason::OutputNotDefined,
                              static_cast<std::uint32_t>(i));
        }
    }
    // 寄存器压力预检：复刻发射器 reg_of 的惰性分配（分配后永不释放），
    // 池耗尽时拒绝而不是在发射期解引用空 optional。
    {
        std::unordered_map<int, int> seen;
        int used = 0;
        for (const CInsn& ci : seg->insns) {
            for (int k = 0; k < ci.nrd; k++) {
                const int idx = ci.rd[k];
                if (idx == sh->src_idx || idx == sh->dst_idx)
                    continue;
                if (seg->regs[idx].kind != Operand::Kind::Var)
                    continue;
                if (!seen.emplace(idx, 0).second)
                    continue;
                if (++used > kJitLocalRegisterCount)
                    return refuse(VmjitRefusalReason::RegisterPressure,
                                  static_cast<std::uint32_t>(idx));
            }
            for (int k = 0; k < ci.nwr; k++) {
                const int idx = ci.wr[k];
                if (idx == sh->src_idx || idx == sh->dst_idx)
                    continue;
                if (seg->regs[idx].kind != Operand::Kind::Var)
                    continue;
                if (!seen.emplace(idx, 0).second)
                    continue;
                if (++used > kJitLocalRegisterCount)
                    return refuse(VmjitRefusalReason::RegisterPressure,
                                  static_cast<std::uint32_t>(idx));
            }
        }
    }
    return true;
}

std::unordered_map<std::string, JitFn>& jit_cache() {
    static std::unordered_map<std::string, JitFn> c;
    return c;
}

// ── 第二阶段（默认开启，RC2_NOJIT2 关闭）：统一 ABI 的槽式执行 ──────
using Jit2Fn = jitabi::GeneralJitFnV1;
bool g_jit2 = std::getenv("RC2_NOJIT2") == nullptr;

// runtimec2 执行单线程（jit cache 本身也是静态 map）：用静态缓冲避免 TLS 开销。
jitabi::ValueSlotV1 g_jit2_slots[64];
jitabi::BinFrameV1 g_jit2_frame;
jitabi::VmContextV1 g_jit2_context;

struct Jit2Entry {
    std::vector<std::uint8_t> bytes;
    Jit2Plan plan;
    void* code = nullptr;
    std::size_t size = 0;
    ~Jit2Entry() {
        if (code)
            munmap(code, size);
    }
};

std::unordered_map<std::string, std::unique_ptr<Jit2Entry>>& jit2_cache() {
    static std::unordered_map<std::string, std::unique_ptr<Jit2Entry>> c;
    return c;
}

Jit2Entry* jit2_lookup(const std::string& func_dir, const Segment* seg) {
    auto& cache = jit2_cache();
    auto it = cache.find(func_dir);
    if (it != cache.end())
        return it->second.get();  // nullptr = 已判定不可 JIT2
    auto fresh = std::make_unique<Jit2Entry>();
    JitRefusal refusal;
    if (!jit2_verify(seg, &fresh->plan, &refusal)) {
        if (g_jitdbg)
            std::fprintf(stderr, "[jit2dbg reject %s reason=%u detail=%u]\n",
                         func_dir.c_str(),
                         static_cast<unsigned>(refusal.refusal_reason),
                         refusal.detail_code);
        cache[func_dir] = nullptr;
        return nullptr;
    }
    fresh->bytes = jit2_emit(seg, fresh->plan);
    if (fresh->bytes.empty()) {
        cache[func_dir] = nullptr;
        return nullptr;
    }
    const std::size_t cap = fresh->bytes.size();
    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        cache[func_dir] = nullptr;
        return nullptr;
    }
    std::memcpy(mem, fresh->bytes.data(), cap);
    if (mprotect(mem, cap, PROT_READ | PROT_EXEC) != 0) {
        munmap(mem, cap);
        cache[func_dir] = nullptr;
        return nullptr;
    }
    fresh->code = mem;
    fresh->size = cap;
    if (g_jitdbg) {
        std::string asg;
        for (std::size_t i = 0; i < fresh->plan.assignment.size(); i++)
            if (fresh->plan.assignment[i] >= 0 ||
                fresh->plan.spill_slot[i] >= 0)
                asg += " r" + std::to_string(i) + "=" +
                       (fresh->plan.assignment[i] >= 0
                            ? std::to_string(fresh->plan.assignment[i])
                            : std::string("S") +
                                  std::to_string(fresh->plan.spill_slot[i]));
        std::fprintf(stderr, "[jit2dbg plan %s%s]\n", func_dir.c_str(),
                     asg.c_str());
    }
    if (g_jitdbg)
        std::fprintf(stderr,
                     "[jit2dbg accept %s in=%zu out=%zu blocks=%zu spills=%d "
                     "handlers=%d ratio=%d]\n",
                     func_dir.c_str(), fresh->plan.shape.inputs.size(),
                     fresh->plan.shape.outputs.size(),
                     fresh->plan.blocks.size(), fresh->plan.spill_count,
                     fresh->plan.handler_count, fresh->plan.handler_percent);
    std::memset(g_jit2_slots, 0, sizeof(g_jit2_slots));  // 预热线程缓冲
    Jit2Entry* entry = fresh.get();
    cache[func_dir] = std::move(fresh);
    return entry;
}

namespace {

// 原生内调 handler 的 C 侧实现：在同一个解释器 Frame 上执行一条 handler，
// 把 JIT 槽与 Frame 寄存器对齐（见 answer-013 之后的 P0 设计）。
// 返回：0 = 继续原生，1 = Error，2 = Stop（交回解释器）。
Jit2Entry* g_jit2_running = nullptr;  // 当前执行中的 JIT2 产物（单线程）

// 把 JIT 槽值写回帧寄存器：按静态 langtype 选写路径（bool 不能当 int64 落）。
bool slot_store_typed(vmbin::Frame* f, const vmbin::Program* seg, std::size_t r,
                      std::int64_t v) {
    const RegDesc& d = seg->regs[r];
    if (d.langtype == vmbin::LangType::Bool) {
        if (g_vmkv.slot_write_bool(*f, static_cast<std::uint8_t>(r), v != 0))
            return true;
    } else if (d.langtype == vmbin::LangType::Int64 ||
               d.langtype == vmbin::LangType::Uint64 ||
               d.langtype == vmbin::LangType::None) {
        if (g_vmkv.slot_write_i64(*f, static_cast<std::uint8_t>(r), v))
            return true;
    } else if (d.langtype == vmbin::LangType::Float64) {
        Scalar sc{};
        sc.k = Scalar::K::Float;
        sc.width = 8;
        std::memcpy(&sc.f, &v, sizeof sc.f);
        if (g_vmkv.slot_write_scalar(*f, static_cast<std::uint8_t>(r), sc, 8)) {
            f->regs[r].fast_i64 = false;
            f->regs[r].fast_bool = false;
            return true;
        }
    } else {
        return false;  // 字符串：原生侧不产生这类值，交给 kvspace 原值
    }
    if (d.langtype != vmbin::LangType::Float64) {
        f->regs[r].fv = v;
        f->regs[r].fast_i64 = true;
    }
    return true;
}

int jit2_handler_call(vmbin::Frame* f, jitabi::ValueSlotV1* slots,
                      std::uint32_t idx) {
    const Jit2Entry* entry = g_jit2_running;
    if (std::getenv("RC2_HDBG2") && f)
        std::fprintf(stderr,
                     "[hcall f=%p entry=%p seg=%p idx=%u dirlen=%zu fa=%zu]\n",
                     (void*)f, (void*)entry, (void*)f->seg_hint, idx,
                     f->func_dir.size(),
                     (std::size_t)(uintptr_t)__builtin_frame_address(0) & 15);
    if (!f || !entry || !f->seg_hint)
        return 1;
    const Segment* seg = f->seg_hint;
    if (idx >= seg->insns.size())
        return 1;
    const CInsn& ins = seg->insns[idx];
    if (ins.handler == rwir::RwirHandlerId::None)
        return 1;
    const Jit2Plan& plan = entry->plan;
    const std::vector<char>& staged = plan.native_stage[idx];
    // 槽 → 帧：把活跃寄存器的值放回解释器寄存器（帧局部走 fast 缓存）。
    for (std::size_t r = 0; r < seg->regs.size(); r++) {
        if (!staged[r] || seg->regs[r].kind != Operand::Kind::Var)
            continue;
        if (seg->regs[r].array_type_id != vmbin::kArrayTypeNone)
            continue;  // 数组以帧内 Register 的 key/view 为准
        const std::int64_t v = static_cast<std::int64_t>(slots[r].payload_lo);
        slot_store_typed(f, seg, r, v);
    }
    if (g_jitdbg) {
        for (std::size_t r = 0; r < seg->regs.size(); r++) {
            if (!staged[r] || seg->regs[r].kind != Operand::Kind::Var ||
                seg->regs[r].array_type_id == vmbin::kArrayTypeNone)
                continue;
            std::uint8_t* before = nullptr;
            std::uint64_t bcap = 0;
            (void)g_vmkv.reg_body(*f, static_cast<std::uint8_t>(r), &before, &bcap);
            std::fprintf(stderr,
                         "[arrstage pc=%u r=%zu slot=%llx frame=%p name=%s]\n",
                         idx, r,
                         (unsigned long long)slots[r].payload_lo,
                         (void*)before, seg->regs[r].name.c_str());
        }
    }
    const RwirHandlerSlot& slot = rwir_handler(ins.handler);
    RwirHandlerCtx ctx{g_vmkv, *f, seg, ins, static_cast<int>(idx) + 1, {}, 0, 0};
    std::string err;
    for (int i = 0; slot.prefetch_operands && i < ins.nrd && i < 2; i++)
        if (!g_vmkv.read_reg(*f, ins.rd[i], &ctx.in[i])) {
            if (g_jitdbg)
                std::fprintf(stderr,
                             "[jit2dbg handler-opfail %s pc=%u op=%d]\n",
                             f->func_dir.c_str(), idx, (int)ins.rd[i]);
            return 1;
        }
    if (!slot.handle(ctx, &err)) {
        if (g_jitdbg)
            std::fprintf(stderr, "[jit2dbg handler-fail %s pc=%u err=%s]\n",
                         f->func_dir.c_str(), idx, err.c_str());
        return 1;
    }
    if (!ctx.yield_pc.empty())
        return 2;  // vthread·run 冒泡：交回解释器出口
    // 帧 → 槽：handler 写回的值（含数组 body 指针）同步给原生代码。
    for (std::size_t r = 0; r < seg->regs.size(); r++) {
        if (!staged[r] || seg->regs[r].kind != Operand::Kind::Var)
            continue;
        if (seg->regs[r].array_type_id != vmbin::kArrayTypeNone) {
            std::uint8_t* body = nullptr;
            std::uint64_t cap = 0;
            if (!g_vmkv.reg_body(*f, static_cast<std::uint8_t>(r), &body, &cap) ||
                !body || !array_body_fits(*seg, r, cap))
                return 2;
            slots[r].payload_lo = reinterpret_cast<std::uint64_t>(body);
            slots[r].tag = jitabi::ValueTagV1::I64;
            if (g_jitdbg)
                std::fprintf(stderr, "[arrreload pc=%u r=%zu body=%p]\n", idx, r,
                             (void*)body);
            continue;
        }
        std::int64_t v = 0;
        if (seg->regs[r].langtype == vmbin::LangType::Float64) {
            // float64：按原始 8 字节取位（不经过 int64 解释）。
            std::uint8_t* body = nullptr;
            std::uint64_t cap = 0;
            if (!g_vmkv.reg_body(*f, static_cast<std::uint8_t>(r), &body, &cap) ||
                !body || cap < 8) {
                // 尚未定义的 may-live float 寄存器没有帧值：跳过（不当作失败）。
                const std::vector<char>& defined = plan.out_defined[idx];
                if (r < defined.size() && !defined[r])
                    continue;
                if (g_jitdbg)
                    std::fprintf(stderr,
                                 "[jit2dbg syncfail %s pc=%u r=%zu lt=%d]\n",
                                 f->func_dir.c_str(), idx, r,
                                 (int)seg->regs[r].langtype);
                return 2;
            }
            std::memcpy(&v, body, 8);
            slots[r].payload_lo = static_cast<std::uint64_t>(v);
            slots[r].tag = jitabi::ValueTagV1::I64;
            continue;
        }
        if (f->regs[r].fast_i64)
            v = f->regs[r].fv;
        else if (!g_vmkv.read_i64(*f, static_cast<std::uint8_t>(r), &v)) {
            // 非数值 8 字节值（time/duration 等 langtype 不可知）：读 body 前 8 字节。
            std::uint8_t* body = nullptr;
            std::uint64_t cap = 0;
            if (g_vmkv.reg_body(*f, static_cast<std::uint8_t>(r), &body, &cap) &&
                body && cap >= 8) {
                std::memcpy(&v, body, 8);
            } else {
                if (g_jitdbg)
                    std::fprintf(stderr,
                                 "[jit2dbg syncfail %s pc=%u r=%zu lt=%d]\n",
                                 f->func_dir.c_str(), idx, r,
                                 (int)seg->regs[r].langtype);
                return 2;
            }
        }
        slots[r].payload_lo = static_cast<std::uint64_t>(v);
        slots[r].tag = jitabi::ValueTagV1::I64;
    }
    return 0;
}

}  // namespace

bool jit2_try_run(Frame& f, const Segment* seg, int start_s0, Outcome* out,
                  std::string* stop_pc, std::string* err) {
    if (g_jitdbg)
        std::fprintf(stderr, "[jit2dbg try %s s0=%d g=%d e=%p]\n",
                     f.func_dir.c_str(), start_s0, (int)g_jit2,
                     f.jit2_entry);
    if (!g_jit2 || start_s0 != 1)
        return false;
    Jit2Entry* entry =
        f.jit2_entry
            ? reinterpret_cast<Jit2Entry*>(const_cast<void*>(f.jit2_entry))
            : jit2_lookup(f.func_dir, seg);
    if (!entry)
        return false;
    const Jit2Shape& shape = entry->plan.shape;
    // slots 只被 JIT 写（输出/物化）或由适配器填（输入/预载）；JIT 不会读未填槽，
    // 因此无需每次清零。
    std::unique_ptr<jitabi::ValueSlotV1[]> big_slots;
    jitabi::ValueSlotV1* slots = g_jit2_slots;
    if (seg->regs.size() > 64) {
        big_slots = std::make_unique<jitabi::ValueSlotV1[]>(seg->regs.size());
        std::memset(big_slots.get(), 0,
                    sizeof(jitabi::ValueSlotV1) * seg->regs.size());
        slots = big_slots.get();
    }
    for (int r : shape.inputs) {
        Register& r0 = f.regs[static_cast<std::size_t>(r)];
        // compact 数组参数：槽里放 body 指针（内联寻址的唯一运行时值）。
        if (seg->regs[static_cast<std::size_t>(r)].array_type_id !=
            vmbin::kArrayTypeNone) {
            std::uint8_t* body = nullptr;
            std::uint64_t cap = 0;
            if (!g_vmkv.reg_body(f, static_cast<std::uint8_t>(r), &body, &cap) ||
                !body || !array_body_fits(*seg, static_cast<std::size_t>(r), cap)) {
                if (g_jitdbg)
                    std::fprintf(stderr, "[jit2dbg input-fail %s r=%d array]\n",
                                 f.func_dir.c_str(), r);
                return false;
            }
            slots[static_cast<std::size_t>(r)].payload_lo =
                reinterpret_cast<std::uint64_t>(body);
            slots[static_cast<std::size_t>(r)].tag = jitabi::ValueTagV1::I64;
            continue;
        }
        Register* p = r0.src_ptr ? r0.src_ptr : &r0;
        while (!p->fast_i64 && p->src_ptr)
            p = p->src_ptr;
        std::int64_t value = 0;
        if (p->fast_i64) {
            value = p->fv;
        } else if (!g_vmkv.read_i64(f, static_cast<std::uint8_t>(r), &value)) {
            // 非纯内存帧（含 handler 的段）参数走 kvspace 槽：用解释器同一条读路径装载。
            if (g_jitdbg)
                std::fprintf(stderr, "[jit2dbg input-fail %s r=%d]\n",
                             f.func_dir.c_str(), r);
            return false;
        }
        slots[static_cast<std::size_t>(r)].payload_lo =
            static_cast<std::uint64_t>(value);
        slots[static_cast<std::size_t>(r)].tag = jitabi::ValueTagV1::I64;
    }
    // 读-写写参：调用方预载 + 可读性证明；未证明 → 回退解释器（与解释器报错一致）。
    for (int r : shape.outputs) {
        if (!entry->plan.output_preload[static_cast<std::size_t>(r)])
            continue;
        // 可读性证明：先沿写参内存别名链（dst_ptr/src_ptr）取调用方 fast 值
        // （`p + 1 -> p` 的 p 在调用方 reg 里是 fast，未落槽）；
        // 无 fast 值时用解释器同一条读路径读本帧写参槽。
        Register& pr0 = f.regs[static_cast<std::size_t>(r)];
        Register* pr = &pr0;
        std::int64_t initial = 0;
        bool got = false;
        for (;;) {
            if (pr->fast_i64) {
                initial = pr->fv;
                got = true;
                break;
            }
            Register* next = pr->dst_ptr ? pr->dst_ptr : pr->src_ptr;
            if (!next)
                break;
            pr = next;
        }
        // 别名链尽头是调用方 reg（值可能只在 fast 缓存、未落槽）：按父帧下标
        // 走解释器同一条读数路径。
        if (!got && f.parent && !f.parent->regs.empty()) {
            Register* base = f.parent->regs.data();
            const std::size_t cnt = f.parent->regs.size();
            if (pr >= base && pr < base + cnt)
                got = g_vmkv.read_i64(
                    *f.parent,
                    static_cast<std::uint8_t>(pr - base), &initial);
        }
        if (!got &&
            !g_vmkv.read_i64(f, static_cast<std::uint8_t>(r), &initial)) {
            if (g_jitdbg)
                std::fprintf(stderr, "[jit2dbg preload-fail %s r=%d]\n",
                             f.func_dir.c_str(), r);
            return false;
        }
        slots[static_cast<std::size_t>(r)].payload_lo =
            static_cast<std::uint64_t>(initial);
        slots[static_cast<std::size_t>(r)].tag = jitabi::ValueTagV1::I64;
    }
    g_jit2_frame.slots = slots;
    g_jit2_frame.slot_count = static_cast<std::uint32_t>(seg->regs.size());
    g_jit2_frame.pc = 1;
    g_jit2_frame.legacy_frame_adapter = &f;  // 原生内调 handler 的 Frame
    g_jit2_context.reserved_u64[0] =
        reinterpret_cast<std::uint64_t>(&jit2_handler_call);
    jitabi::StructuredExitV1 exit_record;
    view_cache_idle_all();
    g_jit2_running = entry;
    reinterpret_cast<Jit2Fn>(entry->code)(&g_jit2_context, &g_jit2_frame,
                                          &exit_record);
    g_jit2_running = nullptr;
    if (g_jitdbg)
        std::fprintf(stderr, "[jit2dbg exit %s kind=%u pc=%u detail=%u]\n",
                     f.func_dir.c_str(), (unsigned)exit_record.kind,
                     exit_record.pc, exit_record.detail_code);
    if (exit_record.kind == jitabi::ExitKindV1::Error) {
        // 出错指令行（printstack cur / stop_pc 契约与解释器一致）。
        if (stop_pc && exit_record.pc >= 1)
            *stop_pc = g_vmkv.param_key(f.frame, static_cast<int>(exit_record.pc), 0);
        if (exit_record.detail_code == 11)
            *err = "division by zero: " + std::string(
                       exit_record.pc >= 1 && exit_record.pc <= seg->insns.size()
                           ? instruction_name(*seg, seg->insns[exit_record.pc - 1])
                           : "int64·div");
        else if (exit_record.detail_code == 10)
            *err = "xv: index out of range";
        else if (exit_record.detail_code == 9)
            *err = "jit2 handler failed: " + f.func_dir;
        else
            *err = exit_record.detail_code == jitabi::kDetailRunaway
                       ? "runaway loop (jit2): " + f.func_dir
                       : "jit2 commit failed: " + f.func_dir;
        *out = Outcome::Error;
        return true;
    }
    if (exit_record.kind == jitabi::ExitKindV1::Native) {
        if (g_jitdbg)
            std::fprintf(stderr, "[jit2dbg native %s pc=%u aux0=%llu]\n",
                         f.func_dir.c_str(), exit_record.pc,
                         (unsigned long long)exit_record.aux0);
        for (std::size_t i = 0; i < seg->regs.size(); i++)
            if (seg->regs[i].kind == Operand::Kind::Var) {
                if (seg->regs[i].langtype == vmbin::LangType::Float64) {
                    // float64 不是 int64 快缓存：位模式不能被当整数暴露给续跑。
                    f.regs[i].fast_i64 = false;
                    f.regs[i].fast_bool = false;
                    continue;
                }
                f.regs[i].fv = static_cast<std::int64_t>(slots[i].payload_lo);
                f.regs[i].fast_i64 = true;
            }
        // 出原生前把物化槽回填到解释器帧（续跑读的是 f.regs）。
        const std::size_t hi = exit_record.pc > 0 ? exit_record.pc - 1 : 0;
        if (hi < seg->insns.size()) {
            const std::vector<char>& staged = entry->plan.native_stage[hi];
            for (std::size_t r = 0; r < seg->regs.size(); r++) {
                if (!staged[r] || seg->regs[r].kind != Operand::Kind::Var)
                    continue;
                if (seg->regs[r].array_type_id != vmbin::kArrayTypeNone)
                    continue;
                const std::int64_t v =
                    static_cast<std::int64_t>(slots[r].payload_lo);
                slot_store_typed(&f, seg, r, v);
                if (g_jitdbg && !seg->regs[r].name.empty())
                    std::fprintf(stderr,
                                 "[exitstage pc=%u r=%zu name=%s v=%lld]\n",
                                 exit_record.pc, r, seg->regs[r].name.c_str(),
                                 (long long)v);
            }
        }
        f.jit_known = true;
        f.jit_hint = nullptr;
        f.skip_enter_frame = true;
        const Outcome resumed =
            g_vmkv.run_frame(f, static_cast<int>(exit_record.pc), stop_pc, err);
        f.skip_enter_frame = false;
        *out = resumed;
        return true;
    }
    // Return：提交写参（slots[reg].payload_lo）。走解释器同一条写路径：
    // 内存别名帧贯通 dst_ptr，kvspace 绑定帧（含 handler 的段）解析 Ptr→调用方槽。
    for (int r : shape.outputs) {
        const std::int64_t value =
            static_cast<std::int64_t>(slots[static_cast<std::size_t>(r)].payload_lo);
        if (!slot_store_typed(&f, seg, static_cast<std::size_t>(r), value)) {
            *err = "jit2 commit failed: " + f.func_dir;
            *out = Outcome::Error;
            return true;
        }
    }
    if (stop_pc)
        stop_pc->clear();
    *out = f.depth <= 1 ? Outcome::Ok : Outcome::Returned;
    return true;
}

}  // namespace

bool callee_int_params(const Segment* seg) {
    for (const RegDesc& d : seg->regs)
        if (d.kind == Operand::Kind::Var && d.param_k != 0 &&
            d.langtype != vmbin::LangType::Int64 &&
            d.langtype != vmbin::LangType::Uint64)
            return false;
    return true;
}

bool jit_enabled() { return !g_nojit; }

bool jit2_enabled() { return g_jit2 && !g_nojit; }

const void* jit2_entry(const std::string& func_dir, const Segment* seg) {
    return g_jit2 && !g_nojit ? jit2_lookup(func_dir, seg) : nullptr;
}

bool jit2_accepted(const std::string& func_dir, const Segment* seg) {
    return jit2_entry(func_dir, seg) != nullptr;
}

void jit_clear_cache() {
    jit_cache().clear();
    jit2_cache().clear();
}

bool jit_verify(const Segment* seg, JitRefusal* refusal) {
    *refusal = JitRefusal{};
    JitShape sh;
    return verify(seg, &sh, refusal);
}

const void* jit_compile(const std::string& func_dir, const Segment* seg) {
    auto& cache = jit_cache();
    auto it = cache.find(func_dir);
    if (it != cache.end())
        return reinterpret_cast<const void*>(it->second);
    JitShape sh;
    JitRefusal refusal;
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg verify %s]\n", func_dir.c_str());
    if (!verify(seg, &sh, &refusal)) {
        if (g_jitdbg) {
            std::fprintf(stderr, "[jitdbg verify FAIL %s]\n", func_dir.c_str());
            std::fprintf(stderr, "[jitdbg refuse reason=%u detail=%u]\n",
                         static_cast<unsigned>(refusal.refusal_reason),
                         refusal.detail_code);
        }
        cache[func_dir] = nullptr;
        return nullptr;
    }
    if (g_jitdbg)
        std::fprintf(stderr, "[jitdbg emit %s src=%d dst=%d]\n", func_dir.c_str(),
                     sh.src_idx, sh.dst_idx);
    std::vector<std::uint8_t> bytes = emit_arch(seg, sh);
    if (bytes.empty()) {
        if (g_jitdbg)
            std::fprintf(stderr, "[jitdbg refuse reason=%u detail=0]\n",
                         static_cast<unsigned>(
                             VmjitRefusalReason::UnsupportedBackend));
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
    // 调度：M1 优先（适配更轻）；M1 拒绝的形状交给 JIT2。
    const void* code = jit_compile(f.func_dir, seg);
    if (!code)
        return jit2_try_run(f, seg, start_s0, out, stop_pc, err);
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
    if (jit_abi_enabled()) {
        JitAbiLink link{&f, seg, static_cast<std::uint8_t>(src_idx),
                        static_cast<std::uint8_t>(dst_idx)};
        if (jit_abi_eligible(link)) {
            jitabi::StructuredExitV1 exit{};
            jit_abi_run(link, code, &exit);
            if (exit.kind == jitabi::ExitKindV1::Error) {
                *err = exit.detail_code == jitabi::kDetailRunaway
                           ? "runaway loop (jit): " + f.func_dir
                           : "jit commit failed: " + f.func_dir;
                *out = Outcome::Error;
                return true;
            }
            if (stop_pc)
                stop_pc->clear();
            *out = f.depth <= 1 ? Outcome::Ok : Outcome::Returned;
            return true;
        }
    }
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
    if (dst->view_ok && dst->body &&
        !g_vmkv.store_i64_slot(f, *dst, res)) {
        *err = "jit commit failed: " + f.func_dir;
        *out = Outcome::Error;
        return true;
    }
    dr0.fv = res;
    dr0.fast_i64 = true;
    if (stop_pc)
        stop_pc->clear();
    *out = f.depth <= 1 ? Outcome::Ok : Outcome::Returned;
    return true;
}

}  // namespace kvlang::runtimec2
