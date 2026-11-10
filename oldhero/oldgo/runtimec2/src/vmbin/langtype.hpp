#pragma once

#include <cstdint>
#include <string_view>

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

// kindexpr → 标量 LangType/宽度。只认无 dims 前缀的数值/布尔标量；
// 数组（`[8,8]float64`）、指针（`*…`）、容器（`[int64]·int64`）一律 false；
// **char 族也一律 false**：字符串的声明类型可能是裸 `char/utf8`（元素类型），
// 而值的完整 kindexpr 恒带 `[N]` 维度（如 `[2]char/utf8`）。静态缓存裸元素类型
// 会让 kindexpr 打印/数组消费者丢维度（xv_meta 回归），必须回退真实读取。
inline bool langtype_kind(std::string_view lt, LangType* kind,
                          std::uint8_t* width) noexcept {
    if (lt.empty() || lt.front() == '[')
        return false;
    struct Entry {
        const char* name;
        LangType kind;
        std::uint8_t width;
    };
    static constexpr Entry kTable[] = {
        {"bool", LangType::Bool, 1},
        {"int8", LangType::Int8, 1},
        {"int16", LangType::Int16, 2},
        {"int32", LangType::Int32, 4},
        {"int64", LangType::Int64, 8},
        {"uint8", LangType::Uint8, 1},
        {"uint16", LangType::Uint16, 2},
        {"uint32", LangType::Uint32, 4},
        {"uint64", LangType::Uint64, 8},
        {"float32", LangType::Float32, 4},
        {"float64", LangType::Float64, 8},
    };
    for (const Entry& e : kTable) {
        if (lt == e.name) {
            *kind = e.kind;
            *width = e.width;
            return true;
        }
    }
    return false;
}

// LangType → 规范 kindexpr 串；不可表示（None/Array/StringKeyMap/StructRef/Ext）→ nullptr。
// char 族不在此列：完整字符串类型恒带 `[N]` dims，不能由静态元素 kind 还原。
inline const char* langtype_name(LangType kind) noexcept {
    switch (kind) {
    case LangType::Int8: return "int8";
    case LangType::Int16: return "int16";
    case LangType::Int32: return "int32";
    case LangType::Int64: return "int64";
    case LangType::Uint8: return "uint8";
    case LangType::Uint16: return "uint16";
    case LangType::Uint32: return "uint32";
    case LangType::Uint64: return "uint64";
    case LangType::Float32: return "float32";
    case LangType::Float64: return "float64";
    case LangType::Bool: return "bool";
    default: return nullptr;
    }
}

}  // namespace kvlang::runtimec2::vmbin
