// BinIR 编解码单测：手工段往返、真实形状语料批量往返、错误字节流防御。
// 不链接 kvspace：语料 fixture 只保留导出器的形状字段（op/读写参/目标/立即数）。
//
// 用法：binir_codec_test <tests/fixtures/binir/real-shapes.txt>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "binir_codec.hpp"

namespace {

using kvlang::runtimec2::Segment;
using kvlang::runtimec2::decode_binir;
using kvlang::runtimec2::encode_binir;
using kvlang::runtimec2::rwir::Entry;
using kvlang::runtimec2::rwir::Op;
using kvlang::runtimec2::vmbin::FastLiteral;
using kvlang::runtimec2::vmbin::Instruction;
using kvlang::runtimec2::vmbin::Operand;
using kvlang::runtimec2::vmbin::RegisterDesc;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what, int line) {
    g_checks++;
    if (ok)
        return;
    g_failures++;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void derive_fast(Instruction* ins) {
    ins->fast_i64 = false;
    ins->opfn = 0;
    if (ins->kind != kvlang::runtimec2::rwir::Kind::Int || ins->width != 8)
        return;
    switch (ins->op) {
    case Op::Add: ins->opfn = 1; break;
    case Op::Sub: ins->opfn = 2; break;
    case Op::Mul: ins->opfn = 3; break;
    case Op::Div: ins->opfn = 4; break;
    case Op::Mod: ins->opfn = 5; break;
    case Op::Max: ins->opfn = 6; break;
    case Op::Min: ins->opfn = 7; break;
    case Op::BitAnd: ins->opfn = 8; break;
    case Op::BitOr: ins->opfn = 9; break;
    case Op::BitXor: ins->opfn = 10; break;
    case Op::Shl: ins->opfn = 11; break;
    case Op::Shr: ins->opfn = 12; break;
    case Op::Eq: ins->opfn = 13; break;
    case Op::Neq: ins->opfn = 14; break;
    case Op::Lt: ins->opfn = 15; break;
    case Op::Le: ins->opfn = 16; break;
    case Op::Gt: ins->opfn = 17; break;
    case Op::Ge: ins->opfn = 18; break;
    default: return;
    }
    ins->fast_i64 = true;
}

const Entry* lookup(const std::string& name) {
    return kvlang::runtimec2::rwir::find_myrwircap(
        name.data(), static_cast<std::uint32_t>(name.size()));
}

std::uint16_t extra_id(Segment* seg, const std::string& name) {
    for (std::size_t i = 0; i < seg->extra_rwir_names.size(); i++)
        if (seg->extra_rwir_names[i] == name)
            return static_cast<std::uint16_t>(
                kvlang::runtimec2::vmbin::kRwirIdExtraBase + i);
    seg->extra_rwir_names.push_back(name);
    return static_cast<std::uint16_t>(kvlang::runtimec2::vmbin::kRwirIdExtraBase +
                                      seg->extra_rwir_names.size() - 1);
}

std::uint16_t callee_id(Segment* seg, const std::string& path) {
    for (std::size_t i = 0; i < seg->callees.size(); i++)
        if (seg->callees[i] == path)
            return static_cast<std::uint16_t>(i);
    seg->callees.push_back(path);
    return static_cast<std::uint16_t>(seg->callees.size() - 1);
}

// ── 段构造 ──────────────────────────────────────────────────────────────
struct Builder {
    Segment seg;

    std::uint8_t var(const char* name,
                     RegisterDesc::VarClass vclass = RegisterDesc::VarClass::Plain,
                     int param_k = 0, bool member = false) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Var;
        desc.name = name;
        desc.vclass = vclass;
        desc.param_k = param_k;
        desc.member = member;
        seg.regs.push_back(desc);
        seg.lit.push_back(FastLiteral{});
        return static_cast<std::uint8_t>(seg.regs.size() - 1);
    }

    std::uint8_t int_const(std::int64_t value, const char* key) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        desc.slot_key = key;
        seg.regs.push_back(desc);
        FastLiteral literal;
        literal.is_i64 = true;
        literal.v = value;
        seg.lit.push_back(literal);
        return static_cast<std::uint8_t>(seg.regs.size() - 1);
    }

    std::uint8_t text_const(const char* key, const char* name) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        desc.slot_key = key;
        desc.name = name;
        seg.regs.push_back(desc);
        seg.lit.push_back(FastLiteral{});
        return static_cast<std::uint8_t>(seg.regs.size() - 1);
    }

    Instruction& insn(const char* name) {
        Instruction ins;
        const Entry* entry = lookup(name);
        if (entry == nullptr) {
            ins.impl = kvlang::runtimec2::rwir::Impl::Handoff;
            ins.rwir_id = extra_id(&seg, name);
        } else {
            ins.op = entry->op;
            ins.kind = entry->kind;
            ins.width = entry->width;
            ins.impl = entry->impl;
            ins.handler = entry->handler;
            ins.rwir_id = static_cast<std::uint16_t>(
                entry - kvlang::runtimec2::rwir::myrwircaps);
            derive_fast(&ins);
        }
        seg.insns.push_back(std::move(ins));
        return seg.insns.back();
    }

    void callee(Instruction* ins, const char* path) {
        seg.call_sites.emplace_back();
        seg.call_sites.back().callee_id = callee_id(&seg, path);
        ins->call_site = static_cast<std::uint16_t>(seg.call_sites.size() - 1);
    }

    Segment done() {
        bool handler = false;
        for (const Instruction& ins : seg.insns)
            handler = handler ||
                      ins.handler != kvlang::runtimec2::rwir::RwirHandlerId::None ||
                      ins.impl == kvlang::runtimec2::rwir::Impl::Handoff;
        seg.has_rwirhandler = handler;
        return seg;
    }
};

// ── 字段级对拍 ──────────────────────────────────────────────────────────
std::string slot_identity(const Segment& seg, std::uint8_t slot) {
    const RegisterDesc& desc = seg.regs[slot];
    if (desc.kind == Operand::Kind::Literal) {
        const FastLiteral literal =
            slot < seg.lit.size() ? seg.lit[slot] : FastLiteral{};
        if (literal.is_i64)
            return "int:" + std::to_string(literal.v);
        return "text:" + desc.slot_key + "|" + desc.name;
    }
    return "var:" + desc.name;
}

std::vector<bool> read_referenced(const Segment& seg) {
    std::vector<bool> ref(seg.regs.size(), false);
    for (const Instruction& ins : seg.insns) {
        for (int k = 0; k < ins.nrd; k++) {
            // 跳转目标/ call 哨兵不是「读常量引用」。
            if (ins.op == Op::Goto && k == 0)
                continue;
            if (ins.op == Op::Br && k >= 1)
                continue;
            if (ins.op == Op::Call && k == 0)
                continue;
            if (ins.rd[k] < ref.size())
                ref[ins.rd[k]] = true;
        }
    }
    return ref;
}

std::vector<bool> constant_slots(const Segment& seg) {
    std::vector<bool> written(seg.regs.size(), false);
    for (const Instruction& ins : seg.insns)
        for (int k = 0; k < ins.nwr; k++)
            if (ins.wr[k] < written.size())
                written[ins.wr[k]] = true;
    std::vector<bool> is_const(seg.regs.size(), false);
    for (std::size_t i = 0; i < seg.regs.size(); i++)
        is_const[i] = seg.regs[i].kind == Operand::Kind::Literal && !written[i];
    return is_const;
}

std::string safe_identity(const Segment& seg, std::uint8_t slot) {
    return slot < seg.regs.size() ? slot_identity(seg, slot)
                                  : "<out-of-range:" + std::to_string(slot) + ">";
}

bool same_segment(const Segment& a, const Segment& b, std::string* why) {
    if (a.insns.size() != b.insns.size()) {
        *why = "instruction count " + std::to_string(a.insns.size()) + " vs " +
               std::to_string(b.insns.size());
        return false;
    }
    if (a.has_rwirhandler != b.has_rwirhandler) {
        *why = "has_rwirhandler differs";
        return false;
    }
    if (a.call_sites.size() != b.call_sites.size()) {
        *why = "call_sites count differs";
        return false;
    }
    for (std::size_t i = 0; i < a.call_sites.size(); i++)
        if (a.call_sites[i].callee_id != b.call_sites[i].callee_id ||
            b.call_sites[i].segment != nullptr || b.call_sites[i].jit != nullptr ||
            b.call_sites[i].ready) {
            *why = "call_site " + std::to_string(i) + " differs";
            return false;
        }
    for (std::size_t i = 0; i < a.insns.size(); i++) {
        const Instruction& x = a.insns[i];
        const Instruction& y = b.insns[i];
        auto fail = [&](const char* field) {
            *why = "insn " + std::to_string(i) + " (" +
                   kvlang::runtimec2::vmbin::instruction_name(a, x) + ") " + field;
            return false;
        };
        if (std::strcmp(kvlang::runtimec2::vmbin::instruction_name(a, x),
                        kvlang::runtimec2::vmbin::instruction_name(b, y)) != 0)
            return fail("name");
        if (x.op != y.op) return fail("op");
        if (x.kind != y.kind) return fail("kind");
        if (x.width != y.width) return fail("width");
        if (x.impl != y.impl) return fail("impl");
        if (x.handler != y.handler) return fail("handler");
        if (x.wr_local != y.wr_local) return fail("wr_local");
        if (x.nrd != y.nrd) return fail("nrd");
        if (x.nwr != y.nwr) return fail("nwr");
        if (kvlang::runtimec2::vmbin::instruction_callee(a, x) !=
            kvlang::runtimec2::vmbin::instruction_callee(b, y))
            return fail("callee");
        const std::size_t read_begin = x.op == Op::Call ? 1 : 0;
        for (int k = static_cast<int>(read_begin); k < x.nrd; k++)
            if (safe_identity(a, x.rd[k]) != safe_identity(b, y.rd[k])) {
                *why = "insn " + std::to_string(i) + " (" + kvlang::runtimec2::vmbin::instruction_name(a, x) + ") read " +
                       std::to_string(k) + ": " + safe_identity(a, x.rd[k]) +
                       " vs " + safe_identity(b, y.rd[k]);
                return false;
            }
        for (int k = 0; k < x.nwr; k++)
            if (safe_identity(a, x.wr[k]) != safe_identity(b, y.wr[k])) {
                *why = "insn " + std::to_string(i) + " (" + kvlang::runtimec2::vmbin::instruction_name(a, x) + ") write " +
                       std::to_string(k) + ": " + safe_identity(a, x.wr[k]) +
                       " vs " + safe_identity(b, y.wr[k]);
                return false;
            }
    }
    const std::vector<bool> ca = constant_slots(a);
    const std::vector<bool> cb = constant_slots(b);
    const std::vector<bool> read_a = read_referenced(a);
    const std::vector<bool> read_b = read_referenced(b);
    auto constant_key = [](const Segment& seg, const std::vector<bool>& ref,
                           std::size_t slot) -> std::string {
        const RegisterDesc& desc = seg.regs[slot];
        const FastLiteral literal =
            slot < seg.lit.size() ? seg.lit[slot] : FastLiteral{};
        const std::string base = literal.is_i64
                                     ? "int:" + std::to_string(literal.v)
                                     : "text:";
        if (!literal.is_i64 || (slot < ref.size() && ref[slot]))
            return base + "|" + desc.slot_key + "|" + desc.name;
        return base;
    };
    std::vector<std::string> ra, rb, ka, kb;
    for (std::size_t i = 0; i < a.regs.size(); i++) {
        if (ca[i])
            ka.push_back(constant_key(a, read_a, i));
        else
            ra.push_back(std::to_string(static_cast<int>(a.regs[i].kind)) + "|" +
                         a.regs[i].name + "|" + a.regs[i].slot_key + "|" +
                         std::to_string(static_cast<int>(a.regs[i].vclass)) + "|" +
                         std::to_string(a.regs[i].param_k) + "|" +
                         (a.regs[i].member ? "m" : "-") + "|" +
                         std::to_string(static_cast<int>(a.regs[i].langtype)) + "|" +
                         std::to_string(a.regs[i].width));
    }
    for (std::size_t i = 0; i < b.regs.size(); i++) {
        if (cb[i])
            kb.push_back(constant_key(b, read_b, i));
        else
            rb.push_back(std::to_string(static_cast<int>(b.regs[i].kind)) + "|" +
                         b.regs[i].name + "|" + b.regs[i].slot_key + "|" +
                         std::to_string(static_cast<int>(b.regs[i].vclass)) + "|" +
                         std::to_string(b.regs[i].param_k) + "|" +
                         (b.regs[i].member ? "m" : "-") + "|" +
                         std::to_string(static_cast<int>(b.regs[i].langtype)) + "|" +
                         std::to_string(b.regs[i].width));
    }
    std::sort(ra.begin(), ra.end());
    std::sort(rb.begin(), rb.end());
    std::sort(ka.begin(), ka.end());
    std::sort(kb.begin(), kb.end());
    if (ra != rb) {
        *why = "register set differs (" + std::to_string(ra.size()) + " vs " +
               std::to_string(rb.size()) + ")";
        return false;
    }
    if (ka != kb) {
        std::vector<std::string> missing;
        {
            std::size_t i = 0, j = 0;
            while (i < ka.size() && j < kb.size()) {
                if (ka[i] == kb[j]) { i++; j++; }
                else if (ka[i] < kb[j]) { missing.push_back(ka[i++]); }
                else j++;
            }
            while (i < ka.size()) missing.push_back(ka[i++]);
        }
        std::string m;
        for (std::size_t i = 0; i < missing.size() && i < 6; i++)
            m += " " + missing[i];
        *why = "constant set differs (" + std::to_string(ka.size()) + " vs " +
               std::to_string(kb.size()) + ") missing:" + m + "; regs " +
               std::to_string(a.regs.size()) + " vs " +
               std::to_string(b.regs.size()) + "; orig=[" +
               (ka.empty() ? "" : ka[0]) + ".." +
               (ka.empty() ? "" : ka.back()) + "] dec=[" +
               (kb.empty() ? "" : kb[0]) + ".." +
               (kb.empty() ? "" : kb.back()) + "]";
        return false;
    }
    return true;
}

bool roundtrip(const Segment& segment, std::vector<std::uint8_t>* encoded,
               std::string* why) {
    if (!encode_binir(segment, encoded)) {
        *why = "encode failed";
        return false;
    }
    Segment decoded;
    if (!decode_binir(encoded->data(), encoded->size(), &decoded)) {
        *why = "decode failed";
        return false;
    }
    if (!same_segment(segment, decoded, why))
        return false;
    std::vector<std::uint8_t> again;
    if (!encode_binir(decoded, &again)) {
        *why = "re-encode failed";
        return false;
    }
    if (again != *encoded) {
        std::size_t at = 0;
        while (at < again.size() && at < encoded->size() &&
               again[at] == (*encoded)[at])
            at++;
        *why = "re-encode bytes differ at " + std::to_string(at) + " (size " +
               std::to_string(encoded->size()) + " vs " +
               std::to_string(again.size()) + ")";
        for (std::size_t i = (at > 12 ? at - 12 : 0);
             i < at + 12 && i < encoded->size(); i++) {
            char buf[32];
            std::snprintf(buf, sizeof buf, " %02x/%02x", (*encoded)[i],
                          i < again.size() ? again[i] : 0);
            *why += buf;
        }
        return false;
    }
    return true;
}

void test_roundtrip(const char* name, Segment segment) {
    std::vector<std::uint8_t> encoded;
    std::string why;
    if (!roundtrip(segment, &encoded, &why)) {
        g_failures++;
        std::fprintf(stderr, "FAIL [%s] %s\n", name, why.c_str());
        return;
    }
    std::printf("ok   %-28s bytes=%zu\n", name, encoded.size());
}

std::uint32_t header_u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(
        bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
        (bytes[at + 3] << 24));
}

// ── 手工段 ──────────────────────────────────────────────────────────────
void handmade_tests() {
    {
        Builder b;
        const std::uint8_t x = b.var("x");
        const std::uint8_t y = b.var("y");
        const std::uint8_t z = b.var("z");
        Instruction& ins = b.insn("int64·add");
        ins.nrd = 2;
        ins.rd[0] = x;
        ins.rd[1] = y;
        ins.nwr = 1;
        ins.wr[0] = z;
        ins.wr_local = true;
        test_roundtrip("narrow_add", b.done());
    }
    {
        Builder b;
        const std::uint8_t x = b.var("x");
        const std::uint8_t k = b.int_const(41, "/lib/demo/[1,-2]");
        const std::uint8_t r = b.var("_0");
        Instruction& ins = b.insn("int64·le");
        ins.nrd = 2;
        ins.rd[0] = x;
        ins.rd[1] = k;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("narrow_read_const", b.done());
    }
    {
        Builder b;
        const std::uint8_t a = b.var("a");
        const std::uint8_t r = b.var("ret");
        Instruction& ins = b.insn("call");
        b.callee(&ins, "/lib/helper");
        ins.nrd = 2;  // 哨兵 + 1 实参
        ins.rd[0] = 0;
        ins.rd[1] = a;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("narrow_call_one_arg", b.done());
    }
    {
        Builder b;
        const std::uint8_t a = b.var("a");
        const std::uint8_t c = b.var("b");
        const std::uint8_t d = b.var("c");
        const std::uint8_t r = b.var("ret");
        Instruction& ins = b.insn("call");
        b.callee(&ins, "/lib/helper");
        ins.nrd = 4;  // 哨兵 + 3 实参
        ins.rd[0] = 0;
        ins.rd[1] = a;
        ins.rd[2] = c;
        ins.rd[3] = d;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("wide_call_three_args", b.done());
    }
    {
        Builder b;
        const std::uint8_t t = b.int_const(2, "/lib/demo/[1,-1]");  // 虚拟出口 +1
        Instruction& ins = b.insn("goto");
        ins.nrd = 1;
        ins.rd[0] = t;
        test_roundtrip("narrow_goto", b.done());
    }
    {
        Builder b;
        const std::uint8_t cond = b.var("cond");
        const std::uint8_t taken = b.int_const(2, "/lib/demo/[1,-2]");  // +1
        const std::uint8_t other = b.int_const(1, "/lib/demo/[1,-3]");
        Instruction& ins = b.insn("br");
        ins.nrd = 3;
        ins.rd[0] = cond;
        ins.rd[1] = taken;
        ins.rd[2] = other;
        test_roundtrip("br_taken_fallthrough", b.done());
    }
    {
        Builder b;
        const std::uint8_t cond = b.var("cond");
        const std::uint8_t other = b.int_const(1, "/lib/demo/[1,-1]");
        const std::uint8_t taken = b.int_const(2, "/lib/demo/[1,-2]");  // +1
        Instruction& ins = b.insn("br");
        ins.nrd = 3;
        ins.rd[0] = cond;
        ins.rd[1] = other;
        ins.rd[2] = taken;
        test_roundtrip("br_false_fallthrough", b.done());
    }
    {
        Builder b;
        const std::uint8_t v0 = b.var("v0");
        const std::uint8_t cond = b.var("cond");
        const std::uint8_t t1 = b.int_const(1, "/lib/demo/[2,-2]");  // 回跳到 1
        const std::uint8_t t2 = b.int_const(2, "/lib/demo/[2,-3]");  // 自身（非 +1）
        const std::uint8_t z = b.var("z");
        Instruction& add = b.insn("int64·add");
        add.nrd = 2;
        add.rd[0] = v0;
        add.rd[1] = v0;
        add.nwr = 1;
        add.wr[0] = z;
        Instruction& ins = b.insn("br");
        ins.nrd = 3;
        ins.rd[0] = cond;
        ins.rd[1] = t1;
        ins.rd[2] = t2;
        Instruction& ret = b.insn("return");
        ret.nrd = 0;
        ret.nwr = 0;
        test_roundtrip("wide_br_two_targets", b.done());
    }
    {
        Builder b;
        Instruction& ins = b.insn("println");
        CHECK(ins.impl == kvlang::runtimec2::rwir::Impl::Handoff);
        test_roundtrip("handoff_plain_name", b.done());
    }
    {
        Builder b;
        const std::uint8_t arr = b.var("arr");
        const std::uint8_t idx = b.var("i");
        const std::uint8_t dflt = b.var("d");
        const std::uint8_t r = b.var("r");
        Instruction& ins = b.insn("compactarray·at");
        ins.nrd = 3;
        ins.rd[0] = arr;
        ins.rd[1] = idx;
        ins.rd[2] = dflt;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("wide_three_reads", b.done());
    }
    {
        Builder b;
        for (int i = 0; i < 64; i++)
            b.var(("v" + std::to_string(i)).c_str());
        const std::uint8_t hi = b.var("hi");  // 寄存器 64 > 63
        const std::uint8_t x = b.var("x");
        const std::uint8_t r = b.var("r");
        Instruction& ins = b.insn("int64·add");
        ins.nrd = 2;
        ins.rd[0] = x;
        ins.rd[1] = hi;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("wide_register_index", b.done());
    }
    {
        Builder b;
        const std::uint8_t param = b.var("*[0,-1]", RegisterDesc::VarClass::ParamPtr, -1);
        const std::uint8_t member = b.var("p·x", RegisterDesc::VarClass::Ptr, 0, true);
        const std::uint8_t r = b.var("r", RegisterDesc::VarClass::Typed);
        Instruction& ins = b.insn("int64");
        ins.nrd = 2;
        ins.rd[0] = param;
        ins.rd[1] = member;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("reg_metadata", b.done());
    }
    {
        Builder b;
        const std::uint8_t lit = b.int_const(7, "/lib/demo/[1,0]");
        const std::uint8_t r = b.var("r");
        Instruction& ins = b.insn("int64");
        ins.nrd = 1;
        ins.rd[0] = lit;
        ins.nwr = 1;
        ins.wr[0] = r;  // 写常量槽 → 该槽变成寄存器
        test_roundtrip("written_literal_register", b.done());
    }
    {
        Builder b;
        const std::uint8_t text = b.text_const("/lib/demo/[1,-1]", "/tmp/path");
        const std::uint8_t r = b.var("r");
        Instruction& ins = b.insn("string·len");
        ins.nrd = 1;
        ins.rd[0] = text;
        ins.nwr = 1;
        ins.wr[0] = r;
        test_roundtrip("text_constant", b.done());
    }
    {
        // 空段（无指令）：仍须往返。
        Builder b;
        b.var("unused");
        test_roundtrip("empty_segment", b.done());
    }
}

// ── 错误流 ──────────────────────────────────────────────────────────────
void error_tests() {
    Builder b;
    const std::uint8_t x = b.var("x");
    const std::uint8_t y = b.var("y");
    const std::uint8_t z = b.var("z");
    Instruction& ins = b.insn("int64·add");
    ins.nrd = 2;
    ins.rd[0] = x;
    ins.rd[1] = y;
    ins.nwr = 1;
    ins.wr[0] = z;
    const Segment segment = b.done();
    std::vector<std::uint8_t> good;
    CHECK(encode_binir(segment, &good));

    Segment decoded;
    auto expect_decode_fail = [&](std::vector<std::uint8_t> bytes, const char* what) {
        Segment scratch;
        const bool ok = decode_binir(bytes.data(), bytes.size(), &scratch);
        check(!ok, what, __LINE__);
    };
    {
        std::vector<std::uint8_t> bytes = good;
        bytes[0] ^= 0xFF;
        expect_decode_fail(bytes, "bad magic");
    }
    {
        std::vector<std::uint8_t> bytes = good;
        bytes[2] = 99;
        expect_decode_fail(bytes, "bad version");
    }
    {
        std::vector<std::uint8_t> bytes = good;
        bytes.back() = 1;
        expect_decode_fail(bytes, "nonzero tail");
    }
    {
        std::vector<std::uint8_t> bytes = good;
        bytes.resize(bytes.size() - 1);
        expect_decode_fail(bytes, "truncated");
    }
    {
        std::vector<std::uint8_t> bytes = good;
        bytes[16] = 0xFF;  // cpool_off 越界
        expect_decode_fail(bytes, "bad cpool offset");
    }
    {
        std::vector<std::uint8_t> bytes = good;
        bytes[8] = 0xFF;  // nreg 越界
        expect_decode_fail(bytes, "bad nreg");
    }
    {
        // WIDE 头 pad 非零。
        Builder w;
        for (int i = 0; i < 3; i++)
            w.var(("v" + std::to_string(i)).c_str());
        Instruction& wide = w.insn("compactarray·at");
        wide.nrd = 3;
        wide.rd[0] = 0;
        wide.rd[1] = 1;
        wide.rd[2] = 2;
        wide.nwr = 0;
        std::vector<std::uint8_t> bytes;
        CHECK(encode_binir(w.done(), &bytes));
        bytes[32 + 7] = 9;
        expect_decode_fail(bytes, "wide pad nonzero");
    }
    {
        // NARROW 普通 op 的保留 imm 位非零。
        std::vector<std::uint8_t> bytes = good;
        bytes[32 + 5] |= 0x10;  // imm 高字节 bit4（imm bit12）
        expect_decode_fail(bytes, "reserved arity bits");
    }
    {
        // NARROW op 落在保留区（305..507）。
        std::vector<std::uint8_t> bytes = good;
        bytes[32] = 200;  // op 低 8 位 = 200，仍是合法 RwirId → 这里改为 0x1F7
        bytes[32] = 0xF7;
        bytes[33] = (bytes[33] & 0xFE) | 0x01;  // op bit8 = 1 → 0x1F7
        expect_decode_fail(bytes, "reserved opcode");
    }
    (void)decoded;
}

// ── 真实形状语料 ────────────────────────────────────────────────────────
struct FixtureInsn {
    std::string op;
    std::vector<std::pair<bool, int>> reads;  // (is_const, index)
    std::vector<int> writes;
    std::vector<int> targets;
    bool has_imm = false;
    int imm = 0;
};

struct FixtureFn {
    std::string name;
    int nreg = 0;
    int nconst = 0;
    std::vector<FixtureInsn> insns;
};

bool parse_list(const std::string& text, std::vector<std::string>* out) {
    out->clear();
    if (text == "-")
        return true;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty())
            return false;
        out->push_back(item);
    }
    return true;
}

bool load_fixture(const char* path, std::vector<FixtureFn>* out) {
    std::ifstream file(path);
    if (!file)
        return false;
    std::string line;
    FixtureFn current;
    bool open = false;
    auto flush = [&]() {
        if (open)
            out->push_back(current);
        current = FixtureFn{};
    };
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream stream(line);
        std::string tag;
        stream >> tag;
        if (tag == "F") {
            flush();
            stream >> current.name >> current.nreg >> current.nconst;
            if (!stream || current.nreg < 0 || current.nconst < 0)
                return false;
            open = true;
        } else if (tag == "I") {
            FixtureInsn insn;
            std::string reads, writes, targets, imm;
            stream >> insn.op >> reads >> writes >> targets >> imm;
            if (!stream)
                return false;
            std::vector<std::string> items;
            if (!parse_list(reads, &items))
                return false;
            for (const std::string& item : items) {
                if (item.size() < 2)
                    return false;
                insn.reads.emplace_back(item[0] == 'c',
                                        std::stoi(item.substr(1)));
            }
            if (!parse_list(writes, &items))
                return false;
            for (const std::string& item : items)
                insn.writes.push_back(std::stoi(item));
            if (!parse_list(targets, &items))
                return false;
            for (const std::string& item : items)
                insn.targets.push_back(std::stoi(item));
            if (imm != "-") {
                insn.has_imm = true;
                insn.imm = std::stoi(imm);
            }
            current.insns.push_back(std::move(insn));
        } else {
            return false;
        }
    }
    flush();
    return !out->empty();
}

bool build_fixture_segment(const FixtureFn& fn, Segment* out, std::string* why) {
    Segment seg;
    std::vector<std::uint8_t> reg_slot(static_cast<std::size_t>(fn.nreg));
    for (int i = 0; i < fn.nreg; i++) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Var;
        desc.name = "v" + std::to_string(i);
        seg.regs.push_back(desc);
        seg.lit.push_back(FastLiteral{});
        reg_slot[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(seg.regs.size() - 1);
    }
    // 导出器的 ConstId 空间把读常量与分支目标常量编在同一编号里：按「每个读常量
    // 下标一个槽 + 每次目标引用一个槽」重建，总数必须回到 constant_count。
    std::set<int> read_const_indices;
    std::vector<std::int64_t> target_ids;
    for (std::size_t i = 0; i < fn.insns.size(); i++) {
        for (const auto& read : fn.insns[i].reads)
            if (read.first)
                read_const_indices.insert(read.second);
        for (const int target : fn.insns[i].targets)
            target_ids.push_back(static_cast<std::int64_t>(i) + 1 + target);
    }
    if (read_const_indices.size() + target_ids.size() !=
        static_cast<std::size_t>(fn.nconst)) {
        *why = "constant_count mismatch in " + fn.name + " (reads " +
               std::to_string(read_const_indices.size()) + " + targets " +
               std::to_string(target_ids.size()) + " != " +
               std::to_string(fn.nconst) + ")";
        return false;
    }
    std::unordered_map<int, std::uint8_t> read_const_slot;
    for (const int index : read_const_indices) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        desc.slot_key = "/lib/fixture/c" + std::to_string(index);
        seg.regs.push_back(desc);
        FastLiteral literal;
        literal.is_i64 = true;
        literal.v = 1000 + index;
        seg.lit.push_back(literal);
        read_const_slot.emplace(index,
                                static_cast<std::uint8_t>(seg.regs.size() - 1));
    }
    std::vector<std::uint8_t> target_slots;
    for (const std::int64_t source_id : target_ids) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        desc.slot_key = "/lib/fixture/target[" + std::to_string(source_id) + "]";
        seg.regs.push_back(desc);
        FastLiteral literal;
        literal.is_i64 = true;
        literal.v = source_id;
        seg.lit.push_back(literal);
        target_slots.push_back(static_cast<std::uint8_t>(seg.regs.size() - 1));
    }
    auto operand_slot = [&](const std::pair<bool, int>& operand,
                            std::uint8_t* slot) -> bool {
        const auto index = static_cast<std::size_t>(operand.second);
        if (operand.first) {
            const auto found = read_const_slot.find(operand.second);
            if (found == read_const_slot.end())
                return false;
            *slot = found->second;
        } else {
            if (index >= reg_slot.size())
                return false;
            *slot = reg_slot[index];
        }
        return true;
    };

    std::size_t target_cursor = 0;
    for (std::size_t i = 0; i < fn.insns.size(); i++) {
        const FixtureInsn& item = fn.insns[i];
        Instruction ins;
        const Entry* entry = lookup(item.op);
        if (entry == nullptr) {
            ins.impl = kvlang::runtimec2::rwir::Impl::Handoff;
            ins.rwir_id = extra_id(&seg, item.op);
        } else {
            ins.op = entry->op;
            ins.kind = entry->kind;
            ins.width = entry->width;
            ins.impl = entry->impl;
            ins.handler = entry->handler;
            ins.rwir_id = static_cast<std::uint16_t>(
                entry - kvlang::runtimec2::rwir::myrwircaps);
            derive_fast(&ins);
        }
        if (entry != nullptr && entry->op == Op::Call) {
            if (!item.has_imm || item.reads.size() + 1 > 16) {
                *why = "call shape invalid in " + fn.name;
                return false;
            }
            seg.call_sites.emplace_back();
            seg.call_sites.back().callee_id =
                callee_id(&seg, "/lib/fixture·callee" + std::to_string(item.imm));
            ins.call_site =
                static_cast<std::uint16_t>(seg.call_sites.size() - 1);
            ins.nrd = static_cast<std::uint8_t>(item.reads.size() + 1);
            ins.rd[0] = 0;
            for (std::size_t k = 0; k < item.reads.size(); k++) {
                std::uint8_t slot = 0;
                if (!operand_slot(item.reads[k], &slot))
                    return false;
                ins.rd[k + 1] = slot;
            }
            ins.nwr = static_cast<std::uint8_t>(item.writes.size());
            for (std::size_t k = 0; k < item.writes.size(); k++)
                ins.wr[k] = reg_slot[static_cast<std::size_t>(item.writes[k])];
        } else if (entry != nullptr && entry->op == Op::Goto) {
            if (item.targets.size() != 1) {
                *why = "goto shape invalid in " + fn.name;
                return false;
            }
            ins.nrd = 1;
            ins.rd[0] = target_slots[target_cursor++];
        } else if (entry != nullptr && entry->op == Op::Br) {
            if (item.targets.size() != 2 || item.reads.size() != 1) {
                *why = "br shape invalid in " + fn.name;
                return false;
            }
            std::uint8_t cond = 0;
            if (!operand_slot(item.reads[0], &cond))
                return false;
            ins.nrd = 3;
            ins.rd[0] = cond;
            ins.rd[1] = target_slots[target_cursor++];
            ins.rd[2] = target_slots[target_cursor++];
        } else if (entry == nullptr) {
            ins.nrd = 0;
            ins.nwr = 0;
        } else {
            if (item.reads.size() > 16 || item.writes.size() > 4) {
                *why = "operand count invalid in " + fn.name;
                return false;
            }
            ins.nrd = static_cast<std::uint8_t>(item.reads.size());
            for (std::size_t k = 0; k < item.reads.size(); k++) {
                std::uint8_t slot = 0;
                if (!operand_slot(item.reads[k], &slot))
                    return false;
                ins.rd[k] = slot;
            }
            ins.nwr = static_cast<std::uint8_t>(item.writes.size());
            for (std::size_t k = 0; k < item.writes.size(); k++)
                ins.wr[k] = reg_slot[static_cast<std::size_t>(item.writes[k])];
        }
        seg.insns.push_back(std::move(ins));
    }
    bool handler = false;
    for (const Instruction& ins : seg.insns)
        handler = handler ||
                  ins.handler != kvlang::runtimec2::rwir::RwirHandlerId::None ||
                  ins.impl == kvlang::runtimec2::rwir::Impl::Handoff;
    seg.has_rwirhandler = handler;
    *out = std::move(seg);
    return true;
}

void corpus_test(const char* path) {
    std::vector<FixtureFn> functions;
    if (!load_fixture(path, &functions)) {
        g_failures++;
        std::fprintf(stderr, "FAIL cannot load fixture %s\n", path);
        return;
    }
    std::size_t insns = 0, code_bytes = 0, total_bytes = 0, functions_ok = 0;
    std::size_t wide = 0;
    for (const FixtureFn& fn : functions) {
        Segment segment;
        std::string why;
        if (!build_fixture_segment(fn, &segment, &why)) {
            g_failures++;
            std::fprintf(stderr, "FAIL [%s] %s\n", fn.name.c_str(), why.c_str());
            continue;
        }
        std::vector<std::uint8_t> encoded;
        if (!roundtrip(segment, &encoded, &why)) {
            g_failures++;
            std::fprintf(stderr, "FAIL [%s] %s\n", fn.name.c_str(), why.c_str());
            continue;
        }
        functions_ok++;
        insns += segment.insns.size();
        total_bytes += encoded.size();
        const std::uint32_t cpool_offset = header_u32(encoded, 16);
        code_bytes += cpool_offset - 32;
        for (std::size_t at = 32; at < cpool_offset;) {
            const std::uint16_t first =
                static_cast<std::uint16_t>(encoded[at] | (encoded[at + 1] << 8));
            if (first == 0xFFFF) {
                wide++;
                const std::uint8_t nrd = encoded[at + 4];
                const std::uint8_t nwr = encoded[at + 5];
                const std::uint8_t ntgt = encoded[at + 6];
                at += 12 + 2 * nrd + 2 * nwr + 4 * ntgt;
            } else {
                at += 6;
            }
        }
    }
    std::printf("corpus: functions=%zu/%zu insns=%zu wide=%zu (%.2f%%) "
                "code=%.2f B/insn total=%.2f B/insn\n",
                functions_ok, functions.size(), insns, wide,
                100.0 * static_cast<double>(wide) /
                    (insns == 0 ? 1 : insns),
                static_cast<double>(code_bytes) /
                    (insns == 0 ? 1 : insns),
                static_cast<double>(total_bytes) /
                    (insns == 0 ? 1 : insns));
    CHECK(functions_ok == functions.size());
    CHECK(insns > 0);
}

}  // namespace

int main(int argc, char** argv) {
    handmade_tests();
    error_tests();
    if (argc > 1) {
        corpus_test(argv[1]);
    } else {
        g_failures++;
        std::fprintf(stderr, "FAIL missing corpus fixture argument\n");
    }
    if (g_failures != 0) {
        std::fprintf(stderr, "FAILED %d of %d checks\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS %d checks\n", g_checks);
    return 0;
}
