#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace kv::rt {

// 读借用 wire（生命周期到该 key 被写/删或 handle close）。
bool read_wire(void* h, std::string_view key, const std::uint8_t** data,
               std::uint32_t* len) noexcept;

// 读标量/字符串值并转成 UTF-8 文本（char/utf8|utf32|ascii）。
bool read_text(void* h, std::string_view key, std::string* out) noexcept;

// 写 UTF-8 字符串值（XValue head 由 kvspaceXhNewShort 生成）。
bool write_text(void* h, std::string_view key, std::string_view text) noexcept;

// 读 rwir 指令值：body = [nr u16][nw u16][dyn u8][opcode…]。
bool read_rwir(void* h, std::string_view key, std::string* op, int* nr,
               int* nw) noexcept;

// 帧的函数目录（<frame>‥lib 的值）；空表示未绑定。
bool frame_func_dir(void* h, std::string_view vid, int depth, std::string* out) noexcept;

// 读十进制整数（用于 ‥seq）。
bool read_int(void* h, std::string_view key, std::int64_t* out) noexcept;
bool write_int(void* h, std::string_view key, std::int64_t v) noexcept;

}  // namespace kv::rt
