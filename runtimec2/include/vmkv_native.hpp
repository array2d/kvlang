#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "vmkv.hpp"

namespace kv::rt {

// Native 分派上下文（vmkv.cpp run_frame native 分支的装载态 + 原内嵌 lambda 提升）。
// handler 一律自由函数，经 ctx.vm 调 Vmkv 方法（消除每 native 重复捕获）。
struct NativeCtx {
    Vmkv& vm;
    bin::FrameCtx& f;
    const Segment* seg;
    const CInsn& ins;
    Wire in[2];
    std::int64_t v0 = 0, v1 = 0;
    const std::string& fn;  // fold 后的 op 名（`·`→`/`，表键）
    // vthread·run 子 vid 撞外部 rwir 时的冒泡 pc：handler 填后，run_frame 以
    // Stop 让出，execute_vthread_impl 把该 pc 交驱动（处理子 rwir 后重入续驱）。
    std::string yield_pc;

    // 读参解码（v_scalar 优先，time/duration 等非数值 langtype body 即 8 字节整数）。
    bool ai64(int i, std::int64_t* v);
    // 读参值按 char 族转 UTF-8 文本。
    bool atext(int i, std::string* out);
    // 写 bool 标量到写槽。
    bool wbool(bool b);
    // kvspace 族路径实参：值文本优先（对齐 C path_arg），变量取帧 key。
    bool path_arg(int i, std::uint8_t rno, const RegDesc& d, std::string* out);
    // kvspace 族 k0/k1 双路径解析 + trace。
    bool kv_paths(std::string* k0, std::string* k1);
    // 写槽描述符 `[s0,1]` 槽 body 的 NUL 后段（容器/数组类型串；可能空）。
    std::string write_slot_lt();
    // `[N,M]kind` 拆 dims + 裸 kind；无 `[` 则原样返回、ndim=0。
    std::string parse_dims(std::string lt, std::int32_t dims[8], std::uint32_t* ndim);
    // 裸 kind → 每元素字节宽（0 = 未知；不含 char 族——新 reinterpret 自行处理）。
    std::uint32_t elem_size(const std::string& kind);
};

// handler：返回 false = 失败（err 已填，或留空由调用方补 "write failed"）。
using NativeFn = bool (*)(NativeCtx&, std::string* err);

// 折叠名 → handler；未命中返回 nullptr（表未命中 = 未实现 op，调用方兜底 error）。
const NativeFn* find_native(std::string_view fn);

}  // namespace kv::rt
