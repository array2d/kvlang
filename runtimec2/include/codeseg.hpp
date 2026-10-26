#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ops.hpp"

namespace kv::rt {

// 操作数：字面量（值在函数槽）或变量（帧栈成员 <frame>/<name>）。
struct Operand {
    enum class Kind : std::uint8_t { None = 0, Literal, Var } kind = Kind::None;
    std::string name;      // Var：变量名或 `*[s0,s1] T` 描述符
    std::string slot_key;  // Literal：函数槽 key（值在其中）
};

// 寄存器描述：一个操作数在帧地址表里的条目（构建期去重，帧入口解析一次）。
struct RegDesc {
    Operand::Kind kind = Operand::Kind::None;
    std::string name;     // Var 名/描述符
    std::string slot_key; // Literal 的函数槽 key
};

struct CInsn {
    ops::Op op = ops::Op::None;
    ops::Kind kind = ops::Kind::None;
    std::uint8_t width = 0;
    std::uint8_t nrd = 0, nwr = 0;
    std::uint8_t rd[4] = {};  // 寄存器号（Segment::regs 下标）
    std::uint8_t wr[2] = {};
    int s0 = 0;  // 明文指令坐标
    std::string name;    // opcode 原文（Native 分派用）
    std::string callee;  // call：被调函数名
    std::uint16_t prof_id = 0;  // 进程级 opcode 名表下标（分段计时用）
    ops::Impl impl = ops::Impl::None;
};

struct Segment {
    std::vector<CInsn> insns;
    std::vector<RegDesc> regs;  // 帧地址表条目（进程级，帧无关）
};

// 从函数目录（/lib/<name>/）构建指令段；遇到非本 runtime 的 opcode 即停
// （返回 false 且 insns 保留已建部分，调用方据此改写为 ext）。
bool build_segment(void* h, const std::string& func_dir, Segment* out);

// 进程级段缓存（按函数目录；失效用 ‥ctime，后续接）。
const Segment* cached_segment(void* h, const std::string& func_dir);
void clear_segment_cache();

// 进程级 opcode 名表（prof_id → 名字），只增不减。
const std::string& op_name(std::uint16_t id);
std::size_t op_name_count();

// 段缓存双键查询：函数目录 + 指令 s0。找不到/不可执行 → false。
// 命中时若 *out 非空，写本函数目录段缓存句柄（省调用方第二次按键查）。
bool cached_insn(const std::string& func_dir, int s0, const CInsn** out,
                 const Segment** seg_out);

}  // namespace kv::rt
