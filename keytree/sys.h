#pragma once
#include <string>
#include <string_view>

namespace kvlang::keytree {

// System-level paths
std::string sys_vm_error(std::string_view vm_id);
std::string sys_vt_list();

} // namespace kvlang::keytree
