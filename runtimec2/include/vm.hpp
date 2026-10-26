#pragma once
#include <cstdint>

#include "status.hpp"

namespace kv::rt {

// 指令项（指令段，进程级）：myrwircaps 下标 + 寄存器号。
struct Insn {
    std::uint16_t op;  // myrwircaps 下标；0xFFFF = 扩展路由
    std::uint8_t nrd, nwr;
    std::uint8_t rd[4];
    std::uint8_t wr[2];
};

// 代码段：指令段（进程级）+ 构建时的函数树 ‥ctime。
struct CodeSegment {
    const Insn* insn = nullptr;
    std::uint32_t ninsn = 0;
    std::uint64_t ctime = 0;
};

// 帧：中间变量段（帧级）是 u64 kvspace 地址数组。
struct Frame {
    const CodeSegment* seg = nullptr;
    std::uint64_t* var = nullptr;
    std::uint32_t nvar = 0;
    std::uint32_t rwirorder = 0;
    std::int32_t stackid = 0;
};

struct PcPair {
    std::int32_t stackid;
    std::int32_t rwirorder;
};

using Handler = Status (*)(Frame&, const Insn&) noexcept;

}  // namespace kv::rt
