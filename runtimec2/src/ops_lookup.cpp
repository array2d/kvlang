#include <cstring>

#include "ops.hpp"

namespace kv::ops {

const Entry* find(const char* name, std::uint32_t len) noexcept {
    for (int i = 0; i < kBaseOpsN; i++) {
        const char* n = kBaseOps[i].name;
        if (std::strlen(n) == len && std::memcmp(n, name, len) == 0)
            return &kBaseOps[i];
    }
    return nullptr;
}

}  // namespace kv::ops
