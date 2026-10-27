#pragma once
#include <string>

// 明文栈接口（vmkv）：与 kvspace 明文栈交互的唯一面。
// 二进制栈（寄存器/帧/缓存）全在 vmbin.hpp——本头不出现任何 bin 结构。
#include "vmbin.hpp"

namespace kv::rt {

// 从 start_s0 起执行指令段（二进制栈帧 f）；Stop 时把停机指令的完整明文 pc
// （含帧层）写入 stop_pc。run_frame 是明文栈 ↔ 二进制栈的执行边界。
bin::RunResult run_frame(bin::FrameCtx& f, int start_s0, std::string* stop_pc,
                         std::string* err);

}  // namespace kv::rt
