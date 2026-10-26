#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ops.hpp"

namespace kv::rt {

// 操作数：字面量（值在函数槽）或变量（帧栈成员 <frame>/<name>）。
struct Operand {
    enum class Kind : std::uint8_t { None = 0, Literal, Var } kind = Kind::None;
    std::string name;  // Var 时的变量名
};

struct CInsn {
    ops::Op op = ops::Op::None;
    ops::Kind kind = ops::Kind::None;
    std::uint8_t width = 0;
    std::uint8_t nrd = 0, nwr = 0;
    Operand rd[4];
    Operand wr[2];
    int s0 = 0;  // 明文指令坐标
};

struct Segment {
    std::vector<CInsn> insns;
};

// 从函数目录（/lib/<name>/）构建指令段；遇到非本 runtime 的 opcode 即停
// （返回 false 且 insns 保留已建部分，调用方据此改写为 ext）。
bool build_segment(void* h, const std::string& func_dir, Segment* out,
                   std::string* stop_op);

}  // namespace kv::rt
