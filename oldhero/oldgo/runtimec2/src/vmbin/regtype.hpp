#pragma once

#include <cstdint>

#include "langtype.hpp"

namespace kvlang::runtimec2::vmbin {

// 单个寄存器（= 变量，按 tag 去重）的读/写类型合并状态（E 层，建段期使用）。
// 规则：所有写者都必须是同一已知标量类型才可信；任一写者无类型或类型冲突 → None。
// 无写者（只读变量/参数）则要求所有读槽类型一致；不一致 → None。
struct RegisterTypeState {
    bool write_seen = false;
    bool write_untyped = false;
    bool write_conflict = false;
    LangType write_kind = LangType::None;
    std::uint8_t write_width = 0;
    bool read_seen = false;
    bool read_conflict = false;
    LangType read_kind = LangType::None;
    std::uint8_t read_width = 0;

    void record(LangType kind, std::uint8_t width, bool write) noexcept {
        if (write) {
            if (kind == LangType::None) {
                write_untyped = true;
            } else if (!write_seen) {
                write_seen = true;
                write_kind = kind;
                write_width = width;
            } else if (write_kind != kind || write_width != width) {
                write_conflict = true;
            }
            return;
        }
        if (kind == LangType::None)
            return;
        if (!read_seen) {
            read_seen = true;
            read_kind = kind;
            read_width = width;
        } else if (read_kind != kind || read_width != width) {
            read_conflict = true;
        }
    }

    // 定稿：写者一致优先；否则只读一致；否则 None（回退 kvspaceGetHead）。
    LangType final_kind(std::uint8_t* width) const noexcept {
        if (write_seen && !write_untyped && !write_conflict) {
            *width = write_width;
            return write_kind;
        }
        if (!write_seen && read_seen && !read_conflict) {
            *width = read_width;
            return read_kind;
        }
        *width = 0;
        return LangType::None;
    }
};

}  // namespace kvlang::runtimec2::vmbin
