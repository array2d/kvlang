#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vmbin/program.hpp"

// KV 函数 → Segment → binir 的唯一编译模块。
// 建段（读签名与指令、解析分支目标/被调方、物化字面量）与按实参类型特化都在
// 这里；binir 字节格式本身属 vmbin（binir_codec），本模块只调用它。
namespace kvlang::runtimec2::binircompiler {

using Operand = vmbin::Operand;
using RegDesc = vmbin::RegisterDesc;
using CInsn = vmbin::Instruction;
using LitFast = vmbin::FastLiteral;
using Segment = vmbin::Program;

// 实参类型签名（langtype 串，如 int64 / float64 / char/utf8）。
struct ArgumentType {
    std::string langtype;
};

// nullopt = 不提供实参类型，通用编译（与历史行为逐字节相同）；
// 有值但个数/类型与函数读参不符 = 编译失败，不静默退回通用；
// 零读参函数传空 vector = 零参特化编译。
struct CompileOptions {
    std::optional<std::vector<ArgumentType>> arguments;
};

struct CompileError {
    std::string func_dir;
    std::string reason;
};

// 特化报告：每条指令的「原算子 → 类型化算子 → 依据/未特化原因」。
// 空 reason + from != to = 已改写；reason 非空 = 未改写。
struct InstructionResolution {
    std::size_t pc = 0;
    std::string from;
    std::string to;
    std::string reason;
};

struct SpecializationReport {
    std::vector<InstructionResolution> instructions;
    std::size_t typed = 0;    // Impl::Typed 指令数（特化后口径）
    std::size_t generic = 0;  // Impl::Dynamic 指令数（特化后口径）
};

// 编译一个函数目录（如 /lib/test）。report 仅在提供实参类型时填充。
bool compile(void* kvspace, const std::string& func_dir,
             const CompileOptions& options, Segment* out, CompileError* error,
             SpecializationReport* report = nullptr);

// 运行时段缓存：通用版本（键 = func_dir）。
const Segment* cached(void* kvspace, const std::string& func_dir);
// 特化版本（键 = func_dir + 实参签名）；arguments 为空则等同上面。
const Segment* cached(void* kvspace, const std::string& func_dir,
                      const CompileOptions& options);
// 纯查缓存，不建段（cached_insn 的未命中语义）。
const Segment* lookup(const std::string& func_dir);
void clear_cache();
std::string take_last_error();
void bump_epoch() noexcept;
std::uint64_t current_epoch() noexcept;
std::uint64_t relayout_epoch() noexcept;
bool segment_may_stop(void* kvspace, const Segment* seg);

// encode → decode → same_program 往返校验（--check）。
bool check_roundtrip(const Segment& segment, std::string* why);

// 编译期检查入口：内部按两条独立规则执行——
//   #594（写参在所有返回路径必须赋值，规则未落地，空实现）
//   阶段 A（语义算子 union 签名：操作数 ⊆ union、结果与写槽类型一致）
bool RunCompileChecks(const Segment& segment, CompileError* error);

// 阶段 A 交叉校验：layout 已发的 typed 指令必须与 union/promotion 选出的实现
// 完全一致；类型全已知时 layout 不得漏特化。供工具与 ctest 使用。
struct UnionCheckReport {
    std::size_t checked = 0;      // 参与检查的语义指令数
    std::size_t matched = 0;
    std::size_t mismatched = 0;
    std::size_t undecidable = 0;  // 操作数类型未知（char/容器/动态槽）
    std::size_t undecidable_constant = 0;  // 字面量（char/路径/非标量）
    std::size_t undecidable_member = 0;    // 结构体/容器成员
    std::size_t undecidable_pointer = 0;   // 指针 / 参数 Ptr
    std::size_t undecidable_untyped = 0;   // 无类型标注的普通槽
    std::size_t no_typed_variant = 0;      // 类型全已知但无唯一 typed 期望
    std::vector<std::string> details;  // 差异明细（最多 32 条）
};
bool CrossCheckUnion(const Segment& segment, UnionCheckReport* report,
                     CompileError* error);

}  // namespace kvlang::runtimec2::binircompiler
