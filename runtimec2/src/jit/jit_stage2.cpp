// 第二阶段最小原型：CFG + 活跃分析 + 线性扫描分配（含栈溢出）+ x86_64 发射。
// 与第一阶段共用 opcode 白名单；放宽形状为 0..15 读参、0..4 写参、任意 reg 下标。
// 入口 ABI：void fn(const int64_t* args, int64_t* outs, int64_t* err);
#include "jit_stage2.hpp"

#include "fast_opfn.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kvlang::runtimec2 {
namespace {

using vmbin::Register;

constexpr std::int64_t kInt32Min = -2147483647LL - 1;
constexpr std::int64_t kInt32Max = 2147483647LL;
constexpr std::int64_t kJit2GuardBudget = 16 * 1024 * 1024;

// ── x86_64 编码 ──────────────────────────────────────────────────
struct X2 {
    std::uint8_t field = 0;
    bool rb = false;  // r/m 需 REX.B
    bool rr = false;  // reg 字段需 REX.R
};

constexpr X2 kRax{0}, kRcx{1}, kRdx{2}, kRsi{6}, kRdi{7};
constexpr X2 kRbx{3, false, false}, kRbp{5, false, false};
constexpr X2 kR8{0, true, true}, kR9{1, true, true}, kR10{2, true, true},
    kR11{3, true, true}, kR12{4, true, true}, kR13{5, true, true},
    kR14{6, true, true}, kR15{7, true, true};
// 第 1 批裁决：R13=frame、R14=slots；值池用 callee-saved
// （RBX/RBP/R12/R15），RAX 作额外 caller-saved 槽以降低溢出。
constexpr X2 kPool[5] = {kRax, kRbx, kRbp, kR12, kR15};
constexpr int kPoolSize = 5;

struct Code2 {
    std::vector<std::uint8_t> b;
    void w8(std::uint8_t v) { b.push_back(v); }
    void w32(std::int32_t v) {
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        for (int i = 0; i < 4; i++)
            b.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
    }
    void rexw(const X2& reg, const X2& rm) {
        w8(static_cast<std::uint8_t>(0x48 | (reg.rr ? 4 : 0) | (rm.rb ? 1 : 0)));
    }
    void modrm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
        w8(static_cast<std::uint8_t>((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
    }
    void sib_rsp() { w8(0x24); }  // scale=0, index=100(none), base=100(rsp)

    void mov_imm64(const X2& d, std::int64_t v) {  // movabs r64, imm64
        rexw(kRax, d);
        w8(0xB8 + (d.field & 7));
        for (int i = 0; i < 8; i++)
            w8(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF));
    }
    void mov_imm(const X2& d, std::int32_t v) {  // mov r64, imm32（符号扩展）
        rexw(kRax, d);
        w8(0xC7);
        modrm(3, 0, d.field);
        w32(v);
    }
    void mov(const X2& d, const X2& s) {
        rexw(d, s);
        w8(0x8B);
        modrm(3, d.field, s.field);
    }
    void sub(const X2& d, const X2& s) {
        rex(true, s, d);
        w8(0x29);
        modrm(3, s.field, d.field);
    }
    void add(const X2& d, const X2& s) {
        rexw(s, d);
        w8(0x01);
        modrm(3, s.field, d.field);
    }
    void add_imm(const X2& d, std::int32_t v) {
        if (v >= -128 && v <= 127) {
            rexw(kRax, d);
            w8(0x83);
            modrm(3, 0, d.field);
            w8(static_cast<std::uint8_t>(v));
        } else {
            rexw(kRax, d);
            w8(0x81);
            modrm(3, 0, d.field);
            w32(v);
        }
    }
    void sub_imm(const X2& d, std::int32_t v) {
        rexw(kRax, d);
        w8(0x83);
        modrm(3, 5, d.field);
        w8(static_cast<std::uint8_t>(v));
    }
    void cmp(const X2& rm, const X2& r) {
        rexw(r, rm);
        w8(0x39);
        modrm(3, r.field, rm.field);
    }
    void cmp_imm(const X2& rm, std::int32_t v) {
        if (v >= -128 && v <= 127) {
            rexw(kRax, rm);
            w8(0x83);
            modrm(3, 7, rm.field);
            w8(static_cast<std::uint8_t>(v));
        } else {
            rexw(kRax, rm);
            w8(0x81);
            modrm(3, 7, rm.field);
            w32(v);
        }
    }
    void test(const X2& rm) {
        rexw(rm, rm);
        w8(0x85);
        modrm(3, rm.field, rm.field);
    }
    void setcc(std::uint8_t cc, const X2& d) {
        rexw(kRax, d);
        w8(0x0F);
        w8(cc);
        modrm(3, 0, d.field);
    }
    void jcc(std::uint8_t cc) {
        w8(0x0F);
        w8(cc);
        w32(0);
    }
    void jmp() {
        w8(0xE9);
        w32(0);
    }
    void ret() { w8(0xC3); }
    void push_reg(const X2& r) {
        if (r.rb)
            w8(0x41);
        w8(static_cast<std::uint8_t>(0x50 | (r.field & 7)));
    }
    void pop_reg(const X2& r) {
        if (r.rb)
            w8(0x41);
        w8(static_cast<std::uint8_t>(0x58 | (r.field & 7)));
    }
    void w16(std::uint16_t v) {
        w8(static_cast<std::uint8_t>(v & 0xFF));
        w8(static_cast<std::uint8_t>(v >> 8));
    }
    // R8–R15 基址需要 REX.B（否则 R14 会被编码成 RSI）；REX 必须紧跟 opcode，
    // 且在 0x66 操作数前缀之后。
    void rex_b(const X2& base) {
        if (base.rb)
            w8(0x41);
    }
    void store_u16(const X2& base, std::int32_t disp, std::uint16_t v) {
        w8(0x66);
        rex_b(base);
        w8(0xC7);
        mem_operand(base, disp);
        w16(v);
    }

    void sub_rsp(std::int32_t v) {  // sub rsp, imm32
        w8(0x48);
        w8(0x81);
        w8(0xEC);
        w32(v);
    }
    void add_rsp(std::int32_t v) {  // add rsp, imm32
        w8(0x48);
        w8(0x81);
        w8(0xC4);
        w32(v);
    }
    // 基址 + disp：disp=0 用 mod=0，±127 用 mod=1，否则 mod=2。
    void mov_reg_from_base(const X2& d, const X2& base, std::int32_t disp) {
        rexw(d, base);
        w8(0x8B);
        const std::uint8_t mod = disp == 0 ? 0 : (disp >= -128 && disp <= 127 ? 1 : 2);
        modrm(mod, d.field, base.field);
        if (disp != 0) {
            if (mod == 1)
                w8(static_cast<std::uint8_t>(disp));
            else
                w32(disp);
        }
    }
    void mov_base_from_reg(const X2& base, std::int32_t disp, const X2& s) {
        rexw(s, base);
        w8(0x89);
        const std::uint8_t mod = disp == 0 ? 0 : (disp >= -128 && disp <= 127 ? 1 : 2);
        modrm(mod, s.field, base.field);
        if (disp != 0) {
            if (mod == 1)
                w8(static_cast<std::uint8_t>(disp));
            else
                w32(disp);
        }
    }
    void mov_base_imm32(const X2& base, std::int32_t disp, std::int32_t v) {
        rexw(kRax, base);
        w8(0xC7);
        const std::uint8_t mod = disp == 0 ? 0 : (disp >= -128 && disp <= 127 ? 1 : 2);
        modrm(mod, 0, base.field);
        if (disp != 0) {
            if (mod == 1)
                w8(static_cast<std::uint8_t>(disp));
            else
                w32(disp);
        }
        w32(v);
    }
    // [rsp + disp32]（SIB：base=rsp, index=none）
    void mov_reg_from_stack(const X2& d, std::int32_t disp) {
        w8(static_cast<std::uint8_t>(0x48 | (d.rr ? 4 : 0)));
        w8(0x8B);
        modrm(2, d.field, 4);
        w8(0x24);
        w32(disp);
    }
    void mov_stack_from_reg(std::int32_t disp, const X2& s) {
        w8(static_cast<std::uint8_t>(0x48 | (s.rr ? 4 : 0)));
        w8(0x89);
        modrm(2, s.field, 4);
        w8(0x24);
        w32(disp);
    }
    void mov_stack_imm32(std::int32_t disp, std::int32_t v) {
        w8(0x48);
        w8(0xC7);
        modrm(2, 0, 4);
        w8(0x24);
        w32(disp);
        w32(v);
    }
    // [base+disp] 的 ModRM + disp（base 必须是 RDI/RSI 等非扩展、非 rsp 基址）
    void mem_operand(const X2& base, std::int32_t disp) {
        const std::uint8_t mod =
            disp == 0 ? 0 : (disp >= -128 && disp <= 127 ? 1 : 2);
        modrm(mod, 0, base.field);
        if (disp != 0) {
            if (mod == 1)
                w8(static_cast<std::uint8_t>(disp));
            else
                w32(disp);
        }
    }
    void store_u32(const X2& base, std::int32_t disp, std::int32_t v) {
        rex_b(base);
        w8(0xC7);
        mem_operand(base, disp);
        w32(v);
    }
    void store_u64(const X2& base, std::int32_t disp, std::int32_t v) {
        w8(base.rb ? 0x49 : 0x48);
        w8(0xC7);
        mem_operand(base, disp);
        w32(v);
    }
    // ── float64（SSE2，XMM0/XMM1 作 scratch；操作数/结果都在栈槽）──────
    // [rsp+disp32] 的 ModRM：mod=2, rm=100(SIB), SIB=0x24
    void xmm_mem(std::uint8_t op, int xmm, std::int32_t disp) {
        w8(0xF2);
        w8(0x0F);
        w8(op);
        modrm(2, static_cast<std::uint8_t>(xmm & 7), 4);
        w8(0x24);
        w32(disp);
    }
    void movsd_load(int xmm, std::int32_t disp) { xmm_mem(0x10, xmm, disp); }
    void movsd_store(int xmm, std::int32_t disp) { xmm_mem(0x11, xmm, disp); }
    void ss_arith(std::uint8_t op, int d, int s) {  // 58 add / 5C sub / 59 mul / 5E div
        w8(0xF2);
        w8(0x0F);
        w8(op);
        modrm(3, static_cast<std::uint8_t>(d & 7), static_cast<std::uint8_t>(s & 7));
    }
    // B1：窄宽度规范化与 64 位有/无符号移位。
    void mov32(const X2& d, const X2& s) {  // mov r32, r/m32（高 32 位清零）
        rex(false, d, s);
        w8(0x8B);
        modrm(3, d.field, s.field);
    }
    void movsx_r(const X2& d, const X2& s, int width) {  // movsx/movsxd r64, r/m
        rexw(d, s);
        if (width == 4) {
            w8(0x63);  // movsxd r64, r/m32
        } else {
            w8(0x0F);
            w8(width == 1 ? 0xBE : 0xBF);
        }
        modrm(3, d.field, s.field);
    }
    void movzx_r(const X2& d, const X2& s, int width) {  // movzx r64, r/m8|16
        if (width == 4) {
            mov32(d, s);
            return;
        }
        rexw(d, s);
        w8(0x0F);
        w8(width == 1 ? 0xB6 : 0xB7);
        modrm(3, d.field, s.field);
    }
    void sar_cl(const X2& d) {  // sar r64, cl
        rexw(kRcx, d);
        w8(0xD3);
        modrm(3, 7, d.field);
    }
    void cvtsi2sd(int xmm, const X2& g) {  // int64 → double
        w8(0xF2);
        // reg 域是 xmm(<8 不需要 REX.R)，rm 域是 GPR → REX.B 跟 GPR 高比特。
        w8(static_cast<std::uint8_t>(0x48 | (g.rb ? 1 : 0)));
        w8(0x0F);
        w8(0x2A);
        modrm(3, static_cast<std::uint8_t>(xmm & 7), g.field);
    }
    void cvttsd2si(const X2& g, int xmm) {  // double → int64（向零截断）
        w8(0xF2);
        // reg 域是 GPR → REX.R；rm 域是 xmm(<8) → 无 B。
        w8(static_cast<std::uint8_t>(0x48 | (g.rr ? 4 : 0)));
        w8(0x0F);
        w8(0x2C);
        modrm(3, g.field, static_cast<std::uint8_t>(xmm & 7));
    }
    void movq_xmm_gpr(int xmm, const X2& g) {  // GPR → XMM 低 64 位
        w8(0x66);
        w8(static_cast<std::uint8_t>(0x48 | (g.rb ? 1 : 0)));
        w8(0x0F);
        w8(0x6E);
        modrm(3, static_cast<std::uint8_t>(xmm & 7), g.field);
    }
    // ── 扩展（P0：整数算术 + 数组寻址）──────────────────────────────
    void rex(bool w, const X2& reg, const X2& rm, const X2* index = nullptr) {
        std::uint8_t v = 0x40 | (w ? 8 : 0) | (reg.rr ? 4 : 0) | (rm.rb ? 1 : 0);
        if (index && index->rr)
            v |= 2;  // REX.X
        w8(v);
    }
    void rex_pure_reg(bool w, const X2& s, const X2& base, const X2* idx) {
        std::uint8_t v = 0x40 | (w ? 8 : 0) | (s.rr ? 4 : 0) | (base.rb ? 1 : 0);
        if (idx && idx->rr)
            v |= 2;
        w8(v);
    }
    void imul(const X2& d, const X2& s) {
        rex(true, d, s);
        w8(0x0F);
        w8(0xAF);
        modrm(3, d.field, s.field);
    }
    void cqo() { w8(0x48); w8(0x99); }
    void idiv(const X2& rm) {
        rex(true, kRax, rm);
        w8(0xF7);
        modrm(3, 7, rm.field);
    }
    void and_(const X2& d, const X2& s) {
        rex(true, s, d); w8(0x21); modrm(3, s.field, d.field);
    }
    void or_(const X2& d, const X2& s) {
        rex(true, s, d); w8(0x09); modrm(3, s.field, d.field);
    }
    void xor_(const X2& d, const X2& s) {
        rex(true, s, d); w8(0x31); modrm(3, s.field, d.field);
    }
    void neg(const X2& d) { rex(true, kRax, d); w8(0xF7); modrm(3, 3, d.field); }
    void shl_cl(const X2& d) { rex(true, kRax, d); w8(0xD3); modrm(3, 4, d.field); }
    void shr_cl(const X2& d) { rex(true, kRax, d); w8(0xD3); modrm(3, 7, d.field); }
    void cmovcc(std::uint8_t cc, const X2& d, const X2& s) {
        rex(true, d, s);
        w8(0x0F);
        w8(cc);
        modrm(3, d.field, s.field);
    }
    // [base + idx*scale]（scale 0..3 = 1/2/4/8）。base 为 rbp/r13 时 mod=1
    // 且显式 disp8=0；idx 不能是 RSP/R12（SIB index=100 保留）。
    void sib_mem(const X2& base, const X2& idx, int scale, std::uint8_t reg_field) {
        const bool need_disp = (base.field & 7) == 5;
        modrm(need_disp ? 1 : 0, reg_field, 4);
        w8(static_cast<std::uint8_t>((scale << 6) | ((idx.field & 7) << 3) |
                                     (base.field & 7)));  // SIB
        if (need_disp)
            w8(0);
    }
    void mov_reg_idx(const X2& d, const X2& base, const X2& idx, int scale) {
        rex(true, d, base, &idx);
        w8(0x8B);
        sib_mem(base, idx, scale, d.field);
    }
    void mov_idx_reg(const X2& base, const X2& idx, int scale, const X2& s) {
        rex(true, s, base, &idx);
        w8(0x89);
        sib_mem(base, idx, scale, s.field);
    }
    void movsx_idx(const X2& d, const X2& base, const X2& idx, int scale,
                   int width, bool sign) {
        if (width == 8) {
            mov_reg_idx(d, base, idx, scale);
            return;
        }
        if (width == 4 && sign) {
            rex(true, d, base, &idx);
            w8(0x63);
        } else if (width == 4) {
            std::uint8_t v = 0x40 | (d.rr ? 4 : 0) | (base.rb ? 1 : 0);
            if (idx.rr) v |= 2;
            if (v != 0x40) w8(v);
            w8(0x8B);
        } else {
            rex(true, d, base, &idx);
            w8(0x0F);
            w8(width == 1 ? (sign ? 0xBE : 0xB6) : (sign ? 0xBF : 0xB7));
        }
        sib_mem(base, idx, scale, d.field);
    }
    void mov_idx_narrow(const X2& base, const X2& idx, int scale, int width,
                        const X2& s) {
        if (width == 2)
            w8(0x66);
        rex_pure_reg(width == 8, s, base, &idx);
        w8(0x89);
        sib_mem(base, idx, scale, s.field);
    }
    void call_mem(const X2& base, std::int32_t disp) {
        std::uint8_t v = 0x40 | (base.rb ? 1 : 0);
        if (v != 0x40)
            w8(v);
        w8(0xFF);
        const std::uint8_t mod =
            disp == 0 ? 0 : (disp >= -128 && disp <= 127 ? 1 : 2);
        modrm(mod, 2, base.field);  // /2 = call
        if (disp != 0) {
            if (mod == 1)
                w8(static_cast<std::uint8_t>(disp));
            else
                w32(disp);
        }
    }
};

std::uint8_t jcc_of(std::uint8_t opfn) {
    switch (opfn) {
    case 13: return 0x84;  // eq
    case 14: return 0x85;  // ne
    case 15: return 0x8C;  // lt
    case 16: return 0x8E;  // le
    case 17: return 0x8F;  // gt
    default: return 0x8D;  // ge
    }
}
std::uint8_t setcc_of(std::uint8_t opfn) {
    switch (opfn) {
    case 13: return 0x94;
    case 14: return 0x95;
    case 15: return 0x9C;
    case 16: return 0x9E;
    case 17: return 0x9F;
    default: return 0x9D;
    }
}

struct Fixup {
    std::size_t pos = 0;
    int target = 0;  // 0 = guard stub
};

std::uint8_t setcc_of_op(rwir::Op op) {
    switch (op) {
    case rwir::Op::Eq: return 0x94;
    case rwir::Op::Neq: return 0x95;
    case rwir::Op::Lt: return 0x9C;
    case rwir::Op::Le: return 0x9E;
    case rwir::Op::Gt: return 0x9F;
    case rwir::Op::Ge: return 0x9D;
    default: return 0;
    }
}

bool is_terminator(const CInsn& ci) {
    return ci.op == rwir::Op::Return || ci.op == rwir::Op::Goto ||
           ci.op == rwir::Op::Br;
}

// ── B1：语义算子 → 标量矩阵 ─────────────────────────────────────────
// rwir::Op → jit2::KernelOp 的唯一映射。矩阵不覆盖的算子（sqrt/exp/log/pow、
// cast、逻辑、copy）返回 false；一元 `-`（nrd == 1）按 Neg 语义。
bool kernel_op_of(rwir::Op op, int nrd, jit2::KernelOp* out) {
    switch (op) {
    case rwir::Op::Add: *out = jit2::KernelOp::Add; return true;
    case rwir::Op::Sub:
        *out = nrd == 1 ? jit2::KernelOp::Neg : jit2::KernelOp::Sub;
        return true;
    case rwir::Op::Mul: *out = jit2::KernelOp::Mul; return true;
    case rwir::Op::Div: *out = jit2::KernelOp::Div; return true;
    case rwir::Op::Mod: *out = jit2::KernelOp::Mod; return true;
    case rwir::Op::Max: *out = jit2::KernelOp::Max; return true;
    case rwir::Op::Min: *out = jit2::KernelOp::Min; return true;
    case rwir::Op::Neg: *out = jit2::KernelOp::Neg; return true;
    case rwir::Op::Abs: *out = jit2::KernelOp::Abs; return true;
    case rwir::Op::Sign: *out = jit2::KernelOp::Sign; return true;
    case rwir::Op::BitAnd: *out = jit2::KernelOp::BitAnd; return true;
    case rwir::Op::BitOr: *out = jit2::KernelOp::BitOr; return true;
    case rwir::Op::BitXor: *out = jit2::KernelOp::BitXor; return true;
    case rwir::Op::Shl: *out = jit2::KernelOp::Shl; return true;
    case rwir::Op::Shr: *out = jit2::KernelOp::Shr; return true;
    case rwir::Op::Eq: *out = jit2::KernelOp::Eq; return true;
    case rwir::Op::Neq: *out = jit2::KernelOp::Neq; return true;
    case rwir::Op::Lt: *out = jit2::KernelOp::Lt; return true;
    case rwir::Op::Le: *out = jit2::KernelOp::Le; return true;
    case rwir::Op::Gt: *out = jit2::KernelOp::Gt; return true;
    case rwir::Op::Ge: *out = jit2::KernelOp::Ge; return true;
    default: return false;
    }
}

// B1 的槽 ABI 可表示范围：只接整数类（窄宽度按规范化 int64 落槽）。
// float64 暂不接：JIT2 的 Return 提交仍是 slot_write_i64（bits 当整数落槽），
// 浮点输出会被 println 读成原始位；该缺口修好前浮点格整段回退解释器。
bool scalar_class_supported(jit2::ScalarTypeClass c) {
    switch (c) {
    case jit2::ScalarTypeClass::SI8:
    case jit2::ScalarTypeClass::SI16:
    case jit2::ScalarTypeClass::SI32:
    case jit2::ScalarTypeClass::SI64:
    case jit2::ScalarTypeClass::UI8:
    case jit2::ScalarTypeClass::UI16:
    case jit2::ScalarTypeClass::UI32:
    case jit2::ScalarTypeClass::UI64:
        return true;
    default:
        return false;
    }
}

jit2::ScalarTypeClass operand_class(const Segment& seg, int r) {
    const RegDesc& d = seg.regs[static_cast<std::size_t>(r)];
    if (d.kind == Operand::Kind::Literal) {
        if (static_cast<std::size_t>(r) >= seg.lit.size())
            return jit2::ScalarTypeClass::None;
        if (seg.lit[static_cast<std::size_t>(r)].is_i64)
            return jit2::ScalarTypeClass::SI64;
        if (seg.lit[static_cast<std::size_t>(r)].is_f64)
            return jit2::ScalarTypeClass::F64;
        return jit2::ScalarTypeClass::None;
    }
    if (d.kind != Operand::Kind::Var)
        return jit2::ScalarTypeClass::None;
    switch (d.langtype) {
    case vmbin::LangType::Int8: return jit2::ScalarTypeClass::SI8;
    case vmbin::LangType::Int16: return jit2::ScalarTypeClass::SI16;
    case vmbin::LangType::Int32: return jit2::ScalarTypeClass::SI32;
    case vmbin::LangType::Int64: return jit2::ScalarTypeClass::SI64;
    case vmbin::LangType::Uint8: return jit2::ScalarTypeClass::UI8;
    case vmbin::LangType::Uint16: return jit2::ScalarTypeClass::UI16;
    case vmbin::LangType::Uint32: return jit2::ScalarTypeClass::UI32;
    case vmbin::LangType::Uint64: return jit2::ScalarTypeClass::UI64;
    case vmbin::LangType::Float64: return jit2::ScalarTypeClass::F64;
    default: return jit2::ScalarTypeClass::None;
    }
}

// B1 发射器覆盖的微操作集合（与 emit_scalar 的 switch 一一对应）。
// 明确排除：除法守卫组（Guard*/Cqo/IDiv64/Div64）、转换组（Convert*）、
// Abs64/Sign64、浮点组（Float32/Float64：输出提交缺口未修）——继续回退解释器。
bool micro_ops_supported(const jit2::ScalarPlan& p) {
    for (std::uint8_t k = 0; k < p.count; k++) {
        switch (p.ops[k]) {
        case jit2::MicroOp::LoadS8:
        case jit2::MicroOp::LoadS16:
        case jit2::MicroOp::LoadS32:
        case jit2::MicroOp::LoadS64:
        case jit2::MicroOp::LoadZ8:
        case jit2::MicroOp::LoadZ16:
        case jit2::MicroOp::LoadZ32:
        case jit2::MicroOp::LoadZ64:
        case jit2::MicroOp::Store8:
        case jit2::MicroOp::Store16:
        case jit2::MicroOp::Store32:
        case jit2::MicroOp::Store64:
        case jit2::MicroOp::Add64:
        case jit2::MicroOp::Sub64:
        case jit2::MicroOp::Mul64:
        case jit2::MicroOp::Neg64:
        case jit2::MicroOp::And64:
        case jit2::MicroOp::Or64:
        case jit2::MicroOp::Xor64:
        case jit2::MicroOp::Shl64:
        case jit2::MicroOp::Sar64:
        case jit2::MicroOp::Shr64:
        case jit2::MicroOp::CmpSigned:
        case jit2::MicroOp::CmpUnsigned:
        case jit2::MicroOp::SetCC:
        case jit2::MicroOp::Cmov:
            break;
        default:
            return false;
        }
    }
    return p.count > 0;
}

}  // namespace

bool jit2_verify(const Segment* seg, Jit2Plan* plan, JitRefusal* refusal) {
    static const int* g_site = nullptr;
    (void)g_site;
    int site = 0;
    auto refuse = [&](VmjitRefusalReason reason, std::uint32_t detail) {
        refusal->refusal_reason = reason;
        refusal->detail_code = detail;
        if (std::getenv("RC2_REFDBG"))
            std::fprintf(stderr, "[refuse reason=%u detail=%u site=%d]\n",
                         static_cast<unsigned>(reason), detail, site);
        return false;
    };
    *plan = Jit2Plan{};
    if (seg->insns.empty())
        return refuse(VmjitRefusalReason::MalformedSegment, 0);
    const int n = static_cast<int>(seg->insns.size());
    const std::size_t nr = seg->regs.size();
    if (nr > 255)
        return refuse(VmjitRefusalReason::MalformedSegment,
                      static_cast<std::uint32_t>(nr));
    std::vector<int> inputs, outputs;
    for (std::size_t i = 0; i < nr; i++) {
        const RegDesc& d = seg->regs[i];
        if (d.kind == Operand::Kind::Literal) {
            // 槽字面量（数组 fill 的类型串、浮点常数等）不进槽；只有被算术
            // 指令当立即数用的字面量才要求 int64（见指令级检查）。
            if (i >= seg->lit.size())
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            continue;
        }
        if (d.kind != Operand::Kind::Var)
            return refuse(VmjitRefusalReason::UnsupportedOperand, i);
        if (d.param_k < 0) {
            const int pos = -d.param_k - 1;
            if (pos > 14)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            if (static_cast<std::size_t>(pos) >= inputs.size())
                inputs.resize(static_cast<std::size_t>(pos) + 1, -1);
            if (inputs[static_cast<std::size_t>(pos)] >= 0)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            inputs[static_cast<std::size_t>(pos)] = static_cast<int>(i);
        } else if (d.param_k > 0) {
            const int pos = d.param_k - 1;
            if (pos > 3)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            if (static_cast<std::size_t>(pos) >= outputs.size())
                outputs.resize(static_cast<std::size_t>(pos) + 1, -1);
            if (outputs[static_cast<std::size_t>(pos)] >= 0)
                return refuse(VmjitRefusalReason::BadParamLayout, i);
            outputs[static_cast<std::size_t>(pos)] = static_cast<int>(i);
        } else if (d.name.empty() || d.name[0] == '*' || d.name[0] == '/' ||
                   d.name.find("\xC2\xB7") != std::string::npos) {
            return refuse(VmjitRefusalReason::UnsupportedOperand, i);
        }
    }
    for (int r : inputs)
        if (r < 0)
            return refuse(VmjitRefusalReason::BadParamLayout, 0);
    for (int r : outputs)
        if (r < 0)
            return refuse(VmjitRefusalReason::BadParamLayout, 0);
    std::vector<char> is_input(nr, 0);
    for (int r : inputs) is_input[r] = 1;

    // ── handler / Handoff / 数组内联预扫 ──────────────────────────────
    // P0 起 handler 不再等于「原生出口」：可内联的 compact 数组 at/set 直接
    // 发射机器码；其余 handler 在原生代码里调 C 侧 helper 执行后继续；
    // 只有 Handoff（println 等交外部引擎的算子）才是真正的停止点。
    // 动态算子（`×`/`÷` 等 Kind::Any）按操作数静态类型定型为 int64 算子。
    auto int_opfn_of = [](const CInsn& ci) -> std::uint8_t {
        return rwir::int64_fast_opfn(ci.op);
    };
    plan->opfn.assign(static_cast<std::size_t>(n), 0);
    auto int_typed = [&](int r) {
        const RegDesc& d = seg->regs[static_cast<std::size_t>(r)];
        if (d.kind == Operand::Kind::Literal)
            return seg->lit[r].is_i64;
        return d.kind == Operand::Kind::Var &&
               d.langtype == vmbin::LangType::Int64 && d.width == 8;
    };
    auto int_operand_ok = [&](int r) {
        const RegDesc& d = seg->regs[static_cast<std::size_t>(r)];
        if (d.kind == Operand::Kind::Literal)
            return seg->lit[r].is_i64;
        return d.kind == Operand::Kind::Var &&
               (d.langtype == vmbin::LangType::None ||
                (d.langtype == vmbin::LangType::Int64 && d.width == 8));
    };
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.fast_i64) {
            if (ci.nrd == 2 && int_operand_ok(ci.rd[0]) && int_operand_ok(ci.rd[1]))
                plan->opfn[static_cast<std::size_t>(i)] = ci.opfn;
            continue;
        }
        if (ci.impl != rwir::Impl::Dynamic || ci.nrd != 2 || ci.nwr != 1)
            continue;
        const std::uint8_t f = int_opfn_of(ci);
        if (f == 0)
            continue;
        if (int_typed(ci.rd[0]) && int_typed(ci.rd[1]))
            plan->opfn[static_cast<std::size_t>(i)] = f;
    }
    // float64：按寄存器静态类型标记；动态算子两个读参都是 float64 时定型为浮点
    // 算子（值常驻栈槽、XMM scratch）。Cast 按源/目的类型定方向。
    plan->is_float.assign(nr, 0);
    for (std::size_t r = 0; r < nr; r++) {
        const RegDesc& d = seg->regs[r];
        if (d.kind == Operand::Kind::Var && d.langtype == vmbin::LangType::Float64)
            plan->is_float[r] = 1;
        else if (d.kind == Operand::Kind::Literal && r < seg->lit.size() &&
                 seg->lit[r].is_f64)
            plan->is_float[r] = 1;
    }
    plan->fopfn.assign(static_cast<std::size_t>(n), 0);
    plan->fcast.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        const bool typed_float =
            ci.impl == rwir::Impl::Typed && ci.kind == rwir::Kind::Float &&
            ci.width == 8;
        if ((ci.impl == rwir::Impl::Dynamic || typed_float) && ci.nrd == 2 &&
            ci.nwr == 1 &&
            plan->is_float[ci.rd[0]] && plan->is_float[ci.rd[1]] &&
            plan->is_float[ci.wr[0]]) {
            std::uint8_t f = 0;
            switch (ci.op) {
            case rwir::Op::Add: f = 1; break;
            case rwir::Op::Sub: f = 2; break;
            case rwir::Op::Mul: f = 3; break;
            case rwir::Op::Div: f = 4; break;
            default: break;
            }
            plan->fopfn[static_cast<std::size_t>(i)] = f;
            continue;
        }
        if (ci.op == rwir::Op::Cast && ci.nrd == 1 && ci.nwr == 1 &&
            seg->regs[ci.rd[0]].kind == Operand::Kind::Var &&
            seg->regs[ci.wr[0]].kind == Operand::Kind::Var) {
            const bool src_f = plan->is_float[ci.rd[0]] != 0;
            const bool dst_f = plan->is_float[ci.wr[0]] != 0;
            if (!src_f && dst_f)
                plan->fcast[static_cast<std::size_t>(i)] = 1;  // int64 → float64
            else if (src_f && !dst_f)
                plan->fcast[static_cast<std::size_t>(i)] = 2;  // float64 → int64
        }
    }
    // B1：矩阵驱动的语义分派（Impl::Dynamic + 同型操作数）。旧路径已能覆盖的
    // 格子保留原 codegen；矩阵返回的 ops 指向 thread_local scratch，必须立刻
    // 复制进 plan 自己的 micro_pool（生命期随 plan/段缓存）。
    plan->micro_pool.clear();
    plan->scalar_offset.assign(static_cast<std::size_t>(n), 0);
    plan->scalar_count.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.impl != rwir::Impl::Dynamic || ci.nrd < 1 || ci.nrd > 2 ||
            ci.nwr != 1)
            continue;
        if (plan->opfn[static_cast<std::size_t>(i)] != 0 ||
            plan->fopfn[static_cast<std::size_t>(i)] != 0 ||
            plan->fcast[static_cast<std::size_t>(i)] != 0)
            continue;
        jit2::KernelOp kop;
        if (!kernel_op_of(ci.op, ci.nrd, &kop))
            continue;
        const jit2::ScalarTypeClass lhs = operand_class(*seg, ci.rd[0]);
        const jit2::ScalarTypeClass rhs =
            ci.nrd == 2 ? operand_class(*seg, ci.rd[1]) : lhs;
        if (!scalar_class_supported(lhs) || !scalar_class_supported(rhs))
            continue;
        const jit2::ScalarPlan sp = jit2::scalar_plan(kop, lhs, rhs, lhs);
        if (!sp.ok || sp.count > 24 || !micro_ops_supported(sp))
            continue;
        const std::uint32_t off =
            static_cast<std::uint32_t>(plan->micro_pool.size());
        for (std::uint8_t k = 0; k < sp.count; k++)
            plan->micro_pool.push_back(sp.ops[k]);
        plan->scalar_offset[static_cast<std::size_t>(i)] = off;
        plan->scalar_count[static_cast<std::size_t>(i)] = sp.count;
    }
    plan->is_handler.assign(static_cast<std::size_t>(n), 0);
    plan->array_op.assign(static_cast<std::size_t>(n), 0);
    plan->call_handler.assign(static_cast<std::size_t>(n), 0);
    plan->handoff_exit.assign(static_cast<std::size_t>(n), 0);
    auto reg_representable = [&](int r, bool allow_opaque) {
        const RegDesc& d = seg->regs[static_cast<std::size_t>(r)];
        if (d.kind != Operand::Kind::Var)
            return true;  // 字面量由 handler 自行解析 kvspace 槽
        if (d.array_type_id != vmbin::kArrayTypeNone)
            return true;  // 槽里放 body 指针
        if (d.langtype == vmbin::LangType::Int64 ||
            d.langtype == vmbin::LangType::Uint64 ||
            d.langtype == vmbin::LangType::Bool)
            return true;
        // time/duration 族的 langtype 静态不可知（None），但值恒为 8 字节。
        return allow_opaque && d.langtype == vmbin::LangType::None;
    };
    auto array_inline_kind = [&](const CInsn& ci) -> int {
        if (std::getenv("RC2_NOARRINLINE"))
            return 0;
        if (ci.handler != rwir::RwirHandlerId::ArrayElementAccess)
            return 0;
        const bool is_set =
            vmbin::instruction_is(ci, rwir::RwirId::CompactArraySet);
        if (ci.nrd < 1 || ci.nwr != 1)
            return 0;
        const int arr = ci.rd[0];
        if (arr < 0 || static_cast<std::size_t>(arr) >= nr)
            return 0;
        const RegDesc& ad = seg->regs[static_cast<std::size_t>(arr)];
        if (ad.kind != Operand::Kind::Var ||
            ad.array_type_id == vmbin::kArrayTypeNone ||
            ad.array_type_id >= seg->array_types.size())
            return 0;
        const vmbin::ArrayTypeDesc& at = seg->array_types[ad.array_type_id];
        if (at.ndim < 1 || at.ndim > 2 || at.width == 0)
            return 0;
        if (at.numel <= 0 || at.numel > kInt32Max)
            return 0;
        if (ci.nrd != (is_set ? 2 : 1) + at.ndim)
            return 0;
        if (is_set && ci.wr[0] != arr)
            return 0;  // handler 只支持同槽写
        if (!is_set && seg->regs[ci.wr[0]].kind != Operand::Kind::Var)
            return 0;
        for (int d = 0; d < at.ndim; d++) {
            const int ix = ci.rd[1 + d];
            if (seg->regs[ix].kind == Operand::Kind::Literal &&
                (!seg->lit[ix].is_i64 || seg->lit[ix].v < kInt32Min ||
                 seg->lit[ix].v > kInt32Max))
                return 0;
        }
        if (is_set) {
            const int v = ci.rd[1 + at.ndim];
            const RegDesc& vd = seg->regs[v];
            if (vd.kind == Operand::Kind::Literal &&
                !seg->lit[v].is_i64)
                return 0;
            if (vd.kind == Operand::Kind::Var &&
                (vd.langtype != at.elem_kind || vd.width != at.width))
                return 0;
        } else {
            const RegDesc& wd = seg->regs[ci.wr[0]];
            if (wd.langtype != at.elem_kind || wd.width != at.width)
                return 0;
        }
        return is_set ? 2 : 1;
    };
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.impl == rwir::Impl::Handoff) {
            plan->handoff_exit[static_cast<std::size_t>(i)] = 1;
            continue;
        }
        if (ci.handler == rwir::RwirHandlerId::None)
            continue;
        const int kind = array_inline_kind(ci);
        if (std::getenv("RC2_CLSDBG"))
            std::fprintf(stderr,
                         "[jit2cls i=%d h=%u set=%d arr=%d wr0=%d rd0=%d "
                         "atype=%u types=%zu]\n",
                         i, (unsigned)ci.handler,
                         (int)vmbin::instruction_is(ci, rwir::RwirId::CompactArraySet),
                         kind, (int)ci.wr[0], (int)ci.rd[0],
                         (unsigned)seg->regs[ci.rd[0]].array_type_id,
                         seg->array_types.size());
        if (kind != 0) {
            plan->array_op[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(kind);
            continue;
        }
        plan->is_handler[static_cast<std::size_t>(i)] = 1;
        plan->call_handler[static_cast<std::size_t>(i)] = 1;
        plan->handler_count++;
        const bool opaque_ok =
            ci.handler == rwir::RwirHandlerId::TimeNow ||
            ci.handler == rwir::RwirHandlerId::TimeArithmetic ||
            ci.handler == rwir::RwirHandlerId::DurationScale ||
            ci.handler == rwir::RwirHandlerId::DurationAs ||
            ci.handler == rwir::RwirHandlerId::RandomNumber;
        for (int k = 0; k < ci.nrd; k++)
            if (!reg_representable(ci.rd[k], opaque_ok))
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.rd[k]);
        for (int k = 0; k < ci.nwr; k++)
            if (!reg_representable(ci.wr[k], opaque_ok))
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.wr[k]);
    }
    plan->handler_percent = n > 0 ? plan->handler_count * 100 / n : 0;
    if (plan->handler_count > 0) {
        // 冷/热门控：循环区间 = 反向边 (target-1 .. i)；首个 handler 落在循环内
        // 或 handler 占比 >25% → 拒绝 JIT2（handler 内调成本压过原生收益）。
        std::vector<char> in_loop(static_cast<std::size_t>(n), 0);
        std::vector<int> loop_start_of(static_cast<std::size_t>(n), -1);
        for (int i = 0; i < n; i++) {
            const CInsn& ci = seg->insns[i];
            if (ci.op != rwir::Op::Goto && ci.op != rwir::Op::Br)
                continue;
            const bool br = ci.op == rwir::Op::Br;
            const int targets[2] = {ci.rd[br ? 1 : 0], br ? ci.rd[2] : -1};
            for (int t : targets) {
                if (t < 0)
                    continue;
                const std::int64_t tv = seg->lit[t].v;
                if (tv < 1 || tv > n)
                    continue;
                const int ti = static_cast<int>(tv) - 1;
                if (ti > i)
                    continue;
                for (int j = ti; j <= i; j++) {
                    in_loop[static_cast<std::size_t>(j)] = 1;
                    int& ls = loop_start_of[static_cast<std::size_t>(j)];
                    if (ls < 0 || ti < ls)
                        ls = ti;
                }
            }
        }
        int first = -1;
        for (int i = 0; i < n; i++)
            if (plan->is_handler[static_cast<std::size_t>(i)]) {
                first = i;
                break;
            }
        const bool first_in_loop =
            first >= 0 && in_loop[static_cast<std::size_t>(first)];
        const int loop_start_pc =
            first_in_loop ? loop_start_of[static_cast<std::size_t>(first)] + 1 : 0;
        if (first_in_loop || plan->handler_percent > 25)
            return refuse(VmjitRefusalReason::HandlerDensity,
                          (static_cast<std::uint32_t>(loop_start_pc) << 16) |
                              static_cast<std::uint32_t>(first + 1));
    }

    // opcode 白名单 + 目标/操作数检查
    std::vector<std::vector<int>> successors(static_cast<std::size_t>(n));

    // 结构与坐标
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.nrd > 16 || ci.nwr > 4)
            return refuse(VmjitRefusalReason::MalformedSegment, i);
        for (int k = 0; k < ci.nrd; k++)
            if (ci.rd[k] >= nr)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
        for (int k = 0; k < ci.nwr; k++)
            if (ci.wr[k] >= nr)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
    }
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        site = 1;
        if (plan->handoff_exit[static_cast<std::size_t>(i)]) {
            if (i + 1 < n)
                successors[static_cast<std::size_t>(i)].push_back(i + 1);
            continue;
        }
        if (plan->array_op[static_cast<std::size_t>(i)] != 0) {
            site = 2;
            for (int k = 0; k < ci.nrd; k++) {
                const RegDesc& d = seg->regs[ci.rd[k]];
                if (d.kind == Operand::Kind::Literal &&
                    !seg->lit[ci.rd[k]].is_i64)
                    return refuse(VmjitRefusalReason::UnsupportedOperand, ci.rd[k]);
            }
            successors[static_cast<std::size_t>(i)].push_back(i + 1);
            continue;
        }
        if (plan->call_handler[static_cast<std::size_t>(i)]) {
            site = 3;
            for (int k = 0; k < ci.nwr; k++) {
                if (is_input[ci.wr[k]] ||
                    seg->regs[ci.wr[k]].kind == Operand::Kind::Literal)
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  ci.wr[k]);
            }
            successors[static_cast<std::size_t>(i)].push_back(i + 1);
            continue;
        }
        if (ci.op == rwir::Op::Return) {
            // return 可在段中间（解释器按 CFG 到达）；不是终点也合法。
            if (ci.nrd != 0 || ci.nwr != 0)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            continue;
        }
        if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
            site = 4;
            const bool br = ci.op == rwir::Op::Br;
            if (ci.impl != rwir::Impl::Control)
                return refuse(VmjitRefusalReason::UnsupportedOpcode, i);
            if ((br && ci.nrd != 3) || (!br && ci.nrd != 1) || ci.nwr != 0)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            if (br) {
                if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)
                    return refuse(VmjitRefusalReason::UnsupportedOperand, i);
            } else if (seg->regs[ci.rd[0]].kind != Operand::Kind::Literal) {
                return refuse(VmjitRefusalReason::UnsupportedOperand, i);
            }
            const int t1 = ci.rd[br ? 1 : 0];
            const int t2 = br ? ci.rd[2] : -1;
            if (seg->regs[t1].kind != Operand::Kind::Literal)
                return refuse(VmjitRefusalReason::UnsupportedOperand, i);
            const std::int64_t v1 = seg->lit[t1].v;
            if (v1 < 1 || v1 > n + 1)
                return refuse(VmjitRefusalReason::BadTarget, i);
            if (v1 <= n)
                successors[static_cast<std::size_t>(i)].push_back(
                    static_cast<int>(v1) - 1);  // n+1 = 虚拟出口，无后继
            if (br) {
                if (seg->regs[t2].kind != Operand::Kind::Literal)
                    return refuse(VmjitRefusalReason::UnsupportedOperand, i);
                const std::int64_t v2 = seg->lit[t2].v;
                if (v2 < 1 || v2 > n + 1)
                    return refuse(VmjitRefusalReason::BadTarget, i);
                if (v2 <= n)
                    successors[static_cast<std::size_t>(i)].push_back(
                        static_cast<int>(v2) - 1);
            }
            continue;
        }
        if (ci.op == rwir::Op::Copy) {
            site = 5;
            if (ci.nrd != 1 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            // 只有 int64 字面量能当立即数搬；字符串/浮点字面量留给解释器。
            const RegDesc& src = seg->regs[ci.rd[0]];
            const bool src_lit = src.kind == Operand::Kind::Literal;
            const vmbin::FastLiteral& slit =
                src_lit ? seg->lit[static_cast<std::size_t>(ci.rd[0])]
                        : vmbin::FastLiteral{};
            if (src_lit && !slit.is_i64 && !slit.is_f64)
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.rd[0]);
            if (src_lit && slit.is_f64 &&
                !plan->is_float[ci.wr[0]])
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.rd[0]);
        } else if (plan->fopfn[static_cast<std::size_t>(i)] != 0) {
            site = 7;
            if (ci.nrd != 2 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            for (int k = 0; k < 2; k++)
                if (!plan->is_float[ci.rd[k]])
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  ci.rd[k]);
            if (!plan->is_float[ci.wr[0]])
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.wr[0]);
        } else if (plan->fcast[static_cast<std::size_t>(i)] != 0) {
            site = 8;
            if (ci.nrd != 1 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
        } else if (plan->scalar_count[static_cast<std::size_t>(i)] != 0) {
            site = 9;
            if (ci.nrd < 1 || ci.nrd > 2 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            for (int k = 0; k < ci.nrd; k++) {
                const RegDesc& d = seg->regs[ci.rd[k]];
                if (d.kind == Operand::Kind::Literal) {
                    const vmbin::FastLiteral& l = seg->lit[ci.rd[k]];
                    if (!l.is_i64 && !l.is_f64)
                        return refuse(VmjitRefusalReason::UnsupportedOperand,
                                      ci.rd[k]);
                } else if (d.kind != Operand::Kind::Var) {
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  ci.rd[k]);
                }
            }
            if (seg->regs[ci.wr[0]].kind != Operand::Kind::Var)
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.wr[0]);
        } else if (plan->opfn[static_cast<std::size_t>(i)] != 0) {
            site = 6;
            const std::uint8_t ofn = plan->opfn[static_cast<std::size_t>(i)];
            if (ofn < 1 || ofn > 18)
                return refuse(VmjitRefusalReason::UnsupportedOpcode, i);
            if (ci.nrd != 2 || ci.nwr != 1)
                return refuse(VmjitRefusalReason::MalformedSegment, i);
            if (seg->regs[ci.rd[0]].kind != Operand::Kind::Var)
                return refuse(VmjitRefusalReason::UnsupportedOperand, i);
            if (seg->regs[ci.wr[0]].kind != Operand::Kind::Var)
                return refuse(VmjitRefusalReason::UnsupportedOperand, i);
            const RegDesc& b = seg->regs[ci.rd[1]];
            if (b.kind == Operand::Kind::Literal) {
                if (!seg->lit[ci.rd[1]].is_i64)
                    return refuse(VmjitRefusalReason::UnsupportedOperand,
                                  ci.rd[1]);
            } else if (b.kind != Operand::Kind::Var) {
                return refuse(VmjitRefusalReason::UnsupportedOperand, ci.rd[1]);
            }
        } else {
            return refuse(VmjitRefusalReason::UnsupportedOpcode, i);
        }
        for (int k = 0; k < ci.nwr; k++) {
            if (is_input[ci.wr[k]] ||
                seg->regs[ci.wr[k]].kind == Operand::Kind::Literal)
                return refuse(VmjitRefusalReason::UnsupportedOperand,
                              ci.wr[k]);
        }
        successors[static_cast<std::size_t>(i)].push_back(i + 1);
    }

    // 定义先于使用：写参不预置（读-写写参需调用方预载，原型不支持）。
    std::vector<char> entry(nr, 0);
    for (std::size_t i = 0; i < nr; i++) {
        const RegDesc& d = seg->regs[i];
        if (d.kind == Operand::Kind::Literal)
            entry[i] = 1;
        else if (d.kind == Operand::Kind::Var && d.param_k < 0)
            entry[i] = 1;  // 读参＝实参；写参必须在函数内先写后用，否则拒绝
    }
    std::vector<std::vector<char>> in_def(static_cast<std::size_t>(n),
                                          std::vector<char>(nr, 0));
    std::vector<std::vector<char>> out_def(static_cast<std::size_t>(n),
                                           std::vector<char>(nr, 0));
    std::vector<char> reach(static_cast<std::size_t>(n), 0);
    in_def[0] = entry;
    reach[0] = 1;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < n; i++) {
            if (!reach[i])
                continue;
            out_def[static_cast<std::size_t>(i)] =
                in_def[static_cast<std::size_t>(i)];
            const CInsn& ci = seg->insns[i];
            for (int k = 0; k < ci.nwr; k++)
                out_def[static_cast<std::size_t>(i)][ci.wr[k]] = 1;
            for (int s2 : successors[static_cast<std::size_t>(i)]) {
                if (s2 < 0 || s2 >= n)
                    continue;
                if (!reach[s2]) {
                    in_def[static_cast<std::size_t>(s2)] =
                        out_def[static_cast<std::size_t>(i)];
                    reach[s2] = 1;
                    changed = true;
                    continue;
                }
                for (std::size_t r = 0; r < nr; r++)
                    if (in_def[static_cast<std::size_t>(s2)][r] &&
                        !out_def[static_cast<std::size_t>(i)][r]) {
                        in_def[static_cast<std::size_t>(s2)][r] = 0;
                        changed = true;
                    }
            }
        }
    }
    // may-defined（并集）：任一路径写过的寄存器。Handoff 出口按它物化——
    // 循环体里定义的局部（如 climb 的 c）在“可能不执行循环”的路径上不是
    // 必然定义，但运行时确实有值，必须同步给解释器。
    {
        plan->out_defined.assign(static_cast<std::size_t>(n),
                                 std::vector<char>(nr, 0));
        std::vector<std::vector<char>> may_in(static_cast<std::size_t>(n),
                                              std::vector<char>(nr, 0));
        may_in[0] = entry;
        // 单调并集，最多 n 轮收敛（路径长度 ≤ n）。
        for (int pass = 0; pass < n; pass++) {
            bool changed = false;
            for (int i = 0; i < n; i++) {
                if (!reach[i])
                    continue;
                std::vector<char>& outm =
                    plan->out_defined[static_cast<std::size_t>(i)];
                if (i == 0)
                    outm = may_in[0];
                for (std::size_t r = 0; r < nr; r++)
                    if (may_in[static_cast<std::size_t>(i)][r] && !outm[r]) {
                        outm[r] = 1;
                        changed = true;
                    }
                const CInsn& ci = seg->insns[i];
                for (int k = 0; k < ci.nwr; k++)
                    if (!outm[ci.wr[k]]) {
                        outm[ci.wr[k]] = 1;
                        changed = true;
                    }
                for (int s2 : successors[static_cast<std::size_t>(i)]) {
                    if (s2 < 0 || s2 >= n)
                        continue;
                    for (std::size_t r = 0; r < nr; r++)
                        if (outm[r] && !may_in[static_cast<std::size_t>(s2)][r]) {
                            may_in[static_cast<std::size_t>(s2)][r] = 1;
                            changed = true;
                        }
                }
            }
            if (!changed)
                break;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!reach[i])
            continue;
        const CInsn& ci = seg->insns[i];
        for (int k = 0; k < ci.nrd; k++) {
            const int idx = ci.rd[k];
            if (seg->regs[idx].kind != Operand::Kind::Var)
                continue;
            if (!in_def[static_cast<std::size_t>(i)][idx])
                return refuse(VmjitRefusalReason::UninitializedRead, idx);
        }
        bool exits = ci.op == rwir::Op::Return;
        if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
            const bool br = ci.op == rwir::Op::Br;
            const int t1 = ci.rd[br ? 1 : 0];
            const int t2 = br ? ci.rd[2] : -1;
            if (seg->lit[t1].v == n + 1 ||
                (br && seg->lit[t2].v == n + 1))
                exits = true;  // 虚拟出口
        }
        if (i == n - 1 && !is_terminator(ci))
            exits = true;  // 末尾 fallthrough
        if (exits) {
            for (int r : outputs)
                if (!out_def[static_cast<std::size_t>(i)][r])
                    return refuse(VmjitRefusalReason::OutputNotDefined, i);
        }
    }

    // ── CFG：leader = 入口、跳转目标、终结指令后继 ─────────────────
    std::vector<char> leader(static_cast<std::size_t>(n), 0);
    leader[0] = 1;
    for (int i = 0; i < n; i++) {
        if (!is_terminator(seg->insns[i]))
            continue;
        if (i + 1 < n)
            leader[static_cast<std::size_t>(i + 1)] = 1;
        for (int s : successors[static_cast<std::size_t>(i)])
            if (s >= 0 && s < n)
                leader[static_cast<std::size_t>(s)] = 1;
    }
    for (int i = 0; i < n; i++) {
        if (!leader[static_cast<std::size_t>(i)])
            continue;
        Jit2Block blk;
        blk.begin = i;
        blk.end = i + 1;
        while (blk.end < n && !leader[static_cast<std::size_t>(blk.end)])
            blk.end++;
        for (int s : successors[static_cast<std::size_t>(blk.end - 1)])
            blk.successors.push_back(s);
        plan->blocks.push_back(std::move(blk));
    }

    // ── 活跃分析：use/def + 不动点 ────────────────────────────────
    std::vector<std::vector<char>> use(static_cast<std::size_t>(n),
                                       std::vector<char>(nr, 0));
    std::vector<std::vector<char>> def(static_cast<std::size_t>(n),
                                       std::vector<char>(nr, 0));
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        auto mark_use = [&](int r) {
            if (seg->regs[r].kind == Operand::Kind::Var)
                use[static_cast<std::size_t>(i)][r] = 1;
        };
        for (int k = 0; k < ci.nrd; k++)
            mark_use(ci.rd[k]);
        // Handoff（println 等）隐式读帧内全部局部：按 use 处理，避免这些值
        // 的活跃区间在 handler 前就结束、被分配到同一个物理寄存器。
        if (plan->handoff_exit[static_cast<std::size_t>(i)])
            for (std::size_t r = 0; r < nr; r++)
                if (seg->regs[r].kind == Operand::Kind::Var)
                    use[static_cast<std::size_t>(i)][r] = 1;
        for (int k = 0; k < ci.nwr; k++)
            def[static_cast<std::size_t>(i)][ci.wr[k]] = 1;
    }
    plan->live_in.assign(static_cast<std::size_t>(n), std::vector<char>(nr, 0));
    plan->live_out.assign(static_cast<std::size_t>(n), std::vector<char>(nr, 0));
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = n - 1; i >= 0; i--) {
            std::vector<char> out(nr, 0);
            for (int s : successors[static_cast<std::size_t>(i)]) {
                if (s < 0 || s >= n)
                    continue;
                for (std::size_t r = 0; r < nr; r++)
                    if (plan->live_in[static_cast<std::size_t>(s)][r])
                        out[r] = 1;
            }
            std::vector<char> in = use[static_cast<std::size_t>(i)];
            for (std::size_t r = 0; r < nr; r++)
                if (out[r] && !def[static_cast<std::size_t>(i)][r])
                    in[r] = 1;
            if (in != plan->live_in[static_cast<std::size_t>(i)] ||
                out != plan->live_out[static_cast<std::size_t>(i)]) {
                plan->live_in[static_cast<std::size_t>(i)] = std::move(in);
                plan->live_out[static_cast<std::size_t>(i)] = std::move(out);
                changed = true;
            }
        }
    }

    // 写参预载已废除：写参在定义前被读会触发上面的「定义先于使用」检查并拒绝
    // 该函数（回退解释器，报与不开 JIT 时相同的错），预载只会把错误变成静默成功。
    plan->output_preload.assign(nr, 0);

    // 物化集合：
    //  - Handoff 出口：操作数 ∪ live_out（之后由解释器接管）；
    //  - 原生内调 handler：操作数 ∪ 写回 ∪ live_out（调用前落槽、调用后重载）。
    plan->native_stage.assign(static_cast<std::size_t>(n),
                              std::vector<char>(nr, 0));
    for (int i = 0; i < n; i++) {
        const bool exit_here =
            plan->handoff_exit[static_cast<std::size_t>(i)] != 0;
        const bool call_here =
            plan->call_handler[static_cast<std::size_t>(i)] != 0;
        if (!exit_here && !call_here)
            continue;
        const CInsn& ci = seg->insns[i];
        for (int k = 0; k < ci.nrd; k++)
            if (seg->regs[ci.rd[k]].kind == Operand::Kind::Var)
                plan->native_stage[static_cast<std::size_t>(i)][ci.rd[k]] = 1;
        if (call_here)
            for (int k = 0; k < ci.nwr; k++)
                if (seg->regs[ci.wr[k]].kind == Operand::Kind::Var)
                    plan->native_stage[static_cast<std::size_t>(i)][ci.wr[k]] = 1;
        for (std::size_t r = 0; r < nr; r++) {
            if (plan->live_out[static_cast<std::size_t>(i)][r])
                plan->native_stage[static_cast<std::size_t>(i)][r] = 1;
            if (exit_here && r < plan->out_defined[i].size() &&
                plan->out_defined[i][r])
                plan->native_stage[static_cast<std::size_t>(i)][r] = 1;
        }
    }

    // ── 线性扫描区间 + 分配（压力大就溢出，不拒绝） ────────────────
    plan->interval_start.assign(nr, 1 << 30);
    plan->interval_end.assign(nr, -1);
    auto touch = [&](int r, int pc) {
        if (pc < plan->interval_start[r])
            plan->interval_start[r] = pc;
        if (pc > plan->interval_end[r])
            plan->interval_end[r] = pc;
    };
    for (int i = 0; i < n; i++)
        for (std::size_t r = 0; r < nr; r++)
            if (plan->live_in[static_cast<std::size_t>(i)][r] ||
                plan->live_out[static_cast<std::size_t>(i)][r] ||
                def[static_cast<std::size_t>(i)][r])
                touch(static_cast<int>(r), i);
    for (int r : inputs) touch(r, -1);  // 序言一次装载：全部输入从 pc -1 起活跃
    for (int r : outputs) touch(r, n - 1);

    struct Interval {
        int reg;
        int start;
        int end;
    };
    std::vector<Interval> intervals;
    for (std::size_t r = 0; r < nr; r++) {
        if (seg->regs[r].kind != Operand::Kind::Var)
            continue;
        if (plan->interval_end[r] < 0)
            continue;
        intervals.push_back({static_cast<int>(r), plan->interval_start[r],
                             plan->interval_end[r]});
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval& a, const Interval& b) {
                  if (a.start != b.start)
                      return a.start < b.start;
                  return a.reg < b.reg;
              });
    plan->assignment.assign(nr, -2);
    plan->spill_slot.assign(nr, -1);
    int active[kPoolSize];  // interval 下标
    for (int s = 0; s < kPoolSize; s++) active[s] = -1;
    auto free_slot = [&]() {
        for (int s = 0; s < kPoolSize; s++)
            if (active[s] < 0)
                return s;
        return -1;
    };
    for (std::size_t k = 0; k < intervals.size(); k++) {
        const Interval& cur = intervals[k];
        for (int s = 0; s < kPoolSize; s++) {
            if (active[s] >= 0 && intervals[static_cast<std::size_t>(active[s])].end < cur.start)
                active[s] = -1;  // 过期释放
        }
        int slot = free_slot();
        if (slot >= 0) {
            active[slot] = static_cast<int>(k);
            plan->assignment[cur.reg] = slot;
            continue;
        }
        int victim = -1;
        int victim_end = -1;
        for (int s = 0; s < kPoolSize; s++) {
            const int end = intervals[static_cast<std::size_t>(active[s])].end;
            if (end > victim_end) {
                victim_end = end;
                victim = s;
            }
        }
        if (victim_end > cur.end) {
            const int old_reg = intervals[static_cast<std::size_t>(active[victim])].reg;
            plan->assignment[old_reg] = -1;
            plan->spill_slot[old_reg] = plan->spill_count++;
            plan->assignment[cur.reg] = victim;
            active[victim] = static_cast<int>(k);
        } else {
            plan->assignment[cur.reg] = -1;
            plan->spill_slot[cur.reg] = plan->spill_count++;
        }
    }
    // float64 值常驻栈槽：XMM 只做 scratch，避免扩展线性扫描的寄存器类。
    for (std::size_t r = 0; r < nr; r++) {
        if (!plan->is_float[r] || seg->regs[r].kind != Operand::Kind::Var)
            continue;
        if (plan->assignment[r] >= 0)
            plan->assignment[r] = -1;
        if (plan->spill_slot[r] < 0 && plan->interval_end[r] >= 0)
            plan->spill_slot[r] = plan->spill_count++;
    }
    plan->shape.inputs = std::move(inputs);
    plan->shape.outputs = std::move(outputs);

    // 融合：比较后紧跟 br 读其写结果，且两个目标都是前向（guard 不破坏 flags）。
    plan->fused.assign(static_cast<std::size_t>(n), false);
    for (int i = 0; i + 1 < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (plan->opfn[static_cast<std::size_t>(i)] < 13 ||
            plan->opfn[static_cast<std::size_t>(i)] > 18)
            continue;
        const CInsn& nx = seg->insns[i + 1];
        if (nx.op != rwir::Op::Br || nx.rd[0] != ci.wr[0])
            continue;
        const int t1 = static_cast<int>(seg->lit[nx.rd[1]].v);
        const int t2 = static_cast<int>(seg->lit[nx.rd[2]].v);
        if (t1 <= i + 1 || t2 <= i + 1)
            continue;
        if (std::getenv("RC2_NOFUSE"))
            continue;
        bool only_here = true;
        for (const CInsn& x : seg->insns)
            for (int k = 0; k < x.nrd; k++)
                if (x.rd[k] == ci.wr[0] && &x != &nx)
                    only_here = false;
        plan->fused[static_cast<std::size_t>(i)] = only_here;
    }
    return true;
}

std::vector<std::uint8_t> jit2_emit(const Segment* seg, const Jit2Plan& plan) {
#if defined(__x86_64__)
    const int n = static_cast<int>(seg->insns.size());
    const auto& shape = plan.shape;
    const int spill_bytes = plan.spill_count * 8;
    const int ctx_slot = spill_bytes;        // VmContextV1*
    const int exit_slot = spill_bytes + 8;   // StructuredExitV1*
    const int guard_slot = spill_bytes + 16; // 调用前后保存 RCX（guard 计数）
    const int tmp_slot = spill_bytes + 24;   // div/mod 临时保存 RAX（不能用 push，
                                             // 否则 spill 槽的 [rsp+d] 偏移全错）
    Code2 c;
    std::vector<std::size_t> start(static_cast<std::size_t>(n), 0);
    std::vector<Fixup> fixes;

    auto assignment = [&](int r) { return plan.assignment[r]; };
    auto spill_slot = [&](int r) { return plan.spill_slot[r] * 8; };
    // 读一个 Var 到物理寄存器：已分配用自己的，否则加载到 scratch。
    auto read_reg = [&](int r, const X2& scratch) -> X2 {
        if (assignment(r) >= 0)
            return kPool[assignment(r)];
        c.mov_reg_from_stack(scratch, spill_slot(r));
        return scratch;
    };
    auto store_dst = [&](int r, const X2& value_reg) {
        if (assignment(r) >= 0) {
            if (!(value_reg.field == kPool[assignment(r)].field &&
                  value_reg.rb == kPool[assignment(r)].rb))
                c.mov(kPool[assignment(r)], value_reg);
            return;
        }
        c.mov_stack_from_reg(spill_slot(r), value_reg);
    };

    // 序言（统一 ABI）：R13=frame、R14=slots；值池 callee-saved 按需保存。
    c.mov_imm(kRcx, static_cast<std::int32_t>(kJit2GuardBudget));
    c.push_reg(kR13);
    c.push_reg(kR14);
    c.mov(kR13, kRsi);                       // frame
    c.mov_reg_from_base(kR14, kR13, 8);      // frame->slots
    // 值池是 callee-saved：按实际用到的池槽保存（线性扫描会复用池槽）。
    bool used_pool[kPoolSize] = {};
    for (std::size_t r = 0; r < seg->regs.size(); r++)
        if (plan.assignment[r] >= 0)
            used_pool[plan.assignment[r]] = true;
    std::vector<X2> saved_pool;
    for (int s = 1; s < kPoolSize; s++)  // 槽 0 = RAX，caller-saved，无需保存
        if (used_pool[s])
            saved_pool.push_back(kPool[s]);
    for (const X2& reg : saved_pool)
        c.push_reg(reg);
    // SysV：call 前 RSP 必须 16 字节对齐。入口 RSP ≡ 8；每次 push 减 8；
    // 已 push 个数 npush = 2 + saved_pool.size()。要求
    //   (8 - 8*npush - frame) % 16 == 0 → frame % 16 == (npush 偶数 ? 8 : 0)。
    const int npush = 2 + static_cast<int>(saved_pool.size());
    const int want_mod = (npush % 2 == 0) ? 8 : 0;
    int frame = ((spill_bytes + 32 + 15) / 16) * 16;
    if ((frame & 15) != want_mod)
        frame += 8;
    auto emit_restore = [&]() {
        for (auto it = saved_pool.rbegin(); it != saved_pool.rend(); ++it)
            c.pop_reg(*it);
        c.pop_reg(kR14);
        c.pop_reg(kR13);
    };
    if (frame)
        c.sub_rsp(frame);
    c.mov_stack_from_reg(ctx_slot, kRdi);   // 入口 RDI = VmContextV1*
    c.mov_stack_from_reg(exit_slot, kRdx);  // 入口 RDX = StructuredExitV1*
    for (int r : shape.inputs) {
        const std::int32_t disp = static_cast<std::int32_t>(32 * r);
        if (assignment(r) >= 0) {
            c.mov_reg_from_base(kPool[assignment(r)], kR14, disp);
        } else {
            c.mov_reg_from_base(kR10, kR14, disp);
            c.mov_stack_from_reg(spill_slot(r), kR10);
        }
    }
    // 读-写写参：调用方已预载，序言同样把它装进寄存器（否则读到垃圾）。
    for (int r : shape.outputs) {
        if (!plan.output_preload[static_cast<std::size_t>(r)])
            continue;
        const std::int32_t disp = static_cast<std::int32_t>(32 * r);
        if (assignment(r) >= 0) {
            c.mov_reg_from_base(kPool[assignment(r)], kR14, disp);
        } else {
            c.mov_reg_from_base(kR10, kR14, disp);
            c.mov_stack_from_reg(spill_slot(r), kR10);
        }
    }
    // 分支目标按 16 字节对齐（热循环对齐收益，标准做法）。
    std::vector<char> is_target(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.op != rwir::Op::Goto && ci.op != rwir::Op::Br)
            continue;
        const bool br = ci.op == rwir::Op::Br;
        const int t1 = ci.rd[br ? 1 : 0];
        const int t2 = br ? ci.rd[2] : -1;
        for (int t : {t1, t2}) {
            if (t < 0)
                continue;
            const int ti = static_cast<int>(seg->lit[t].v) - 1;
            // 只对齐反向边目标（循环头）：前向目标常与 fallthrough 重合，
            // 在那里插 nop 会每轮执行。
            if (ti >= 0 && ti < n && ti <= i)
                is_target[static_cast<std::size_t>(ti)] = 1;
        }
    }
    // B1：按矩阵 MicroOp 序列发射。值栈 ≤2（二元），整数值栈用 R10/R11；
    // 唯一分派键是 MicroOp 枚举（与 micro_ops_supported 一一对应）。
    auto emit_scalar = [&](int i) -> bool {
        const CInsn& ci = seg->insns[i];
        const std::uint32_t off = plan.scalar_offset[static_cast<std::size_t>(i)];
        const std::uint32_t cnt = plan.scalar_count[static_cast<std::size_t>(i)];
        if (cnt == 0 || off + cnt > plan.micro_pool.size())
            return false;
        const int dr = ci.wr[0];
        const jit2::ScalarTypeClass lhs = operand_class(*seg, ci.rd[0]);
        const bool lhs_signed =
            lhs == jit2::ScalarTypeClass::SI8 ||
            lhs == jit2::ScalarTypeClass::SI16 ||
            lhs == jit2::ScalarTypeClass::SI32 ||
            lhs == jit2::ScalarTypeClass::SI64;
        const jit2::ScalarTypeClass res = operand_class(*seg, dr);
        const bool res_signed =
            res == jit2::ScalarTypeClass::SI8 ||
            res == jit2::ScalarTypeClass::SI16 ||
            res == jit2::ScalarTypeClass::SI32 ||
            res == jit2::ScalarTypeClass::SI64;
        int depth = 0;
        int loads = 0;
        std::uint8_t cc = 0;
        auto extend = [&](const X2& r, int width, bool sign) {
            if (width >= 8)
                return;
            if (sign)
                c.movsx_r(r, r, width);
            else
                c.movzx_r(r, r, width);
        };
        for (std::uint32_t k = 0; k < cnt; k++) {
            const jit2::MicroOp m = plan.micro_pool[off + k];
            switch (m) {
            case jit2::MicroOp::LoadS8:
            case jit2::MicroOp::LoadS16:
            case jit2::MicroOp::LoadS32:
            case jit2::MicroOp::LoadS64:
            case jit2::MicroOp::LoadZ8:
            case jit2::MicroOp::LoadZ16:
            case jit2::MicroOp::LoadZ32:
            case jit2::MicroOp::LoadZ64: {
                if (loads > 1)
                    return false;
                const int r = ci.rd[loads];
                const X2 scratch = loads == 0 ? kR10 : kR11;
                const RegDesc& d = seg->regs[r];
                if (d.kind == Operand::Kind::Literal) {
                    const vmbin::FastLiteral& l = seg->lit[r];
                    if (!l.is_i64)
                        return false;
                    c.mov_imm64(scratch, l.v);
                } else {
                    const X2 src = read_reg(r, scratch);
                    if (!(src.field == scratch.field && src.rb == scratch.rb))
                        c.mov(scratch, src);
                }
                int width = 8;
                bool sign = true;
                switch (m) {
                case jit2::MicroOp::LoadS8: width = 1; break;
                case jit2::MicroOp::LoadS16: width = 2; break;
                case jit2::MicroOp::LoadS32: width = 4; break;
                case jit2::MicroOp::LoadZ8: width = 1; sign = false; break;
                case jit2::MicroOp::LoadZ16: width = 2; sign = false; break;
                case jit2::MicroOp::LoadZ32: width = 4; sign = false; break;
                default: break;
                }
                extend(scratch, width, sign);
                loads++;
                depth = loads;
                break;
            }
            case jit2::MicroOp::Add64:
                if (depth != 2) return false;
                c.add(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::Sub64:
                if (depth != 2) return false;
                c.sub(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::Mul64:
                if (depth != 2) return false;
                c.imul(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::And64:
                if (depth != 2) return false;
                c.and_(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::Or64:
                if (depth != 2) return false;
                c.or_(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::Xor64:
                if (depth != 2) return false;
                c.xor_(kR10, kR11);
                depth = 1;
                break;
            case jit2::MicroOp::Neg64:
                if (depth != 1) return false;
                c.neg(kR10);
                break;
            case jit2::MicroOp::Shl64:
            case jit2::MicroOp::Sar64:
            case jit2::MicroOp::Shr64:
                if (depth != 2) return false;
                c.mov_stack_from_reg(guard_slot, kRcx);
                c.mov(kRcx, kR11);
                if (m == jit2::MicroOp::Shl64)
                    c.shl_cl(kR10);
                else if (m == jit2::MicroOp::Sar64)
                    c.sar_cl(kR10);
                else
                    c.shr_cl(kR10);
                c.mov_reg_from_stack(kRcx, guard_slot);
                depth = 1;
                break;
            case jit2::MicroOp::CmpSigned:
            case jit2::MicroOp::CmpUnsigned:
                if (depth != 2) return false;
                c.cmp(kR10, kR11);
                cc = setcc_of_op(ci.op);
                if (cc == 0) return false;
                break;
            case jit2::MicroOp::SetCC:
                if (depth != 2 || cc == 0) return false;
                c.setcc(cc, kR10);
                c.movzx_r(kR10, kR10, 1);  // 0/1
                depth = 1;
                break;
            case jit2::MicroOp::Cmov: {
                if (depth != 2) return false;
                std::uint8_t cond = 0;
                if (ci.op == rwir::Op::Max)
                    cond = lhs_signed ? 0x4C : 0x42;  // cmovl / cmovb
                else if (ci.op == rwir::Op::Min)
                    cond = lhs_signed ? 0x4F : 0x47;  // cmovg / cmova
                else
                    return false;
                c.cmovcc(cond, kR10, kR11);
                depth = 1;
                break;
            }
            case jit2::MicroOp::Store8:
            case jit2::MicroOp::Store16:
            case jit2::MicroOp::Store32:
            case jit2::MicroOp::Store64: {
                if (depth != 1) return false;
                int width = 8;
                switch (m) {
                case jit2::MicroOp::Store8: width = 1; break;
                case jit2::MicroOp::Store16: width = 2; break;
                case jit2::MicroOp::Store32: width = 4; break;
                default: break;
                }
                extend(kR10, width, res_signed);
                store_dst(dr, kR10);
                depth = 0;
                break;
            }
            default:
                return false;
            }
        }
        return depth == 0;
    };
    for (int i = 0; i < n; i++) {
        if (is_target[static_cast<std::size_t>(i)])
            while ((c.b.size() & 15) != 0)
                c.w8(0x90);  // nop 对齐
        start[static_cast<std::size_t>(i)] = c.b.size();
        const CInsn& ci = seg->insns[i];
        // 槽物化：把寄存器值写进 BinFrame 槽（payload_lo + tag=I64）。
        // 不参与分配（assignment=-2、spill_slot=-1）的寄存器没有值可物化，
        // 必须跳过：否则 mov_reg_from_stack(kR10, -8) 会读到栈外（会把栈地址
        // 当值写进帧，见 069_sqrt 的 _1）。
        auto stage_reg = [&](int r) {
            if (assignment(r) < 0 && plan.spill_slot[r] < 0)
                return;
            if (assignment(r) >= 0) {
                c.mov_base_from_reg(kR14, 32 * r, kPool[assignment(r)]);
            } else {
                c.mov_reg_from_stack(kR10, spill_slot(r));
                c.mov_base_from_reg(kR14, 32 * r, kR10);
            }
            c.store_u16(kR14, 32 * r + 20, 5);  // tag = I64
        };
        auto reload_reg = [&](int r) {
            if (assignment(r) < 0 && plan.spill_slot[r] < 0)
                return;
            if (assignment(r) >= 0) {
                c.mov_reg_from_base(kPool[assignment(r)], kR14, 32 * r);
            } else {
                c.mov_reg_from_base(kR10, kR14, 32 * r);
                c.mov_stack_from_reg(spill_slot(r), kR10);
            }
        };
        auto emit_exit_record = [&](std::uint16_t kind, std::uint16_t flags,
                                    std::uint32_t detail, std::uint32_t pc,
                                    std::uint32_t resume) {
            c.mov_reg_from_stack(kRdx, exit_slot);
            c.store_u16(kRdx, 8, kind);
            c.store_u16(kRdx, 10, flags);
            c.store_u32(kRdx, 12, static_cast<std::int32_t>(detail));
            c.store_u32(kRdx, 16, static_cast<std::int32_t>(pc));
            c.store_u32(kRdx, 20, static_cast<std::int32_t>(resume));
            c.store_u32(kRdx, 24, 0);
            c.store_u32(kRdx, 28, 0);
            c.store_u64(kRdx, 32, 0);
        };
        auto emit_return_from_stub = [&]() {
            if (frame)
                c.add_rsp(frame);
            emit_restore();
            c.ret();
        };
        // 返回出口：写回所有写参 + StructuredExit(kind=Return)。
        auto emit_output_commit = [&]() {
            for (int r : shape.outputs) {
                const std::int32_t disp = static_cast<std::int32_t>(32 * r);
                if (assignment(r) >= 0) {
                    c.mov_base_from_reg(kR14, disp, kPool[assignment(r)]);
                } else {
                    c.mov_reg_from_stack(kR10, spill_slot(r));
                    c.mov_base_from_reg(kR14, disp, kR10);
                }
                c.store_u16(kR14, disp + 20, 5);  // tag = I64
            }
            emit_exit_record(0 /*Return*/, 0, 0,
                             static_cast<std::uint32_t>(i + 1), 0xFFFFFFFFu);
            c.store_u32(kRdx, 28,
                        static_cast<std::int32_t>(shape.outputs.size()));
            emit_return_from_stub();
        };
        if (plan.handoff_exit[static_cast<std::size_t>(i)]) {
            // Handoff（println 等交外部引擎）：原生到这里停止，解释器接管。
            for (std::size_t r = 0; r < seg->regs.size(); r++)
                if (plan.native_stage[static_cast<std::size_t>(i)][r] &&
                    seg->regs[r].kind == Operand::Kind::Var)
                    stage_reg(static_cast<int>(r));
            emit_exit_record(2 /*Native*/, 1 /*kFrameMaterialized*/, 0,
                             static_cast<std::uint32_t>(i + 1),
                             static_cast<std::uint32_t>(i + 2));
            emit_return_from_stub();
            continue;
        }
        if (plan.call_handler[static_cast<std::size_t>(i)]) {
            // 原生内调 handler：物化活跃槽 → 调 C 侧 helper（解释器同一套
            // handler 表）→ 回填 → 继续原生。helper 返回 1=Error、2=Stop。
            for (std::size_t r = 0; r < seg->regs.size(); r++)
                if (plan.native_stage[static_cast<std::size_t>(i)][r] &&
                    seg->regs[r].kind == Operand::Kind::Var)
                    stage_reg(static_cast<int>(r));
            c.mov_stack_from_reg(guard_slot, kRcx);
            c.mov_reg_from_base(kRdi, kR13, 32);  // frame->legacy_frame_adapter（offset 32）
            c.mov(kRsi, kR14);                    // slots
            c.mov_imm(kRdx, i);                   // 指令下标
            c.mov_reg_from_stack(kRax, ctx_slot);
            c.call_mem(kRax, 16);  // VmContextV1.reserved_u64[0] = helper
            c.mov_reg_from_stack(kRcx, guard_slot);
            c.test(kRax);
            c.jcc(0x84);  // jz continue（0 = 继续原生）
            const std::size_t jz_patch = c.b.size() - 4;
            c.cmp_imm(kRax, 2);
            c.jcc(0x85);  // jnz → error
            const std::size_t jnz_patch = c.b.size() - 4;
            emit_exit_record(2 /*Native*/, 1, 0,
                             static_cast<std::uint32_t>(i + 1),
                             static_cast<std::uint32_t>(i + 2));
            emit_return_from_stub();
            const std::size_t err_pos = c.b.size();
            emit_exit_record(6 /*Error*/, 0, 9 /*kDetailHandler*/, 0, 0);
            emit_return_from_stub();
            const std::int32_t rel_err =
                static_cast<std::int32_t>(err_pos) -
                static_cast<std::int32_t>(jnz_patch + 4);
            std::memcpy(c.b.data() + jnz_patch, &rel_err, 4);
            const std::int32_t rel_ok =
                static_cast<std::int32_t>(c.b.size()) -
                static_cast<std::int32_t>(jz_patch + 4);
            std::memcpy(c.b.data() + jz_patch, &rel_ok, 4);
            for (std::size_t r = 0; r < seg->regs.size(); r++)
                if (plan.native_stage[static_cast<std::size_t>(i)][r] &&
                    seg->regs[r].kind == Operand::Kind::Var)
                    reload_reg(static_cast<int>(r));
            continue;
        }
        if (plan.array_op[static_cast<std::size_t>(i)] != 0) {
            const int arr = ci.rd[0];
            const vmbin::ArrayTypeDesc& at =
                seg->array_types[seg->regs[arr].array_type_id];
            const bool is_set = plan.array_op[static_cast<std::size_t>(i)] == 2;
            const X2 body = kR8;   // 数组 body 指针
            const X2 flat = kR9;   // 展平下标
            {
                const X2 r = read_reg(arr, kR10);
                c.mov(body, r);
            }
            auto read_idx = [&](int rdix, const X2& dst) {
                if (seg->regs[rdix].kind == Operand::Kind::Literal) {
                    const std::int64_t lv = seg->lit[rdix].v;
                    if (lv >= kInt32Min && lv <= kInt32Max)
                        c.mov_imm(dst, static_cast<std::int32_t>(lv));
                    else
                        c.mov_imm64(dst, lv);
                    return;
                }
                const X2 r = read_reg(rdix, kR11);
                if (!(r.field == dst.field && r.rb == dst.rb))
                    c.mov(dst, r);
            };
            read_idx(ci.rd[1], flat);
            if (at.ndim == 2) {
                const std::int64_t st0 = at.strides[0];
                if (st0 != 1) {
                    c.mov_imm(kR10, static_cast<std::int32_t>(st0));
                    c.imul(flat, kR10);
                }
                read_idx(ci.rd[2], kR10);
                c.add(flat, kR10);
            }
            // 边界：无符号比较 flat < numel 一并拒绝负索引。
            c.cmp_imm(flat, static_cast<std::int32_t>(at.numel));
            c.jcc(0x83);  // jae → 越界
            const std::size_t jae_patch = c.b.size() - 4;
            const int scale = at.width == 8   ? 3
                              : at.width == 4 ? 2
                              : at.width == 2 ? 1
                                              : 0;
            const bool sign = at.elem_kind == vmbin::LangType::Int8 ||
                              at.elem_kind == vmbin::LangType::Int16 ||
                              at.elem_kind == vmbin::LangType::Int32 ||
                              at.elem_kind == vmbin::LangType::Int64;
            if (!is_set) {
                const int wr = ci.wr[0];
                if (assignment(wr) >= 0) {
                    c.movsx_idx(kPool[assignment(wr)], body, flat, scale,
                                at.width, sign);
                } else {
                    c.movsx_idx(kR10, body, flat, scale, at.width, sign);
                    c.mov_stack_from_reg(spill_slot(wr), kR10);
                }
            } else {
                const int vr = ci.rd[1 + at.ndim];
                if (seg->regs[vr].kind == Operand::Kind::Literal) {
                    const std::int64_t lv = seg->lit[vr].v;
                    if (lv >= kInt32Min && lv <= kInt32Max)
                        c.mov_imm(kR10, static_cast<std::int32_t>(lv));
                    else
                        c.mov_imm64(kR10, lv);
                } else {
                    const X2 r = read_reg(vr, kR10);
                    if (!(r.field == kR10.field && r.rb == kR10.rb))
                        c.mov(kR10, r);
                }
                if (at.width == 8)
                    c.mov_idx_reg(body, flat, scale, kR10);
                else
                    c.mov_idx_narrow(body, flat, scale, at.width, kR10);
            }
            // 正常路径跳过越界 stub。
            c.jmp();
            const std::size_t jmp_ok = c.b.size() - 4;
            const std::size_t overflow_pos = c.b.size();
            emit_exit_record(6 /*Error*/, 0, 10 /*index out of range*/,
                             static_cast<std::uint32_t>(i + 1), 0);
            emit_return_from_stub();
            const std::int32_t rel = static_cast<std::int32_t>(overflow_pos) -
                                     static_cast<std::int32_t>(jae_patch + 4);
            std::memcpy(c.b.data() + jae_patch, &rel, 4);
            const std::int32_t rel_ok =
                static_cast<std::int32_t>(c.b.size()) -
                static_cast<std::int32_t>(jmp_ok + 4);
            std::memcpy(c.b.data() + jmp_ok, &rel_ok, 4);
            continue;
        }
        if (ci.op == rwir::Op::Return) {
            if (i + 1 == n)
                continue;  // 末尾 return 落到公共出口
            emit_output_commit();
            continue;
        }
        auto lit_value = [&](int idx) {
            return static_cast<std::int32_t>(seg->lit[idx].v);
        };
        if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
            const bool br = ci.op == rwir::Op::Br;
            const int t1 = static_cast<int>(seg->lit[ci.rd[br ? 1 : 0]].v);
            const int t2 = br ? static_cast<int>(seg->lit[ci.rd[2]].v) : 0;
            const bool backward = t1 <= i + 1 || (br && t2 <= i + 1);
            if (backward) {
                c.sub_imm(kRcx, 1);
                c.jcc(0x84);  // jz guard
                fixes.push_back({c.b.size() - 4, 0});
            }
            if (!br) {
                c.jmp();
                fixes.push_back({c.b.size() - 4, t1});
                continue;
            }
            if (i > 0 && plan.fused[static_cast<std::size_t>(i - 1)]) {
                const std::uint8_t cc = jcc_of(plan.opfn[static_cast<std::size_t>(i - 1)]);
                if (t1 == i + 2) {
                    c.jcc(static_cast<std::uint8_t>(cc ^ 1));
                    fixes.push_back({c.b.size() - 4, t2});
                } else if (t2 == i + 2) {
                    c.jcc(cc);
                    fixes.push_back({c.b.size() - 4, t1});
                } else {
                    c.jcc(cc);
                    fixes.push_back({c.b.size() - 4, t1});
                    c.jmp();
                    fixes.push_back({c.b.size() - 4, t2});
                }
            } else {
                const X2 cond = read_reg(ci.rd[0], kR10);
                c.test(cond);
                c.jcc(0x85);  // jne t1
                fixes.push_back({c.b.size() - 4, t1});
                c.jmp();
                fixes.push_back({c.b.size() - 4, t2});
            }
            continue;
        }
        const std::uint8_t fop = plan.fopfn[static_cast<std::size_t>(i)];
        if (fop != 0) {
            const int dr = ci.wr[0];
            c.movsd_load(0, spill_slot(ci.rd[0]));
            if (seg->regs[ci.rd[1]].kind == Operand::Kind::Literal) {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &seg->lit[ci.rd[1]].f, sizeof bits);
                c.mov_imm64(kR11, static_cast<std::int64_t>(bits));
                c.movq_xmm_gpr(1, kR11);
            } else if (fop == 4) {
                c.mov_reg_from_stack(kR11, spill_slot(ci.rd[1]));
                c.movq_xmm_gpr(1, kR11);
            } else {
                c.movsd_load(1, spill_slot(ci.rd[1]));
            }
            if (fop == 4) {
                // Reject +0.0 and -0.0 before `divsd` to raise `ZeroDivisionError`.
                c.mov_imm64(kR10, 0x7FFFFFFFFFFFFFFFLL);
                c.and_(kR10, kR11);
                c.jcc(0x85);  // jnz non-zero
                const std::size_t jnz_ok = c.b.size() - 4;
                emit_exit_record(6 /*Error*/, 0, 11 /*div by zero*/,
                                 static_cast<std::uint32_t>(i + 1), 0);
                emit_return_from_stub();
                const std::int32_t rel =
                    static_cast<std::int32_t>(c.b.size()) -
                    static_cast<std::int32_t>(jnz_ok + 4);
                std::memcpy(c.b.data() + jnz_ok, &rel, 4);
            }
            switch (fop) {
            case 1: c.ss_arith(0x58, 0, 1); break;  // addsd
            case 2: c.ss_arith(0x5C, 0, 1); break;  // subsd
            case 3: c.ss_arith(0x59, 0, 1); break;  // mulsd
            default: c.ss_arith(0x5E, 0, 1); break; // divsd
            }
            c.movsd_store(0, spill_slot(dr));
            continue;
        }
        const std::uint8_t fcast = plan.fcast[static_cast<std::size_t>(i)];
        if (fcast != 0) {
            const int dr = ci.wr[0];
            if (fcast == 1) {  // int64 → float64
                const X2 a = read_reg(ci.rd[0], kR10);
                c.cvtsi2sd(0, a);
                c.movsd_store(0, spill_slot(dr));
            } else {  // float64 → int64（向零截断，对齐 C cast 语义）
                c.movsd_load(0, spill_slot(ci.rd[0]));
                c.cvttsd2si(kR10, 0);
                store_dst(dr, kR10);
            }
            continue;
        }
        const std::uint8_t iop = plan.opfn[static_cast<std::size_t>(i)];
        if (iop >= 13 && iop <= 18) {
            const int a_reg = ci.rd[0];
            X2 a = read_reg(a_reg, kR10);
            if (seg->regs[ci.rd[1]].kind == Operand::Kind::Literal) {
                const std::int64_t lv = seg->lit[ci.rd[1]].v;
                if (lv >= kInt32Min && lv <= kInt32Max) {
                    c.cmp_imm(a, static_cast<std::int32_t>(lv));
                } else {
                    c.mov_imm64(kR11, lv);
                    c.cmp(a, kR11);
                }
            } else {
                X2 b = read_reg(ci.rd[1], kR11);
                c.cmp(a, b);
            }
            if (!plan.fused[static_cast<std::size_t>(i)]) {
                const int d = ci.wr[0];
                if (assignment(d) >= 0) {
                    c.mov_imm(kPool[assignment(d)], 0);
                    c.setcc(setcc_of(iop), kPool[assignment(d)]);
                } else {
                    c.mov_imm(kR10, 0);
                    // 溢出目的寄存器也要用「解析后的 opfn」
                    // （动态算子的 ci.opfn 为 0，用它会落到 default=setge）。
                    c.setcc(setcc_of(iop), kR10);
                    c.mov_stack_from_reg(spill_slot(d), kR10);
                }
            }
            continue;
        }
        if (iop >= 1 && iop <= 12) {
            const int dr = ci.wr[0];
            const bool dst_alloc = assignment(dr) >= 0;
            const X2 d = dst_alloc ? kPool[assignment(dr)] : kR10;
            const bool rhs_lit = seg->regs[ci.rd[1]].kind == Operand::Kind::Literal;
            const std::int64_t rhs_val = rhs_lit ? seg->lit[ci.rd[1]].v : 0;
            auto rhs_inline = [&]() -> bool {
                return rhs_lit && rhs_val >= kInt32Min && rhs_val <= kInt32Max;
            };
            auto load_rhs = [&](const X2& scratch) -> X2 {
                if (rhs_lit) {
                    if (rhs_val >= kInt32Min && rhs_val <= kInt32Max)
                        c.mov_imm(scratch, static_cast<std::int32_t>(rhs_val));
                    else
                        c.mov_imm64(scratch, rhs_val);
                    return scratch;
                }
                return read_reg(ci.rd[1], scratch);
            };
            auto load_a = [&]() {
                const X2 a = read_reg(ci.rd[0], kR11);
                if (!(a.field == d.field && a.rb == d.rb))
                    c.mov(d, a);
            };
            switch (iop) {
            case 1:  // add
                load_a();
                if (rhs_inline()) c.add_imm(d, static_cast<std::int32_t>(rhs_val));
                else c.add(d, load_rhs(kR11));
                break;
            case 2:  // sub
                load_a();
                if (rhs_inline()) c.add_imm(d, static_cast<std::int32_t>(-rhs_val));
                else {
                    const X2 b = load_rhs(kR11);
                    c.sub(d, b);
                }
                break;
            case 3:  // mul
                load_a();
                c.imul(d, load_rhs(kR11));
                break;
            case 6:    // max
            case 7: {  // min
                load_a();
                const X2 b = load_rhs(kR11);
                c.cmp(d, b);
                c.cmovcc(iop == 6 ? 0x4C : 0x4F, d, b);  // cmovl / cmovg
                break;
            }
            case 8:  // and
                load_a();
                c.and_(d, load_rhs(kR11));
                break;
            case 9:  // or
                load_a();
                c.or_(d, load_rhs(kR11));
                break;
            case 10:  // xor
                load_a();
                c.xor_(d, load_rhs(kR11));
                break;
            case 11:    // shl (a << (b & 63))
            case 12: {  // shr (a >> (b & 63))
                c.mov_stack_from_reg(guard_slot, kRcx);
                load_a();
                if (rhs_lit) {
                    if (rhs_val >= kInt32Min && rhs_val <= kInt32Max)
                        c.mov_imm(kRcx, static_cast<std::int32_t>(rhs_val));
                    else
                        c.mov_imm64(kRcx, rhs_val);
                } else {
                    c.mov(kRcx, read_reg(ci.rd[1], kR11));
                }
                if (iop == 11) c.shl_cl(d); else c.shr_cl(d);
                c.mov_reg_from_stack(kRcx, guard_slot);
                break;
            }
            case 4:    // div（b=0 → 除零错误；b=-1 → -a，避开 INT64_MIN 溢出）
            case 5: {  // mod（b=0 → 除零错误；b=-1 → 0）
                // RAX 可能承载 live 值（输入/池槽 0）：先存再占用。
                c.mov_stack_from_reg(tmp_slot, kRax);
                // 先把被除数放进 RAX（read_reg 可能用 kR11 作 scratch），
                // 再物化除数——否则字面量除数会被被除数覆盖。
                c.mov(kRax, read_reg(ci.rd[0], kR11));
                const X2 b = load_rhs(kR11);
                c.mov_imm(kR10, 0);
                c.test(b);
                c.jcc(0x84);  // jz → 除零错误
                const std::size_t jz0 = c.b.size() - 4;
                c.cmp_imm(b, -1);
                c.jcc(0x85);  // jnz → 正常
                const std::size_t jnz1 = c.b.size() - 4;
                if (iop == 4) {
                    c.mov(kR10, kRax);
                    c.neg(kR10);
                }
                c.jmp();
                const std::size_t jmp_done = c.b.size() - 4;
                const std::size_t normal = c.b.size();
                {
                    const std::int32_t rel_n =
                        static_cast<std::int32_t>(normal) -
                        static_cast<std::int32_t>(jnz1 + 4);
                    std::memcpy(c.b.data() + jnz1, &rel_n, 4);
                }
                c.cqo();
                c.idiv(b);
                if (iop == 4)
                    c.mov(kR10, kRax);
                else
                    c.mov(kR10, kRdx);
                c.mov_reg_from_stack(kRax, tmp_slot);
                c.jmp();
                const std::size_t jmp_over_zero = c.b.size() - 4;
                const std::size_t zero_pos = c.b.size();
                emit_exit_record(6 /*Error*/, 0, 11 /*div by zero*/,
                                 static_cast<std::uint32_t>(i + 1), 0);
                emit_return_from_stub();
                const std::size_t done = c.b.size();
                {
                    const std::int32_t rel0 =
                        static_cast<std::int32_t>(zero_pos) -
                        static_cast<std::int32_t>(jz0 + 4);
                    std::memcpy(c.b.data() + jz0, &rel0, 4);
                }
                for (std::size_t pch : {jmp_done, jmp_over_zero}) {
                    const std::int32_t rel =
                        static_cast<std::int32_t>(done) -
                        static_cast<std::int32_t>(pch + 4);
                    std::memcpy(c.b.data() + pch, &rel, 4);
                }
                if (dst_alloc && !(d.field == kR10.field && d.rb == kR10.rb))
                    c.mov(d, kR10);
                else if (!dst_alloc)
                    c.mov(kR10, kR10);
                break;
            }
            default:
                return {};
            }
            if (!dst_alloc)
                c.mov_stack_from_reg(spill_slot(dr), d);
            continue;
        }
        if (plan.scalar_count[static_cast<std::size_t>(i)] != 0) {
            if (!emit_scalar(i))
                return {};
            continue;
        }
        if (ci.op == rwir::Op::Copy) {
            const int dr = ci.wr[0];
            if (seg->regs[ci.rd[0]].kind == Operand::Kind::Literal &&
                seg->lit[ci.rd[0]].is_f64) {
                // float64 字面量：位模式直写目的栈槽（目的恒为 float 寄存器）。
                std::uint64_t bits = 0;
                std::memcpy(&bits, &seg->lit[ci.rd[0]].f, sizeof bits);
                c.mov_imm64(kR10, static_cast<std::int64_t>(bits));
                store_dst(dr, kR10);
                continue;
            }
            if (seg->regs[ci.rd[0]].kind == Operand::Kind::Literal) {
                const std::int64_t lv = seg->lit[ci.rd[0]].v;
                auto put = [&](const X2& dst) {
                    if (lv >= kInt32Min && lv <= kInt32Max)
                        c.mov_imm(dst, static_cast<std::int32_t>(lv));
                    else
                        c.mov_imm64(dst, lv);
                };
                if (assignment(dr) >= 0) {
                    put(kPool[assignment(dr)]);
                } else {
                    put(kR10);
                    c.mov_stack_from_reg(spill_slot(dr), kR10);
                }
            } else {
                X2 s = read_reg(ci.rd[0], kR10);
                store_dst(dr, s);
            }
            continue;
        }
        return {};
    }
    // 公共出口：写回所有写参
    const std::size_t epilogue_pos = c.b.size();
    c.mov_reg_from_stack(kRdx, exit_slot);  // handler 内调可能清掉 RDX
    for (int r : shape.outputs) {
        const std::int32_t disp = static_cast<std::int32_t>(32 * r);
        if (assignment(r) >= 0) {
            c.mov_base_from_reg(kR14, disp, kPool[assignment(r)]);
        } else {
            c.mov_reg_from_stack(kR10, spill_slot(r));
            c.mov_base_from_reg(kR14, disp, kR10);
        }
        c.store_u16(kR14, disp + 20, 5);  // tag = I64
    }
    // StructuredExitV1：kind=Return, pc=n+1, resume_pc=kNoPc, value_count=写参数
    c.store_u16(kRdx, 8, 0);
    c.store_u16(kRdx, 10, 0);
    c.store_u32(kRdx, 12, 0);
    c.store_u32(kRdx, 16, static_cast<std::uint32_t>(n + 1));
    c.store_u32(kRdx, 20, 0xFFFFFFFFu);
    c.store_u32(kRdx, 24, 0);
    c.store_u32(kRdx, 28, static_cast<std::uint32_t>(shape.outputs.size()));
    c.store_u64(kRdx, 32, 0);
    if (frame)
        c.add_rsp(frame);
    emit_restore();
    c.ret();
    // guard stub：kind=Error, detail_code=kDetailRunaway
    const std::size_t guard_stub = c.b.size();
    c.store_u16(kRdx, 8, 6);
    c.store_u16(kRdx, 10, 0);
    c.store_u32(kRdx, 12, 2);
    c.store_u32(kRdx, 16, 0);
    c.store_u32(kRdx, 20, 0xFFFFFFFFu);
    c.store_u32(kRdx, 24, 0);
    c.store_u32(kRdx, 28, 0);
    c.store_u64(kRdx, 32, 0);
    if (frame)
        c.add_rsp(frame);
    emit_restore();
    c.ret();
    for (const Fixup& fx : fixes) {
        const std::size_t tgt =
            fx.target == 0
                ? guard_stub
                : (fx.target == n + 1
                       ? epilogue_pos
                       : start[static_cast<std::size_t>(fx.target - 1)]);
        const std::int32_t rel = static_cast<std::int32_t>(tgt) -
                                 static_cast<std::int32_t>(fx.pos + 4);
        std::memcpy(c.b.data() + fx.pos, &rel, 4);
    }
    if (std::getenv("RC2_JIT2DUMP")) {
        std::fprintf(stderr, "[jit2dump %zu bytes]\n", c.b.size());
        for (std::uint8_t x : c.b)
            std::fprintf(stderr, "%02x ", x);
        std::fprintf(stderr, "\n");
    }
    return std::move(c.b);
#else
    (void)seg;
    (void)plan;
    return {};
#endif
}

}  // namespace kvlang::runtimec2
