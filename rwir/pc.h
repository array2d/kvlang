#pragma once
#include <string>
#include <string_view>

// Program counter (PC) utilities. PC is a KV path string.

namespace kvlang::op {

// Split PC into frame root + instruction coordinate.
struct PC {
    std::string frame_root;
    int row = 0;
    int col = 0;
};

[[nodiscard]] PC decode_pc(std::string_view pc);
[[nodiscard]] std::string next_pc(std::string_view pc);

} // namespace kvlang::op
