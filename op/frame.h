#pragma once
#include <string>
#include <string_view>

// Frame-level instruction helpers.

namespace kvlang::op {

[[nodiscard]] std::string frame_root(std::string_view pc);
[[nodiscard]] std::string link_base(std::string_view frame_root);

} // namespace kvlang::op
