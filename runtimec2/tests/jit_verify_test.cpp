// JIT verify() 负向/正向单测：直接构造 Segment，断言拒绝原因。
// 编译方式见 CMake 片段（jit.cpp + jit_x64.cpp + 本文件）；jit.cpp 的
// vmkv 依赖在下方以最小桩替代，测试只调用 jit_verify，不执行机器码。
#include <cstdio>
#include <cstdlib>
#include <string>

#include "codeseg.hpp"
#include "jit.hpp"
#include "valueio.hpp"
#include "vmkv.hpp"

namespace kvlang::runtimec2 {
void view_cache_idle_all() noexcept {}
Vmkv g_vmkv;
bool Vmkv::store_i64_slot(vmbin::Frame&, vmbin::Register&, std::int64_t) {
    return true;
}
}  // namespace kvlang::runtimec2

namespace {

using namespace kvlang::runtimec2;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

int push_lit(Segment* seg, std::int64_t v) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Literal;
    seg->lit.push_back(LitFast{true, v});
    return static_cast<int>(seg->regs.size()) - 1;
}

int push_var(Segment* seg, const char* name, int param_k) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Var;
    seg->regs.back().name = name;
    seg->regs.back().param_k = param_k;
    seg->lit.push_back(LitFast{});  // lit 与 regs 同下标对齐
    return static_cast<int>(seg->regs.size()) - 1;
}

void push_insn(Segment* seg, rwir::Op op, int s0,
               std::initializer_list<int> rd, std::initializer_list<int> wr,
               bool fast_i64 = false, int opfn = 0) {
    CInsn ci;
    ci.op = op;
    ci.impl = (op == rwir::Op::Goto || op == rwir::Op::Br) ? rwir::Impl::Control
                                                           : rwir::Impl::Typed;
    ci.s0 = s0;
    ci.fast_i64 = fast_i64;
    ci.opfn = static_cast<std::uint8_t>(opfn);
    ci.nrd = static_cast<std::uint8_t>(rd.size());
    ci.nwr = static_cast<std::uint8_t>(wr.size());
    int k = 0;
    for (int x : rd) ci.rd[k++] = static_cast<std::uint8_t>(x);
    k = 0;
    for (int x : wr) ci.wr[k++] = static_cast<std::uint8_t>(x);
    seg->insns.push_back(ci);
}

// iops 形状：1 读参/1 写参，Copy + le + add + 前向 br + 后向 goto + return。
Segment iops_segment() {
    Segment s;
    const int dst = push_var(&s, "*[0,1]", 1);
    const int src = push_var(&s, "*[0,-1]", -1);
    const int i = push_var(&s, "i", 0);
    const int acc = push_var(&s, "acc", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    const int t1 = push_lit(&s, 4);
    const int t2 = push_lit(&s, 6);
    const int t3 = push_lit(&s, 9);
    const int t4 = push_lit(&s, 10);
    const int back = push_lit(&s, 4);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    push_insn(&s, rwir::Op::Copy, 2, {l1}, {i});
    push_insn(&s, rwir::Op::Goto, 3, {t1}, {});
    push_insn(&s, rwir::Op::Le, 4, {i, src}, {acc}, true, 16);
    push_insn(&s, rwir::Op::Br, 5, {acc, t2, t3}, {});
    push_insn(&s, rwir::Op::Add, 6, {dst, l1}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Add, 7, {i, t4}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 8, {back}, {});
    push_insn(&s, rwir::Op::Return, 9, {}, {});
    return s;
}

void expect_accept(const Segment& s, const char* what) {
    JitRefusal r;
    check(jit_verify(&s, &r), what);
    if (r.refusal_reason != VmjitRefusalReason::None)
        std::fprintf(stderr, "  %s: unexpected reason %u\n", what,
                     static_cast<unsigned>(r.refusal_reason));
}

void expect_refuse(const Segment& s, VmjitRefusalReason want, const char* what) {
    JitRefusal r;
    const bool ok = jit_verify(&s, &r);
    check(!ok && r.refusal_reason == want, what);
    if (ok)
        std::fprintf(stderr, "  %s: accepted, want refusal %u\n", what,
                     static_cast<unsigned>(want));
}

}  // namespace

int main() {
    expect_accept(iops_segment(), "iops shape accepted");

    {
        Segment s;
        expect_refuse(s, VmjitRefusalReason::MalformedSegment, "empty segment");
    }
    {
        Segment s = iops_segment();
        s.has_rwirhandler = true;
        expect_refuse(s, VmjitRefusalReason::MalformedSegment, "has rwirhandler");
    }
    {
        Segment s = iops_segment();
        s.lit[4].is_i64 = false;
        expect_refuse(s, VmjitRefusalReason::TypeMismatch, "literal not int64");
    }
    {
        Segment s = iops_segment();
        s.lit[4].v = 1LL << 40;
        expect_refuse(s, VmjitRefusalReason::TypeMismatch, "literal out of int32");
    }
    {
        Segment s = iops_segment();
        const int p = push_var(&s, "*p", 0);
        s.insns[0].rd[0] = static_cast<std::uint8_t>(p);
        expect_refuse(s, VmjitRefusalReason::UnsupportedOperand, "ptr operand");
    }
    {
        Segment s = iops_segment();
        push_var(&s, "*[0,-2]", -2);
        expect_refuse(s, VmjitRefusalReason::BadParamLayout, "two read params");
    }
    {
        Segment s = iops_segment();
        s.insns[2].s0 = 5;  // 破坏 s0 稠密
        expect_refuse(s, VmjitRefusalReason::BadTarget, "non-dense s0");
    }
    {
        Segment s = iops_segment();
        s.lit[6].v = 99;  // 分支目标越界
        expect_refuse(s, VmjitRefusalReason::BadTarget, "branch target range");
    }
    {
        Segment s = iops_segment();
        s.insns.pop_back();  // 末条不是 return
        expect_refuse(s, VmjitRefusalReason::OutputNotDefined, "no trailing return");
    }
    {
        Segment s = iops_segment();
        s.insns[5].opfn = 2;  // sub：不在白名单
        expect_refuse(s, VmjitRefusalReason::UnsupportedOpcode, "sub opcode");
    }
    {
        Segment s = iops_segment();
        s.insns[5].wr[0] = 1;  // iops_segment 的读参下标
        expect_refuse(s, VmjitRefusalReason::UnsupportedOperand, "write to src");
    }
    {
        Segment s = iops_segment();
        s.insns[0].rd[0] = 200;  // 越界操作数
        expect_refuse(s, VmjitRefusalReason::MalformedSegment, "rd out of range");
    }
    // 9 个局部变量：池耗尽
    {
        Segment s;
        const int dst = push_var(&s, "*[0,1]", 1);
        const int src = push_var(&s, "*[0,-1]", -1);
        const int l0 = push_lit(&s, 0);
        push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
        int regs[9];
        for (int k = 0; k < 9; k++) regs[k] = push_var(&s, "loc", 0);
        int s0 = 2;
        for (int k = 0; k < 9; k++)
            push_insn(&s, rwir::Op::Add, s0++, {src, l0}, {regs[k]}, true, 1);
        for (int k = 0; k < 9; k++)
            push_insn(&s, rwir::Op::Add, s0++, {dst, regs[k]}, {dst}, true, 1);
        push_insn(&s, rwir::Op::Return, s0, {}, {});
        expect_refuse(s, VmjitRefusalReason::RegisterPressure, "register pressure");
    }
    // 先读后写 dst（r = r + x）
    {
        Segment s;
        const int dst = push_var(&s, "*[0,1]", 1);
        const int src = push_var(&s, "*[0,-1]", -1);
        push_insn(&s, rwir::Op::Add, 1, {dst, src}, {dst}, true, 1);
        push_insn(&s, rwir::Op::Return, 2, {}, {});
        expect_refuse(s, VmjitRefusalReason::UninitializedRead, "read before write");
    }
    // 写参未在 return 前定义：首条写 dst 被移除后由 F3 拒绝
    {
        Segment s;
        const int dst = push_var(&s, "*[0,1]", 1);
        const int src = push_var(&s, "*[0,-1]", -1);
        const int l0 = push_lit(&s, 0);
        push_insn(&s, rwir::Op::Add, 1, {src, l0}, {dst}, true, 1);
        push_insn(&s, rwir::Op::Return, 2, {}, {});
        expect_accept(s, "dst defined before return");
    }

    if (g_failures == 0) {
        std::printf("OK jit_verify_test\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
