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
// 帧寄存器 = kvspace 明文槽的**派生视图**（不是第二事实源）：
// - body/cap 指向 shm 槽（借用），fast 缓存纯 u64/布尔值；
// - 任何写（本 runtime 或外部）都先写回 kvspace 明文槽再更新 fast，
//   槽被删/重建/换 Ptr → 缓存失效重建（frame-entry 解析 + view 失效）。
struct FReg {
    std::string key;
    std::string desc;  // Ptr 描述符原样（惰性解析用；非 Ptr 为空）
    kvspaceRef_t ref{};
    bool resolved = false;
    kvspaceSlotView_t view{};
    bool view_ok = false;
    std::uint8_t* body = nullptr;  // view 的 body 指针（槽搬迁/写后失效）
    std::uint64_t cap = 0;
    bool sc_ok = false;  // 标量 kind/width 已缓存（写后失效）
    Scalar::K sck = Scalar::K::None;
    std::uint8_t scw = 0;
    // 热路径快缓存：已确认 Int/8 时直存 u64；已确认 Bool/1 时直存 b。
    bool fast_i64 = false;
    std::int64_t fv = 0;
    bool fast_bool = false;
    bool fb = false;
};

struct FrameCtx {
    void* h = nullptr;
    std::string vid;
    int depth = 0;
    std::string frame;    // /vthread/<vid>/[depth]
    std::string func_dir;    // /lib/<name>
    std::string frame_dir_hint;  // 段缓存已持有的函数目录（复用，避免二次构造/查询）
    std::vector<FReg> regs;  // 帧地址表（帧入口一次解析）
};

// 单帧执行结果：Ok=入口帧走到段尾/return（vthread done）；
// Returned=子帧 return 弹出（stop_pc 为调用方返回地址）；Stop=遇到他人 op。
enum class RunResult : std::uint8_t { Ok = 0, Stop, Returned, Error };

// 从 start_s0 起执行指令段；Stop 时把停机指令的完整 pc（含帧层）写入 stop_pc。
RunResult run_frame(FrameCtx& f, int start_s0, std::string* stop_pc,
                    std::string* err);

}  // namespace kv::rt
