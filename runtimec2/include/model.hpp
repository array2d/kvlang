#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace kv::rt {

// PC 路径：/vthread/<vid>/[<depth>]/[<s0>,<s1>]
struct Pc {
    std::string vid;
    int depth = 0;
    int s0 = 0;
    int s1 = 0;
};

bool parse_pc(std::string_view pc, Pc* out) noexcept;
std::string vthread_root(std::string_view vid);
std::string frame_root(std::string_view vid, int depth);
std::string entry_pc(std::string_view vid, int depth);
std::string next_pc(const Pc& pc);
std::string instr_key(const Pc& pc);  // <frame_root>/[<s0>,<s1>]
std::string param_key(std::string_view frame_root, int s0, int s1);

// fold：`·` 与 `/` 都折叠成 `/`（ISA 节点路径规则）。
std::string fold_op(std::string_view op);

}  // namespace kv::rt
