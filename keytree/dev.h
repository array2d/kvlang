#pragma once
#include <string>
#include <string_view>

namespace kvlang::keytree {

// Device paths
std::string dev_terminal(std::string_view vm_id);
std::string dev_ws(std::string_view vm_id);

} // namespace kvlang::keytree
