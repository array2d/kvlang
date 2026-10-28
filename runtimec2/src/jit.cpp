#include "jit.hpp"
#include "valueio.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/mman.h>

namespace kv::rt {
namespace {

using bin::FReg;
using bin::FrameCtx;
using bin::RunResult;

bool g_nojit = std::getenv("RC2_NOJIT") != nullptr;
bool g_jitdump = std::getenv("RC2_JITDUMP") != nullptr;
bool g_jitdbg = std::getenv("RC2_JITDBG") != nullptr;

// ── 最小 x86_64 发射器（M1：add + 比较 + copy + 控制流）──────────────
// 寄存器：RDI=读参、RAX=结果/写参、RCX=guard 计数、RSI=err_out、局部 R8..R15。
// 全部 rel32 相对跳转（位置无关），mmap RW → mprotect RX。

struct XReg {
    std::uint8_t field = 0;
    bool rb = false;  // r/m 需 REX.B
    bool rr = false;  // reg 字段需 REX.R
    bool operator==(const XReg& o) const {
        return field == o.field && rb == o.rb && rr == o.rr;
    }
};

constexpr XReg kRAX{0}, kRCX{1}, kRDI{7}, kRSI{6};
// R8-R15 任一字段（reg/rm）都要 REX 位：rb 管 rm、rr 管 reg，两者都置。
const XReg kLocals[] = {
    {0, true, true}, {1, true, true}, {2, true, true}, {3, true, true},
    {4, true, true}, {5, true, true}, {6, true, true}, {7, true, true},
};

struct Code {
    std::vector<std::uint8_t> b;
    void w8(std::uint8_t v) { b.push_back(v); }
    void w32(std::int32_t v) {
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        for (int i = 0; i < 4; i++) b.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
    }
    // REX.W 固定；R 位管 reg 字段寄存器、B 位管 r/m 字段寄存器。
    void rexw(const XReg& reg, const XReg& rm) {
        w8(static_cast<std::uint8_t>(0x48 | (reg.rr ? 4 : 0) | (rm.rb ? 1 : 0)));
    }
    void modrm(std::uint8_t reg, const XReg& rm) {
        w8(static_cast<std::uint8_t>((3 << 6) | ((reg & 7) << 3) | (rm.field & 7)));
    }
    void mov_imm(const XReg& d, std::int32_t v) {  // mov r64, imm32
        rexw(kRAX, d);
        w8(0xC7);
        modrm(0, d);
        w32(v);
    }
    void mov(const XReg& d, const XReg& s) {  // mov r64, r/m64：reg=目的、r/m=源
        rexw(d, s);
        w8(0x8B);
        modrm(d.field, s);
    }
    void add_imm(const XReg& d, std::int32_t v) {  // add r64, imm8/32
        if (v >= -128 && v <= 127) {
            rexw(kRAX, d);
            w8(0x83);
            modrm(0, d);
            w8(static_cast<std::uint8_t>(v));
        } else {
            rexw(kRAX, d);
            w8(0x81);
            modrm(0, d);
            w32(v);
        }
    }
    void add(const XReg& d, const XReg& s) {  // add r/m64, r64
        rexw(s, d);
        w8(0x01);
        modrm(s.field, d);
    }
    void sub_imm(const XReg& d, std::int32_t v) {  // sub r64, imm8（guard 递减）
        rexw(kRAX, d);
        w8(0x83);
        modrm(5, d);
        w8(static_cast<std::uint8_t>(v));
    }
    void cmp(const XReg& rm, const XReg& r) {  // cmp r/m64, r64
        rexw(r, rm);
        w8(0x39);
        modrm(r.field, rm);
    }
    void cmp_imm(const XReg& rm, std::int32_t v) {  // cmp r64, imm8/32
        if (v >= -128 && v <= 127) {
            rexw(kRAX, rm);
            w8(0x83);
            modrm(7, rm);
            w8(static_cast<std::uint8_t>(v));
        } else {
            rexw(kRAX, rm);
            w8(0x81);
            modrm(7, rm);
            w32(v);
        }
    }
    void test(const XReg& rm) {  // test r64, r64
        rexw(rm, rm);
        w8(0x85);
        modrm(rm.field, rm);
    }
    void setcc(std::uint8_t cc, const XReg& d) {  // setcc r8
        rexw(kRAX, d);
        w8(0x0F);
        w8(cc);
        modrm(0, d);
    }
    void jcc(std::uint8_t cc) {  // jcc rel32（占位，后修补）
        w8(0x0F);
        w8(cc);
        w32(0);
    }
    void jmp() {  // jmp rel32（占位）
        w8(0xE9);
        w32(0);
    }
    void ret() { w8(0xC3); }
};

// opfn 13-18 → jcc / setcc 变体。
std::uint8_t jcc_of(std::uint8_t opfn) {
    switch (opfn) {
    case 13: return 0x84;  // eq → je
    case 14: return 0x85;  // ne → jne
    case 15: return 0x8C;  // lt → jl
    case 16: return 0x8E;  // le → jle
    case 17: return 0x8F;  // gt → jg
    default: return 0x8D;  // ge → jge
    }
}
std::uint8_t setcc_of(std::uint8_t opfn) {
    switch (opfn) {
    case 13: return 0x94;  // sete
    case 14: return 0x95;  // setne
    case 15: return 0x9C;  // setl
    case 16: return 0x9E;  // setle
    case 17: return 0x9F;  // setg
    default: return 0x9D;  // setge
    }
}

// ── 静态判定（M1 形状）──────────────────────────────────────────────

constexpr std::int64_t kGuardBudget = 16 * 1024 * 1024;
constexpr std::int64_t kInt32Min = -2147483647LL - 1;
constexpr std::int64_t kInt32Max = 2147483647LL;

struct Shape {
    int src_idx = -1;
    int dst_idx = -1;
};

bool verify(const Segment* seg, Shape* sh) {
    if (seg->has_native || seg->insns.empty())
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
        if (ci.op == ops::Op::Return) {
            if (ci.nrd != 0 || ci.nwr != 0 || ii + 1 != seg->insns.size())
                return false;
            continue;
        }
        if (ci.op == ops::Op::Goto || ci.op == ops::Op::Br) {
            const bool br = ci.op == ops::Op::Br;
            if (ci.impl != ops::Impl::Control || (br && ci.nrd != 3) ||
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
        if (ci.op == ops::Op::Copy) {
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

// ── 发射 ───────────────────────────────────────────────────────────

struct Fixup {
    std::size_t pos = 0;
    int target = 0;  // 目标 s0；0 = guard stub
};

std::vector<std::uint8_t> emit(const Segment* seg, const Shape& sh) {
    const std::size_t n = seg->insns.size();
    Code c;
    std::vector<std::size_t> start(n, 0);
    std::vector<Fixup> fixes;
    std::unordered_map<int, XReg> loc;
    int loc_cur = 0;
    auto reg_of = [&](int idx) -> std::optional<XReg> {
        const RegDesc& d = seg->regs[idx];
        if (d.kind == Operand::Kind::Literal)
            return std::nullopt;
        if (idx == sh.src_idx)
            return kRDI;
        if (idx == sh.dst_idx)
            return kRAX;
        auto it = loc.find(idx);
        if (it != loc.end())
            return it->second;
        if (loc_cur >= static_cast<int>(sizeof(kLocals) / sizeof(kLocals[0])))
            return std::nullopt;
        const XReg r = kLocals[loc_cur++];
        loc[idx] = r;
        return r;
    };
    auto lit = [&](int idx) -> std::int64_t { return seg->lit[idx].v; };
    auto is_lit = [&](int idx) -> bool {
        return seg->regs[idx].kind == Operand::Kind::Literal;
    };
    // 比较+br 融合：compare[i] 融合 iff 下一条是 br 读其 wr 且 wr 只被该 br 读
    std::vector<bool> fused(n, false);
    for (std::size_t i = 0; i + 1 < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (!ci.fast_i64 || ci.opfn < 13 || ci.opfn > 18)
            continue;
        const CInsn& nx = seg->insns[i + 1];
        if (nx.op != ops::Op::Br || nx.rd[0] != ci.wr[0])
            continue;
        bool only_here = true;
        for (const CInsn& x : seg->insns)
            for (int k = 0; k < x.nrd; k++)
                if (x.rd[k] == ci.wr[0] && &x != &nx)
                    only_here = false;
        fused[i] = only_here;
    }
    // 入口：guard 计数 + 写参初值（对齐 enter_frame fv=0）
    c.mov_imm(kRCX, static_cast<std::int32_t>(kGuardBudget));
    c.mov_imm(kRAX, 0);
    for (std::size_t i = 0; i < n; i++) {
        start[i] = c.b.size();
        const CInsn& ci = seg->insns[i];
        if (ci.op == ops::Op::Return) {
            c.ret();
            continue;
        }
        if (ci.op == ops::Op::Goto || ci.op == ops::Op::Br) {
            const bool br = ci.op == ops::Op::Br;
            const int t1 = static_cast<int>(lit(ci.rd[br ? 1 : 0]));
            const int t2 = br ? static_cast<int>(lit(ci.rd[2])) : 0;
            const bool backward =
                t1 < ci.s0 || (br && t2 < ci.s0);
            if (backward) {
                c.sub_imm(kRCX, 1);
                c.jcc(0x84);  // jz guard
                fixes.push_back({c.b.size() - 4, 0});
            }
            if (!br) {
                c.jmp();
                fixes.push_back({c.b.size() - 4, t1});
                continue;
            }
            if (i > 0 && fused[i - 1]) {
                const std::uint8_t cc = jcc_of(seg->insns[i - 1].opfn);
                if (t1 == ci.s0 + 1) {  // true 落下一指令 → 只跳 false
                    c.jcc(static_cast<std::uint8_t>(cc ^ 1));
                    fixes.push_back({c.b.size() - 4, t2});
                } else if (t2 == ci.s0 + 1) {  // false 落下一指令 → 只跳 true
                    c.jcc(cc);
                    fixes.push_back({c.b.size() - 4, t1});
                } else {
                    c.jcc(cc);
                    fixes.push_back({c.b.size() - 4, t1});
                    c.jmp();
                    fixes.push_back({c.b.size() - 4, t2});
                }
            } else {
                const XReg cr = *reg_of(ci.rd[0]);
                c.test(cr);
                c.jcc(0x85);  // jne t1
                fixes.push_back({c.b.size() - 4, t1});
                c.jmp();
                fixes.push_back({c.b.size() - 4, t2});
            }
            continue;
        }
        if (ci.fast_i64) {
            if (ci.opfn >= 13 && ci.opfn <= 18) {
                // 比较：写 opfn 到 wr（融合时无实写，flags 直达 br）
                const XReg a = *reg_of(ci.rd[0]);
                if (is_lit(ci.rd[1]))
                    c.cmp_imm(a, static_cast<std::int32_t>(lit(ci.rd[1])));
                else
                    c.cmp(a, *reg_of(ci.rd[1]));
                if (!fused[i]) {
                    c.setcc(setcc_of(ci.opfn), *reg_of(ci.wr[0]));
                }
            } else {
                // add（opfn 1）：dst = a + b
                const XReg d = *reg_of(ci.wr[0]);
                if (is_lit(ci.rd[0])) {
                    c.mov_imm(d, static_cast<std::int32_t>(lit(ci.rd[0])));
                    if (is_lit(ci.rd[1]))
                        c.add_imm(d, static_cast<std::int32_t>(lit(ci.rd[1])));
                    else
                        c.add(d, *reg_of(ci.rd[1]));
                } else {
                    const XReg a = *reg_of(ci.rd[0]);
                    if (!(d == a))
                        c.mov(d, a);
                    if (is_lit(ci.rd[1]))
                        c.add_imm(d, static_cast<std::int32_t>(lit(ci.rd[1])));
                    else
                        c.add(d, *reg_of(ci.rd[1]));
                }
            }
            continue;
        }
        if (ci.op == ops::Op::Copy) {
            const XReg d = *reg_of(ci.wr[0]);
            if (is_lit(ci.rd[0]))
                c.mov_imm(d, static_cast<std::int32_t>(lit(ci.rd[0])));
            else
                c.mov(d, *reg_of(ci.rd[0]));
            continue;
        }
        return {};
    }
    // guard stub（fixup target=0）：err_out = 1；eax = 0；ret
    const std::size_t guard_stub = c.b.size();
    c.rexw(kRAX, kRSI);
    c.w8(0xC7);
    c.modrm(0, kRSI);
    c.w32(1);
    c.w8(0xB8);
    c.w32(0);
    c.ret();
    // 修补跳转
    for (const Fixup& fx : fixes) {
        const std::size_t tgt = fx.target == 0 ? guard_stub : start[fx.target - 1];
        const std::int32_t rel = static_cast<std::int32_t>(tgt) -
                                 static_cast<std::int32_t>(fx.pos + 4);
        std::memcpy(c.b.data() + fx.pos, &rel, 4);
    }
    if (g_jitdump) {
        std::fprintf(stderr, "[jitdump %zu bytes]", c.b.size());
        for (std::uint8_t x : c.b)
            std::fprintf(stderr, " %02x", x);
        std::fprintf(stderr, "\n");
    }
    return std::move(c.b);
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
    Shape sh;
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
    std::vector<std::uint8_t> bytes = emit(seg, sh);
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

bool jit_try_run(FrameCtx& f, const Segment* seg, int start_s0,
                 RunResult* out, std::string* stop_pc, std::string* err) {
    *out = RunResult::Ok;
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
    FReg& sr0 = f.regs[src_idx];
    FReg* src = sr0.src_ptr ? sr0.src_ptr : &sr0;
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
        *out = RunResult::Error;
        return true;
    }
    // 结果回写（镜像 write_reg 的 dst_ptr 分支：fast 缓存 + 已有槽直写）
    FReg& dr0 = f.regs[dst_idx];
    FReg* dst = dr0.dst_ptr ? dr0.dst_ptr : &dr0;
    dst->fv = res;
    dst->fast_i64 = true;
    if (dst->view_ok && dst->body)
        std::memcpy(dst->body, &res, 8);
    dr0.fv = res;
    dr0.fast_i64 = true;
    if (stop_pc)
        stop_pc->clear();
    *out = f.depth <= 1 ? RunResult::Ok : RunResult::Returned;
    return true;
}

}  // namespace kv::rt
