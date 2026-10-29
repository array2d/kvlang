#include "jit.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

// x86_64 JIT 发射器（M1：add + 比较 + copy + 控制流）。
// 寄存器：RDI=读参、RAX=结果/写参、RCX=guard 计数、RSI=err_out、局部 R8..R15。
// 全部 rel32 相对跳转（位置无关）。jit.cpp 经 emit_arch 调用本文件。

namespace kvlang::runtimec2 {
namespace {

// ── 最小 x86_64 发射器 ─────────────────────────────────────────────
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

struct Fixup {
    std::size_t pos = 0;
    int target = 0;  // 目标 s0；0 = guard stub
};

}  // namespace

// 发射：M1 形状（add + 比较 + copy + goto/br + return）。
std::vector<std::uint8_t> emit_arch(const Segment* seg, const JitShape& sh) {
#if defined(__x86_64__)
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
    c.mov_imm(kRCX, static_cast<std::int32_t>(kJitGuardBudget));
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
    if (std::getenv("RC2_JITDUMP")) {
        std::fprintf(stderr, "[jitdump %zu bytes]", c.b.size());
        for (std::uint8_t x : c.b)
            std::fprintf(stderr, " %02x", x);
        std::fprintf(stderr, "\n");
    }
    return std::move(c.b);
#else
    (void)seg;
    (void)sh;
    return {};  // 非 x86_64：不可发射（jit_arm.cpp 覆盖 ARM64）
#endif
}

}  // namespace kvlang::runtimec2
