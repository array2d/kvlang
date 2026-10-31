#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kvspace/kvspace.h>

#include "codeseg.hpp"

namespace kvlang::runtimec2 {
void jit_clear_cache() {}
}

namespace {

using kvlang::runtimec2::CInsn;
using kvlang::runtimec2::Operand;
using kvlang::runtimec2::Segment;
using kvlang::runtimec2::build_segment;
using kvlang::runtimec2::rwir::Op;

constexpr std::string_view kProvenance =
    "Exported by runtimec2 segment_shape_dump from build_segment() over "
    "layout-backed kvspace Segment::insns/regs/lit. Functions and the "
    "corpus-wide callee pool are lexicographically sorted; call immediate is "
    "the callee-pool index. source_id is CInsn::s0. Segment exposes no effect, "
    "safepoint, or side-exit metadata; effect/safepoint are false and "
    "side_exits is empty.";

class KvHandle {
  public:
    explicit KvHandle(const char* dsn) : handle_(kvspaceConnect(dsn)) {}
    ~KvHandle() {
        if (handle_)
            kvspaceClose(handle_);
    }
    KvHandle(const KvHandle&) = delete;
    KvHandle& operator=(const KvHandle&) = delete;
    void* get() const { return handle_; }

  private:
    void* handle_ = nullptr;
};

struct SlotId {
    bool is_const = false;
    std::uint32_t index = 0;
};

struct ReadOperand {
    bool is_const = false;
    std::uint32_t index = 0;
};

struct ExportInsn {
    std::string op;
    std::string family;
    std::vector<ReadOperand> reads;
    std::vector<std::uint32_t> writes;
    std::vector<std::int64_t> targets;
    bool has_immediate = false;
    std::uint32_t immediate = 0;
    std::uint32_t source_id = 0;
};

struct ExportFunction {
    std::string name;
    std::uint32_t register_count = 0;
    std::uint32_t constant_count = 0;
    std::vector<ExportInsn> instructions;
};

void write_json_string(std::ostream& out, std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.put('"');
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u00" << kHex[ch >> 4] << kHex[ch & 0x0f];
            } else {
                out.put(static_cast<char>(ch));
            }
        }
    }
    out.put('"');
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool normalize_function(std::string value, std::string* out) {
    while (value.size() > 5 && value.back() == '/')
        value.pop_back();
    if (!starts_with(value, "/lib/") || value.size() == 5)
        return false;
    *out = std::move(value);
    return true;
}

std::string family_of(const Segment& seg, const CInsn& ins) {
    if (ins.op == Op::Call)
        return "call";
    if (ins.op == Op::Br || ins.op == Op::Goto)
        return "branch";
    static constexpr std::string_view kArrayPrefixes[] = {
        "array\xC2\xB7", "compactarray\xC2\xB7", "ndarray\xC2\xB7",
        "scatterarray\xC2\xB7",
    };
    for (std::string_view prefix : kArrayPrefixes)
        if (starts_with(instruction_name(seg, ins), prefix))
            return "array";
    return "generic";
}

bool checked_slot(const Segment& segment, std::uint8_t slot, std::size_t instruction,
                  std::string_view role, std::string* error) {
    if (slot < segment.regs.size())
        return true;
    *error = "instruction " + std::to_string(instruction) + " has out-of-range " +
             std::string(role) + " slot " + std::to_string(slot);
    return false;
}

bool target_displacement(const Segment& segment, const std::map<int, std::size_t>& pc_index,
                         int virtual_exit_source, std::size_t virtual_exit_index,
                         std::size_t instruction_index, std::uint8_t slot,
                         std::int64_t* out, std::string* error) {
    if (!checked_slot(segment, slot, instruction_index, "branch target", error))
        return false;
    if (segment.regs[slot].kind != Operand::Kind::Literal ||
        slot >= segment.lit.size() || !segment.lit[slot].is_i64) {
        *error = "instruction " + std::to_string(instruction_index) +
                 " has a branch target not represented by Segment::lit int64";
        return false;
    }
    const std::int64_t target_source = segment.lit[slot].v;
    if (target_source < std::numeric_limits<int>::min() ||
        target_source > std::numeric_limits<int>::max()) {
        *error = "instruction " + std::to_string(instruction_index) +
                 " has out-of-range branch source id " + std::to_string(target_source);
        return false;
    }
    const auto target = pc_index.find(static_cast<int>(target_source));
    std::size_t target_index = 0;
    if (target != pc_index.end()) {
        target_index = target->second;
    } else if (target_source == virtual_exit_source) {
        target_index = virtual_exit_index;
    } else {
        *error = "instruction " + std::to_string(instruction_index) +
                 " targets missing source id " + std::to_string(target_source);
        return false;
    }
    *out = static_cast<std::int64_t>(target_index) -
           static_cast<std::int64_t>(instruction_index);
    return true;
}

bool export_function(const std::string& name, const Segment& segment,
                     const std::map<std::string, std::uint32_t>& callee_pool,
                     ExportFunction* out, std::string* error) {
    if (segment.insns.empty()) {
        *error = name + ": build_segment produced no instructions";
        return false;
    }

    std::vector<bool> written(segment.regs.size(), false);
    for (std::size_t instruction_index = 0; instruction_index < segment.insns.size();
         ++instruction_index) {
        const CInsn& ins = segment.insns[instruction_index];
        for (std::uint8_t index = 0; index < ins.nwr; ++index) {
            if (!checked_slot(segment, ins.wr[index], instruction_index, "write", error))
                return false;
            written[ins.wr[index]] = true;
        }
    }

    std::vector<SlotId> slots(segment.regs.size());
    for (std::size_t index = 0; index < segment.regs.size(); ++index) {
        const bool is_const = segment.regs[index].kind == Operand::Kind::Literal &&
                              !written[index];
        slots[index].is_const = is_const;
        if (is_const)
            slots[index].index = out->constant_count++;
        else
            slots[index].index = out->register_count++;
    }

    if (segment.insns.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        *error = name + ": instruction count cannot represent a virtual exit";
        return false;
    }
    const int virtual_exit_source = static_cast<int>(segment.insns.size()) + 1;
    const std::size_t virtual_exit_index = segment.insns.size();
    std::map<int, std::size_t> pc_index;
    // pc = 指令下标 + 1（Instruction 不再存 s0）。
    for (std::size_t index = 0; index < segment.insns.size(); ++index)
        pc_index.emplace(static_cast<int>(index) + 1, index);

    out->name = name;
    out->instructions.reserve(segment.insns.size());
    for (std::size_t instruction_index = 0; instruction_index < segment.insns.size();
         ++instruction_index) {
        const CInsn& ins = segment.insns[instruction_index];
        ExportInsn item;
        item.op = instruction_name(segment, ins);
        item.family = family_of(segment, ins);
        item.source_id = static_cast<std::uint32_t>(instruction_index) + 1;

        std::uint8_t read_begin = 0;
        std::uint8_t read_end = ins.nrd;
        if (ins.op == Op::Call) {
            const std::string& callee_path = instruction_callee(segment, ins);
            if (callee_path.empty()) {
                *error = name + ": call at source id " +
                         std::to_string(instruction_index + 1) +
                         " has no resolved callee";
                return false;
            }
            const auto callee = callee_pool.find(callee_path);
            if (callee == callee_pool.end()) {
                *error = name + ": internal callee-pool mismatch for " + callee_path;
                return false;
            }
            item.has_immediate = true;
            item.immediate = callee->second;
            read_begin = 1;
        } else if (ins.op == Op::Goto || ins.op == Op::Br) {
            const bool conditional = ins.op == Op::Br;
            const std::uint8_t expected = conditional ? 3 : 1;
            if (ins.nrd != expected) {
                *error = name + ": branch at source id " +
                         std::to_string(instruction_index + 1) +
                         " has unexpected read arity " + std::to_string(ins.nrd);
                return false;
            }
            if (conditional)
                read_end = 1;
            else
                read_end = 0;
            const std::uint8_t target_begin = conditional ? 1 : 0;
            for (std::uint8_t index = target_begin; index < ins.nrd; ++index) {
                std::int64_t displacement = 0;
                if (!target_displacement(segment, pc_index, virtual_exit_source,
                                         virtual_exit_index, instruction_index,
                                         ins.rd[index], &displacement, error))
                    return false;
                item.targets.push_back(displacement);
            }
        }

        for (std::uint8_t index = read_begin; index < read_end; ++index) {
            const std::uint8_t slot = ins.rd[index];
            if (!checked_slot(segment, slot, instruction_index, "read", error))
                return false;
            item.reads.push_back({slots[slot].is_const, slots[slot].index});
        }
        for (std::uint8_t index = 0; index < ins.nwr; ++index) {
            const std::uint8_t slot = ins.wr[index];
            if (!checked_slot(segment, slot, instruction_index, "write", error))
                return false;
            if (slots[slot].is_const) {
                *error = name + ": write at source id " +
                         std::to_string(instruction_index + 1) +
                         " was classified as ConstId";
                return false;
            }
            item.writes.push_back(slots[slot].index);
        }
        out->instructions.push_back(std::move(item));
    }
    return true;
}

template <typename T>
void write_number_array(std::ostream& out, const std::vector<T>& values) {
    out.put('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index)
            out.put(',');
        out << values[index];
    }
    out.put(']');
}

void write_instruction(std::ostream& out, const ExportInsn& ins) {
    out << "{\"effect\":false,\"family\":";
    write_json_string(out, ins.family);
    if (ins.has_immediate)
        out << ",\"immediate\":" << ins.immediate;
    out << ",\"op\":";
    write_json_string(out, ins.op);
    out << ",\"reads\":[";
    for (std::size_t index = 0; index < ins.reads.size(); ++index) {
        if (index)
            out.put(',');
        out << "{\"index\":" << ins.reads[index].index << ",\"kind\":\""
            << (ins.reads[index].is_const ? "const" : "reg") << "\"}";
    }
    out << "],\"safepoint\":false,\"side_exits\":[],\"source_id\":"
        << ins.source_id << ",\"targets\":";
    write_number_array(out, ins.targets);
    out << ",\"writes\":";
    write_number_array(out, ins.writes);
    out.put('}');
}

void write_document(std::ostream& out, const std::vector<ExportFunction>& functions) {
    out << "{\"corpus\":\"runtimec2-real-segments\",\"fixture_only\":false,"
           "\"functions\":[";
    for (std::size_t function_index = 0; function_index < functions.size();
         ++function_index) {
        if (function_index)
            out.put(',');
        const ExportFunction& function = functions[function_index];
        out << "{\"constant_count\":" << function.constant_count
            << ",\"instructions\":[";
        for (std::size_t instruction_index = 0;
             instruction_index < function.instructions.size(); ++instruction_index) {
            if (instruction_index)
                out.put(',');
            write_instruction(out, function.instructions[instruction_index]);
        }
        out << "],\"name\":";
        write_json_string(out, function.name);
        out << ",\"register_count\":" << function.register_count << '}';
    }
    out << "],\"provenance\":";
    write_json_string(out, kProvenance);
    out << ",\"schema\":\"kvlang.binir-probe.v1\"}\n";
}

int fail(std::string_view message) {
    std::cerr << "segment_shape_dump: " << message << '\n';
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <dsn> </lib/function>...\n";
        return 2;
    }

    std::vector<std::string> function_names;
    function_names.reserve(static_cast<std::size_t>(argc - 2));
    for (int index = 2; index < argc; ++index) {
        std::string normalized;
        if (!normalize_function(argv[index], &normalized))
            return fail(std::string("function must be under /lib/: ") + argv[index]);
        function_names.push_back(std::move(normalized));
    }
    std::sort(function_names.begin(), function_names.end());
    if (std::adjacent_find(function_names.begin(), function_names.end()) !=
        function_names.end())
        return fail("duplicate function directory");

    KvHandle handle(argv[1]);
    if (!handle.get())
        return fail(std::string("cannot connect to DSN: ") + argv[1]);

    std::vector<std::pair<std::string, Segment>> segments;
    segments.reserve(function_names.size());
    for (const std::string& name : function_names) {
        Segment segment;
        if (!build_segment(handle.get(), name, &segment))
            return fail(name + ": build_segment failed");
        segments.emplace_back(name, std::move(segment));
    }

    std::vector<std::string> callees;
    for (const auto& [name, segment] : segments) {
        (void)name;
        for (const CInsn& ins : segment.insns) {
            if (ins.op != Op::Call)
                continue;
            const std::string& callee_path = instruction_callee(segment, ins);
            if (!callee_path.empty())
                callees.push_back(callee_path);
        }
    }
    std::sort(callees.begin(), callees.end());
    callees.erase(std::unique(callees.begin(), callees.end()), callees.end());
    std::map<std::string, std::uint32_t> callee_pool;
    for (std::size_t index = 0; index < callees.size(); ++index)
        callee_pool.emplace(callees[index], static_cast<std::uint32_t>(index));

    std::vector<ExportFunction> functions;
    functions.reserve(segments.size());
    std::size_t instruction_count = 0;
    for (const auto& [name, segment] : segments) {
        ExportFunction function;
        std::string error;
        if (!export_function(name, segment, callee_pool, &function, &error))
            return fail(error);
        instruction_count += function.instructions.size();
        functions.push_back(std::move(function));
    }

    std::cerr << "segment_shape_dump: Segment has no effect/safepoint/side-exit "
                 "metadata; exported false/false/empty\n";
    for (const auto& [callee, index] : callee_pool)
        std::cerr << "segment_shape_dump: callee_pool[" << index << "]=" << callee << '\n';
    std::cerr << "segment_shape_dump: exported " << functions.size() << " functions and "
              << instruction_count << " instructions\n";
    write_document(std::cout, functions);
    return std::cout.good() ? 0 : fail("failed to write stdout");
}
