// B2 建段期派生表单测：只验证 impl=Dynamic 指令按读参静态类型定型。
#include <cstdio>
#include <initializer_list>

#include "codeseg.hpp"
#include "dispatch_derive.hpp"

namespace {

using namespace kvlang::runtimec2;

int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

int push_var(Segment* seg, const char* name, int param_k,
             vmbin::LangType lt, std::uint8_t width) {
    seg->regs.push_back(RegDesc{});
    RegDesc& d = seg->regs.back();
    d.kind = Operand::Kind::Var;
    d.name = name;
    d.param_k = param_k;
    d.langtype = lt;
    d.width = width;
    seg->lit.push_back(LitFast{});
    return static_cast<int>(seg->regs.size()) - 1;
}

int push_lit_int(Segment* seg, std::int64_t v) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Literal;
    seg->lit.push_back(LitFast{true, v});
    return static_cast<int>(seg->regs.size()) - 1;
}

int push_lit_float(Segment* seg, double v) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Literal;
    LitFast lit;
    lit.is_f64 = true;
    lit.f = v;
    seg->lit.push_back(lit);
    return static_cast<int>(seg->regs.size()) - 1;
}

void push_insn(Segment* seg, rwir::Op op, rwir::Impl impl,
               std::initializer_list<int> rd, std::initializer_list<int> wr) {
    CInsn ci;
    ci.op = op;
    ci.impl = impl;
    ci.nrd = static_cast<std::uint8_t>(rd.size());
    ci.nwr = static_cast<std::uint8_t>(wr.size());
    int k = 0;
    for (int x : rd) ci.rd[k++] = static_cast<std::uint8_t>(x);
    k = 0;
    for (int x : wr) ci.wr[k++] = static_cast<std::uint8_t>(x);
    seg->insns.push_back(ci);
}

Segment build(rwir::Op op, rwir::Impl impl, bool second_is_float_literal,
              vmbin::LangType second_type, std::uint8_t second_width) {
    Segment s;
    const int a = push_var(&s, "a", -1, vmbin::LangType::Int64, 8);
    const int w = push_var(&s, "r", 1, vmbin::LangType::Int64, 8);
    const int b = second_is_float_literal
                      ? push_lit_float(&s, 1.5)
                      : (second_type == vmbin::LangType::Int64
                             ? push_lit_int(&s, 1)
                             : push_var(&s, "b", 0, second_type, second_width));
    push_insn(&s, op, impl, {a, b}, {w});
    push_insn(&s, rwir::Op::Return, rwir::Impl::Control, {}, {});
    return s;
}

}  // namespace

int main() {
    using LT = vmbin::LangType;
    {
        Segment s = build(rwir::Op::Add, rwir::Impl::Dynamic, false, LT::Int64, 8);
        derive_dispatch(&s);
        check(s.derived.size() == s.insns.size(), "table sized to insns");
        check(s.derived[0].fast_i64 && s.derived[0].opfn == 1,
              "dynamic int64 add -> opfn 1");
        check(!s.derived[1].fast_i64, "return has no derived entry");
    }
    {
        Segment s = build(rwir::Op::Le, rwir::Impl::Dynamic, false, LT::Int64, 8);
        derive_dispatch(&s);
        check(s.derived[0].fast_i64 && s.derived[0].opfn == 16,
              "dynamic int64 le -> opfn 16");
    }
    {
        Segment s = build(rwir::Op::Add, rwir::Impl::Dynamic, true, LT::Int64, 8);
        derive_dispatch(&s);
        check(!s.derived[0].fast_i64, "float literal operand not derived");
    }
    {
        Segment s = build(rwir::Op::Add, rwir::Impl::Dynamic, false, LT::Int32, 4);
        derive_dispatch(&s);
        check(!s.derived[0].fast_i64, "int32 operand not derived");
    }
    {
        Segment s = build(rwir::Op::Add, rwir::Impl::Typed, false, LT::Int64, 8);
        derive_dispatch(&s);
        check(!s.derived[0].fast_i64, "typed insn not derived");
    }
    {
        Segment s = build(rwir::Op::Sqrt, rwir::Impl::Dynamic, false, LT::Int64, 8);
        derive_dispatch(&s);
        check(!s.derived[0].fast_i64, "sqrt not in fast kernel");
    }

    if (g_failures == 0) {
        std::printf("OK dispatch_derive_test\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
