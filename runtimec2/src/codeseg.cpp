#include "codeseg.hpp"
#include "binir_codec.hpp"
#include "binir_dump.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "jit.hpp"

#include <kvspace/kvspace.h>

#include "value.hpp"
#include "valueio.hpp"
#include "vmbin/regtype.hpp"

namespace kvlang::runtimec2 {
namespace {

using vmbin::LangType;
using vmbin::langtype_kind;
using vmbin::langtype_name;

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
    std::string op, ltype;
    int nr = 0, nw = 0;
    if (read_rwir(h, key, &op, &nr, &nw, &ltype)) {
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
        using VC = RegDesc::VarClass;
        out->vclass = op.empty()                     ? VC::Plain
                      : op.rfind("*[0,", 0) == 0     ? VC::ParamPtr
                      : op[0] == '*'                 ? VC::Ptr
                      : op.find(' ') == std::string::npos ? VC::Plain
                                                          : VC::Typed;
        out->member = !op.empty() && op[0] != '/' &&
                      op.find("\xC2\xB7") != std::string::npos;
        // E 层：操作数槽 NUL 后的类型串（layout typed_slot_value）。
        if (!ltype.empty())
            langtype_kind(ltype, &out->langtype, &out->width);
        return true;
    }
    out->kind = Operand::Kind::Literal;
    out->slot_key = key;
    // 绝对路径字面量（对齐 C is_absolute_key）：槽值是 char/ 且文本 `/` 开头 →
    // name 存路径本体，运行期 read/write 直接以路径作 key（`42 -> "/path"` 路径写、
    // `"/path" -> v` 路径读、println 实参）。`·`（U+00B7）是成员分隔，保留原样。
    std::string lt;
    if (read_langtype(h, key, &lt)) {
        langtype_kind(lt, &out->langtype, &out->width);
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        std::string txt;
        if (lt.find("char/") != std::string::npos &&
            read_wire_raw(h, key, &d, &n) && d && n &&
            wire_text(d, n, lt, &txt) && !txt.empty() && txt[0] == '/')
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

// 扩展 rwir 声明：runtime 以 kvlangDefRwir 注册到 `/lib/<opcode>`（kind=def rwir）。
// 契约域外算子（println 等）只有这种声明，才允许走 handoff。
bool is_declared_rwir(void* h, const std::string& op) {
    std::string lt;
    return read_langtype(h, "/lib/" + op, &lt) && lt == "def rwir";
}

// 段内池去重：rwir 身份用表下标（扩展算子进 extra 池），callee 路径进 callees 池。
std::uint16_t pool_add(std::vector<std::string>* pool,
                       std::unordered_map<std::string, std::uint16_t>* index,
                       const std::string& text) {
    auto it = index->find(text);
    if (it != index->end())
        return it->second;
    const auto id = static_cast<std::uint16_t>(pool->size());
    pool->push_back(text);
    (*index)[text] = id;
    return id;
}

// rwir::Kind/width → 标量 LangType（typed op 的写参兜底；Kind::Any/None 无信息）。
LangType scalar_langtype(rwir::Kind kind, std::uint8_t width) {
    switch (kind) {
    case rwir::Kind::Int:
        switch (width) {
        case 1: return LangType::Int8;
        case 2: return LangType::Int16;
        case 4: return LangType::Int32;
        case 8: return LangType::Int64;
        default: return LangType::None;
        }
    case rwir::Kind::Uint:
        switch (width) {
        case 1: return LangType::Uint8;
        case 2: return LangType::Uint16;
        case 4: return LangType::Uint32;
        case 8: return LangType::Uint64;
        default: return LangType::None;
        }
    case rwir::Kind::Float:
        return width == 4 ? LangType::Float32
               : width == 8 ? LangType::Float64
                            : LangType::None;
    default:
        return LangType::None;
    }
}

// #583 L2 阶段 B：段存储改用 binir 字节流（纯内存）。RC2_SEGMENT_FROM_BINIR=0
// 保留旧路径做 A/B（默认 0，验证后由架构师定）；RC2_SEGMENT_BINIR_CHECK=1 时
// 逐字段对拍两路径，不一致直接失败（不静默）。
bool segment_from_binir() {
    static const bool from_binir = [] {
        const char* env = std::getenv("RC2_SEGMENT_FROM_BINIR");
        // 默认 1 = binir 解码视图（#583 L2 阶段 B 裁决 2026-10-31）；
        // RC2_SEGMENT_FROM_BINIR=0 保留旧路径做 A/B。
        return env == nullptr || std::strcmp(env, "0") != 0;
    }();
    return from_binir;
}

bool binir_check_enabled() {
    static const bool enabled = std::getenv("RC2_SEGMENT_BINIR_CHECK") != nullptr;
    return enabled;
}

std::vector<bool> read_referenced(const Segment& seg) {
    std::vector<bool> ref(seg.regs.size(), false);
    for (const CInsn& ins : seg.insns) {
        for (int k = 0; k < ins.nrd; k++) {
            if (ins.op == rwir::Op::Goto && k == 0)
                continue;  // goto 的读参是分支目标
            if (ins.op == rwir::Op::Br && k >= 1)
                continue;
            if (ins.op == rwir::Op::Call && k == 0)
                continue;  // call 的 rd[0] 是哨兵
            if (ins.rd[k] < ref.size())
                ref[ins.rd[k]] = true;
        }
    }
    return ref;
}

std::string slot_identity(const Segment& seg, std::uint8_t slot) {
    if (slot >= seg.regs.size())
        return "<oor:" + std::to_string(slot) + ">";
    const RegDesc& d = seg.regs[slot];
    if (d.kind == Operand::Kind::Literal) {
        const LitFast lit = slot < seg.lit.size() ? seg.lit[slot] : LitFast{};
        if (lit.is_i64)
            return "i|" + std::to_string(lit.v);
        return "s|" + d.slot_key + "|" + d.name;
    }
    return "v|" + d.name;
}

bool same_program(const Segment& a, const Segment& b, std::string* why) {
    if (a.insns.size() != b.insns.size()) {
        *why = "insn count";
        return false;
    }
    for (std::size_t i = 0; i < a.insns.size(); i++) {
        const CInsn& x = a.insns[i];
        const CInsn& y = b.insns[i];
        auto fail = [&](const char* field) {
            *why = "insn " + std::to_string(i) + " (" +
                   vmbin::instruction_name(a, x) + ") " + field;
            return false;
        };
        if (x.rwir_id != y.rwir_id) return fail("rwir_id");
        if (x.handler != y.handler) return fail("handler");
        if (x.call_site != y.call_site) return fail("call_site");
        if (x.op != y.op) return fail("op");
        if (x.kind != y.kind) return fail("kind");
        if (x.impl != y.impl) return fail("impl");
        if (x.width != y.width) return fail("width");
        if (x.opfn != y.opfn) return fail("opfn");
        if (x.fast_i64 != y.fast_i64) return fail("fast_i64");
        if (x.wr_local != y.wr_local) return fail("wr_local");
        if (x.nrd != y.nrd) return fail("nrd");
        if (x.nwr != y.nwr) return fail("nwr");
        const std::size_t read_begin = x.op == rwir::Op::Call ? 1 : 0;
        for (std::size_t k = read_begin; k < x.nrd; k++)
            if (slot_identity(a, x.rd[k]) != slot_identity(b, y.rd[k]))
                return fail("read operand");
        for (std::size_t k = 0; k < x.nwr; k++)
            if (slot_identity(a, x.wr[k]) != slot_identity(b, y.wr[k]))
                return fail("write operand");
    }
    if (a.callees != b.callees || a.extra_rwir_names != b.extra_rwir_names ||
        a.has_rwirhandler != b.has_rwirhandler) {
        *why = "pool/has_rwirhandler";
        return false;
    }
    if (a.call_sites.size() != b.call_sites.size()) {
        *why = "call_sites count";
        return false;
    }
    for (std::size_t i = 0; i < a.call_sites.size(); i++) {
        const vmbin::CallSite& x = a.call_sites[i];
        const vmbin::CallSite& y = b.call_sites[i];
        if (x.callee_id != y.callee_id || y.segment != nullptr ||
            y.jit != nullptr || y.ready || !y.read_types.empty() ||
            !y.write_types.empty()) {
            *why = "call_site " + std::to_string(i);
            return false;
        }
    }
    const std::vector<bool> ra = read_referenced(a);
    const std::vector<bool> rb = read_referenced(b);
    auto classify = [](const Segment& seg, const std::vector<bool>& ref,
                       std::vector<std::string>* regs_out,
                       std::vector<std::string>* consts_out) {
        std::vector<bool> written(seg.regs.size(), false);
        for (const CInsn& ins : seg.insns)
            for (int k = 0; k < ins.nwr; k++)
                if (ins.wr[k] < written.size())
                    written[ins.wr[k]] = true;
        for (std::size_t i = 0; i < seg.regs.size(); i++) {
            const RegDesc& d = seg.regs[i];
            const bool is_const = d.kind == Operand::Kind::Literal && !written[i];
            const LitFast lit = i < seg.lit.size() ? seg.lit[i] : LitFast{};
            if (!is_const) {
                regs_out->push_back(
                    std::to_string(static_cast<int>(d.kind)) + "|" + d.name +
                    "|" + d.slot_key + "|" + std::to_string(d.param_k) + "|" +
                    std::to_string(static_cast<int>(d.vclass)) + "|" +
                    (d.member ? "m" : "-") + "|" +
                    std::to_string(static_cast<int>(d.langtype)) + "|" +
                    std::to_string(d.width));
                continue;
            }
            std::string key = lit.is_i64
                                  ? "int:" + std::to_string(lit.v)
                                  : "text:" + d.slot_key + "|" + d.name;
            // 只读引用的常量（池条目）连 key 一起比；分支目标常量只比值。
            if (!lit.is_i64 || (i < ref.size() && ref[i]))
                key += "|" + d.slot_key + "|" + d.name;
            consts_out->push_back(std::move(key));
        }
    };
    std::vector<std::string> ar, br, ac, bc;
    classify(a, ra, &ar, &ac);
    classify(b, rb, &br, &bc);
    std::sort(ar.begin(), ar.end());
    std::sort(br.begin(), br.end());
    std::sort(ac.begin(), ac.end());
    std::sort(bc.begin(), bc.end());
    if (ar != br) {
        *why = "register set differs";
        return false;
    }
    if (ac != bc) {
        *why = "constant set differs";
        return false;
    }
    if (a.lit.size() != b.lit.size()) {
        *why = "lit size";
        return false;
    }
    return true;
}

bool build(void* h, const std::string& func_dir, Segment* out) {
    out->insns.clear();
    out->regs.clear();
    out->callees.clear();
    out->extra_rwir_names.clear();
    out->call_sites.clear();
    out->member_write_cache.clear();
    std::unordered_map<std::string, int> dedup;
    std::unordered_map<std::string, std::uint16_t> callee_ids;
    std::unordered_map<std::string, std::uint16_t> extra_ids;
    std::vector<vmbin::RegisterTypeState> type_state;
    auto record_type = [&type_state](int idx, LangType kind,
                                     std::uint8_t width, bool write) {
        if (idx < 0)
            return;
        if (static_cast<std::size_t>(idx) >= type_state.size())
            type_state.resize(static_cast<std::size_t>(idx) + 1);
        type_state[static_cast<std::size_t>(idx)].record(kind, width, write);
    };
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
        const rwir::Entry* e = rwir::find_myrwircap(op.data(),
                                                   static_cast<std::uint32_t>(op.size()));
        CInsn ins;
        ins.op = rwir::Op::None;
        ins.impl = rwir::Impl::Handoff;
        if (is_call) {
            ins.call_site = static_cast<std::uint16_t>(out->call_sites.size());
            out->call_sites.emplace_back();
            out->call_sites.back().callee_id = pool_add(
                &out->callees, &callee_ids, callee);
            ins.op = rwir::Op::Call;
            ins.impl = rwir::Impl::Control;
            const rwir::Entry* ce = rwir::find_myrwircap("call", 4);
            ins.rwir_id = ce ? static_cast<std::uint16_t>(ce - rwir::myrwircaps)
                             : static_cast<std::uint16_t>(
                                   vmbin::kRwirIdExtraBase +
                                   pool_add(&out->extra_rwir_names, &extra_ids, "call"));
        } else if (e) {
            ins.rwir_id = static_cast<std::uint16_t>(e - rwir::myrwircaps);
            ins.op = e->op;
            ins.kind = e->kind;
            ins.width = e->width;
            ins.impl = e->impl;
            ins.handler = e->handler;
            if (e->kind == rwir::Kind::Int && e->width == 8) {
                switch (e->op) {
                case rwir::Op::Add: ins.fast_i64 = true; ins.opfn = 1; break;
                case rwir::Op::Sub: ins.fast_i64 = true; ins.opfn = 2; break;
                case rwir::Op::Mul: ins.fast_i64 = true; ins.opfn = 3; break;
                case rwir::Op::Div: ins.fast_i64 = true; ins.opfn = 4; break;
                case rwir::Op::Mod: ins.fast_i64 = true; ins.opfn = 5; break;
                case rwir::Op::Max: ins.fast_i64 = true; ins.opfn = 6; break;
                case rwir::Op::Min: ins.fast_i64 = true; ins.opfn = 7; break;
                case rwir::Op::BitAnd: ins.fast_i64 = true; ins.opfn = 8; break;
                case rwir::Op::BitOr: ins.fast_i64 = true; ins.opfn = 9; break;
                case rwir::Op::BitXor: ins.fast_i64 = true; ins.opfn = 10; break;
                case rwir::Op::Shl: ins.fast_i64 = true; ins.opfn = 11; break;
                case rwir::Op::Shr: ins.fast_i64 = true; ins.opfn = 12; break;
                case rwir::Op::Eq: ins.fast_i64 = true; ins.opfn = 13; break;
                case rwir::Op::Neq: ins.fast_i64 = true; ins.opfn = 14; break;
                case rwir::Op::Lt: ins.fast_i64 = true; ins.opfn = 15; break;
                case rwir::Op::Le: ins.fast_i64 = true; ins.opfn = 16; break;
                case rwir::Op::Gt: ins.fast_i64 = true; ins.opfn = 17; break;
                case rwir::Op::Ge: ins.fast_i64 = true; ins.opfn = 18; break;
                default: break;
                }
            }
        } else if (!is_declared_rwir(h, op)) {
            // #583 §2·4：未知 rwir 不得静默走 handoff——不在 myrwircap 且
            // `/lib/<op>` 无 def rwir 声明，即 layout 产出了本 runtime 无法执行的算子。
            std::fprintf(stderr,
                         "runtimec2: unknown rwir \"%s\" in %s: not in myrwircap "
                         "and no /lib/%s def rwir\n",
                         op.c_str(), func_dir.c_str(), op.c_str());
            return false;
        } else {
            // 已声明但不在 myrwircap：扩展算子 handoff，名字进 extra 池（仅诊断）。
            ins.rwir_id = static_cast<std::uint16_t>(
                vmbin::kRwirIdExtraBase +
                pool_add(&out->extra_rwir_names, &extra_ids, op));
        }
        if (ins.handler != rwir::RwirHandlerId::None)
            out->has_rwirhandler = true;
        if (ins.impl == rwir::Impl::Handoff) {
            out->has_rwirhandler = true;
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
            const int ri = reg_of(out, &dedup, tag, d);
            ins.rd[rbase + k - 1] = static_cast<std::uint8_t>(ri);
            record_type(ri, d.langtype, d.width, false);
        }
        for (int k = 1; k <= nw; k++) {
            const std::string wk = slot_key(func_dir, s0, k);
            RegDesc d;
            if (!operand_desc(h, wk, &d))
                return false;
            const std::string tag = std::string(d.kind == Operand::Kind::Var ? "v:" : "l:") +
                                    (d.kind == Operand::Kind::Var ? d.name : wk);
            const int ri = reg_of(out, &dedup, tag, d);
            ins.wr[k - 1] = static_cast<std::uint8_t>(ri);
            // 写参类型：槽后缀优先；无后缀时用 typed op 的结果 kind/width 兜底。
            LangType wk_kind = d.langtype;
            std::uint8_t wk_width = d.width;
            if (wk_kind == LangType::None) {
                wk_kind = scalar_langtype(ins.kind, ins.width);
                wk_width = wk_kind == LangType::None ? 0 : ins.width;
            }
            record_type(ri, wk_kind, wk_width, true);
            // 写槽帧局部预标记：普通变量（非 * 参数/非 / 绝对路径/非 · 成员写）
            // → fast 写直走 fv。成员写（`base·member`）排除（fast kernel 单参 copy 误判）。
            if (d.kind == Operand::Kind::Var && !d.name.empty() &&
                d.name[0] != '*' && d.name[0] != '/' &&
                d.name.find("\xC2\xB7") == std::string::npos)
                ins.wr_local = true;
        }
        out->insns.push_back(ins);
    }
    // E 层定稿：写者一致且全部有类型 → 写入；否则只读变量看读槽；再否则 None。
    for (std::size_t i = 0; i < out->regs.size(); i++) {
        const vmbin::RegisterTypeState s =
            i < type_state.size() ? type_state[i] : vmbin::RegisterTypeState{};
        std::uint8_t width = 0;
        out->regs[i].langtype = s.final_kind(&width);
        out->regs[i].width = width;
    }
    if (std::getenv("RC2_TRACE")) {
        for (std::size_t ri = 0; ri < out->regs.size(); ri++) {
            const RegDesc& d = out->regs[ri];
            const char* ltn = langtype_name(d.langtype);
            std::fprintf(stderr,
                         "[segdmp reg %zu kind=%d name='%s' key='%s' pk=%d "
                         "lt=%s w=%u]\n",
                         ri, (int)d.kind, d.name.c_str(), d.slot_key.c_str(),
                         d.param_k, ltn ? ltn : "?", (unsigned)d.width);
        }
        for (std::size_t ii = 0; ii < out->insns.size(); ii++) {
            const CInsn& ci = out->insns[ii];
            std::fprintf(stderr,
                         "[segdmp s0=%d op=%s opfn=%d fast=%d wrl=%d nrd=%u nwr=%u "
                         "rd=%d,%d,%d wr=%d,%d,%d]\n",
                         vmbin::instruction_pc(ii), instruction_name(*out, ci),
                         (int)ci.opfn,
                         (int)ci.fast_i64,
                         (int)ci.wr_local, ci.nrd, ci.nwr, ci.rd[0], ci.rd[1],
                         ci.rd[2], ci.wr[0], ci.wr[1], ci.wr[2]);
        }
    }
    // 字面量 int64 快值缓存：build 期读槽一次，运行期帧入口零 kvspace 预读
    // （fib 递归每帧省 1 次 kvspaceXvalueBodyView 读字面量 `1`）。
    out->lit.assign(out->regs.size(), LitFast{});
    for (std::size_t i = 0; i < out->regs.size(); i++) {
        const RegDesc& d = out->regs[i];
        if (d.kind == Operand::Kind::Literal) {
            std::string lt;
            if (!read_langtype(h, d.slot_key, &lt))
                continue;
            const std::uint8_t* dd = nullptr;
            std::uint32_t dn = 0;
            if (read_wire(h, d.slot_key, &dd, &dn)) {
                Scalar sc{};
                if (v_scalar(Wire{dd, dn}, lt, &sc) && sc.k == Scalar::K::Int &&
                    sc.width == 8) {
                    out->lit[i].is_i64 = true;
                    out->lit[i].v = sc.i;
                }
            }
        }
    }
    // #583 L2 阶段 B：先在内存编码成 binir 字节流，再按开关用解码视图替换
    // （insns/regs/lit/call_sites）。旧路径不付解码成本；编码失败时旧路径照常
    // 交给上层校验（例如导出器报 invalid target），要 binir 视图则硬失败。
    std::vector<std::uint8_t> bytes;
    const bool encoded = encode_binir(*out, &bytes);
    if (encoded)
        out->binir = std::move(bytes);
    if (!segment_from_binir() && !binir_check_enabled())
        return true;
    if (!encoded) {
        std::fprintf(stderr,
                     "runtimec2: binir encode failed for %s (invalid branch target "
                     "or unsupported instruction shape)\n",
                     func_dir.c_str());
        return false;
    }
    Segment decoded;
    if (!decode_binir(out->binir.data(), out->binir.size(), &decoded))
        return false;
    if (binir_check_enabled()) {
        std::string why;
        if (!same_program(*out, decoded, &why)) {
            std::fprintf(stderr, "runtimec2: binir check failed for %s: %s\n",
                         func_dir.c_str(), why.c_str());
            return false;
        }
    }
    if (segment_from_binir()) {
        decoded.binir = out->binir;
        *out = std::move(decoded);
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

const char* op_name(std::uint16_t id) {
    if (id < static_cast<std::uint16_t>(rwir::kMyrwircapsN))
        return rwir::myrwircaps[id].name;
    return id == static_cast<std::uint16_t>(rwir::kMyrwircapsN) ? "<ext>"
                                                                : "<unknown>";
}

std::size_t op_name_count() { return static_cast<std::size_t>(rwir::kMyrwircapsN) + 1; }

const Segment* cached_segment(void* h, const std::string& func_dir) {
    auto& c = cache();
    auto it = c.find(func_dir);
    if (it != c.end())
        return &it->second;
    Segment seg;
    if (!build(h, func_dir, &seg))
        return nullptr;
    auto res = c.emplace(func_dir, std::move(seg));
    if (binir_dump_on_build())
        dump_binir(res.first->second, func_dir.c_str());
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

void clear_segment_cache() {
    cache().clear();
    jit_clear_cache();
}

}  // namespace kvlang::runtimec2
