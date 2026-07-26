#pragma once
#include <string>
#include <string_view>

namespace kvlang::keytree {

// Member access paths within frames
std::string member_path(std::string_view base, std::string_view member);

} // namespace kvlang::keytree
