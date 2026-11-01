#include "binircompiler/compiler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

#include <kvspace/kvspace.h>

#include "binir_codec.hpp"
#include "binir_dump.hpp"
#include "rwir_signature.hpp"
#include "value.hpp"
#include "valueio.hpp"
#include "vmbin/regtype.hpp"

namespace kvlang::runtimec2::binircompiler {
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

// 静态数组类型 kindexpr：`[d0,d1,...]elem`（compact 定长、row-major）。
// 只收标量元素（bool/int*/uint*/float*）；char 族与容器不进 JIT 内联。
bool parse_array_kindexpr(std::string_view lt, vmbin::ArrayTypeDesc* out) {
    if (lt.size() < 4 || lt.front() != '[')
        return false;
    const std::size_t close = lt.find(']');
    if (close == std::string_view::npos || close + 1 >= lt.size())
        return false;
    std::uint8_t ndim = 0;
    std::size_t i = 0;
    const std::string_view dims = lt.substr(1, close - 1);
    while (i <= dims.size()) {
        const std::size_t comma = dims.find(',', i);
        const std::string_view one =
            comma == std::string_view::npos ? dims.substr(i)
                                            : dims.substr(i, comma - i);
        if (one.empty() || ndim >= 4)
            return false;
        std::int64_t v = 0;
        for (char ch : one) {
            if (ch < '0' || ch > '9')
                return false;
            v = v * 10 + (ch - '0');
        }
        if (v <= 0)
            return false;
        out->dims[ndim++] = v;
        if (comma == std::string_view::npos)
            break;
        i = comma + 1;
    }
    vmbin::LangType kind = vmbin::LangType::None;
    std::uint8_t width = 0;
    if (!vmbin::langtype_kind(lt.substr(close + 1), &kind, &width))
        return false;
    out->elem_kind = kind;
    out->width = width;
    out->ndim = ndim;
    out->numel = 1;
    for (std::uint8_t d = 0; d < ndim; d++)
        out->numel *= out->dims[d];
    out->strides[ndim - 1] = 1;
    for (int d = static_cast<int>(ndim) - 2; d >= 0; d--)
        out->strides[d] = out->strides[d + 1] * out->dims[d + 1];
    return true;
}

// 数组类型池去重登记（layout 的 `[d0,d1]T` 串 → array_type_id）。
std::uint16_t array_type_of(vmbin::Program* seg, std::string_view lt) {
    vmbin::ArrayTypeDesc t;
    if (!parse_array_kindexpr(lt, &t))
        return vmbin::kArrayTypeNone;
    for (std::size_t k = 0; k < seg->array_types.size(); k++) {
        const vmbin::ArrayTypeDesc& e = seg->array_types[k];
        if (e.elem_kind != t.elem_kind || e.width != t.width ||
            e.ndim != t.ndim || e.numel != t.numel)
            continue;
        bool same = true;
        for (std::uint8_t d = 0; d < t.ndim; d++)
            if (e.dims[d] != t.dims[d])
                same = false;
        if (same)
            return static_cast<std::uint16_t>(k);
    }
    if (seg->array_types.size() >= vmbin::kArrayTypeNone)
        return vmbin::kArrayTypeNone;
    seg->array_types.push_back(t);
    return static_cast<std::uint16_t>(seg->array_types.size() - 1);
}

bool operand_desc(void* h, const std::string& key, RegDesc* out,
                  vmbin::Program* seg) {
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
        // compact 定长数组：`[d0,d1,...]T` → 静态类型池（JIT 内联的唯一前置）。
        if (!ltype.empty() && ltype.front() == '[')
            out->array_type_id = array_type_of(seg, ltype);
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

// 控制指令 call 的表内 id：按 Op/Impl 查表，不走字符串（阶段 B 要求算子身份
// 来自指令本身而不是名字解析；这里是建段期的一次性定位）。
std::uint16_t call_rwir_id() {
    static const std::uint16_t id = [] {
        for (int i = 0; i < rwir::kMyrwircapsN; i++) {
            if (rwir::myrwircaps[i].op == rwir::Op::Call &&
                rwir::myrwircaps[i].impl == rwir::Impl::Control)
                return static_cast<std::uint16_t>(i);
        }
        return static_cast<std::uint16_t>(0xFFFF);
    }();
    return id;
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
            std::string key;
            if (lit.is_i64)
                key = "int:" + std::to_string(lit.v);
            else if (lit.is_f64) {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &lit.f, sizeof bits);
                key = "float:" + std::to_string(bits);
            } else {
                key = "text:" + d.slot_key + "|" + d.name;
            }
            // 只读引用的常量（池条目）连 key 一起比；分支目标常量只比值。
            if ((!lit.is_i64 && !lit.is_f64) || (i < ref.size() && ref[i]))
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

// 从 myrwircaps 条目填指令字段（建段与特化共用的唯一一份映射）。
void fill_from_entry(const rwir::Entry* e, CInsn* ins) {
    ins->rwir_id = static_cast<std::uint16_t>(e - rwir::myrwircaps);
    ins->op = e->op;
    ins->kind = e->kind;
    ins->width = e->width;
    ins->impl = e->impl;
    ins->handler = e->handler;
    ins->fast_i64 = false;
    ins->opfn = 0;
    if (e->kind == rwir::Kind::Int && e->width == 8) {
        switch (e->op) {
        case rwir::Op::Add: ins->fast_i64 = true; ins->opfn = 1; break;
        case rwir::Op::Sub: ins->fast_i64 = true; ins->opfn = 2; break;
        case rwir::Op::Mul: ins->fast_i64 = true; ins->opfn = 3; break;
        case rwir::Op::Div: ins->fast_i64 = true; ins->opfn = 4; break;
        case rwir::Op::Mod: ins->fast_i64 = true; ins->opfn = 5; break;
        case rwir::Op::Max: ins->fast_i64 = true; ins->opfn = 6; break;
        case rwir::Op::Min: ins->fast_i64 = true; ins->opfn = 7; break;
        case rwir::Op::BitAnd: ins->fast_i64 = true; ins->opfn = 8; break;
        case rwir::Op::BitOr: ins->fast_i64 = true; ins->opfn = 9; break;
        case rwir::Op::BitXor: ins->fast_i64 = true; ins->opfn = 10; break;
        case rwir::Op::Shl: ins->fast_i64 = true; ins->opfn = 11; break;
        case rwir::Op::Shr: ins->fast_i64 = true; ins->opfn = 12; break;
        case rwir::Op::Eq: ins->fast_i64 = true; ins->opfn = 13; break;
        case rwir::Op::Neq: ins->fast_i64 = true; ins->opfn = 14; break;
        case rwir::Op::Lt: ins->fast_i64 = true; ins->opfn = 15; break;
        case rwir::Op::Le: ins->fast_i64 = true; ins->opfn = 16; break;
        case rwir::Op::Gt: ins->fast_i64 = true; ins->opfn = 17; break;
        case rwir::Op::Ge: ins->fast_i64 = true; ins->opfn = 18; break;
        default: break;
        }
    }
}

// 建段：从 kvspace 读签名与指令、解析分支目标/被调方、物化字面量。
bool build(void* h, const std::string& func_dir, Segment* out,
           CompileError* err) {
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
            const std::uint16_t cid = call_rwir_id();
            ins.rwir_id = cid != 0xFFFF
                              ? cid
                              : static_cast<std::uint16_t>(
                                    vmbin::kRwirIdExtraBase +
                                    pool_add(&out->extra_rwir_names,
                                             &extra_ids, "call"));
        } else if (e) {
            fill_from_entry(e, &ins);
        } else if (!is_declared_rwir(h, op)) {
            // #583 §2·4：未知 rwir 不得静默走 handoff——不在 myrwircap 且
            // `/lib/<op>` 无 def rwir 声明，即 layout 产出了本 runtime 无法执行的算子。
            std::fprintf(stderr,
                         "runtimec2: unknown rwir \"%s\" in %s: not in myrwircap "
                         "and no /lib/%s def rwir\n",
                         op.c_str(), func_dir.c_str(), op.c_str());
            err->reason = "unknown rwir \"" + op + "\": not in myrwircap and no /lib/" +
                          op + " def rwir";
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
        if (nr + (is_call ? 1 : 0) > 16 || nw > 4) {
            err->reason = "instruction at pc " + std::to_string(s0) +
                          ": read/write arity out of range";
            return false;
        }
        ins.nrd = static_cast<std::uint8_t>(nr + (is_call ? 1 : 0));
        ins.nwr = static_cast<std::uint8_t>(nw);
        int rbase = is_call ? 1 : 0;
        for (int k = 1; k <= nr; k++) {
            const std::string rk = slot_key(func_dir, s0, -k);
            RegDesc d;
            if (!operand_desc(h, rk, &d, out)) {
                err->reason = "cannot read read-operand slot at pc " +
                              std::to_string(s0);
                return false;
            }
            const std::string tag = std::string(d.kind == Operand::Kind::Var ? "v:" : "l:") +
                                    (d.kind == Operand::Kind::Var ? d.name : rk);
            const int ri = reg_of(out, &dedup, tag, d);
            ins.rd[rbase + k - 1] = static_cast<std::uint8_t>(ri);
            record_type(ri, d.langtype, d.width, false);
        }
        for (int k = 1; k <= nw; k++) {
            const std::string wk = slot_key(func_dir, s0, k);
            RegDesc d;
            if (!operand_desc(h, wk, &d, out)) {
                err->reason = "cannot read write-operand slot at pc " +
                              std::to_string(s0);
                return false;
            }
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
            std::fprintf(stderr, "[segdmp reg %zu atype=%u]\n", ri,
                         (unsigned)d.array_type_id);
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
                } else if (sc.k == Scalar::K::Float && sc.width == 8) {
                    out->lit[i].is_f64 = true;
                    out->lit[i].f = sc.f;
                }
            }
        }
    }
    return true;
}

// binir 收尾：#583 L2 阶段 B 的编码与解码视图切换。顺序与历史一致：
// 先 encode（成功即存 out->binir），再按开关决定是否用解码视图替换。
bool finalize_binir(Segment* out, const std::string& func_dir,
                    CompileError* err) {
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
        err->reason =
            "binir encode failed (invalid branch target or unsupported "
            "instruction shape)";
        return false;
    }
    Segment decoded;
    if (!decode_binir(out->binir.data(), out->binir.size(), &decoded)) {
        err->reason = "binir decode failed";
        return false;
    }
    if (binir_check_enabled()) {
        std::string why;
        if (!same_program(*out, decoded, &why)) {
            std::fprintf(stderr, "runtimec2: binir check failed for %s: %s\n",
                         func_dir.c_str(), why.c_str());
            err->reason = "binir check failed: " + why;
            return false;
        }
    }
    if (segment_from_binir()) {
        decoded.binir = out->binir;
        *out = std::move(decoded);
    }
    return true;
}

// ── 按实参类型特化 ───────────────────────────────────────────────────
//
// 数值 promotion 的唯一书面定义：
//   stdlib/kvlang/spec/05-runtime语义/11-指令派发与特化.kv
//   §opcode 特化命名、§数值 promotion（normative）。
// layout 的 specialize 与本模块都按该表实现；实现偏差按 bug 单独修，任一侧都
// 不得加兼容分支。现状已知偏差（见 workspace/binircompiler-STATUS.md）：
// layout 的 promoted_numeric_kind 对 float64+int 用位/字节混比 wmax，发出
// float32·add——那是 layout 的实现 bug，本模块不绕过。

struct NumKind {
    rwir::Kind kind = rwir::Kind::None;
    std::uint8_t width = 0;
};

bool numeric_of(LangType t, NumKind* out) {
    switch (t) {
    case LangType::Int8: out->kind = rwir::Kind::Int; out->width = 1; return true;
    case LangType::Int16: out->kind = rwir::Kind::Int; out->width = 2; return true;
    case LangType::Int32: out->kind = rwir::Kind::Int; out->width = 4; return true;
    case LangType::Int64: out->kind = rwir::Kind::Int; out->width = 8; return true;
    case LangType::Uint8: out->kind = rwir::Kind::Uint; out->width = 1; return true;
    case LangType::Uint16: out->kind = rwir::Kind::Uint; out->width = 2; return true;
    case LangType::Uint32: out->kind = rwir::Kind::Uint; out->width = 4; return true;
    case LangType::Uint64: out->kind = rwir::Kind::Uint; out->width = 8; return true;
    case LangType::Float32: out->kind = rwir::Kind::Float; out->width = 4; return true;
    case LangType::Float64: out->kind = rwir::Kind::Float; out->width = 8; return true;
    default: return false;
    }
}

LangType langtype_of(NumKind k) {
    switch (k.kind) {
    case rwir::Kind::Int:
        return k.width == 1   ? LangType::Int8
               : k.width == 2 ? LangType::Int16
               : k.width == 4 ? LangType::Int32
               : k.width == 8 ? LangType::Int64
                              : LangType::None;
    case rwir::Kind::Uint:
        return k.width == 1   ? LangType::Uint8
               : k.width == 2 ? LangType::Uint16
               : k.width == 4 ? LangType::Uint32
               : k.width == 8 ? LangType::Uint64
                              : LangType::None;
    case rwir::Kind::Float:
        return k.width == 4 ? LangType::Float32
               : k.width == 8 ? LangType::Float64
                              : LangType::None;
    default:
        return LangType::None;
    }
}

std::optional<NumKind> promote(const std::vector<NumKind>& kinds,
                               bool allow_mixed) {
    if (kinds.empty())
        return std::nullopt;
    bool has_int = false, has_uint = false, has_float = false, has_f64 = false;
    for (const NumKind& k : kinds) {
        switch (k.kind) {
        case rwir::Kind::Int: has_int = true; break;
        case rwir::Kind::Uint: has_uint = true; break;
        case rwir::Kind::Float:
            has_float = true;
            if (k.width == 8)
                has_f64 = true;
            break;
        default: return std::nullopt;
        }
    }
    if (has_int && has_uint)
        return std::nullopt;  // 混合符号：通用实现报错，不特化（spec）
    if (has_float) {
        if ((has_int || has_uint) && !allow_mixed)
            return std::nullopt;  // 比较类跨类混合：不特化（spec）
        // float × float / int × float：任一方 float64 → float64，否则 float32。
        return NumKind{rwir::Kind::Float, has_f64 ? std::uint8_t{8}
                                                  : std::uint8_t{4}};
    }
    NumKind out = kinds[0];
    for (std::size_t i = 1; i < kinds.size(); i++)
        out.width = std::max(out.width, kinds[i].width);
    return out;
}

bool compare_op(rwir::Op op) {
    switch (op) {
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

bool arithmetic_op(rwir::Op op) {
    switch (op) {
    case rwir::Op::Add:
    case rwir::Op::Sub:
    case rwir::Op::Mul:
    case rwir::Op::Div:
    case rwir::Op::Mod:
    case rwir::Op::Max:
    case rwir::Op::Min:
    case rwir::Op::Neg:
    case rwir::Op::Sign:
    case rwir::Op::Abs:
    case rwir::Op::BitAnd:
    case rwir::Op::BitOr:
    case rwir::Op::BitXor:
    case rwir::Op::Shl:
    case rwir::Op::Shr:
        return true;
    default:
        return false;
    }
}

// spec §数值 promotion：sqrt/exp/log/pow 结果与特化 kind 恒为 float64；
// float32 操作数不特化。
bool float64_result_op(rwir::Op op) {
    switch (op) {
    case rwir::Op::Sqrt:
    case rwir::Op::Exp:
    case rwir::Op::Log:
    case rwir::Op::Pow:
        return true;
    default:
        return false;
    }
}

bool logical_op(rwir::Op op) {
    return op == rwir::Op::And || op == rwir::Op::Or || op == rwir::Op::Not;
}

// (op, 数值类型) → 唯一的类型化条目；零命中或多命中都返回 nullptr（不猜）。
const rwir::Entry* typed_variant(rwir::Op op, NumKind kind) {
    const rwir::Entry* found = nullptr;
    for (int i = 0; i < rwir::kMyrwircapsN; i++) {
        const rwir::Entry& e = rwir::myrwircaps[i];
        if (e.impl != rwir::Impl::Typed || e.op != op)
            continue;
        if (e.kind != kind.kind || e.width != kind.width)
            continue;
        if (found)
            return nullptr;
        found = &e;
    }
    return found;
}

enum class TypeOrigin : std::uint8_t {
    None = 0,
    Argument,    // 实参类型
    Slot,        // 槽自身带类型串（layout 已标注）
    Literal,     // 字面量
    Propagated,  // 传播得到
};

const char* origin_name(TypeOrigin o) {
    switch (o) {
    case TypeOrigin::Argument: return "实参";
    case TypeOrigin::Slot: return "槽类型";
    case TypeOrigin::Literal: return "字面量";
    case TypeOrigin::Propagated: return "传播";
    default: return "";
    }
}

struct RegType {
    enum class State : std::uint8_t { Unknown = 0, Known, Conflict };
    State state = State::Unknown;
    LangType langtype = LangType::None;
    std::uint8_t width = 0;
    TypeOrigin origin = TypeOrigin::None;
};

// 形参/返回槽的声明是函数键的成员（`<func>.[0,±k]`，点分隔），
// 与指令槽 `<func>/[s0,±k]`（斜杠分隔）不是同一种键。
std::string declaration_key(const std::string& func_dir, int s1) {
    std::string k = func_dir;
    if (!k.empty() && k.back() == '/')
        k.pop_back();
    k += ".[0,";
    k += std::to_string(s1);
    k += "]";
    return k;
}

// 声明键 body = 名字\0类型串（head langtype 恒为 "def langtype"，
// 所以不能走 read_langtype；对齐 vmkv 的 param_decl_type）。
bool read_param_decl(void* h, const std::string& func_dir, int s1,
                     std::string* name, std::string* langtype) {
    const std::uint8_t* data = nullptr;
    std::uint32_t len = 0;
    if (!read_wire(h, declaration_key(func_dir, s1), &data, &len) || !data || !len)
        return false;
    std::string txt;
    if (!wire_text(data, len, "def langtype", &txt))
        return false;
    const std::size_t nul = txt.find('\0');
    if (name)
        *name = nul == std::string::npos ? std::string() : txt.substr(0, nul);
    if (langtype)
        *langtype = nul == std::string::npos
                        ? txt
                        : (nul + 1 < txt.size() ? txt.substr(nul + 1)
                                                : std::string());
    return true;
}

int declared_read_param_count(void* h, const std::string& func_dir) {
    int n = 0;
    while (n < 16 && read_param_decl(h, func_dir, -(n + 1), nullptr, nullptr))
        n++;
    return n;
}

// 声明类型与给定实参类型是否冲突：两者都能解析成标量时按 (kind,width) 比；
// 否则按串比（char/容器/数组维度必须逐字相等才算同一类型）。
bool argument_conflicts(const std::string& declared, const std::string& supplied) {
    if (declared.empty())
        return false;  // 显式无约束形参（`x:`）
    LangType a = LangType::None, b = LangType::None;
    std::uint8_t aw = 0, bw = 0;
    const bool pa = langtype_kind(declared, &a, &aw);
    const bool pb = langtype_kind(supplied, &b, &bw);
    if (pa && pb)
        return a != b || aw != bw;
    return declared != supplied;
}

std::string reg_label(const Segment& seg, std::uint8_t index) {
    if (index >= seg.regs.size())
        return "<oor:" + std::to_string(index) + ">";
    const RegDesc& d = seg.regs[index];
    if (!d.name.empty())
        return d.name;
    if (index < seg.lit.size() && seg.lit[index].is_i64)
        return std::to_string(seg.lit[index].v);
    if (index < seg.lit.size() && seg.lit[index].is_f64) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", seg.lit[index].f);
        return buf;
    }
    return d.slot_key;
}

std::string type_name(LangType lt) {
    const char* n = langtype_name(lt);
    return n ? n : "?";
}

struct Specialization {
    void* h = nullptr;
    Segment* seg = nullptr;
    std::string func_dir;
    std::vector<RegType> types;
    bool changed = false;

    bool known(std::uint8_t r) const {
        return r < types.size() && types[r].state == RegType::State::Known;
    }
    LangType type_of(std::uint8_t r) const {
        return r < types.size() ? types[r].langtype : LangType::None;
    }
    void assign(std::uint8_t r, LangType lt, std::uint8_t width,
                TypeOrigin origin) {
        if (r >= types.size())
            return;
        RegType& t = types[r];
        if (t.state == RegType::State::Conflict)
            return;
        if (t.state == RegType::State::Unknown) {
            t.state = RegType::State::Known;
            t.langtype = lt;
            t.width = width;
            t.origin = origin;
            changed = true;
        } else if (t.langtype != lt || t.width != width) {
            t.state = RegType::State::Conflict;
            t.langtype = LangType::None;
            t.width = 0;
            changed = true;
        }
    }
    void assign_writes(const CInsn& ins, LangType lt, std::uint8_t width,
                       TypeOrigin origin) {
        for (int k = 0; k < ins.nwr; k++)
            assign(ins.wr[k], lt, width, origin);
    }
};

// 操作数类型收集：全部已知且为数值才返回 true；缺失的寄存器进 missing。
bool collect_operands(const Specialization& s, const CInsn& ins,
                      std::vector<NumKind>* kinds,
                      std::vector<std::uint8_t>* missing) {
    for (int k = 0; k < ins.nrd; k++) {
        const std::uint8_t r = ins.rd[k];
        if (!s.known(r)) {
            missing->push_back(r);
            continue;
        }
        NumKind nk;
        if (!numeric_of(s.type_of(r), &nk)) {
            missing->push_back(r);
            continue;
        }
        kinds->push_back(nk);
    }
    return missing->empty();
}

// typed 指令自证的结果类型（写槽用）。copy 由读参决定 → None。
std::optional<LangType> typed_result_type(const CInsn& ins,
                                          std::uint8_t* width) {
    if (compare_op(ins.op)) {
        *width = 1;
        return LangType::Bool;
    }
    if (float64_result_op(ins.op)) {
        *width = 8;
        return LangType::Float64;
    }
    if (ins.kind == rwir::Kind::None)
        return std::nullopt;
    *width = ins.width;
    return langtype_of(NumKind{ins.kind, ins.width});
}

// 被调方返回槽的声明类型（`/lib/<callee>/[0,k]`）；读不到 = 无信息。
LangType callee_return_type(void* h, const Segment& seg, const CInsn& ins,
                            int index, std::uint8_t* width) {
    if (ins.op != rwir::Op::Call || ins.call_site >= seg.call_sites.size())
        return LangType::None;
    const std::uint16_t callee_id = seg.call_sites[ins.call_site].callee_id;
    if (callee_id >= seg.callees.size())
        return LangType::None;
    std::string lt;
    if (!read_param_decl(h, seg.callees[callee_id], index + 1, nullptr, &lt) ||
        lt.empty())
        return LangType::None;
    LangType kind = LangType::None;
    std::uint8_t w = 0;
    if (!langtype_kind(lt, &kind, &w))
        return LangType::None;
    *width = w;
    return kind;
}

// 不动点：只做可证明的传播（同写者一致原则；冲突寄存器吸收为 Conflict）。
void propagate(Specialization* s) {
    const int max_iter = 2 * static_cast<int>(s->seg->insns.size()) + 2;
    for (int iter = 0; iter < max_iter; iter++) {
        s->changed = false;
        for (const CInsn& ins : s->seg->insns) {
            if (ins.impl == rwir::Impl::Handoff)
                continue;
            if (ins.impl == rwir::Impl::Typed && ins.op != rwir::Op::Copy) {
                std::uint8_t width = 0;
                if (const auto lt = typed_result_type(ins, &width); lt)
                    s->assign_writes(ins, *lt, width, TypeOrigin::Propagated);
                continue;
            }
            if (ins.op == rwir::Op::Copy) {
                if (ins.nrd >= 1 && s->known(ins.rd[0])) {
                    const std::uint8_t r = ins.rd[0];
                    s->assign_writes(ins, s->type_of(r), s->types[r].width,
                                     TypeOrigin::Propagated);
                }
                continue;
            }
            if (logical_op(ins.op)) {
                bool all_bool = ins.nrd > 0;
                for (int k = 0; k < ins.nrd && all_bool; k++)
                    all_bool = s->known(ins.rd[k]) &&
                               s->type_of(ins.rd[k]) == LangType::Bool;
                if (all_bool)
                    s->assign_writes(ins, LangType::Bool, 1, TypeOrigin::Propagated);
                continue;
            }
            if (ins.op == rwir::Op::Call) {
                for (int k = 0; k < ins.nwr; k++) {
                    std::uint8_t width = 0;
                    const LangType lt = callee_return_type(
                        s->h, *s->seg, ins, k, &width);
                    if (lt != LangType::None)
                        s->assign(ins.wr[k], lt, width, TypeOrigin::Propagated);
                }
                continue;
            }
            if (!compare_op(ins.op) && !arithmetic_op(ins.op) &&
                !float64_result_op(ins.op))
                continue;
            std::vector<NumKind> kinds;
            std::vector<std::uint8_t> missing;
            if (!collect_operands(*s, ins, &kinds, &missing))
                continue;
            if (compare_op(ins.op)) {
                if (!promote(kinds, /*allow_mixed=*/false))
                    continue;
                s->assign_writes(ins, LangType::Bool, 1, TypeOrigin::Propagated);
                continue;
            }
            const auto promoted = promote(kinds, /*allow_mixed=*/true);
            if (!promoted)
                continue;
            if (float64_result_op(ins.op)) {
                if (promoted->kind == rwir::Kind::Float && promoted->width == 4)
                    continue;
                s->assign_writes(ins, LangType::Float64, 8,
                                 TypeOrigin::Propagated);
                continue;
            }
            // 一元 `-`（op=Sub, 1 参）按 Neg 语义。
            rwir::Op target = ins.op;
            if (ins.op == rwir::Op::Sub && ins.nrd == 1)
                target = rwir::Op::Neg;
            if ((target == rwir::Op::Neg || target == rwir::Op::Abs) &&
                promoted->kind == rwir::Kind::Uint)
                continue;
            if (target == rwir::Op::Sign &&
                !(promoted->kind == rwir::Kind::Int && promoted->width == 8))
                continue;
            if ((target == rwir::Op::BitAnd || target == rwir::Op::BitOr ||
                 target == rwir::Op::BitXor || target == rwir::Op::Shl ||
                 target == rwir::Op::Shr) &&
                promoted->width != 8)
                continue;
            s->assign_writes(ins, langtype_of(*promoted), promoted->width,
                             TypeOrigin::Propagated);
        }
        if (!s->changed)
            break;
    }
}

// 逐条改写：泛型数值算子 → 唯一类型化算子；同时产出诊断报告。
void rewrite(Specialization* s, SpecializationReport* report) {
    Segment& seg = *s->seg;
    for (std::size_t i = 0; i < seg.insns.size(); i++) {
        CInsn& ins = seg.insns[i];
        InstructionResolution res;
        res.pc = i;
        res.from = vmbin::instruction_name(seg, ins);
        res.to = res.from;
        if (report)
            report->instructions.push_back(res);
        InstructionResolution* row =
            report ? &report->instructions.back() : nullptr;

        if (ins.impl == rwir::Impl::Typed) {
            if (row) row->reason = "已是类型化算子";
            continue;
        }
        if (ins.op == rwir::Op::Copy) {
            if (row) row->reason = "copy：类型传播（非数值算子）";
            continue;
        }
        if (logical_op(ins.op)) {
            if (row) row->reason = "无 bool 类型化变体";
            continue;
        }
        if (!compare_op(ins.op) && !arithmetic_op(ins.op) &&
            !float64_result_op(ins.op)) {
            if (row) row->reason = "不参与特化";
            continue;
        }
        std::vector<NumKind> kinds;
        std::vector<std::uint8_t> missing;
        if (!collect_operands(*s, ins, &kinds, &missing)) {
            if (row) {
                std::string names;
                for (std::size_t k = 0; k < missing.size(); k++) {
                    if (k) names += ",";
                    names += reg_label(seg, missing[k]);
                }
                row->reason = "操作数类型未知（" + names + "）";
            }
            continue;
        }
        // 一元 `-`（op=Sub, 1 参）按 Neg 语义。
        rwir::Op target = ins.op;
        if (ins.op == rwir::Op::Sub && ins.nrd == 1)
            target = rwir::Op::Neg;
        const auto promoted = promote(kinds, /*allow_mixed=*/!compare_op(ins.op));
        if (!promoted) {
            if (row) row->reason = "操作数跨类混合（int/float 或 int/uint），不可证明";
            continue;
        }
        LangType result = langtype_of(*promoted);
        std::uint8_t result_width = promoted->width;
        NumKind variant_kind = *promoted;
        if (compare_op(ins.op)) {
            result = LangType::Bool;
            result_width = 1;
        } else if (float64_result_op(ins.op)) {
            if (promoted->kind == rwir::Kind::Float && promoted->width == 4) {
                if (row) row->reason = "float32 无 float64 结果的类型化变体";
                continue;
            }
            result = LangType::Float64;
            result_width = 8;
            variant_kind = NumKind{rwir::Kind::Float, 8};
        } else if ((target == rwir::Op::Neg || target == rwir::Op::Abs) &&
                   promoted->kind == rwir::Kind::Uint) {
            if (row) row->reason = "无符号数无 neg/abs 类型化变体";
            continue;
        } else if (target == rwir::Op::Sign &&
                   !(promoted->kind == rwir::Kind::Int &&
                     promoted->width == 8)) {
            if (row) row->reason = "sign 只有 int64 类型化变体";
            continue;
        } else if ((target == rwir::Op::BitAnd || target == rwir::Op::BitOr ||
                    target == rwir::Op::BitXor || target == rwir::Op::Shl ||
                    target == rwir::Op::Shr) &&
                   promoted->width != 8) {
            if (row) row->reason = "位运算只有 64 位类型化变体";
            continue;
        }
        // 写槽必须先自洽：已有不同类型 = 冲突，不改写。
        bool writes_ok = true;
        for (int k = 0; k < ins.nwr; k++) {
            const std::uint8_t r = ins.wr[k];
            if (!s->known(r))
                continue;
            if (s->type_of(r) != result ||
                s->types[r].width != result_width)
                writes_ok = false;
        }
        if (!writes_ok) {
            if (row) row->reason = "写槽类型冲突";
            continue;
        }
        const rwir::Entry* e = typed_variant(target, variant_kind);
        if (!e) {
            if (row) row->reason = "找不到唯一类型化变体";
            continue;
        }
        fill_from_entry(e, &ins);
        s->assign_writes(ins, result, result_width, TypeOrigin::Propagated);
        if (row) {
            std::string evidence;
            for (std::size_t k = 0; k < ins.nrd; k++) {
                const std::uint8_t r = ins.rd[k];
                if (k) evidence += ", ";
                evidence += reg_label(seg, r) + ":" + type_name(s->type_of(r));
                const char* origin = origin_name(s->types[r].origin);
                if (*origin)
                    evidence += std::string("(") + origin + ")";
            }
            evidence += std::string(" -> ") + type_name(result);
            row->to = vmbin::instruction_name(seg, ins);
            row->reason = evidence;
        }
    }
    if (!report)
        return;
    for (const CInsn& ins : seg.insns) {
        if (ins.impl == rwir::Impl::Typed)
            report->typed++;
        else if (ins.impl == rwir::Impl::Dynamic)
            report->generic++;
    }
}

bool specialize(void* h, const std::string& func_dir,
                const std::vector<ArgumentType>& arguments, Segment* seg,
                CompileError* err, SpecializationReport* report) {
    const int declared = declared_read_param_count(h, func_dir);
    if (static_cast<int>(arguments.size()) != declared) {
        err->reason = "expects " + std::to_string(declared) +
                      " read argument(s), got " +
                      std::to_string(arguments.size());
        return false;
    }
    Specialization s;
    s.h = h;
    s.seg = seg;
    s.func_dir = func_dir;
    s.types.resize(seg->regs.size());
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        const RegDesc& d = seg->regs[i];
        if (d.langtype != LangType::None) {
            s.types[i].state = RegType::State::Known;
            s.types[i].langtype = d.langtype;
            s.types[i].width = d.width;
            s.types[i].origin = TypeOrigin::Slot;
        } else if (i < seg->lit.size() && seg->lit[i].is_i64) {
            s.types[i].state = RegType::State::Known;
            s.types[i].langtype = LangType::Int64;
            s.types[i].width = 8;
            s.types[i].origin = TypeOrigin::Literal;
        } else if (i < seg->lit.size() && seg->lit[i].is_f64) {
            s.types[i].state = RegType::State::Known;
            s.types[i].langtype = LangType::Float64;
            s.types[i].width = 8;
            s.types[i].origin = TypeOrigin::Literal;
        }
    }
    // 实参种子：形参槽 `*[0,-k]` 必须与声明类型一致，否则编译失败（不猜）。
    for (int k = 1; k <= declared; k++) {
        const std::string& supplied = arguments[static_cast<std::size_t>(k - 1)]
                                          .langtype;
        if (supplied.empty()) {
            err->reason = "argument " + std::to_string(k) + ": empty langtype";
            return false;
        }
        std::string declared_type;
        read_param_decl(h, func_dir, -k, nullptr, &declared_type);
        if (argument_conflicts(declared_type, supplied)) {
            err->reason = "argument " + std::to_string(k) + ": supplied \"" +
                          supplied + "\", declared \"" + declared_type + "\"";
            return false;
        }
        LangType lt = LangType::None;
        std::uint8_t width = 0;
        if (!langtype_kind(supplied, &lt, &width))
            continue;  // char/容器：不参与数值传播
        for (std::size_t i = 0; i < seg->regs.size(); i++) {
            const RegDesc& d = seg->regs[i];
            if (d.vclass != RegDesc::VarClass::ParamPtr || d.param_k != -k)
                continue;
            RegType& t = s.types[i];
            if (t.state == RegType::State::Known &&
                (t.langtype != lt || t.width != width)) {
                err->reason = "argument " + std::to_string(k) +
                              ": conflicts with slot type";
                return false;
            }
            t.state = RegType::State::Known;
            t.langtype = lt;
            t.width = width;
            t.origin = TypeOrigin::Argument;
        }
    }
    propagate(&s);
    rewrite(&s, report);
    // 定稿：只有明确证明过的槽补写静态类型（不覆盖建段 E 层已有值）。
    for (std::size_t i = 0; i < seg->regs.size(); i++) {
        if (s.types[i].state == RegType::State::Known &&
            seg->regs[i].langtype == LangType::None) {
            seg->regs[i].langtype = s.types[i].langtype;
            seg->regs[i].width = s.types[i].width;
        }
    }
    return true;
}

std::unordered_map<std::string, Segment>& cache() {
    static std::unordered_map<std::string, Segment> c;
    return c;
}

std::string cache_key(const std::string& func_dir,
                      const std::vector<ArgumentType>& arguments) {
    std::string key = func_dir;
    key += '\x1f';
    key += std::to_string(arguments.size());
    for (const ArgumentType& a : arguments) {
        key += '\x1f';
        key += a.langtype;
    }
    return key;
}

}  // namespace

namespace {

// #594（规则 2/3：每个写参在所有返回路径必须赋值）——独立实现，规则落地前为空。
// 与阶段 A 的 union 校验分开，互不掺杂。
bool run_write_assignment_checks(const Segment& segment, CompileError* error) {
    (void)segment;
    (void)error;
    return true;
}

rwir::ScalarClass scalar_class_of(LangType t) {
    switch (t) {
    case LangType::Int8: return rwir::ScalarClass::SI8;
    case LangType::Int16: return rwir::ScalarClass::SI16;
    case LangType::Int32: return rwir::ScalarClass::SI32;
    case LangType::Int64: return rwir::ScalarClass::SI64;
    case LangType::Uint8: return rwir::ScalarClass::UI8;
    case LangType::Uint16: return rwir::ScalarClass::UI16;
    case LangType::Uint32: return rwir::ScalarClass::UI32;
    case LangType::Uint64: return rwir::ScalarClass::UI64;
    case LangType::Float32: return rwir::ScalarClass::F32;
    case LangType::Float64: return rwir::ScalarClass::F64;
    case LangType::Bool: return rwir::ScalarClass::Bool;
    default: return rwir::ScalarClass::None;
    }
}

std::optional<NumKind> numkind_of_class(rwir::ScalarClass c) {
    switch (c) {
    case rwir::ScalarClass::SI8: return NumKind{rwir::Kind::Int, 1};
    case rwir::ScalarClass::SI16: return NumKind{rwir::Kind::Int, 2};
    case rwir::ScalarClass::SI32: return NumKind{rwir::Kind::Int, 4};
    case rwir::ScalarClass::SI64: return NumKind{rwir::Kind::Int, 8};
    case rwir::ScalarClass::UI8: return NumKind{rwir::Kind::Uint, 1};
    case rwir::ScalarClass::UI16: return NumKind{rwir::Kind::Uint, 2};
    case rwir::ScalarClass::UI32: return NumKind{rwir::Kind::Uint, 4};
    case rwir::ScalarClass::UI64: return NumKind{rwir::Kind::Uint, 8};
    case rwir::ScalarClass::F32: return NumKind{rwir::Kind::Float, 4};
    case rwir::ScalarClass::F64: return NumKind{rwir::Kind::Float, 8};
    default: return std::nullopt;
    }
}

// 指令的族签名：动态行直接看 Entry.signature，typed 行按 Op 查。
const rwir::OpSignature* instruction_signature(const CInsn& ins) {
    if (ins.rwir_id >= static_cast<std::uint16_t>(rwir::kMyrwircapsN))
        return nullptr;  // 扩展算子（extra 池）
    const rwir::Entry& e = rwir::myrwircaps[ins.rwir_id];
    return e.signature ? e.signature : rwir::signature_of(e.op);
}

// 全操作数已知时的结果 class；nullopt 表示 spec 给不出（或比较类跨类，
// 只由通用实现按值处理，不算错误）。
std::optional<rwir::ScalarClass> result_class(
    const rwir::OpSignature& sig, const CInsn& ins,
    const std::vector<rwir::ScalarClass>& operands) {
    switch (sig.result) {
    case rwir::OpSignature::Result::Bool:
        return rwir::ScalarClass::Bool;
    case rwir::OpSignature::Result::Float64:
        return rwir::ScalarClass::F64;
    case rwir::OpSignature::Result::Target:
        return std::nullopt;
    case rwir::OpSignature::Result::Same:
        if (operands.empty())
            return std::nullopt;
        return operands[0];
    case rwir::OpSignature::Result::Promoted:
        break;
    }
    std::vector<NumKind> kinds;
    kinds.reserve(operands.size());
    for (const rwir::ScalarClass c : operands) {
        const auto k = numkind_of_class(c);
        if (!k)
            return std::nullopt;
        kinds.push_back(*k);
    }
    const auto promoted = promote(kinds, /*allow_mixed=*/!compare_op(ins.op));
    if (!promoted)
        return std::nullopt;
    return scalar_class_of(langtype_of(*promoted));
}

// 阶段 A（def rwir union 签名）的编译期校验入口：操作数类型集合必须 ⊆ 算子
// union；全已知时结果 class 必须与写槽声明一致。规则见
// stdlib/kvlang/spec/05-runtime语义/11-指令派发与特化.kv。
bool run_union_signature_checks(const Segment& segment, CompileError* error) {
    for (std::size_t i = 0; i < segment.insns.size(); i++) {
        const CInsn& ins = segment.insns[i];
        for (int k = 0; k < ins.nrd; k++) {
            // 与 read_referenced 同一口径：这些位置的 rd 不是寄存器槽
            // （call 的 rd[0] 是哨兵，goto 的 rd[0] 与 br 的 rd[1..] 是分支目标）。
            if (ins.op == rwir::Op::Call && k == 0)
                continue;
            if (ins.op == rwir::Op::Goto)
                continue;
            if (ins.op == rwir::Op::Br && k >= 1)
                continue;
            if (ins.rd[k] >= segment.regs.size()) {
                error->reason = "insn " + std::to_string(i) +
                                ": read slot out of range";
                return false;
            }
        }
        for (int k = 0; k < ins.nwr; k++) {
            if (ins.wr[k] >= segment.regs.size()) {
                error->reason = "insn " + std::to_string(i) +
                                ": write slot out of range";
                return false;
            }
        }
        if (ins.impl == rwir::Impl::Typed &&
            (ins.kind == rwir::Kind::None || ins.width == 0)) {
            error->reason = "insn " + std::to_string(i) +
                            ": typed instruction without kind/width";
            return false;
        }
        const rwir::OpSignature* sig = instruction_signature(ins);
        if (!sig)
            continue;
        const std::uint8_t arity = ins.nrd;
        if (arity < sig->min_arity ||
            (sig->max_arity != 0 && arity > sig->max_arity)) {
            error->reason = "insn " + std::to_string(i) + ": arity " +
                            std::to_string(arity) + " outside operator union";
            return false;
        }
        std::vector<rwir::ScalarClass> operands;
        bool all_known = true;
        for (int k = 0; k < ins.nrd; k++) {
            const rwir::ScalarClass cls =
                scalar_class_of(segment.regs[ins.rd[k]].langtype);
            operands.push_back(cls);
            if (cls == rwir::ScalarClass::None) {
                all_known = false;  // char/容器/动态槽：运行时决定
                continue;
            }
            if (!(sig->operands & rwir::type_bit(cls))) {
                error->reason = "insn " + std::to_string(i) +
                                ": operand class not in operator union";
                return false;
            }
        }
        if (!all_known)
            continue;
        // 结果侧只校验「promotion 表必须给得出结果」：算术/位运算的
        // int×uint 混合在运行时直接报错，这里提前拒绝。比较类跨类混合是合法
        // 的（通用实现按值比较），不报错。
        // 注意：不校验写槽的声明类型——layout 对未特化的通用算子可能按运行时
        // 实际宽度落槽，与 typed 变体的结果宽度不同属于阶段 B 的统一范围。
        if (sig->result == rwir::OpSignature::Result::Promoted &&
            !compare_op(ins.op)) {
            std::vector<NumKind> kinds;
            for (const rwir::ScalarClass c : operands) {
                const auto nk = numkind_of_class(c);
                if (nk)
                    kinds.push_back(*nk);
            }
            if (!promote(kinds, /*allow_mixed=*/true)) {
                error->reason = "insn " + std::to_string(i) +
                                ": promotion undefined for mixed operands";
                return false;
            }
        }
    }
    return true;
}

// 交叉校验：layout 已发出的 typed 指令，必须与「按 union/ promotion 选出的
// 唯一实现」完全一致；反过来，操作数类型全已知、表里存在对应 typed 变体时，
// layout 不该留泛型。类型未知的指令计入 undecidable（char/容器/动态槽）。
bool cross_check_union(const Segment& segment, UnionCheckReport* report,
                       CompileError* error) {
    for (std::size_t i = 0; i < segment.insns.size(); i++) {
        const CInsn& ins = segment.insns[i];
        if (ins.impl == rwir::Impl::Handoff)
            continue;
        const rwir::OpSignature* sig = instruction_signature(ins);
        if (!sig)
            continue;
        report->checked++;
        // 变参算子（min/max）没有 typed 变体，不参与逐条对拍。
        if (ins.nrd > 2) {
            report->matched++;
            continue;
        }
        std::vector<rwir::ScalarClass> operands;
        bool all_known = true;
        for (int k = 0; k < ins.nrd; k++) {
            const rwir::ScalarClass cls =
                scalar_class_of(segment.regs[ins.rd[k]].langtype);
            operands.push_back(cls);
            if (cls == rwir::ScalarClass::None)
                all_known = false;
        }
        if (!all_known) {
            report->undecidable++;
            for (int k = 0; k < ins.nrd; k++) {
                const std::uint8_t slot = ins.rd[k];
                if (slot >= segment.regs.size())
                    continue;
                const RegDesc& d = segment.regs[slot];
                if (scalar_class_of(d.langtype) != rwir::ScalarClass::None)
                    continue;
                if (d.kind == Operand::Kind::Literal)
                    report->undecidable_constant++;
                else if (d.member)
                    report->undecidable_member++;
                else if (d.vclass == RegDesc::VarClass::ParamPtr ||
                         d.vclass == RegDesc::VarClass::Ptr)
                    report->undecidable_pointer++;
                else
                    report->undecidable_untyped++;
                break;
            }
            continue;
        }
        const auto expected = result_class(*sig, ins, operands);
        if (!expected) {
            // 比较跨类 / cast：类型已知但没有唯一 typed 期望。
            report->no_typed_variant++;
            continue;
        }
        rwir::Op target = ins.op;
        if (ins.op == rwir::Op::Sub && ins.nrd == 1)
            target = rwir::Op::Neg;
        const auto expected_kind = numkind_of_class(*expected);
        if (!expected_kind) {
            report->undecidable++;
            continue;
        }
        const rwir::Entry* want = typed_variant(target, *expected_kind);
        if (ins.impl == rwir::Impl::Typed) {
            const NumKind actual{ins.kind, ins.width};
            if (!want || actual.kind != want->kind || actual.width != want->width) {
                report->mismatched++;
                if (report->details.size() < 32)
                    report->details.push_back(
                        "pc " + std::to_string(i) + " " +
                        vmbin::instruction_name(segment, ins) +
                        ": typed op disagrees with union");
            } else {
                report->matched++;
            }
            continue;
        }
        if (want) {
            report->mismatched++;
            if (report->details.size() < 32)
                report->details.push_back("pc " + std::to_string(i) + " " +
                                          vmbin::instruction_name(segment, ins) +
                                          ": layout left generic, union selects " +
                                          want->name);
        } else {
            report->matched++;
        }
    }
    (void)error;
    return report->mismatched == 0;
}

}  // namespace

bool RunCompileChecks(const Segment& segment, CompileError* error) {
    if (!run_write_assignment_checks(segment, error))
        return false;
    return run_union_signature_checks(segment, error);
}

bool CrossCheckUnion(const Segment& segment, UnionCheckReport* report,
                     CompileError* error) {
    UnionCheckReport local;
    return cross_check_union(segment, report ? report : &local, error);
}

bool compile(void* h, const std::string& func_dir,
             const CompileOptions& options, Segment* out, CompileError* error,
             SpecializationReport* report) {
    CompileError local;
    CompileError* err = error ? error : &local;
    err->func_dir = func_dir;
    err->reason.clear();
    if (!build(h, func_dir, out, err))
        return false;
    // 第一阶段运行时只走通用编译（codeseg.cpp 只传 CompileOptions{}）；
    // 特化版本只出现在工具进程 / --compare。
    if (options.arguments) {
        if (!specialize(h, func_dir, *options.arguments, out, err, report))
            return false;
    }
    if (!RunCompileChecks(*out, err))
        return false;
    return finalize_binir(out, func_dir, err);
}

bool check_roundtrip(const Segment& segment, std::string* why) {
    std::vector<std::uint8_t> bytes;
    const std::uint8_t* data = segment.binir.data();
    std::size_t size = segment.binir.size();
    if (size == 0) {
        if (!encode_binir(segment, &bytes)) {
            *why = "encode failed";
            return false;
        }
        data = bytes.data();
        size = bytes.size();
    }
    Segment decoded;
    if (!decode_binir(data, size, &decoded)) {
        *why = "decode failed";
        return false;
    }
    return same_program(segment, decoded, why);
}

const Segment* cached(void* h, const std::string& func_dir) {
    auto& c = cache();
    auto it = c.find(func_dir);
    if (it != c.end())
        return &it->second;
    Segment seg;
    CompileError err;
    if (!compile(h, func_dir, CompileOptions{}, &seg, &err))
        return nullptr;
    auto res = c.emplace(func_dir, std::move(seg));
    if (binir_dump_on_build())
        dump_binir(res.first->second, func_dir.c_str());
    return &res.first->second;
}

const Segment* cached(void* h, const std::string& func_dir,
                      const CompileOptions& options) {
    if (!options.arguments)
        return cached(h, func_dir);
    const std::string key = cache_key(func_dir, *options.arguments);
    auto& c = cache();
    auto it = c.find(key);
    if (it != c.end())
        return &it->second;
    Segment seg;
    CompileError err;
    if (!compile(h, func_dir, options, &seg, &err))
        return nullptr;
    auto res = c.emplace(key, std::move(seg));
    if (binir_dump_on_build())
        dump_binir(res.first->second, func_dir.c_str());
    return &res.first->second;
}

const Segment* lookup(const std::string& func_dir) {
    auto& c = cache();
    auto it = c.find(func_dir);
    return it == c.end() ? nullptr : &it->second;
}

void clear_cache() { cache().clear(); }

}  // namespace kvlang::runtimec2::binircompiler
