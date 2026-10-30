#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace kvlang::runtimec2 {

// 读借用 wire（生命周期到该 key 被写/删或 handle close）。
bool read_wire(void* h, std::string_view key, const std::uint8_t** data,
               std::uint32_t* len) noexcept;

// resolve=0：读键自身（Ptr 不穿透）。槽/参数 Ptr 的读取必须用它。
bool read_wire_raw(void* h, std::string_view key, const std::uint8_t** data,
                   std::uint32_t* len) noexcept;

// Drop cached view tokens and quiesce the handle; call before going idle.
void view_cache_idle(void* h) noexcept;
// Same for whichever handle currently holds tokens.
void view_cache_idle_all() noexcept;
// Ack the recovery epoch; call only between instructions (no wires held).
void view_cache_tick() noexcept;

// Quiesces live view tokens when an ABI entry returns to the host.
struct ViewIdleGuard {
    ~ViewIdleGuard() { view_cache_idle_all(); }
};

// 读 key 的 x‥langtype attr key（v2 明文事实源，kvspaceGetHead）；无/缺失 → false。
bool read_langtype(void* h, std::string_view key, std::string* out) noexcept;

// 把一段 XValue 线数据按 char 族解码成 UTF-8 文本（langtype 由调用方提供）。
bool wire_text(const std::uint8_t* data, std::uint32_t len,
               std::string_view langtype, std::string* out) noexcept;

// 读标量/字符串值并转成 UTF-8 文本（char/utf8|utf32|ascii）。
bool read_text(void* h, std::string_view key, std::string* out) noexcept;

// 写 UTF-8 字符串值（v2 固定头由 kvspaceXvalueHeadNewSlack 生成）。
bool write_text(void* h, std::string_view key, std::string_view text) noexcept;

/* 组装字符串 wire：langtype 带 [len] 形状段（码点数）。 */
bool v_char_wire(const char* kind, const std::uint8_t* bytes, std::uint32_t n,
                 std::uint8_t** out, std::uint32_t* out_len) noexcept;

// 读 rwir 指令值：body = [nr u16][nw u16][dyn u8][opcode…]。
bool read_rwir(void* h, std::string_view key, std::string* op, int* nr,
               int* nw) noexcept;

// 读 rwfunc 值（调用点）：body = [nr u16][nw u16][dyn u8][name…]。
bool read_rwfunc(void* h, std::string_view key, std::string* name, int* nr,
                 int* nw) noexcept;

// 帧的函数目录（<frame>‥lib 的值）；空表示未绑定。
bool frame_func_dir(void* h, std::string_view vid, int depth, std::string* out) noexcept;

// 读十进制整数（用于 ‥seq）。
bool read_int(void* h, std::string_view key, std::int64_t* out) noexcept;
bool write_int(void* h, std::string_view key, std::int64_t v) noexcept;

}  // namespace kvlang::runtimec2
