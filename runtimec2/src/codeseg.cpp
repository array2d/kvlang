#include "codeseg.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "valueio.hpp"

namespace kv::rt {
namespace {

std::vector<std::string> g_op_names;

std::uint16_t intern_op(const std::string& name) {
    static std::unordered_map<std::string, std::uint16_t> idx;
    auto it = idx.find(name);
    if (it != idx.end())
        return it->second;
    const auto id = static_cast<std::uint16_t>(g_op_names.size());
    g_op_names.push_back(name);
    idx[name] = id;
    return id;
}

std::string slot_key(const std::string& func_dir, int s0, int s1) {
    std::string k = func_dir;
    if (!k.empty() && k.back() == '/')
        k.pop_back();
    k += "/[";
    k += std::to_string(s0);
    k += ",";
    k += std::to_string(s1);
    k += "]";
    return k;
}

int reg_of(Segment* seg, std::unordered_map<std::string, int>* dedup,
           const std::string& tag, const RegDesc& d) {
    auto it = dedup->find(tag);
    if (it != dedup->end())
        return it->second;
    int idx = static_cast<int>(seg->regs.size());
    seg->regs.push_back(d);
    (*dedup)[tag] = idx;
    return idx;
}

bool operand_desc(void* h, const std::string& key, RegDesc* out) {
    std::string op;
    int nr = 0, nw = 0;
    if (read_rwir(h, key, &op, &nr, &nw)) {
        out->kind = Operand::Kind::Var;
        out->name = op;
        return true;
    }
    out->kind = Operand::Kind::Literal;
    out->slot_key = key;
    return true;
}

bool build(void* h, const std::string& func_dir, Segment* out) {
    out->insns.clear();
    out->regs.clear();
    std::unordered_map<std::string, int> dedup;
    for (int s0 = 1;; s0++) {
        const std::string op_key = slot_key(func_dir, s0, 0);
        std::string op, callee;
        int nr = 0, nw = 0;
        bool is_call = false;
        if (!read_rwir(h, op_key, &op, &nr, &nw)) {
            if (!read_rwfunc(h, op_key, &callee, &nr, &nw))
                break;  // 函数末尾
            is_call = true;
            op = "call";
        }
        const ops::Entry* e = ops::find(op.data(), static_cast<std::uint32_t>(op.size()));
        CInsn ins;
        ins.op = ops::Op::None;
        ins.impl = ops::Impl::Handoff;
        ins.s0 = s0;
        ins.name = op;
        ins.prof_id = intern_op(op);
        if (is_call) {
            ins.callee = callee;
            ins.op = ops::Op::Call;
            ins.impl = ops::Impl::Control;
        } else if (e) {
            ins.op = e->op;
            ins.kind = e->kind;
            ins.width = e->width;
            ins.impl = e->impl;
        }
        if (ins.impl == ops::Impl::Handoff) {
            out->insns.push_back(ins);
            continue;
        }
        if (nr + (is_call ? 1 : 0) > 4 || nw > 2)
            return false;
        ins.nrd = static_cast<std::uint8_t>(nr + (is_call ? 1 : 0));
        ins.nwr = static_cast<std::uint8_t>(nw);
        int rbase = is_call ? 1 : 0;
        for (int k = 1; k <= nr; k++) {
            const std::string rk = slot_key(func_dir, s0, -k);
            RegDesc d;
            if (!operand_desc(h, rk, &d))
                return false;
            const std::string tag = std::string(d.kind == Operand::Kind::Var ? "v:" : "l:") +
                                    (d.kind == Operand::Kind::Var ? d.name : rk);
            ins.rd[rbase + k - 1] = static_cast<std::uint8_t>(reg_of(out, &dedup, tag, d));
        }
        for (int k = 1; k <= nw; k++) {
            const std::string wk = slot_key(func_dir, s0, k);
            RegDesc d;
            if (!operand_desc(h, wk, &d))
                return false;
            const std::string tag = std::string(d.kind == Operand::Kind::Var ? "v:" : "l:") +
                                    (d.kind == Operand::Kind::Var ? d.name : wk);
            ins.wr[k - 1] = static_cast<std::uint8_t>(reg_of(out, &dedup, tag, d));
        }
        out->insns.push_back(ins);
    }
    return true;
}

std::unordered_map<std::string, Segment>& cache() {
    static std::unordered_map<std::string, Segment> c;
    return c;
}

}  // namespace

bool build_segment(void* h, const std::string& func_dir, Segment* out) {
    return build(h, func_dir, out);
}

const std::string& op_name(std::uint16_t id) {
    static const std::string kEmpty;
    return id < g_op_names.size() ? g_op_names[id] : kEmpty;
}

std::size_t op_name_count() { return g_op_names.size(); }

const Segment* cached_segment(void* h, const std::string& func_dir) {
    auto& c = cache();
    auto it = c.find(func_dir);
    if (it != c.end())
        return &it->second;
    Segment seg;
    if (!build(h, func_dir, &seg))
        return nullptr;
    auto res = c.emplace(func_dir, std::move(seg));
    return &res.first->second;
}

bool cached_insn(const std::string& func_dir, int s0, const CInsn** out,
                 const Segment** seg_out) {
    auto& c = cache();
    auto it = c.find(func_dir);
    if (it == c.end())
        return false;  // 未缓存：交给调用方走 cached_segment 补建
    const Segment& seg = it->second;
    const auto i = s0 - 1;
    if (i < 0 || i >= static_cast<int>(seg.insns.size()))
        return false;
    if (out)
        *out = &seg.insns[static_cast<std::size_t>(i)];
    if (seg_out)
        *seg_out = &seg;
    return true;
}

void clear_segment_cache() { cache().clear(); }

}  // namespace kv::rt
