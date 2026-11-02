#ifndef VMBIN_VALUE_H
#define VMBIN_VALUE_H

/*
 * 组件：vmbin/value
 * 作用：vmbin 与 vmjit 共享的 32 字节目标中立值槽。
 * 联系：Binding、Frame、interpreter 与 vmjit 共同使用。
 * 约束：布局是内部二进制接口，不是持久格式。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmbin_value_tag {
    VMBIN_VALUE_INVALID = 0,
    VMBIN_VALUE_EMPTY,
    VMBIN_VALUE_BOOL,
    VMBIN_VALUE_SIGNED,
    VMBIN_VALUE_UNSIGNED,
    VMBIN_VALUE_FLOAT,
    VMBIN_VALUE_REFERENCE,
    VMBIN_VALUE_ARRAY,
    VMBIN_VALUE_DYNAMIC
} vmbin_value_tag_t;

typedef enum vmbin_value_flags {
    VMBIN_VALUE_FLAG_NONE = 0,
    VMBIN_VALUE_FLAG_OWNED = 1U << 0,
    VMBIN_VALUE_FLAG_BORROWED = 1U << 1,
    VMBIN_VALUE_FLAG_NULLABLE = 1U << 2,
    VMBIN_VALUE_FLAG_DIRTY = 1U << 3,
    VMBIN_VALUE_FLAG_PINNED = 1U << 4
} vmbin_value_flags_t;

typedef struct vmbin_value_slot {
    uint64_t payload_low;   /* 标量位或句柄标识符，低 64 位 */
    uint64_t payload_high;  /* 保留/高 64 位 */
    uint32_t type_identifier; /* canonical 类型或形状标识符 */
    uint16_t tag;           /* vmbin_value_tag_t */
    uint16_t flags;         /* owned/borrowed/nullable/dirty/pinned */
    uint32_t generation;    /* 句柄/借用的世代守卫 */
    uint32_t reserved_32_bits;
} vmbin_value_slot_t;

_Static_assert(sizeof(vmbin_value_slot_t) == 32U,
               "vmbin value slot must remain 32 bytes");
_Static_assert(offsetof(vmbin_value_slot_t, payload_low) == 0U,
               "vmbin value payload must start at offset zero");
_Static_assert(offsetof(vmbin_value_slot_t, generation) == 24U,
               "vmbin value generation offset is contractual");

#ifdef __cplusplus
}
#endif

#endif
