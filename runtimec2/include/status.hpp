#pragma once
#include <cstdint>
#include <cstddef>

namespace kv::rt {

enum class Status : std::uint8_t { Ok = 0, Error = 1, Yield = 2, Wait = 3, Fatal = 4 };

struct ErrorBuf {
    std::uint16_t code = 0;
    char msg[192] = {};

    void set(std::uint16_t c, const char* m) noexcept;
};

}  // namespace kv::rt
