#include "binir_dump.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace kvlang::runtimec2 {
namespace {

using vmbin::Operand;
using vmbin::RegisterDesc;

// 与 tools/segment_shape_dump.cpp 同一套槽位归类：Literal 且从未被写 = 常量池
// 条目，其余 = 寄存器；ConstId / RegId 都按 Segment 槽位顺序紧凑编号。
// （§2·6 的 rd0/rd1 高位 k 标志在内存视图里尚未物化，这里先按此映射打印。）
struct SlotMap {
    std::vector<std::uint32_t> reg_id;   // 槽位 → RegId（常量槽无意义）
    std::vector<std::uint32_t> const_id; // 槽位 → ConstId（寄存器槽无意义）
    std::vector<bool> is_const;
    std::uint32_t nreg = 0;
    std::uint32_t nconst = 0;
};

SlotMap build_slot_map(const Segment& seg) {
    std::vector<bool> written(seg.regs.size(), false);
    for (const CInsn& ci : seg.insns)
        for (std::uint8_t k = 0; k < ci.nwr; k++)
            if (ci.wr[k] < written.size())
                written[ci.wr[k]] = true;
    SlotMap m;
    m.reg_id.assign(seg.regs.size(), 0);
    m.const_id.assign(seg.regs.size(), 0);
    m.is_const.assign(seg.regs.size(), false);
    for (std::size_t i = 0; i < seg.regs.size(); i++) {
        m.is_const[i] = seg.regs[i].kind == Operand::Kind::Literal && !written[i];
        if (m.is_const[i])
            m.const_id[i] = m.nconst++;
        else
            m.reg_id[i] = m.nreg++;
    }
    return m;
}

// A 桶（§2·8 简化判定，近似真实 JIT）：int64 算术/比较 + 控制流。
// 真实覆盖判定是整段形状 verify（src/jit.cpp verify() + 平台发射器），
// 依赖参数槽形状与平台；dump 先按 opcode 集合近似标注，仅用于可视化。
bool in_jit_bucket(const CInsn& ci) {
    switch (ci.op) {
    case rwir::Op::Copy:
    case rwir::Op::Br:
    case rwir::Op::Goto:
    case rwir::Op::Return:
        return true;
    default:
        break;
    }
    if (ci.kind != rwir::Kind::Int || ci.width != 8)
        return false;
    switch (ci.op) {
    case rwir::Op::Add:
    case rwir::Op::Eq:
    case rwir::Op::Neq:
    case rwir::Op::Lt:
    case rwir::Op::Le:
    case rwir::Op::Gt:
    case rwir::Op::Ge:
        return true;
    default:
        return false;
    }
}

bool is_handler(const CInsn& ci) {
    return ci.handler != rwir::RwirHandlerId::None ||
           ci.impl == rwir::Impl::Handoff;
}

// 汇编助记符（§2·6）：typed rwir 取 `·` 后缀（int64·add → add）；
// 控制流用 br_if / goto / call / return / copy。
std::string mnemonic_of(const CInsn& ci) {
    switch (ci.op) {
    case rwir::Op::Copy: return "copy";
    case rwir::Op::Br: return "br_if";
    case rwir::Op::Goto: return "goto";
    case rwir::Op::Call: return "call";
    case rwir::Op::Return: return "return";
    default: break;
    }
    if (ci.name.empty())
        return "?";
    const auto dot = ci.name.rfind("\xC2\xB7"); // U+00B7
    return dot == std::string::npos ? ci.name : ci.name.substr(dot + 2);
}

const char* kind_name(rwir::Kind kind) {
    switch (kind) {
    case rwir::Kind::Int: return "int";
    case rwir::Kind::Uint: return "uint";
    case rwir::Kind::Float: return "float";
    case rwir::Kind::Any: return "any";
    default: return "none";
    }
}

// RegDesc 本身不携带 kind/width（§2·6 的 E 层字段待落地）：从消费该寄存器的
// 第一条 typed 指令反推；推不出再退回 VarClass。
std::pair<std::string, unsigned> reg_kind(const Segment& seg, std::size_t slot) {
    for (const CInsn& ci : seg.insns) {
        if (ci.kind == rwir::Kind::None)
            continue;
        bool uses = false;
        for (int k = 0; k < ci.nrd; k++)
            uses = uses || ci.rd[k] == slot;
        for (int k = 0; k < ci.nwr; k++)
            uses = uses || ci.wr[k] == slot;
        if (uses)
            return {kind_name(ci.kind), ci.width};
    }
    switch (seg.regs[slot].vclass) {
    case RegisterDesc::VarClass::ParamPtr:
    case RegisterDesc::VarClass::Ptr: return {"ptr", 8};
    case RegisterDesc::VarClass::Typed: return {"typed", 0};
    case RegisterDesc::VarClass::Plain: return {"plain", 0};
    default: return {"none", 0};
    }
}

// 常量池条目的文本形式：int64 快值 → 数字；char/ 路径字面量 → 带引号文本；
// 其余（本视图无语义）→ 槽 key。
std::string const_text(const Segment& seg, std::size_t slot) {
    if (slot < seg.lit.size() && seg.lit[slot].is_i64)
        return std::to_string(seg.lit[slot].v);
    if (!seg.regs[slot].name.empty())
        return "\"" + seg.regs[slot].name + "\"";
    return "slot[" + seg.regs[slot].slot_key + "]";
}

std::string const_tag(const Segment& seg, std::size_t slot) {
    if (slot < seg.lit.size() && seg.lit[slot].is_i64)
        return "int64";
    if (!seg.regs[slot].name.empty())
        return "string";
    return "slot";
}

std::string operand(const Segment& seg, const SlotMap& m, std::uint8_t slot) {
    if (slot >= seg.regs.size())
        return "r?" + std::to_string(slot);
    if (m.is_const[slot])
        return "#" + std::to_string(m.const_id[slot]);
    return "r" + std::to_string(m.reg_id[slot]);
}

std::string insn_text(const Segment& seg, const SlotMap& m, const CInsn& ci) {
    std::string text = mnemonic_of(ci);
    if (text.size() < 7)
        text.append(7 - text.size(), ' ');
    std::string args;
    if (ci.op == rwir::Op::Call) {
        // call 的 rd[0] 是占位哨兵（codeseg.cpp: rbase=1），callee 在文本字段。
        args = ci.callee;
        for (int k = 1; k < ci.nrd; k++) {
            if (!args.empty())
                args += ", ";
            args += operand(seg, m, ci.rd[k]);
        }
        for (int k = 0; k < ci.nwr; k++) {
            args += k == 0 ? " \xE2\x86\x92 " : ", "; // →
            args += operand(seg, m, ci.wr[k]);
        }
    } else {
        // 汇编视图：写参（结果）在前，读参在后。
        for (int k = 0; k < ci.nwr; k++) {
            if (!args.empty())
                args += ", ";
            args += operand(seg, m, ci.wr[k]);
        }
        for (int k = 0; k < ci.nrd; k++) {
            if (!args.empty())
                args += ", ";
            args += operand(seg, m, ci.rd[k]);
        }
    }
    return text + args;
}

// 来源 rwir 注释：Call 给 callee，分支给目标槽，读常量给常量池下标+值。
std::string insn_comment(const Segment& seg, const SlotMap& m, const CInsn& ci) {
    std::string c = ci.name.empty() ? "?" : ci.name;
    if (ci.op == rwir::Op::Call) {
        c = "callee=" + ci.callee;
    } else if (ci.op == rwir::Op::Goto || ci.op == rwir::Op::Br) {
        const int k = ci.op == rwir::Op::Br ? 1 : 0;
        if (k < ci.nrd)
            c += "  target=" + operand(seg, m, ci.rd[k]);
    }
    for (int k = 0; k < ci.nrd; k++) {
        const std::uint8_t slot = ci.rd[k];
        if (slot < seg.regs.size() && m.is_const[slot])
            c += "  const[" + std::to_string(m.const_id[slot]) + "]=" +
                 const_text(seg, slot);
    }
    return c;
}

void appendf(std::string* out, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (static_cast<std::size_t>(n) < sizeof buf) {
        out->append(buf, static_cast<std::size_t>(n));
        return;
    }
    std::vector<char> big(static_cast<std::size_t>(n) + 1);
    va_start(ap, fmt);
    std::vsnprintf(big.data(), big.size(), fmt, ap);
    va_end(ap);
    out->append(big.data(), static_cast<std::size_t>(n));
}

}  // namespace

void dump_binir(const Segment& seg, const char* func_dir) {
    const SlotMap m = build_slot_map(seg);

    bool has_handler = seg.has_rwirhandler;
    for (const CInsn& ci : seg.insns)
        has_handler = has_handler || is_handler(ci);

    std::string out;
    // nslot = RegDesc 总条目数（§2·6 段头 nreg 口径）；nreg/nconst 是当前内存视图
    // 的拆分：常量物化为 Literal 槽，binir 落地后常量进常量池、RegDesc 只剩寄存器。
    appendf(&out,
            "[binir] func=%s  ninsn=%zu nslot=%zu nreg=%u nconst=%u "
            "has_rwirhandler=%d\n",
            func_dir ? func_dir : "", seg.insns.size(), seg.regs.size(), m.nreg,
            m.nconst, has_handler ? 1 : 0);
    out += "  note: 常量当前物化为 RegDesc 的 Literal 槽；binir 落地后进常量池\n";
    out += "  pc   insn                          ; 来源 rwir\n";

    std::size_t jit_n = 0, interp_n = 0;
    for (std::size_t i = 0; i < seg.insns.size(); i++) {
        const CInsn& ci = seg.insns[i];
        const bool jit = !has_handler && in_jit_bucket(ci);
        (jit ? jit_n : interp_n)++;
        const std::string text = insn_text(seg, m, ci);
        const std::string comment = insn_comment(seg, m, ci);
        appendf(&out, "  %04zu  %-30s ; %-18s [%s]\n", i, text.c_str(),
                comment.c_str(), jit ? "JIT" : "INTERP");
    }

    for (std::size_t slot = 0; slot < seg.regs.size(); slot++) {
        if (!m.is_const[slot])
            continue;
        appendf(&out, "  const[%u] %-8s%s\n", m.const_id[slot],
                const_tag(seg, slot).c_str(), const_text(seg, slot).c_str());
    }

    for (std::size_t slot = 0; slot < seg.regs.size(); slot++) {
        if (m.is_const[slot])
            continue;
        const auto [kind, width] = reg_kind(seg, slot);
        std::string head = "  reg[" + std::to_string(m.reg_id[slot]) + "]";
        if (head.size() < 10)
            head.append(10 - head.size(), ' ');
        appendf(&out, "%s kind=%-5s width=%u\n", head.c_str(), kind.c_str(), width);
    }

    const std::size_t total = jit_n + interp_n;
    const double pct = total ? 100.0 * static_cast<double>(jit_n) /
                                   static_cast<double>(total)
                             : 0.0;
    appendf(&out, "  exit: JIT %zu / INTERP %zu  (%.1f%%)\n", jit_n, interp_n, pct);

    std::fwrite(out.data(), 1, out.size(), stderr);
    std::fflush(stderr);
}

}  // namespace kvlang::runtimec2
