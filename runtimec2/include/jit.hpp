#pragma once
#include <cstdint>

#include "codeseg.hpp"
#include "vmbin.hpp"

// M1 原生发射（iops 形态）：verify_jitable 静态判定 + 最小 x86_64 发射器。
// 发射产物 = 纯计算函数 int64_t(int64_t)，帧 I/O（参数装载/结果回写/错误）
// 全在 jit_try_run 的 C++ 侧完成——JIT 代码本身零 kvspace/零 FReg 访问。
// 语义：值跨多条指令存活于机器寄存器，出口一次性回写；mem_frame 无观察点，
// 与 #499 checkpoint 语义一致（检查点=函数边界）。

namespace kv::rt {

// 发射函数 ABI（M1）：arg → RAX 结果；err_out 非 0 = runaway guard 触发。
using JitFn = std::int64_t (*)(std::int64_t arg, std::int64_t* err_out);

// 判定 + 发射 + 缓存（进程级，键 = 函数目录，随段缓存失效）。
// 返回 nullptr = 不可 JIT / 发射失败（调用方回退 fast kernel）。
const void* jit_compile(const std::string& func_dir, const Segment* seg);
void jit_clear_cache();

// run_frame 钩子：命中则执行并把 RunResult 写入 *out，返回 true；
// 未命中/禁用/不满足运行时前提 → false（走解释路径）。
bool jit_try_run(bin::FrameCtx& f, const Segment* seg, int start_s0,
                 bin::RunResult* out, std::string* stop_pc, std::string* err);

// 环境开关：RC2_NOJIT=1 关 JIT；RC2_JITDUMP=1 发射后打印机器码。
bool jit_enabled();

}  // namespace kv::rt
