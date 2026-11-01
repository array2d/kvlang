// 第二阶段原型单测：构造 Segment → jit2_verify（CFG/活跃/分配）→ jit2_emit → 执行。
#include <cstdio>
#include <cstring>
#include <vector>

#include <sys/mman.h>

#include "codeseg.hpp"
#include "jit_stage2.hpp"

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
    seg->lit.push_back(LitFast{});
    return static_cast<int>(seg->regs.size()) - 1;
}
void push_insn(Segment* seg, rwir::Op op, int s0, std::initializer_list<int> rd,
               std::initializer_list<int> wr, bool fast_i64 = false,
               int opfn = 0) {
    CInsn ci;
    ci.op = op;
    ci.impl = (op == rwir::Op::Goto || op == rwir::Op::Br) ? rwir::Impl::Control
                                                           : rwir::Impl::Typed;
    (void)s0;
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

using Jit2Fn = jitabi::GeneralJitFnV1;

struct Exec {
    void* mem = nullptr;
    std::size_t len = 0;
    ~Exec() {
        if (mem) munmap(mem, len);
    }
};

bool run_with_exit(const Jit2Plan& plan, const Segment& seg,
                   const std::vector<std::int64_t>& args,
                   std::vector<std::int64_t>* slots_out,
                   jitabi::StructuredExitV1* exit_out) {
    const auto bytes = jit2_emit(&seg, plan);
    check(!bytes.empty(), "emit non-empty");
    if (bytes.empty()) return false;
    void* mem = mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(mem != MAP_FAILED, "mmap");
    if (mem == MAP_FAILED) return false;
    std::memcpy(mem, bytes.data(), bytes.size());
    check(mprotect(mem, bytes.size(), PROT_READ | PROT_EXEC) == 0, "mprotect");
    std::vector<jitabi::ValueSlotV1> slots(seg.regs.size());
    for (std::size_t i = 0; i < args.size() && i < plan.shape.inputs.size(); i++) {
        const std::size_t r = static_cast<std::size_t>(plan.shape.inputs[i]);
        slots[r].payload_lo = static_cast<std::uint64_t>(args[i]);
        slots[r].tag = jitabi::ValueTagV1::I64;
    }
    jitabi::BinFrameV1 frame{};
    frame.abi_version = jitabi::kAbiVersionV1;
    frame.struct_size = sizeof(frame);
    frame.slots = slots.data();
    frame.slot_count = static_cast<std::uint32_t>(slots.size());
    frame.pc = 1;
    jitabi::VmContextV1 context{};
    context.abi_version = jitabi::kAbiVersionV1;
    context.struct_size = sizeof(context);
    jitabi::StructuredExitV1 exit_record{};
    auto fn = reinterpret_cast<Jit2Fn>(mem);
    fn(&context, &frame, &exit_record);
    munmap(mem, bytes.size());
    if (slots_out) {
        slots_out->assign(slots.size(), 0);
        for (std::size_t i = 0; i < slots.size(); i++)
            (*slots_out)[i] = static_cast<std::int64_t>(slots[i].payload_lo);
    }
    if (exit_out)
        *exit_out = exit_record;
    const bool ok = exit_record.kind != jitabi::ExitKindV1::Error;
    check(ok, "no runaway");
    return ok;
}

bool run(const Jit2Plan& plan, const Segment& seg,
         const std::vector<std::int64_t>& args,
         std::vector<std::int64_t>* outs) {
    std::vector<std::int64_t> slots;
    jitabi::StructuredExitV1 exit_record{};
    if (!run_with_exit(plan, seg, args, &slots, &exit_record))
        return false;
    if (exit_record.kind != jitabi::ExitKindV1::Return)
        return false;  // 只用于 Return 出口的用例
    outs->assign(plan.shape.outputs.size(), 0);
    for (std::size_t j = 0; j < plan.shape.outputs.size(); j++)
        (*outs)[j] = slots[static_cast<std::size_t>(plan.shape.outputs[j])];
    return true;
}

bool verify_and_report(const Segment& seg, Jit2Plan* plan, const char* what) {
    JitRefusal r;
    const bool ok = jit2_verify(&seg, plan, &r);
    if (!ok)
        std::fprintf(stderr, "  %s: refused reason=%u detail=%u\n", what,
                     static_cast<unsigned>(r.refusal_reason), r.detail_code);
    return ok;
}

// A）0 读参 / 1 写参：r = 0+1+2+3+4
Segment zero_param_loop() {
    Segment s;
    const int r = push_var(&s, "*[0,1]", 1);
    const int i = push_var(&s, "i", 0);
    const int cond = push_var(&s, "cond", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    const int l5 = push_lit(&s, 5);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {r});
    push_insn(&s, rwir::Op::Copy, 2, {l0}, {i});
    push_insn(&s, rwir::Op::Le, 3, {i, l5}, {cond}, true, 16);
    push_insn(&s, rwir::Op::Br, 4, {cond, push_lit(&s, 5), push_lit(&s, 8)}, {});
    push_insn(&s, rwir::Op::Add, 5, {r, i}, {r}, true, 1);
    push_insn(&s, rwir::Op::Add, 6, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 7, {push_lit(&s, 3)}, {});
    push_insn(&s, rwir::Op::Return, 8, {}, {});
    return s;
}

// B）1 读参 / 2 写参：x = a+1, y = a+2
Segment pair() {
    Segment s;
    const int a = push_var(&s, "*[0,-1]", -1);
    const int x = push_var(&s, "*[0,1]", 1);
    const int y = push_var(&s, "*[0,2]", 2);
    const int l1 = push_lit(&s, 1);
    const int l2 = push_lit(&s, 2);
    push_insn(&s, rwir::Op::Add, 1, {a, l1}, {x}, true, 1);
    push_insn(&s, rwir::Op::Add, 2, {a, l2}, {y}, true, 1);
    push_insn(&s, rwir::Op::Return, 3, {}, {});
    return s;
}

// C）2 读参 / 1 写参 + 局部临时变量循环（压力足够触发溢出）
Segment mix() {
    Segment s;
    const int a = push_var(&s, "*[0,-1]", -1);
    const int b = push_var(&s, "*[0,-2]", -2);
    const int r = push_var(&s, "*[0,1]", 1);
    const int i = push_var(&s, "i", 0);
    const int t = push_var(&s, "t", 0);
    const int cond = push_var(&s, "cond", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Copy, 1, {a}, {r});
    push_insn(&s, rwir::Op::Copy, 2, {l0}, {i});
    push_insn(&s, rwir::Op::Lt, 3, {i, b}, {cond}, true, 15);
    push_insn(&s, rwir::Op::Br, 4, {cond, push_lit(&s, 5), push_lit(&s, 9)}, {});
    push_insn(&s, rwir::Op::Add, 5, {r, i}, {t}, true, 1);
    push_insn(&s, rwir::Op::Add, 6, {t, l1}, {r}, true, 1);
    push_insn(&s, rwir::Op::Add, 7, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 8, {push_lit(&s, 3)}, {});
    push_insn(&s, rwir::Op::Return, 9, {}, {});
    return s;
}

// D）压力：8 个局部同时活跃，必须溢出而不是拒绝
Segment pressure() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    int loc[8];
    int s0 = 2;
    for (int k = 0; k < 8; k++) {
        loc[k] = push_var(&s, "loc", 0);
        push_insn(&s, rwir::Op::Add, s0++, {src, l1}, {loc[k]}, true, 1);
    }
    for (int k = 0; k < 8; k++)
        push_insn(&s, rwir::Op::Add, s0++, {dst, loc[k]}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Return, s0, {}, {});
    return s;
}

// F）0 返回值（无写参）的过程
Segment no_return_value() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int i = push_var(&s, "i", 0);
    const int cond = push_var(&s, "cond", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {i});
    push_insn(&s, rwir::Op::Lt, 2, {i, src}, {cond}, true, 15);
    push_insn(&s, rwir::Op::Br, 3, {cond, push_lit(&s, 4), push_lit(&s, 6)}, {});
    push_insn(&s, rwir::Op::Add, 4, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 5, {push_lit(&s, 2)}, {});
    return s;
}

// G）虚拟出口：末条是 br -> n+1，没有 Return
Segment virtual_exit() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int cond = push_var(&s, "cond", 0);
    const int l0 = push_lit(&s, 0);
    const int l10 = push_lit(&s, 10);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    push_insn(&s, rwir::Op::Add, 2, {dst, src}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Lt, 3, {dst, l10}, {cond}, true, 15);
    push_insn(&s, rwir::Op::Br, 4, {cond, push_lit(&s, 2), push_lit(&s, 5)}, {});
    return s;  // n=4，n+1=5 = 虚拟出口
}

// H）入口就是 goto（入口块先跳到后面的块）
Segment goto_entry() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int l99 = push_lit(&s, 99);
    push_insn(&s, rwir::Op::Goto, 1, {push_lit(&s, 3)}, {});
    push_insn(&s, rwir::Op::Copy, 2, {l99}, {dst});  // 不可达
    push_insn(&s, rwir::Op::Copy, 3, {src}, {dst});
    push_insn(&s, rwir::Op::Return, 4, {}, {});
    return s;
}

// I）冷 handler：循环之后的首个 handler（允许 JIT，Native 出口）
Segment cold_handler() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int i = push_var(&s, "i", 0);
    const int cond = push_var(&s, "cond", 0);
    const int t = push_var(&s, "t", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    push_insn(&s, rwir::Op::Copy, 2, {l0}, {i});
    push_insn(&s, rwir::Op::Lt, 3, {i, src}, {cond}, true, 15);
    push_insn(&s, rwir::Op::Br, 4, {cond, push_lit(&s, 5), push_lit(&s, 8)}, {});
    push_insn(&s, rwir::Op::Add, 5, {dst, i}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Add, 6, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 7, {push_lit(&s, 3)}, {});
    // handler（time·now）：在循环之后
    s.regs.push_back(RegDesc{});
    s.regs.back().kind = Operand::Kind::Var;
    s.regs.back().name = "t";
    s.lit.push_back(LitFast{});
    s.insns.push_back(CInsn{});
    s.insns.back().handler = rwir::RwirHandlerId::TimeNow;
    s.insns.back().nwr = 1;
    s.insns.back().wr[0] = static_cast<std::uint8_t>(t);
    push_insn(&s, rwir::Op::Add, 9, {dst, t}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Return, 10, {}, {});
    return s;
}

// J）热 handler：handler 在循环体内（必须拒绝 HandlerDensity）
Segment hot_handler() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int i = push_var(&s, "i", 0);
    const int cond = push_var(&s, "cond", 0);
    const int t = push_var(&s, "t", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    push_insn(&s, rwir::Op::Copy, 2, {l0}, {i});
    push_insn(&s, rwir::Op::Lt, 3, {i, src}, {cond}, true, 15);
    push_insn(&s, rwir::Op::Br, 4, {cond, push_lit(&s, 5), push_lit(&s, 8)}, {});
    s.regs.push_back(RegDesc{});
    s.regs.back().kind = Operand::Kind::Var;
    s.regs.back().name = "t";
    s.lit.push_back(LitFast{});
    s.insns.push_back(CInsn{});
    s.insns.back().handler = rwir::RwirHandlerId::TimeNow;
    s.insns.back().nwr = 1;
    s.insns.back().wr[0] = static_cast<std::uint8_t>(t);
    push_insn(&s, rwir::Op::Add, 6, {dst, i}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Add, 7, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Goto, 8, {push_lit(&s, 3)}, {});
    push_insn(&s, rwir::Op::Return, 9, {}, {});
    return s;
}

}  // namespace

int main() {
    // A
    {
        Segment s = zero_param_loop();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "zero_param_loop"), "A verify");
        check(plan.blocks.size() >= 3, "A basic blocks");
        std::vector<std::int64_t> outs(1, 0);
        check(run(plan, s, {}, &outs), "A run");
        check(outs[0] == 15, "A result == 15");
        std::printf("A zero-param: blocks=%zu spills=%d result=%lld\n",
                    plan.blocks.size(), plan.spill_count,
                    static_cast<long long>(outs[0]));
    }
    // B
    {
        Segment s = pair();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "pair"), "B verify");
        std::vector<std::int64_t> outs(2, 0);
        check(run(plan, s, {10}, &outs), "B run");
        check(outs[0] == 11 && outs[1] == 12, "B results 11/12");
        std::printf("B multi-write: spills=%d result=%lld,%lld\n", plan.spill_count,
                    (long long)outs[0], (long long)outs[1]);
    }
    // C
    {
        Segment s = mix();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "mix"), "C verify");
        std::vector<std::int64_t> outs(1, 0);
        check(run(plan, s, {2, 4}, &outs), "C run");
        check(outs[0] == 12, "C result == 12");
        std::printf("C multi-read loop: blocks=%zu spills=%d result=%lld\n",
                    plan.blocks.size(), plan.spill_count,
                    static_cast<long long>(outs[0]));
    }
    // D
    {
        Segment s = pressure();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "pressure"), "D verify");
        std::vector<std::int64_t> outs(1, 0);
        check(run(plan, s, {10}, &outs), "D run");
        check(outs[0] == 88, "D result == 88");
        std::printf("D pressure: spills=%d result=%lld\n", plan.spill_count,
                    (long long)outs[0]);
    }
    // F）0 返回值
    {
        Segment s = no_return_value();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "no_return_value"), "F verify");
        check(plan.shape.outputs.empty(), "F zero outputs");
        std::vector<std::int64_t> outs;
        check(run(plan, s, {3}, &outs), "F run");
        std::printf("F no-return: blocks=%zu spills=%d\n", plan.blocks.size(),
                    plan.spill_count);
    }
    // G）虚拟出口（无 Return）
    {
        Segment s = virtual_exit();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "virtual_exit"), "G verify");
        std::vector<std::int64_t> outs(1, 0);
        check(run(plan, s, {3}, &outs), "G run");
        check(outs[0] == 12, "G result == 12");
        std::printf("G virtual-exit: blocks=%zu result=%lld\n", plan.blocks.size(),
                    (long long)outs[0]);
    }
    // H）入口 goto
    {
        Segment s = goto_entry();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "goto_entry"), "H verify");
        std::vector<std::int64_t> outs(1, 0);
        check(run(plan, s, {7}, &outs), "H run");
        check(outs[0] == 7, "H result == 7");
        std::printf("H goto-entry: blocks=%zu result=%lld\n", plan.blocks.size(),
                    (long long)outs[0]);
    }
    // I）冷 handler：允许，Native 出口记录正确，live 值已物化到 slots
    {
        Segment s = cold_handler();
        Jit2Plan plan;
        check(verify_and_report(s, &plan, "cold_handler"), "I verify");
        std::vector<std::int64_t> slots;
        jitabi::StructuredExitV1 ex{};
        check(run_with_exit(plan, s, {5}, &slots, &ex), "I run");
        check(ex.kind == jitabi::ExitKindV1::Native, "I Native exit");
        check(ex.pc == 8, "I exit pc == 8");
        check(ex.aux0 == static_cast<std::uint64_t>(rwir::RwirHandlerId::TimeNow),
              "I aux0 == handler id");
        const int dst = plan.shape.outputs[0];
        check(slots[static_cast<std::size_t>(dst)] == 10,
              "I live dst staged (0+1+2+3+4)");
        std::printf("I cold-handler: kind=%u pc=%u aux0=%llu staged_dst=%lld\n",
                    static_cast<unsigned>(ex.kind), ex.pc, (unsigned long long)ex.aux0,
                    (long long)slots[static_cast<std::size_t>(dst)]);
    }
    // J）热 handler：拒绝 HandlerDensity
    {
        Segment s = hot_handler();
        Jit2Plan plan;
        JitRefusal r;
        check(!jit2_verify(&s, &plan, &r) &&
                  r.refusal_reason == VmjitRefusalReason::HandlerDensity,
              "J hot handler refused");
        std::printf("J hot-handler: refused reason=%u percent=%d\n",
                    static_cast<unsigned>(r.refusal_reason), plan.handler_percent);
    }
    // E）不支持 opcode 仍拒绝
    {
        Segment s = pair();
        s.insns[0].opfn = 2;  // sub
        Jit2Plan plan;
        JitRefusal r;
        check(!jit2_verify(&s, &plan, &r) &&
                  r.refusal_reason == VmjitRefusalReason::UnsupportedOpcode,
              "E sub still refused");
    }

    if (g_failures == 0) {
        std::printf("OK jit_stage2_test\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
