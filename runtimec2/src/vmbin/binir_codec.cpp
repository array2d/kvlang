// BinIR 字节码编解码。格式：workspace/583/architect/binir-format-spec.md。
//
// 段 = [段头 32B][指令流][常量池][RegDesc 表][2B 零填充]
// 段头：magic u16=0x4642 | version u8 | reserved u8 | ninsn u32 | nreg u16 |
//       reserved u16 | nconst u32 | cpool_off u32 | regs_off u32 | reserved u32×2
//
// 实现约定（2026-10-31 与架构师确认）：
// - op = RwirId = myrwircaps 下标（0..0x1FB）；0x1FC=HANDOFF、0x1FD=BR_IF_FALSE、
//   0x1FE=BR_IF_TRUE、0x1FF=WIDE 标记。
// - NARROW 普通 op 的 imm 低 3 位 = nrd | (nwr << 2)；goto/br 的 imm 是位移、
//   call 的 imm 是 callee 表下标、handoff 的 imm 是名字串池偏移。
// - br 折成单目标：false 目标为 +1 → BR_IF_FALSE，taken 目标为 +1 → BR_IF_TRUE；
//   两目标都不是 +1 时走 WIDE（op = RwirId("br")，ntgt=2）。
// - 常量池每函数一个：值条目（ConstId）按首次引用顺序排在最前；callee 表项与
//   字符串条目随后。解码按「指令流引用到的最大 ConstId + 1」切出值条目区。
// - tag 0 = u64 raw + u32 key 串偏移（0xFFFFFFFF = 无）；tag 3 = u32 len + utf8
//   （在值条目区 = 无 fast 值的常量槽，其文本即槽 key；在串区 = 普通字符串）。
// - RegDesc 8B = kind u8 | width u8 | addr i32（池内串偏移）| flags u8 | param_k i8；
//   flags = vclass(3b) | member(1b)；槽文本用 '\0' 连接 slot_key 与 path 名。
// - 段尾补 2 字节零，允许 fast kernel 8B over-read。

#include "binir_codec.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "rwir.hpp"

namespace kvlang::runtimec2 {
namespace {

using vmbin::FastLiteral;
using vmbin::Instruction;
using vmbin::Operand;
using vmbin::Program;
using vmbin::RegisterDesc;

constexpr std::uint16_t kMagic = 0x4642;
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 32;
constexpr std::uint16_t kOpHandoff = 0x1FC;
constexpr std::uint16_t kOpBrIfFalse = 0x1FD;
constexpr std::uint16_t kOpBrIfTrue = 0x1FE;
constexpr std::uint16_t kOpWide = 0x1FF;
constexpr std::uint16_t kMaxRwirId = 0x1FB;
constexpr std::uint32_t kNoString = 0xFFFFFFFFu;
constexpr std::size_t kRegDescBytes = 8;
constexpr std::size_t kTailBytes = 2;
constexpr int kMaxNarrowIndex = 63;
constexpr int kMaxReads = 16;
constexpr int kMaxWrites = 4;

constexpr std::uint8_t kTagInt64 = 0;
constexpr std::uint8_t kTagFloat64 = 1;
constexpr std::uint8_t kTagKey = 2;
constexpr std::uint8_t kTagString = 3;
constexpr std::uint8_t kTagCallee = 4;

void put_u8(std::vector<std::uint8_t>* out, std::uint8_t value) {
    out->push_back(value);
}
void put_u16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    put_u8(out, static_cast<std::uint8_t>(value & 0xFF));
    put_u8(out, static_cast<std::uint8_t>(value >> 8));
}
void put_u32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    put_u16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    put_u16(out, static_cast<std::uint16_t>(value >> 16));
}
void put_u64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    put_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFF));
    put_u32(out, static_cast<std::uint32_t>(value >> 32));
}
void put_bytes(std::vector<std::uint8_t>* out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::uint16_t read_u16_le(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}
std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(read_u16_le(data)) |
           (static_cast<std::uint32_t>(read_u16_le(data + 2)) << 16);
}

std::string slot_text(const RegisterDesc& desc) {
    if (desc.name.empty())
        return desc.slot_key;
    std::string text = desc.slot_key;
    text.push_back('\0');
    text += desc.name;
    return text;
}

std::string register_text(const RegisterDesc& desc) {
    return desc.kind == Operand::Kind::Var ? desc.name : slot_text(desc);
}

void apply_slot_text(const std::string& text, RegisterDesc* desc) {
    const std::size_t split = text.find('\0');
    if (split == std::string::npos) {
        desc->slot_key = text;
        desc->name.clear();
        return;
    }
    desc->slot_key = text.substr(0, split);
    desc->name = text.substr(split + 1);
}

void derive_fast(Instruction* ins) {
    ins->fast_i64 = false;
    ins->opfn = 0;
    if (ins->kind != rwir::Kind::Int || ins->width != 8)
        return;
    switch (ins->op) {
    case rwir::Op::Add: ins->opfn = 1; break;
    case rwir::Op::Sub: ins->opfn = 2; break;
    case rwir::Op::Mul: ins->opfn = 3; break;
    case rwir::Op::Div: ins->opfn = 4; break;
    case rwir::Op::Mod: ins->opfn = 5; break;
    case rwir::Op::Max: ins->opfn = 6; break;
    case rwir::Op::Min: ins->opfn = 7; break;
    case rwir::Op::BitAnd: ins->opfn = 8; break;
    case rwir::Op::BitOr: ins->opfn = 9; break;
    case rwir::Op::BitXor: ins->opfn = 10; break;
    case rwir::Op::Shl: ins->opfn = 11; break;
    case rwir::Op::Shr: ins->opfn = 12; break;
    case rwir::Op::Eq: ins->opfn = 13; break;
    case rwir::Op::Neq: ins->opfn = 14; break;
    case rwir::Op::Lt: ins->opfn = 15; break;
    case rwir::Op::Le: ins->opfn = 16; break;
    case rwir::Op::Gt: ins->opfn = 17; break;
    case rwir::Op::Ge: ins->opfn = 18; break;
    default: return;
    }
    ins->fast_i64 = true;
}

struct Ref {
    bool is_const = false;
    std::uint16_t index = 0;
};

struct Wire {
    std::uint16_t op = 0;
    std::uint8_t flags = 0;
    bool wide = false;
    std::vector<Ref> reads;
    std::vector<std::uint16_t> writes;
    std::vector<std::int32_t> targets;
    std::uint32_t imm = 0;  // handoff：名字串池偏移
};

}  // namespace

// ── 编码 ────────────────────────────────────────────────────────────────
bool encode_binir(const Segment& segment, std::vector<std::uint8_t>* out) {
    if (out == nullptr)
        return false;
    out->clear();
    const std::size_t ninsn = segment.insns.size();
    const std::size_t nslots = segment.regs.size();
    if (nslots > 255 || ninsn > 0xFFFFFFu)
        return false;

    std::vector<bool> written(nslots, false);
    for (const Instruction& ins : segment.insns) {
        if (ins.nwr > kMaxWrites || ins.nrd > kMaxReads)
            return false;
        for (int k = 0; k < ins.nwr; k++) {
            if (ins.wr[k] >= nslots)
                return false;
            written[ins.wr[k]] = true;
        }
        if (ins.s0 != static_cast<int>(&ins - segment.insns.data()) + 1)
            return false;
    }

    std::vector<bool> is_const(nslots, false);
    std::vector<int> reg_id(nslots, -1);
    std::vector<int> const_id(nslots, -1);
    int nreg = 0;
    for (std::size_t i = 0; i < nslots; i++) {
        const RegisterDesc& desc = segment.regs[i];
        if (desc.kind != Operand::Kind::Literal && desc.kind != Operand::Kind::Var)
            return false;
        is_const[i] = desc.kind == Operand::Kind::Literal && !written[i];
        if (!is_const[i])
            reg_id[i] = nreg++;
    }
    if (nreg > 255)
        return false;

    auto entry_of = [](const Instruction& ins) -> const rwir::Entry* {
        if (ins.rwir_id >= vmbin::kRwirIdExtraBase ||
            ins.rwir_id >= static_cast<std::uint16_t>(rwir::kMyrwircapsN))
            return nullptr;
        return &rwir::myrwircaps[ins.rwir_id];
    };

    std::vector<std::size_t> const_text_index;  // ConstId → 串条目下标（tag 0 的 key）
    std::vector<std::uint64_t> const_value;
    std::vector<bool> const_has_value;
    std::vector<std::string> const_texts;
    std::vector<std::string> callees;
    std::unordered_map<std::string, std::size_t> callee_index;

    auto note_const = [&](std::uint8_t slot) {
        if (const_id[slot] < 0) {
            const int id = static_cast<int>(const_texts.size());
            const_id[slot] = id;
            const_texts.push_back(slot_text(segment.regs[slot]));
            const FastLiteral literal =
                slot < segment.lit.size() ? segment.lit[slot] : FastLiteral{};
            const_has_value.push_back(literal.is_i64);
            const_value.push_back(static_cast<std::uint64_t>(literal.v));
        }
    };

    auto branch_disp = [&](std::size_t index, std::uint8_t slot,
                           std::int32_t* disp) -> bool {
        if (slot >= nslots)
            return false;
        if (segment.regs[slot].kind != Operand::Kind::Literal)
            return false;
        if (slot >= segment.lit.size() || !segment.lit[slot].is_i64)
            return false;
        const std::int64_t source = segment.lit[slot].v;
        std::int64_t target_index = -1;
        if (source >= 1 && source <= static_cast<std::int64_t>(ninsn))
            target_index = source - 1;
        else if (source == static_cast<std::int64_t>(ninsn) + 1)
            target_index = static_cast<std::int64_t>(ninsn);
        else
            return false;
        const std::int64_t delta = target_index - static_cast<std::int64_t>(index);
        if (delta < -2147483648LL || delta > 2147483647LL)
            return false;
        *disp = static_cast<std::int32_t>(delta);
        return true;
    };

    enum class Form { Table, Goto, Br, Call, Handoff };
    struct Planned {
        Form form = Form::Table;
        std::size_t entry = 0;
        std::size_t callee = 0;
        std::int32_t taken = 0;
        std::int32_t fallthrough = 0;
        bool two_target = false;
        std::string name;  // 扩展算子（handoff）名
    };
    std::vector<Planned> planned(ninsn);

    for (std::size_t i = 0; i < ninsn; i++) {
        const Instruction& ins = segment.insns[i];
        const rwir::Entry* entry = entry_of(ins);
        Planned& plan = planned[i];
        if (entry == nullptr) {
            if (ins.rwir_id < vmbin::kRwirIdExtraBase ||
                ins.impl != rwir::Impl::Handoff || ins.nrd != 0 || ins.nwr != 0)
                return false;
            const std::size_t extra = ins.rwir_id - vmbin::kRwirIdExtraBase;
            if (extra >= segment.extra_rwir_names.size() ||
                segment.extra_rwir_names[extra].empty())
                return false;
            plan.name = segment.extra_rwir_names[extra];
            plan.form = Form::Handoff;
            continue;
        }
        if (ins.op != entry->op || ins.kind != entry->kind ||
            ins.width != entry->width || ins.impl != entry->impl ||
            ins.handler != entry->handler)
            return false;
        const std::size_t entry_id =
            static_cast<std::size_t>(entry - rwir::myrwircaps);
        if (entry_id > kMaxRwirId)
            return false;
        plan.entry = entry_id;

        if (entry->op == rwir::Op::Call) {
            const std::string& callee = vmbin::instruction_callee(segment, ins);
            if (callee.empty() || ins.nrd < 1)
                return false;
            plan.form = Form::Call;
            const auto found = callee_index.find(callee);
            if (found != callee_index.end()) {
                plan.callee = found->second;
            } else {
                plan.callee = callees.size();
                callee_index.emplace(callee, plan.callee);
                callees.push_back(callee);
            }
            for (int k = 1; k < ins.nrd; k++)
                if (is_const[ins.rd[k]])
                    note_const(ins.rd[k]);
        } else if (entry->op == rwir::Op::Goto) {
            if (ins.nrd != 1 || ins.nwr != 0)
                return false;
            if (!branch_disp(i, ins.rd[0], &plan.taken))
                return false;
            plan.form = Form::Goto;
        } else if (entry->op == rwir::Op::Br) {
            if (ins.nrd != 3 || ins.nwr != 0)
                return false;
            if (!branch_disp(i, ins.rd[1], &plan.taken) ||
                !branch_disp(i, ins.rd[2], &plan.fallthrough))
                return false;
            plan.two_target = plan.taken != 1 && plan.fallthrough != 1;
            plan.form = Form::Br;
            if (is_const[ins.rd[0]])
                note_const(ins.rd[0]);
        } else {
            plan.form = Form::Table;
            for (int k = 0; k < ins.nrd; k++)
                if (is_const[ins.rd[k]])
                    note_const(ins.rd[k]);
        }
        for (int k = 0; k < ins.nwr; k++)
            if (is_const[ins.wr[k]])
                return false;
    }

    std::vector<std::string> strings;
    std::unordered_map<std::string, std::size_t> string_index;
    auto intern = [&](const std::string& text) -> std::size_t {
        const auto found = string_index.find(text);
        if (found != string_index.end())
            return found->second;
        const std::size_t index = strings.size();
        strings.push_back(text);
        string_index.emplace(text, index);
        return index;
    };
    const_text_index.assign(const_texts.size(), 0);
    for (std::size_t i = 0; i < const_texts.size(); i++)
        if (const_has_value[i])
            const_text_index[i] = intern(const_texts[i]);
    std::vector<std::size_t> callee_string(callees.size());
    for (std::size_t i = 0; i < callees.size(); i++)
        callee_string[i] = intern(callees[i]);
    std::vector<std::size_t> handoff_string(ninsn, 0);
    for (std::size_t i = 0; i < ninsn; i++)
        if (planned[i].form == Form::Handoff)
            handoff_string[i] = intern(planned[i].name);
    std::vector<std::size_t> reg_string(static_cast<std::size_t>(nreg));
    for (std::size_t slot = 0; slot < nslots; slot++)
        if (!is_const[slot])
            reg_string[static_cast<std::size_t>(reg_id[slot])] =
                intern(register_text(segment.regs[slot]));

    std::size_t value_bytes = 0;
    for (std::size_t i = 0; i < const_texts.size(); i++)
        value_bytes += const_has_value[i] ? (1 + 8 + 4)
                                          : (1 + 4 + const_texts[i].size());
    const std::size_t callee_bytes = callees.size() * (1 + 4);
    std::vector<std::uint32_t> string_offset(strings.size());
    std::size_t pool_cursor = value_bytes + callee_bytes;
    for (std::size_t i = 0; i < strings.size(); i++) {
        string_offset[i] = static_cast<std::uint32_t>(pool_cursor);
        pool_cursor += 1 + 4 + strings[i].size();
    }

    std::vector<std::uint8_t> code;
    for (std::size_t i = 0; i < ninsn; i++) {
        const Instruction& ins = segment.insns[i];
        const Planned& plan = planned[i];
        Wire w;
        std::uint8_t flags = ins.wr_local ? 0x1 : 0;
        if (plan.form != Form::Handoff && plan.form != Form::Table)
            flags |= 0x2;
        if (plan.form == Form::Table &&
            rwir::myrwircaps[plan.entry].impl == rwir::Impl::Control)
            flags |= 0x2;
        if (plan.form != Form::Handoff &&
            rwir::myrwircaps[plan.entry].handler != rwir::RwirHandlerId::None)
            flags |= 0x4;
        w.flags = flags;

        auto read_ref = [&](int k) -> Ref {
            const std::uint8_t slot = ins.rd[k];
            Ref ref;
            if (is_const[slot]) {
                ref.is_const = true;
                ref.index = static_cast<std::uint16_t>(const_id[slot]);
            } else {
                ref.index = static_cast<std::uint16_t>(reg_id[slot]);
            }
            return ref;
        };

        switch (plan.form) {
        case Form::Handoff:
            w.op = kOpHandoff;
            w.imm = string_offset[handoff_string[i]];
            break;
        case Form::Call:
            w.op = static_cast<std::uint16_t>(plan.entry);
            w.imm = static_cast<std::uint32_t>(plan.callee);
            for (int k = 1; k < ins.nrd; k++)
                w.reads.push_back(read_ref(k));
            for (int k = 0; k < ins.nwr; k++)
                w.writes.push_back(static_cast<std::uint16_t>(reg_id[ins.wr[k]]));
            break;
        case Form::Goto:
            w.op = static_cast<std::uint16_t>(plan.entry);
            w.targets.push_back(plan.taken);
            break;
        case Form::Br:
            w.reads.push_back(read_ref(0));
            if (plan.two_target) {
                w.op = static_cast<std::uint16_t>(plan.entry);
                w.targets.push_back(plan.taken);
                w.targets.push_back(plan.fallthrough);
            } else if (plan.fallthrough == 1) {
                // false 路径落空 → 条件真时跳到 taken
                w.op = kOpBrIfTrue;
                w.targets.push_back(plan.taken);
            } else {
                // true 路径落空 → 条件假时跳到 false 目标
                w.op = kOpBrIfFalse;
                w.targets.push_back(plan.fallthrough);
            }
            break;
        case Form::Table:
            w.op = static_cast<std::uint16_t>(plan.entry);
            w.imm = static_cast<std::uint32_t>(ins.nrd | (ins.nwr << 2));
            for (int k = 0; k < ins.nrd; k++)
                w.reads.push_back(read_ref(k));
            for (int k = 0; k < ins.nwr; k++)
                w.writes.push_back(static_cast<std::uint16_t>(reg_id[ins.wr[k]]));
            break;
        }

        bool narrow = w.reads.size() <= 2 && w.writes.size() <= 1;
        for (const Ref& ref : w.reads)
            if (ref.index > kMaxNarrowIndex)
                narrow = false;
        for (const std::uint16_t write : w.writes)
            if (write > kMaxNarrowIndex)
                narrow = false;
        if (plan.form == Form::Call &&
            (w.reads.size() != 1 || w.writes.size() != 1 || plan.callee > 0xFFFF))
            narrow = false;
        if (plan.form == Form::Handoff && w.imm > 0xFFFF)
            narrow = false;
        if (plan.form == Form::Goto || plan.form == Form::Br) {
            if (w.targets.size() != 1)
                narrow = false;
            else if (w.targets[0] < -32768 || w.targets[0] > 32767)
                narrow = false;
        }
        if (w.op == kOpBrIfTrue || w.op == kOpBrIfFalse) {
            if (w.reads.size() != 1)
                return false;
        }

        if (narrow) {
            std::uint16_t rd0 = 0, rd1 = 0, wr = 0, imm = 0;
            auto narrow_ref = [](const Ref& ref) -> std::uint16_t {
                return static_cast<std::uint16_t>(
                    ((ref.is_const ? 0x40 : 0) | (ref.index & 0x3F)));
            };
            if (plan.form == Form::Call) {
                rd0 = w.reads.empty() ? 0 : narrow_ref(w.reads[0]);
                wr = w.writes.empty() ? 0 : w.writes[0];
                imm = static_cast<std::uint16_t>(plan.callee);
            } else if (plan.form == Form::Handoff) {
                imm = static_cast<std::uint16_t>(w.imm);
            } else if (plan.form == Form::Goto || plan.form == Form::Br) {
                rd0 = w.reads.empty() ? 0 : narrow_ref(w.reads[0]);
                imm = static_cast<std::uint16_t>(
                    static_cast<std::int32_t>(w.targets[0]) & 0xFFFF);
            } else {
                rd0 = w.reads.size() > 0 ? narrow_ref(w.reads[0]) : 0;
                rd1 = w.reads.size() > 1 ? narrow_ref(w.reads[1]) : 0;
                wr = w.writes.empty() ? 0 : w.writes[0];
                imm = static_cast<std::uint16_t>(w.imm);
            }
            std::uint64_t word = w.op;
            word |= static_cast<std::uint64_t>(w.flags) << 9;
            word |= static_cast<std::uint64_t>(rd0) << 12;
            word |= static_cast<std::uint64_t>(rd1) << 19;
            word |= static_cast<std::uint64_t>(wr) << 26;
            word |= static_cast<std::uint64_t>(imm) << 32;
            for (int byte = 0; byte < 6; byte++)
                put_u8(&code, static_cast<std::uint8_t>((word >> (8 * byte)) & 0xFF));
            continue;
        }

        const std::size_t read_count =
            w.reads.size() + (plan.form == Form::Call ? 1 : 0);
        put_u16(&code, 0xFFFF);
        put_u16(&code, static_cast<std::uint16_t>(w.op | (w.flags << 9)));
        put_u8(&code, static_cast<std::uint8_t>(read_count));
        put_u8(&code, static_cast<std::uint8_t>(w.writes.size()));
        put_u8(&code, static_cast<std::uint8_t>(w.targets.size()));
        put_u8(&code, 0);
        put_u32(&code, plan.form == Form::Handoff ? w.imm : 0);
        if (plan.form == Form::Call)
            put_u16(&code, static_cast<std::uint16_t>(plan.callee));
        for (const Ref& ref : w.reads)
            put_u16(&code, static_cast<std::uint16_t>(
                                (ref.is_const ? 0x8000 : 0) | ref.index));
        for (const std::uint16_t write : w.writes)
            put_u16(&code, write);
        for (const std::int32_t target : w.targets)
            put_u32(&code, static_cast<std::uint32_t>(target));
    }

    std::vector<std::uint8_t> pool;
    for (std::size_t i = 0; i < const_texts.size(); i++) {
        if (const_has_value[i]) {
            put_u8(&pool, kTagInt64);
            put_u64(&pool, const_value[i]);
            put_u32(&pool, string_offset[const_text_index[i]]);
        } else {
            put_u8(&pool, kTagString);
            put_u32(&pool, static_cast<std::uint32_t>(const_texts[i].size()));
            put_bytes(&pool, const_texts[i].data(), const_texts[i].size());
        }
    }
    for (std::size_t i = 0; i < callees.size(); i++) {
        put_u8(&pool, kTagCallee);
        put_u32(&pool, string_offset[callee_string[i]]);
    }
    for (std::size_t i = 0; i < strings.size(); i++) {
        put_u8(&pool, kTagString);
        put_u32(&pool, static_cast<std::uint32_t>(strings[i].size()));
        put_bytes(&pool, strings[i].data(), strings[i].size());
    }
    if (pool.size() != pool_cursor)
        return false;

    std::vector<std::uint8_t> regs;
    for (std::size_t slot = 0; slot < nslots; slot++) {
        if (is_const[slot])
            continue;
        const RegisterDesc& desc = segment.regs[slot];
        const std::size_t reg = static_cast<std::size_t>(reg_id[slot]);
        put_u8(&regs, static_cast<std::uint8_t>(desc.kind));
        put_u8(&regs, 0);
        put_u32(&regs, string_offset[reg_string[reg]]);
        std::uint8_t flags = static_cast<std::uint8_t>(desc.vclass) & 0x7;
        if (desc.member)
            flags |= 0x8;
        put_u8(&regs, flags);
        put_u8(&regs, static_cast<std::uint8_t>(desc.param_k));
    }

    const std::size_t cpool_offset = kHeaderBytes + code.size();
    const std::size_t regs_offset = cpool_offset + pool.size();
    std::vector<std::uint8_t> header;
    put_u16(&header, kMagic);
    put_u8(&header, kVersion);
    put_u8(&header, 0);
    put_u32(&header, static_cast<std::uint32_t>(ninsn));
    put_u16(&header, static_cast<std::uint16_t>(nreg));
    put_u16(&header, 0);
    put_u32(&header, static_cast<std::uint32_t>(pool.size()));
    put_u32(&header, static_cast<std::uint32_t>(cpool_offset));
    put_u32(&header, static_cast<std::uint32_t>(regs_offset));
    put_u32(&header, 0);
    put_u32(&header, 0);
    if (header.size() != kHeaderBytes)
        return false;

    out->insert(out->end(), header.begin(), header.end());
    out->insert(out->end(), code.begin(), code.end());
    out->insert(out->end(), pool.begin(), pool.end());
    out->insert(out->end(), regs.begin(), regs.end());
    out->insert(out->end(), kTailBytes, 0);
    return true;
}

// ── 解码 ────────────────────────────────────────────────────────────────
bool decode_binir(const std::uint8_t* data, std::size_t size, Segment* out) {
    if (data == nullptr || out == nullptr || size < kHeaderBytes + kTailBytes)
        return false;
    if (read_u16_le(data) != kMagic || data[2] != kVersion || data[3] != 0)
        return false;
    const std::uint32_t ninsn = read_u32_le(data + 4);
    const std::uint16_t nreg = read_u16_le(data + 8);
    if (read_u16_le(data + 10) != 0)
        return false;
    const std::uint32_t pool_bytes = read_u32_le(data + 12);
    const std::uint32_t cpool_offset = read_u32_le(data + 16);
    const std::uint32_t regs_offset = read_u32_le(data + 20);
    if (read_u32_le(data + 24) != 0 || read_u32_le(data + 28) != 0)
        return false;
    if (cpool_offset < kHeaderBytes || regs_offset < cpool_offset)
        return false;
    if (static_cast<std::size_t>(cpool_offset) + pool_bytes != regs_offset)
        return false;
    if (static_cast<std::size_t>(regs_offset) +
            static_cast<std::size_t>(nreg) * kRegDescBytes + kTailBytes != size)
        return false;
    if (ninsn > 0xFFFFFFu || nreg > 255)
        return false;
    for (std::size_t i = 0; i < kTailBytes; i++)
        if (data[size - kTailBytes + i] != 0)
            return false;

    struct PoolEntry {
        std::uint8_t tag = 0;
        std::uint64_t value = 0;
        std::uint32_t key_offset = kNoString;
        std::uint32_t text_offset = kNoString;  // tag 3 条目自身的池内偏移
    };
    std::vector<PoolEntry> entries;
    std::unordered_map<std::uint32_t, std::string> strings;
    {
        std::size_t offset = cpool_offset;
        while (offset < regs_offset) {
            const std::uint32_t entry_offset =
                static_cast<std::uint32_t>(offset - cpool_offset);
            const std::uint8_t tag = data[offset++];
            PoolEntry entry;
            entry.tag = tag;
            if (tag == kTagInt64 || tag == kTagFloat64) {
                if (offset + 12 > regs_offset)
                    return false;
                entry.value = static_cast<std::uint64_t>(read_u32_le(data + offset)) |
                              (static_cast<std::uint64_t>(
                                   read_u32_le(data + offset + 4))
                               << 32);
                entry.key_offset = read_u32_le(data + offset + 8);
                offset += 12;
            } else if (tag == kTagKey) {
                if (offset + 4 > regs_offset)
                    return false;
                offset += 4;
                return false;  // 当前编码不产出 tag 2
            } else if (tag == kTagCallee) {
                if (offset + 4 > regs_offset)
                    return false;
                entry.key_offset = read_u32_le(data + offset);
                offset += 4;
            } else if (tag == kTagString) {
                if (offset + 4 > regs_offset)
                    return false;
                const std::uint32_t length = read_u32_le(data + offset);
                offset += 4;
                if (offset + length > regs_offset)
                    return false;
                const std::string text(
                    reinterpret_cast<const char*>(data + offset), length);
                offset += length;
                entry.text_offset = entry_offset;
                if (!strings.emplace(entry_offset, text).second)
                    return false;
            } else {
                return false;
            }
            entries.push_back(entry);
        }
        if (offset != regs_offset)
            return false;
    }

    Segment decoded;
    decoded.regs.clear();
    for (std::uint16_t r = 0; r < nreg; r++) {
        const std::uint8_t* base =
            data + regs_offset + static_cast<std::size_t>(r) * kRegDescBytes;
        const std::uint8_t kind = base[0];
        const std::uint32_t addr = read_u32_le(base + 2);
        const std::uint8_t flags = base[6];
        const std::int8_t param_k = static_cast<std::int8_t>(base[7]);
        if (kind != static_cast<std::uint8_t>(Operand::Kind::Literal) &&
            kind != static_cast<std::uint8_t>(Operand::Kind::Var))
            return false;
        const std::uint8_t vclass = flags & 0x7;
        if (vclass > static_cast<std::uint8_t>(RegisterDesc::VarClass::Typed))
            return false;
        RegisterDesc desc;
        desc.kind = static_cast<Operand::Kind>(kind);
        desc.vclass = static_cast<RegisterDesc::VarClass>(vclass);
        desc.member = (flags & 0x8) != 0;
        desc.param_k = param_k;
        if (addr != kNoString) {
            const auto found = strings.find(addr);
            if (found == strings.end())
                return false;
            if (desc.kind == Operand::Kind::Literal)
                apply_slot_text(found->second, &desc);
            else
                desc.name = found->second;
        }
        decoded.regs.push_back(std::move(desc));
    }

    // 指令流 → wire（此阶段不落地槽号）。
    std::size_t offset = kHeaderBytes;
    std::vector<Wire> wires;
    wires.reserve(ninsn);
    bool have_wide = false;
    for (std::uint32_t index = 0; index < ninsn; index++) {
        if (offset + 6 > cpool_offset)
            return false;
        const std::uint16_t first = read_u16_le(data + offset);
        Wire wire;
        bool wide = false;
        if (first == 0xFFFF) {
            if (offset + 12 > cpool_offset)
                return false;
            wide = true;
            have_wide = true;
            const std::uint16_t op_flags = read_u16_le(data + offset + 2);
            const std::uint8_t nrd = data[offset + 4];
            const std::uint8_t nwr = data[offset + 5];
            const std::uint8_t ntgt = data[offset + 6];
            if (data[offset + 7] != 0 || nrd > kMaxReads || nwr > kMaxWrites ||
                ntgt > 2)
                return false;
            wire.op = static_cast<std::uint16_t>(op_flags & 0x1FF);
            wire.flags = static_cast<std::uint8_t>(op_flags >> 9);
            wire.imm = read_u32_le(data + offset + 8);
            std::size_t cursor = offset + 12;
            if (cursor + 2 * static_cast<std::size_t>(nrd) +
                    2 * static_cast<std::size_t>(nwr) +
                    4 * static_cast<std::size_t>(ntgt) >
                cpool_offset)
                return false;
            for (std::uint8_t k = 0; k < nrd; k++) {
                const std::uint16_t bits = read_u16_le(data + cursor);
                cursor += 2;
                wire.reads.push_back(
                    Ref{(bits & 0x8000) != 0,
                        static_cast<std::uint16_t>(bits & 0x7FFF)});
            }
            for (std::uint8_t k = 0; k < nwr; k++) {
                wire.writes.push_back(read_u16_le(data + cursor));
                cursor += 2;
            }
            for (std::uint8_t k = 0; k < ntgt; k++) {
                wire.targets.push_back(
                    static_cast<std::int32_t>(read_u32_le(data + cursor)));
                cursor += 4;
            }
            offset = cursor;
        } else {
            std::uint64_t word = first;
            word |= static_cast<std::uint64_t>(read_u32_le(data + offset + 2)) << 16;
            offset += 6;
            wire.op = static_cast<std::uint16_t>(word & 0x1FF);
            wire.flags = static_cast<std::uint8_t>((word >> 9) & 0x7);
            const std::uint16_t rd0 =
                static_cast<std::uint16_t>((word >> 12) & 0x7F);
            const std::uint16_t rd1 =
                static_cast<std::uint16_t>((word >> 19) & 0x7F);
            const std::uint16_t wr0 =
                static_cast<std::uint16_t>((word >> 26) & 0x3F);
            const std::uint16_t imm = static_cast<std::uint16_t>(word >> 32);
            wire.imm = imm;
            auto ref = [](std::uint16_t bits) {
                return Ref{(bits & 0x40) != 0,
                           static_cast<std::uint16_t>(bits & 0x3F)};
            };
            const rwir::Entry* entry =
                wire.op < static_cast<std::uint16_t>(rwir::kMyrwircapsN)
                    ? &rwir::myrwircaps[wire.op]
                    : nullptr;
            const bool call = entry != nullptr && entry->op == rwir::Op::Call;
            const bool branch =
                wire.op == kOpBrIfFalse || wire.op == kOpBrIfTrue;
            if (branch) {
                wire.reads.push_back(ref(rd0));
                wire.targets.push_back(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(imm)));
            } else if (wire.op == kOpHandoff) {
                // imm = 名字串池偏移；无操作数。
            } else if (entry != nullptr && entry->op == rwir::Op::Goto) {
                wire.targets.push_back(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(imm)));
            } else if (call) {
                wire.reads.push_back(ref(rd0));
                wire.writes.push_back(wr0);
                wire.imm = imm;
            } else {
                const std::uint8_t nrd = static_cast<std::uint8_t>(imm & 0x3);
                const std::uint8_t nwr =
                    static_cast<std::uint8_t>((imm >> 2) & 0x1);
                if ((imm & 0xFFF8u) != 0)
                    return false;
                if (nrd >= 1)
                    wire.reads.push_back(ref(rd0));
                if (nrd >= 2)
                    wire.reads.push_back(ref(rd1));
                if (nwr == 1)
                    wire.writes.push_back(wr0);
            }
        }
        wire.wide = wide;
        wires.push_back(std::move(wire));
    }
    if (offset != cpool_offset)
        return false;
    (void)have_wide;

    // 值条目区 = 池开头 max(ConstId)+1 个条目（无指令引用则 0 个）。
    std::size_t value_count = 0;
    for (const Wire& wire : wires) {
        const rwir::Entry* entry =
            wire.op < static_cast<std::uint16_t>(rwir::kMyrwircapsN)
                ? &rwir::myrwircaps[wire.op]
                : nullptr;
        if (entry != nullptr && entry->op == rwir::Op::Call) {
            for (std::size_t k = wire.wide ? 1 : 0; k < wire.reads.size(); k++)
                if (wire.reads[k].is_const)
                    value_count = std::max<std::size_t>(
                        value_count, static_cast<std::size_t>(wire.reads[k].index) + 1);
        } else if (wire.op == kOpHandoff) {
            continue;
        } else {
            for (const Ref& ref : wire.reads)
                if (ref.is_const)
                    value_count = std::max<std::size_t>(
                        value_count, static_cast<std::size_t>(ref.index) + 1);
        }
    }
    if (value_count > entries.size())
        return false;

    struct ConstSlot {
        std::string text;
        bool has_value = false;
        std::uint64_t value = 0;
    };
    std::vector<ConstSlot> constants;
    std::vector<std::uint32_t> callee_offsets;
    for (std::size_t i = 0; i < entries.size(); i++) {
        const PoolEntry& entry = entries[i];
        if (i < value_count) {
            if (entry.tag == kTagInt64) {
                ConstSlot slot;
                slot.has_value = true;
                slot.value = entry.value;
                if (entry.key_offset != kNoString) {
                    const auto found = strings.find(entry.key_offset);
                    if (found == strings.end())
                        return false;
                    slot.text = found->second;
                }
                constants.push_back(std::move(slot));
            } else if (entry.tag == kTagString) {
                ConstSlot slot;
                const auto found = strings.find(entry.text_offset);
                if (found == strings.end())
                    return false;
                slot.text = found->second;
                constants.push_back(std::move(slot));
            } else {
                return false;
            }
            continue;
        }
        if (entry.tag == kTagCallee) {
            if (entry.key_offset == kNoString)
                return false;
            const auto found = strings.find(entry.key_offset);
            if (found == strings.end())
                return false;
            callee_offsets.push_back(entry.key_offset);
        } else if (entry.tag == kTagString) {
            // 串区条目：RegDesc/callee/handoff 名，已进 strings。
        } else {
            return false;
        }
    }
    if (constants.size() != value_count)
        return false;

    for (const ConstSlot& constant : constants) {
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        apply_slot_text(constant.text, &desc);
        decoded.regs.push_back(std::move(desc));
    }
    if (decoded.regs.size() > 255)
        return false;
    decoded.lit.assign(decoded.regs.size(), FastLiteral{});
    for (std::size_t i = 0; i < constants.size(); i++) {
        if (!constants[i].has_value)
            continue;
        const std::size_t slot = static_cast<std::size_t>(nreg) + i;
        decoded.lit[slot].is_i64 = true;
        decoded.lit[slot].v = static_cast<std::int64_t>(constants[i].value);
    }

    auto extra_id = [&](const std::string& name) -> std::uint16_t {
        for (std::size_t i = 0; i < decoded.extra_rwir_names.size(); i++)
            if (decoded.extra_rwir_names[i] == name)
                return static_cast<std::uint16_t>(vmbin::kRwirIdExtraBase + i);
        decoded.extra_rwir_names.push_back(name);
        return static_cast<std::uint16_t>(vmbin::kRwirIdExtraBase +
                                          decoded.extra_rwir_names.size() - 1);
    };
    auto callee_id = [&](const std::string& path) -> std::uint16_t {
        for (std::size_t i = 0; i < decoded.callees.size(); i++)
            if (decoded.callees[i] == path)
                return static_cast<std::uint16_t>(i);
        decoded.callees.push_back(path);
        return static_cast<std::uint16_t>(decoded.callees.size() - 1);
    };
    auto slot_of = [&](const Ref& ref) -> int {
        if (ref.is_const)
            return ref.index < constants.size()
                       ? static_cast<int>(static_cast<std::size_t>(nreg) +
                                          ref.index)
                       : -1;
        return ref.index < nreg ? static_cast<int>(ref.index) : -1;
    };
    auto target_slot = [&](std::int64_t source_id) -> int {
        if (decoded.regs.size() >= 255)
            return -1;
        RegisterDesc desc;
        desc.kind = Operand::Kind::Literal;
        decoded.regs.push_back(desc);
        FastLiteral literal;
        literal.is_i64 = true;
        literal.v = source_id;
        decoded.lit.push_back(literal);
        return static_cast<int>(decoded.regs.size() - 1);
    };

    decoded.insns.clear();
    decoded.insns.reserve(wires.size());
    for (std::size_t index = 0; index < wires.size(); index++) {
        const Wire& wire = wires[index];
        Instruction ins;
        ins.s0 = static_cast<int>(index) + 1;
        ins.wr_local = (wire.flags & 0x1) != 0;
        const rwir::Entry* entry =
            wire.op < static_cast<std::uint16_t>(rwir::kMyrwircapsN)
                ? &rwir::myrwircaps[wire.op]
                : nullptr;
        const bool branch = wire.op == kOpBrIfFalse || wire.op == kOpBrIfTrue;
        const bool call = entry != nullptr && entry->op == rwir::Op::Call;

        if (wire.op == kOpHandoff) {
            if (wire.flags != 0 || !wire.reads.empty() || !wire.writes.empty() ||
                !wire.targets.empty())
                return false;
            const auto found = strings.find(wire.imm);
            if (found == strings.end())
                return false;
            ins.impl = rwir::Impl::Handoff;
            ins.rwir_id = extra_id(found->second);
        } else if (branch) {
            if (entry != nullptr || wire.reads.size() != 1 ||
                !wire.writes.empty() || wire.targets.size() != 1)
                return false;
            if ((wire.flags & 0x2) == 0)
                return false;
            const rwir::Entry* br_entry = rwir::find_myrwircap("br", 2);
            if (br_entry == nullptr)
                return false;
            ins.op = br_entry->op;
            ins.kind = br_entry->kind;
            ins.width = br_entry->width;
            ins.impl = br_entry->impl;
            ins.handler = br_entry->handler;
            ins.rwir_id = static_cast<std::uint16_t>(br_entry - rwir::myrwircaps);
        } else if (entry != nullptr) {
            ins.op = entry->op;
            ins.kind = entry->kind;
            ins.width = entry->width;
            ins.impl = entry->impl;
            ins.handler = entry->handler;
            ins.rwir_id = wire.op;
            const std::uint8_t derived = static_cast<std::uint8_t>(
                (entry->impl == rwir::Impl::Control ? 0x2 : 0) |
                (entry->handler != rwir::RwirHandlerId::None ? 0x4 : 0));
            if ((wire.flags & 0x6) != derived)
                return false;
        } else {
            return false;  // 保留 op 值
        }

        const bool two_target_br = entry != nullptr && entry->op == rwir::Op::Br &&
                                   wire.targets.size() == 2;
        if (branch || two_target_br) {
            if (wire.reads.size() != 1 || !wire.writes.empty())
                return false;
            if (two_target_br) {
                const std::int64_t taken_source =
                    static_cast<std::int64_t>(index) + wire.targets[0] + 1;
                const std::int64_t false_source =
                    static_cast<std::int64_t>(index) + wire.targets[1] + 1;
                const std::int64_t limit = static_cast<std::int64_t>(ninsn) + 1;
                if (taken_source < 1 || false_source < 1 ||
                    taken_source > limit || false_source > limit)
                    return false;
                const int cond = slot_of(wire.reads[0]);
                const int taken_slot = target_slot(taken_source);
                const int false_slot = target_slot(false_source);
                if (cond < 0 || taken_slot < 0 || false_slot < 0)
                    return false;
                ins.nrd = 3;
                ins.rd[0] = static_cast<std::uint8_t>(cond);
                ins.rd[1] = static_cast<std::uint8_t>(taken_slot);
                ins.rd[2] = static_cast<std::uint8_t>(false_slot);
            } else {
            const std::int64_t next_source = static_cast<std::int64_t>(index) + 2;
            const std::int64_t target_source =
                static_cast<std::int64_t>(index) + wire.targets[0] + 1;
            const bool jump_when_true = wire.op == kOpBrIfTrue;
            const std::int64_t taken = jump_when_true ? target_source : next_source;
            const std::int64_t fallthrough =
                jump_when_true ? next_source : target_source;
            const std::int64_t limit = static_cast<std::int64_t>(ninsn) + 1;
            if (taken < 1 || fallthrough < 1 || taken > limit ||
                fallthrough > limit)
                return false;
            const int cond = slot_of(wire.reads[0]);
            const int taken_slot = target_slot(taken);
            const int fall_slot = target_slot(fallthrough);
            if (cond < 0 || taken_slot < 0 || fall_slot < 0)
                return false;
            ins.nrd = 3;
            ins.rd[0] = static_cast<std::uint8_t>(cond);
            ins.rd[1] = static_cast<std::uint8_t>(taken_slot);
            ins.rd[2] = static_cast<std::uint8_t>(fall_slot);
            }
        } else if (entry != nullptr && entry->op == rwir::Op::Goto) {
            if (!wire.reads.empty() || !wire.writes.empty() ||
                wire.targets.size() != 1)
                return false;
            const std::int64_t source =
                static_cast<std::int64_t>(index) + wire.targets[0] + 1;
            if (source < 1 || source > static_cast<std::int64_t>(ninsn) + 1)
                return false;
            const int slot = target_slot(source);
            if (slot < 0)
                return false;
            ins.nrd = 1;
            ins.rd[0] = static_cast<std::uint8_t>(slot);
        } else if (call) {
            std::vector<Ref> args;
            std::uint16_t callee_ordinal = 0;
            if (wire.wide) {
                if (wire.reads.empty())
                    return false;
                callee_ordinal = wire.reads[0].index;
                args.assign(wire.reads.begin() + 1, wire.reads.end());
            } else {
                if (wire.reads.size() != 1 || wire.writes.size() != 1)
                    return false;
                callee_ordinal = static_cast<std::uint16_t>(wire.imm);
                args.push_back(wire.reads[0]);
            }
            if (callee_ordinal >= callee_offsets.size())
                return false;
            const auto found = strings.find(callee_offsets[callee_ordinal]);
            if (found == strings.end())
                return false;
            ins.callee_id = callee_id(found->second);
            if (args.size() + 1 > static_cast<std::size_t>(kMaxReads))
                return false;
            ins.nrd = static_cast<std::uint8_t>(args.size() + 1);
            ins.rd[0] = 0;
            for (std::size_t k = 0; k < args.size(); k++) {
                const int slot = slot_of(args[k]);
                if (slot < 0)
                    return false;
                ins.rd[k + 1] = static_cast<std::uint8_t>(slot);
            }
            ins.nwr = static_cast<std::uint8_t>(wire.writes.size());
            for (std::size_t k = 0; k < wire.writes.size(); k++) {
                if (wire.writes[k] >= nreg)
                    return false;
                ins.wr[k] = static_cast<std::uint8_t>(wire.writes[k]);
            }
        } else {
            if (!wire.targets.empty())
                return false;
            ins.nrd = static_cast<std::uint8_t>(wire.reads.size());
            ins.nwr = static_cast<std::uint8_t>(wire.writes.size());
            for (std::size_t k = 0; k < wire.reads.size(); k++) {
                const int slot = slot_of(wire.reads[k]);
                if (slot < 0)
                    return false;
                ins.rd[k] = static_cast<std::uint8_t>(slot);
            }
            for (std::size_t k = 0; k < wire.writes.size(); k++) {
                if (wire.writes[k] >= nreg)
                    return false;
                ins.wr[k] = static_cast<std::uint8_t>(wire.writes[k]);
            }
        }
        derive_fast(&ins);
        decoded.insns.push_back(std::move(ins));
    }

    bool has_handler = false;
    for (const Instruction& ins : decoded.insns)
        has_handler = has_handler || ins.handler != rwir::RwirHandlerId::None ||
                      ins.impl == rwir::Impl::Handoff;
    decoded.has_rwirhandler = has_handler;
    *out = std::move(decoded);
    return true;
}

}  // namespace kvlang::runtimec2
