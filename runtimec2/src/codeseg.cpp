#include "codeseg.hpp"

#include <cstdio>

#include "valueio.hpp"

namespace kv::rt {
namespace {

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

bool read_operand(void* h, const std::string& key, Operand* out) {
    // 字面量：任意非 rwir 值；变量：rwir 值，opcode 即变量名。
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(h, key, &d, &n))
        return false;
    std::string op;
    int nr = 0, nw = 0;
    if (read_rwir(h, key, &op, &nr, &nw)) {
        out->kind = Operand::Kind::Var;
        out->name = op;
        return true;
    }
    out->kind = Operand::Kind::Literal;
    return true;
}

}  // namespace

bool build_segment(void* h, const std::string& func_dir, Segment* out,
                   std::string* stop_op) {
    out->insns.clear();
    if (stop_op)
        stop_op->clear();
    for (int s0 = 1;; s0++) {
        const std::string op_key = slot_key(func_dir, s0, 0);
        std::string op;
        int nr = 0, nw = 0;
        if (!read_rwir(h, op_key, &op, &nr, &nw))
            break;  // 指令序列结束
        const ops::Entry* e = ops::find(op.data(), static_cast<uint32_t>(op.size()));
        if (!e || e->impl == ops::Impl::Handoff) {
            // 非 base：本指令段到此为止（调用方转 handoff/扩展）。
            if (stop_op)
                *stop_op = op;
            return false;
        }
        if (nr > 4 || nw > 2)
            return false;
        CInsn ins;
        ins.op = e->op;
        ins.kind = e->kind;
        ins.width = e->width;
        ins.nrd = static_cast<std::uint8_t>(nr);
        ins.nwr = static_cast<std::uint8_t>(nw);
        ins.s0 = s0;
        for (int k = 1; k <= nr; k++)
            if (!read_operand(h, slot_key(func_dir, s0, -k), &ins.rd[k - 1]))
                return false;
        for (int k = 1; k <= nw; k++)
            if (!read_operand(h, slot_key(func_dir, s0, k), &ins.wr[k - 1]))
                return false;
        out->insns.push_back(ins);
    }
    return true;
}

}  // namespace kv::rt
