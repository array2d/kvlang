#pragma once
#include <cstdint>
#include <string>

#include "codeseg.hpp"
#include "status.hpp"
#include <vector>

#include <kvspace/kvspace.h>

#include "value.hpp"

namespace kv::rt {

// 单帧执行上下文（中间变量段：帧栈成员；值仍在 kvspace）。
struct FReg {
    std::string key;
    kvspaceRef_t ref{};
    bool resolved = false;
    kvspaceSlotView_t view{};
    bool view_ok = false;
    std::uint8_t* body = nullptr;  // view 的 body 指针（槽搬迁/写后失效）
    std::uint64_t cap = 0;
    bool sc_ok = false;  // 标量 kind/width 已缓存（写后失效）
    Scalar::K sck = Scalar::K::None;
    std::uint8_t scw = 0;
};

struct FrameCtx {
    void* h = nullptr;
    std::string vid;
    int depth = 0;
    std::string frame;    // /vthread/<vid>/[depth]
    std::string func_dir; // /lib/<name>
    std::vector<FReg> regs;  // 帧地址表（帧入口一次解析）
};

// 单帧执行结果：Ok=入口帧走到段尾/return（vthread done）；
// Returned=子帧 return 弹出（stop_pc 为调用方返回地址）；Stop=遇到他人 op。
enum class RunResult : std::uint8_t { Ok = 0, Stop, Returned, Error };

// 从 start_s0 起执行指令段；Stop 时把停机指令的完整 pc（含帧层）写入 stop_pc。
RunResult run_frame(FrameCtx& f, int start_s0, std::string* stop_pc,
                    std::string* err);

}  // namespace kv::rt
