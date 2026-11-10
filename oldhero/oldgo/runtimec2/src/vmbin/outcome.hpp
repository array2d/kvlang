#pragma once

#include <cstdint>

namespace kvlang::runtimec2::vmbin {

enum class Outcome : std::uint8_t {
    Ok = 0,
    Stop = 1,
    Returned = 2,
    Error = 3,
};

}  // namespace kvlang::runtimec2::vmbin
