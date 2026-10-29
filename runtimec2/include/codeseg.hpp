#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "rwir.hpp"

namespace kvlang::runtimec2 {

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
    int param_k = 0;      // 参数 Ptr 槽号（`*[0,±k]` build 预解析；帧入口零 substr/atoi）
    // Var shape, classified at build: param Ptr `*[0,k]`, other Ptr, plain name, typed name.
    enum class VarClass : std::uint8_t { None = 0, ParamPtr, Ptr, Plain, Typed } vclass =
        VarClass::None;
    bool member = false;  // `base·name` write target: its key follows base, never cache it
};

struct Segment;

struct CInsn {
    rwir::Op op = rwir::Op::None;
    rwir::Kind kind = rwir::Kind::None;
    rwir::Impl impl = rwir::Impl::None;
    std::uint8_t width = 0;
    std::uint8_t opfn = 0;      // 快路径算术函数表下标（Add=1..Ge=6，0=非快算术）
    bool fast_i64 = false;      // 快路径候选：Typed int64 二目二元 op（iops 热循环）
    std::uint8_t nrd = 0, nwr = 0;
    std::uint8_t rd[16] = {};  // 寄存器号（Segment::regs 下标）；call 时 rd[0]=callee，参数从 rd[1]；数组字面量元素从 rd[0]
    std::uint8_t wr[4] = {};
    bool wr_local = false;  // 写槽是帧局部普通变量（build 预标记）：fast 写免 is_frame_local string 比较
    std::uint16_t prof_id = 0;  // 进程级 opcode 名表下标（分段计时用）
    int s0 = 0;  // 明文指令坐标
    std::string name;    // opcode 原文（Native 分派用）
    std::string callee;  // call：被调函数名
    // Lazily filled run-time caches; valid for the segment's lifetime.
    mutable std::string fn_fold;           // name with `·` folded to `/`
    mutable const void* native = nullptr;  // const NativeFn*
    mutable bool native_ready = false;
    mutable bool native_noprefetch = false;
    mutable const Segment* call_seg = nullptr;
    mutable const void* call_jit = nullptr;
    mutable bool call_ready = false;
    mutable std::vector<std::string> call_rtyp;  // callee decl type per read arg (1-based)
    mutable std::vector<std::string> call_wtyp;  // per write arg (1-based)
    mutable std::vector<std::uint8_t> call_rtyp_ok, call_wtyp_ok;
    // Member write `v -> base·key`: reg indexes found by name (-2 = not yet, -1 = none).
    mutable int mw_keyreg = -2, mw_basereg = -2;
};

// 字面量 int64 快值缓存（build 期读槽一次，帧入口零 kvspace 预读）。
struct LitFast {
    bool is_i64 = false;
    std::int64_t v = 0;
};

struct Segment {
    std::vector<CInsn> insns;
    std::vector<RegDesc> regs;  // 帧地址表条目（进程级，帧无关）
    std::vector<LitFast> lit;   // 下标 = reg 号；字面量 int64 快值（fib 递归每帧省 kvspaceXvalueBodyView）
    bool has_native = false;    // 含 native/handoff：return/Stop 前须冲刷纯内存帧局部变量
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

}  // namespace kvlang::runtimec2
