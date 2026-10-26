#pragma once
#include <cstdint>
#include <string>

namespace kv::rt {

struct Wire {
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
};

// 标量视图：kind 串（去 dims）与宽度。
struct Scalar {
    enum class K : std::uint8_t { None, Int, Uint, Float, Bool, Char } k = K::None;
    std::int64_t i = 0;
    std::uint64_t u = 0;
    double f = 0;
    std::uint8_t width = 0;
};

bool v_ptr(Wire w) noexcept;
bool v_ptr_target(Wire w, std::string* out) noexcept;
bool v_langtype(Wire w, std::string* out) noexcept;
bool v_is_none(Wire w) noexcept;

// 解码标量；kind 取 langtype（忽略 dims 前缀），支持 int*/uint*/float*/bool。
bool v_scalar(Wire w, Scalar* out) noexcept;

// 已解码出 kind/width 后，直接按 body 原始字节取标量（零拷贝热路径）。
bool v_scalar_raw(Scalar::K k, std::uint8_t width, const std::uint8_t* body,
                  std::uint64_t cap, Scalar* out) noexcept;

// 直接按 head 里的 langtype + body 指针解标量（零拷贝路径用）。
bool v_scalar_view(const std::uint8_t* langtype, std::size_t lt_len,
                   const std::uint8_t* body, std::uint64_t cap,
                   Scalar* out) noexcept;

// 按 kind/width 把标量编码成 wire（malloc），调用方 free。
bool v_encode(const char* kind, std::uint8_t width, const Scalar& s,
              std::uint8_t** out, std::uint32_t* out_len) noexcept;

// 写入标量值（SetValue）。
bool v_write_scalar(void* h, const std::string& key, const char* kind,
                    std::uint8_t width, const Scalar& s) noexcept;

}  // namespace kv::rt
