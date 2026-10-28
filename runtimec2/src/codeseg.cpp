#include "codeseg.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <kvspace/kvspace.h>

#include "value.hpp"
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
        // 参数 Ptr 槽号预解析（`*[0,±k]`）：帧入口零 substr/atoi（fib 递归 setup 大头）。
        if (op.size() > 4 && op[0] == '*' && op.compare(1, 3, "[0,") == 0) {
            const char* p = op.c_str() + 4;
            char* end = nullptr;
            const long v = std::strtol(p, &end, 10);
            if (end && *end == ']')
                out->param_k = static_cast<int>(v);
        }
        return true;
    }
    out->kind = Operand::Kind::Literal;
    out->slot_key = key;
    // 绝对路径字面量（对齐 C is_absolute_key）：槽值是 char/ 且文本 `/` 开头 →
    // name 存路径本体，运行期 read/write 直接以路径作 key（`42 -> "/path"` 路径写、
    // `"/path" -> v` 路径读、println 实参）。`·`（U+00B7）是成员分隔，保留原样。
    {
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        std::string lt, txt;
        if (read_wire_raw(h, key, &d, &n) && d && n &&
            v_langtype(Wire{d, n}, &lt) && lt.find("char/") != std::string::npos &&
            wire_text(d, n, &txt) && !txt.empty() && txt[0] == '/')
            out->name = txt;
    }
    return true;
}

// callee 预解析（build 期一次，运行期 call 直用，免每帧 read_text(‥lib)+kvspaceGet 探测）。
// func_dir = 调用方函数目录（= 帧 ‥lib，同一函数所有帧一致）。
std::string resolve_callee(void* h, const std::string& func_dir,
                           const std::string& fn) {
    if (fn.rfind("/lib/", 0) == 0)
        return fn;
    if (fn.find("\xC2\xB7") != std::string::npos)
        return "/lib/" + fn;
    if (fn.find('/') != std::string::npos)
        return "/lib/" + fn;
    // 裸名：同 pkg 优先（读帧 ‥lib 取 pkg 等价于 func_dir 推导）。
    std::string libdir = func_dir;
    if (libdir.size() > 5 && libdir.back() == '/')
        libdir.pop_back();
    if (libdir.size() > 5) {
        const std::string rel = libdir.substr(5);
        const auto dot = rel.rfind("\xC2\xB7");
        const auto sl = rel.rfind('/');
        const std::size_t sep = dot != std::string::npos &&
                                       (sl == std::string::npos || dot > sl)
                                   ? dot
                                   : sl;
        const std::string pkg = sep == std::string::npos ? rel : rel.substr(0, sep);
        const std::string cand = "/lib/" + pkg + "\xC2\xB7" + fn;
        std::uint8_t* cd = nullptr;
        std::uint32_t cn = 0;
        const std::string ck = cand + "/[0,0]";
        if (kvspaceGet(h, ck.c_str(), 1, &cd, &cn) == 0 && cd && cn)
            return cand;
    }
    return "/lib/" + fn;
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
            callee = resolve_callee(h, func_dir, callee);  // 预解析为最终 /lib/ 路径
        }
        const ops::Entry* e = ops::find_myrwircap(op.data(),
                                                   static_cast<std::uint32_t>(op.size()));
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
            if (e->kind == ops::Kind::Int && e->width == 8) {
                switch (e->op) {
                case ops::Op::Add: ins.fast_i64 = true; ins.opfn = 1; break;
                case ops::Op::Sub: ins.fast_i64 = true; ins.opfn = 2; break;
                case ops::Op::Mul: ins.fast_i64 = true; ins.opfn = 3; break;
                case ops::Op::Div: ins.fast_i64 = true; ins.opfn = 4; break;
                case ops::Op::Mod: ins.fast_i64 = true; ins.opfn = 5; break;
                case ops::Op::Max: ins.fast_i64 = true; ins.opfn = 6; break;
                case ops::Op::Min: ins.fast_i64 = true; ins.opfn = 7; break;
                case ops::Op::BitAnd: ins.fast_i64 = true; ins.opfn = 8; break;
                case ops::Op::BitOr: ins.fast_i64 = true; ins.opfn = 9; break;
                case ops::Op::BitXor: ins.fast_i64 = true; ins.opfn = 10; break;
                case ops::Op::Shl: ins.fast_i64 = true; ins.opfn = 11; break;
                case ops::Op::Shr: ins.fast_i64 = true; ins.opfn = 12; break;
                case ops::Op::Eq: ins.fast_i64 = true; ins.opfn = 13; break;
                case ops::Op::Neq: ins.fast_i64 = true; ins.opfn = 14; break;
                case ops::Op::Lt: ins.fast_i64 = true; ins.opfn = 15; break;
                case ops::Op::Le: ins.fast_i64 = true; ins.opfn = 16; break;
                case ops::Op::Gt: ins.fast_i64 = true; ins.opfn = 17; break;
                case ops::Op::Ge: ins.fast_i64 = true; ins.opfn = 18; break;
                default: break;
                }
            }
        }
        if (ins.impl == ops::Impl::Native)
            out->has_native = true;
        if (ins.impl == ops::Impl::Handoff) {
            out->has_native = true;
            out->insns.push_back(ins);
            continue;
        }
        if (nr + (is_call ? 1 : 0) > 16 || nw > 4)
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
            // 写槽帧局部预标记：普通变量（非 * 参数/非 / 绝对路径/非 · 成员写）
            // → fast 写直走 fv。成员写（`base·member`）排除（fast kernel 单参 copy 误判）。
            if (d.kind == Operand::Kind::Var && !d.name.empty() &&
                d.name[0] != '*' && d.name[0] != '/' &&
                d.name.find("\xC2\xB7") == std::string::npos)
                ins.wr_local = true;
        }
        out->insns.push_back(ins);
    }
    // 字面量 int64 快值缓存：build 期读槽一次，运行期帧入口零 kvspace 预读
    // （fib 递归每帧省 1 次 kvspaceXvalueBodyView 读字面量 `1`）。
    out->lit.assign(out->regs.size(), LitFast{});
    for (std::size_t i = 0; i < out->regs.size(); i++) {
        const RegDesc& d = out->regs[i];
        if (d.kind == Operand::Kind::Literal) {
            const std::uint8_t* dd = nullptr;
            std::uint32_t dn = 0;
            if (read_wire(h, d.slot_key, &dd, &dn)) {
                Scalar sc{};
                if (v_scalar(Wire{dd, dn}, &sc) && sc.k == Scalar::K::Int &&
                    sc.width == 8) {
                    out->lit[i].is_i64 = true;
                    out->lit[i].v = sc.i;
                }
            }
        }
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
