#pragma once
#include <string>
#include <string_view>

namespace kvlang::keytree {

// VThread paths
std::string vthread_root(std::string_view vtid);
std::string vthread_call(std::string_view vtid, int depth);

} // namespace kvlang::keytree
