#pragma once
#include <string>
#include <string_view>

// 明文栈接口（vmkv）：与 kvspace 明文栈交互的唯一面 + 明文 PC 契约。
// 二进制栈结构（FReg/FrameCtx/VmBin）在 vmbin.hpp；本头声明 Vmkv struct
// （持 VmBin 成员 + 明文方法）。vmkv 可依赖 vmbin 公开接口，vmbin 不依赖 vmkv。
#include "vmbin.hpp"

namespace kv::rt {

// PC 路径：/vthread/<vid>/[<depth>]/[<s0>,<s1>]（明文 PC 是外部/持久契约）。
struct Pc {
    std::string vid;
    int depth = 0;
    int s0 = 0;
    int s1 = 0;
};

bool parse_pc(std::string_view pc, Pc* out) noexcept;

// 明文栈 VM（Vmkv）：持 VmBin 成员（二进制栈），方法 = 明文装载/槽解析/回写
// + 明文 PC 构造。依赖 vmbin 公开接口（vmkv.bin.xxx()）。
struct Vmkv {
    bin::VmBin bin;

    // ── 明文 PC/路径构造（原 model.hpp 职责）──
    std::string vthread_root(std::string_view vid);
    std::string frame_root(std::string_view vid, int depth);
    std::string entry_pc(std::string_view vid, int depth);
    std::string next_pc(const Pc& pc);
    std::string pc_key_for(const Pc& p);
    std::string instr_key(const Pc& pc);
    std::string param_key(std::string_view frame_root, int s0, int s1);
    std::string fold_op(std::string_view op);

    // ── 明文装载/槽解析/回写（原 vmkv.cpp 散函数）──
    bool resolve_desc(bin::FrameCtx& f, const std::string& name,
                      std::string* key, std::string* err);
    bool reg_slot(bin::FrameCtx& f, std::uint8_t idx,
                  kvspaceXvalueBodyView_t* out);
    bool reg_body(bin::FrameCtx& f, std::uint8_t idx, std::uint8_t** body,
                  std::uint64_t* cap);
    bool slot_langtype(const kvspaceXvalueBodyView_t& v, std::string* lt);
    bool slot_read_scalar(const kvspaceXvalueBodyView_t& v, Scalar* out);
    bool ensure_reg_key(bin::FrameCtx& f, bin::FReg& r);
    bool read_reg(bin::FrameCtx& f, std::uint8_t idx, Wire* out);
    bool read_scalar(bin::FrameCtx& f, std::uint8_t idx, Scalar* out);
    bool read_i64(bin::FrameCtx& f, std::uint8_t idx, std::int64_t* out);
    bool read_bool(bin::FrameCtx& f, std::uint8_t idx, bool* out);
    void flush_frame_locals(bin::FrameCtx& f);
    bool write_reg(bin::FrameCtx& f, std::uint8_t idx,
                   const std::uint8_t* data, std::uint32_t len);
    bool slot_write_i64(bin::FrameCtx& f, std::uint8_t idx, std::int64_t v);
    bool slot_write_bool(bin::FrameCtx& f, std::uint8_t idx, bool b);
    bool slot_write_scalar(bin::FrameCtx& f, std::uint8_t idx,
                           const Scalar& s, std::uint8_t width);
    void sync_fast_after_write(bin::FrameCtx& f, std::uint8_t idx,
                               const Scalar& s);
    bool is_frame_local(const bin::FrameCtx& f, const std::string& rk);
    const char* kind_name(Scalar::K k, std::uint8_t width);
    bool write_named_i64(void* h, std::uint8_t idx, bin::FrameCtx& f,
                         const char* kind, std::int64_t v);

    // ── 标量运算（步骤 2 挪 vmbin 后删，此处转发）──
    bool truthy(const Scalar& s);
    bool is_cmp(ops::Op op);
    bool same_class(Scalar::K a, Scalar::K b);
    bool compute_int(ops::Op op, Scalar::K k, const Scalar& a,
                     const Scalar& b, Scalar* r);
    bool compute_float(ops::Op op, double a, double b, double* r);
    bool compare(ops::Op op, Scalar::K k, const Scalar& a,
                 const Scalar& b, bool* out);

    // ── 执行（原 vm.cpp run_frame 方法化）──
    // 帧入口：逐 reg 热字段重置 + 字面量快值缓存 + Ptr 参数内存别名绑定 + 描述符解析。
    // 每 call 唯一的 kvspace 冷路径，与指令派发正交。false = err 已填。
    bool enter_frame(bin::FrameCtx& f, const Segment* seg, std::string* err);
    // 从 start_s0 起执行指令段（二进制栈帧 f）；Stop 时把停机指令的完整明文 pc
    // （含帧层）写入 stop_pc。run_frame 是明文栈 ↔ 二进制栈的执行边界。
    bin::RunResult run_frame(bin::FrameCtx& f, int start_s0,
                             std::string* stop_pc, std::string* err);
    // Call 块（原 run_frame 内嵌）：callee 解析、mem_frame 判定、帧初始化、
    // arg_src/arg_dst 内存别名表、递归 run_frame。Ok = 调用方继续下一条指令。
    bin::RunResult do_call(bin::FrameCtx& f, const Segment* seg, const CInsn& ins,
                           std::string* stop_pc, std::string* err);
};

// 进程级明文栈实例（Runtime 注入共享 kvspace 句柄；bin 成员经 g_vmkv.bin 访问）。
extern Vmkv g_vmkv;

}  // namespace kv::rt
