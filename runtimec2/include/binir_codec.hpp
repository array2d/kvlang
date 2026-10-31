#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "codeseg.hpp"

namespace kvlang::runtimec2 {

// BinIR 字节码编解码（格式见 workspace/583/architect/binir-format-spec.md）。
// encode：Segment → 字节流；decode：字节流 → Segment（CInsn 解码视图）。
// 两者都严格校验：任何越界、非规范形态、未知 tag 一律返回 false，不产生部分结果。
bool encode_binir(const Segment& segment, std::vector<std::uint8_t>* out);
bool decode_binir(const std::uint8_t* data, std::size_t size, Segment* out);

}  // namespace kvlang::runtimec2
