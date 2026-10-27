#pragma once
#include <string>

// 明文栈接口（vmkv）：与 kvspace 明文栈交互的唯一面。
// 二进制栈结构（FReg/FrameCtx）在 vmbin.hpp；本头只声明明文栈操作
// （装载/槽解析/回写），实现见 vmkv.cpp。vmkv 可依赖 vmbin 公开接口，
// vmbin 不依赖 vmkv。
#include "vmbin.hpp"

namespace kv::rt {

// 从 start_s0 起执行指令段（二进制栈帧 f）；Stop 时把停机指令的完整明文 pc
// （含帧层）写入 stop_pc。run_frame 是明文栈 ↔ 二进制栈的执行边界。
bin::RunResult run_frame(bin::FrameCtx& f, int start_s0, std::string* stop_pc,
                         std::string* err);

// ── 明文栈操作（装载/槽解析/回写；vmkv.cpp 实现）───────────────────

// 变量描述符 → 具体 key（帧入口一次；Ptr 链在此解析完）。
bool resolve_desc(bin::FrameCtx& f, const std::string& name, std::string* key,
                  std::string* err);
// shm slot view：head/body 直接指针（帧内缓存，写后失效）。
bool reg_slot(bin::FrameCtx& f, std::uint8_t idx,
              kvspaceXvalueBodyView_t* out);
// 只取 body/cap（热路径不搬 view 结构）。
bool reg_body(bin::FrameCtx& f, std::uint8_t idx, std::uint8_t** body,
              std::uint64_t* cap);
bool slot_langtype(const kvspaceXvalueBodyView_t& v, std::string* lt);
bool slot_read_scalar(const kvspaceXvalueBodyView_t& v, Scalar* out);
// 惰性解析：Ptr 描述符首次读/写时补 key（读写共用）。
bool ensure_reg_key(bin::FrameCtx& f, bin::FReg& r);
bool read_reg(bin::FrameCtx& f, std::uint8_t idx, Wire* out);
bool read_scalar(bin::FrameCtx& f, std::uint8_t idx, Scalar* out);
bool read_i64(bin::FrameCtx& f, std::uint8_t idx, std::int64_t* out);
bool read_bool(bin::FrameCtx& f, std::uint8_t idx, bool* out);
// 冲刷帧局部纯内存变量落明文（Stop/native 边界前）。
void flush_frame_locals(bin::FrameCtx& f);
bool write_reg(bin::FrameCtx& f, std::uint8_t idx, const std::uint8_t* data,
               std::uint32_t len);
bool slot_write_i64(bin::FrameCtx& f, std::uint8_t idx, std::int64_t v);
bool slot_write_bool(bin::FrameCtx& f, std::uint8_t idx, bool b);
bool slot_write_scalar(bin::FrameCtx& f, std::uint8_t idx, const Scalar& s,
                       std::uint8_t width);
void sync_fast_after_write(bin::FrameCtx& f, std::uint8_t idx,
                           const Scalar& s);
// 帧局部普通变量判定（fast 写免 kvspace）。
bool is_frame_local(const bin::FrameCtx& f, const std::string& rk);
const char* kind_name(Scalar::K k, std::uint8_t width);
bool write_named_i64(void* h, std::uint8_t idx, bin::FrameCtx& f,
                     const char* kind, std::int64_t v);
// 标量运算（纯值，无明文依赖）。
bool truthy(const Scalar& s);
bool is_cmp(ops::Op op);
bool same_class(Scalar::K a, Scalar::K b);
bool compute_int(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b,
                 Scalar* r);
bool compute_float(ops::Op op, double a, double b, double* r);
bool compare(ops::Op op, Scalar::K k, const Scalar& a, const Scalar& b,
             bool* out);

}  // namespace kv::rt
