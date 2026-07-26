#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace kvlang::keytree {

// Frame coordinate encoding
std::string frame_coord(int row, int col);
std::string frame_root(std::string_view vtid);
std::string frame_link_base(std::string_view pc);

} // namespace kvlang::keytree
