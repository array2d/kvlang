#include "dispatch_derive.hpp"

#include "fast_opfn.hpp"

namespace kvlang::runtimec2 {

namespace {

bool operand_int64(const Segment& seg, int r) {
    const RegDesc& d = seg.regs[static_cast<std::size_t>(r)];
    if (d.kind == Operand::Kind::Literal)
        return static_cast<std::size_t>(r) < seg.lit.size() &&
               seg.lit[static_cast<std::size_t>(r)].is_i64;
    return d.kind == Operand::Kind::Var &&
           d.langtype == vmbin::LangType::Int64 && d.width == 8;
}

}  // namespace

void derive_dispatch(Segment* seg) {
    seg->derived.assign(seg->insns.size(), Segment::DerivedDispatch{});
    for (std::size_t i = 0; i < seg->insns.size(); i++) {
        const CInsn& ci = seg->insns[i];
        if (ci.impl != rwir::Impl::Dynamic || ci.fast_i64 || ci.nrd != 2 ||
            ci.nwr != 1)
            continue;
        const std::uint8_t opfn = rwir::int64_fast_opfn(ci.op);
        if (opfn == 0)
            continue;
        if (!operand_int64(*seg, ci.rd[0]) || !operand_int64(*seg, ci.rd[1]))
            continue;
        seg->derived[i].opfn = opfn;
        seg->derived[i].fast_i64 = true;
    }
}

}  // namespace kvlang::runtimec2
