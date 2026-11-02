#ifndef BINRWIR_VALUE_H
#define BINRWIR_VALUE_H

/*
 * 组件：binrwir/value
 * 作用：vmbin 与 vmjit 共享的 32 字节目标中立值槽。
 * 联系：execution binding、execution frame、vmbin 与 vmjit 共同使用。
 * 约束：布局是内部二进制接口，不是持久格式。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_value_tag {
    BINRWIR_VALUE_INVALID = 0,
    BINRWIR_VALUE_EMPTY,
    BINRWIR_VALUE_BOOL,
    BINRWIR_VALUE_SIGNED,
    BINRWIR_VALUE_UNSIGNED,
    BINRWIR_VALUE_FLOAT,
    BINRWIR_VALUE_REFERENCE,
    BINRWIR_VALUE_ARRAY,
    BINRWIR_VALUE_DYNAMIC
} binrwir_value_tag_t;

typedef enum binrwir_value_flags {
    BINRWIR_VALUE_FLAG_NONE = 0,
    BINRWIR_VALUE_FLAG_OWNED = 1U << 0,
    BINRWIR_VALUE_FLAG_BORROWED = 1U << 1,
    BINRWIR_VALUE_FLAG_NULLABLE = 1U << 2,
    BINRWIR_VALUE_FLAG_DIRTY = 1U << 3,
    BINRWIR_VALUE_FLAG_PINNED = 1U << 4
} binrwir_value_flags_t;

typedef struct binrwir_value_slot {
    uint64_t payload_low;   /* 标量位或句柄标识符，低 64 位 */
    uint64_t payload_high;  /* 保留/高 64 位 */
    uint32_t type_identifier; /* canonical 类型或形状标识符 */
    uint16_t tag;           /* binrwir_value_tag_t */
    uint16_t flags;         /* owned/borrowed/nullable/dirty/pinned */
    uint32_t generation;    /* 句柄/借用的世代守卫 */
    uint32_t reserved_32_bits;
} binrwir_value_slot_t;

_Static_assert(sizeof(binrwir_value_slot_t) == 32U,
               "binrwir value slot must remain 32 bytes");
_Static_assert(offsetof(binrwir_value_slot_t, payload_low) == 0U,
               "binrwir value payload must start at offset zero");
_Static_assert(offsetof(binrwir_value_slot_t, generation) == 24U,
               "binrwir value generation offset is contractual");

#ifdef __cplusplus
}
#endif

#endif
