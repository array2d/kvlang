#include <cstring>

#include "rwir.hpp"

namespace kvlang::runtimec2::ops {

const Entry* find_myrwircap(const char* name, std::uint32_t len) noexcept {
    for (int i = 0; i < kMyrwircapsN; i++) {
        const char* n = myrwircaps[i].name;
        if (std::strlen(n) == len && std::memcmp(n, name, len) == 0)
            return &myrwircaps[i];
    }
    return nullptr;
}

}  // namespace kvlang::runtimec2::ops
