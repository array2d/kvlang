#pragma once

#include <cstdint>

namespace kvlang::runtimec2::vmbin {

enum class LangType : std::uint8_t {
    None = 0,
    Int8,
    Int16,
    Int32,
    Int64,
    Uint8,
    Uint16,
    Uint32,
    Uint64,
    Float32,
    Float64,
    Bool,
    CharUtf8,
    CharUtf32,
    CharAscii,
    Array,
    StringKeyMap,
    StructRef,
    Ext,
};

}  // namespace kvlang::runtimec2::vmbin
