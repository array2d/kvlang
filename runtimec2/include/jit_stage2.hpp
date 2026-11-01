#pragma once
#include <cstdint>
#include <vector>

#include "codeseg.hpp"
#include "jit.hpp"
#include "jit_abi_v1.hpp"

// 第二阶段最小原型：CFG + 活跃分析 + 线性扫描分配（含栈溢出）。
// 与第一阶段共用 opcode 白名单，但放宽「单入单出」形状：
// 0..15 读参、0..4 写参、任意合法 reg 下标、局部变量可溢出到栈。
// 入口 ABI（统一）：jitabi::GeneralJitFnV1(VmContextV1*, BinFrameV1*, StructuredExitV1*)；
// slots 以 reg 下标寻址（ValueSlotV1，32B；payload_lo 存 int64、tag=I64），
// 输入由调用方预填、局部清 0、写参在 Return 出口写回。读-写写参需调用方预载，
// 本原型只接受「写参在定义前不被读」的形状（见 jit2_verify）。
namespace kvlang::runtimec2 {

struct Jit2Shape {
    std::vector<int> inputs;   // 按 param_k -1,-2,... 排序
    std::vector<int> outputs;  // 按 param_k +1,+2,... 排序
};

struct Jit2Block {
    int begin = 0;  // 指令下标 [begin, end)
    int end = 0;
    std::vector<int> successors;
};

struct Jit2Plan {
    Jit2Shape shape;
    std::vector<Jit2Block> blocks;
    std::vector<std::vector<char>> live_in;   // 每条指令
    std::vector<std::vector<char>> live_out;
    std::vector<int> interval_start;  // 每个 reg（-1 = 不参与）
    std::vector<int> interval_end;
    std::vector<int> assignment;  // 每个 reg：0..2 = 物理池，-1 = 溢出，-2 = 无
    std::vector<int> spill_slot;  // 每个 reg 的栈槽（-1 = 无）
    std::vector<bool> fused;      // 比较 + 后向无关的 br 融合
    std::vector<char> is_handler;  // 该指令是 handler（原生内调或 Handoff 出口）
    std::vector<std::vector<char>> native_stage;  // 出口/调用前后要物化的 reg 集
    // P0：compact 数组内联。array_op[i]：0 无，1 = at，2 = set；
    // call_handler[i]：原生内调 handler 后继续；handoff_exit[i]：Handoff 停止原生。
    std::vector<std::vector<char>> out_defined;  // 每条指令出口“已定义”寄存器集
    std::vector<std::uint8_t> opfn;  // 有效 int64 算子号（含动态算子静态定型）
    // float64 扩展：值常驻栈槽（XMM 只做 scratch），算子号 1 add / 2 sub /
    // 3 mul / 4 div；fcast 1 = int64→float64，2 = float64→int64。
    std::vector<std::uint8_t> fopfn;
    std::vector<std::uint8_t> fcast;
    std::vector<char> is_float;  // 该寄存器是 float64 表示
    std::vector<std::uint8_t> array_op;
    std::vector<std::uint8_t> call_handler;
    std::vector<std::uint8_t> handoff_exit;
    std::vector<char> output_preload;  // 读-写写参：调用方必须预载的写参

    int handler_count = 0;
    int handler_percent = 0;       // handler 指令占全段百分比
    int spill_count = 0;
};

// 静态判定 + CFG + 活跃 + 分配；不发射、不执行。
bool jit2_verify(const Segment* seg, Jit2Plan* plan, JitRefusal* refusal);

// 按 plan 发射 x86_64 机器码（统一 ABI，见文件头）。
std::vector<std::uint8_t> jit2_emit(const Segment* seg, const Jit2Plan& plan);

}  // namespace kvlang::runtimec2
