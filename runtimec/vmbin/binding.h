#ifndef VMBIN_BINDING_H
#define VMBIN_BINDING_H

/*
 * 组件：vmbin/binding
 * 作用：Binding 将一个有方向的寄存器映射到 canonical Cell。
 * 联系：Frame 持有 Cell 与 Binding；call 按签名检查显式别名。
 * 约束：不同读写寄存器可以指向同一 Cell；Cell 可从 kvspace 重建。
 */

#include <stdint.h>

#include "contract/status.h"
#include "vmbin/value.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_CELL_IDENTIFIER_NONE UINT32_MAX
#define VMBIN_REGISTER_IDENTIFIER_NONE UINT32_MAX

typedef enum vmbin_cell_state {
    VMBIN_CELL_UNKNOWN = 0,
    VMBIN_CELL_ABSENT = 1,
    VMBIN_CELL_PRESENT = 2,
    VMBIN_CELL_INVALID = 3
} vmbin_cell_state_t;

typedef enum vmbin_cell_flags {
    VMBIN_CELL_FLAG_NONE = 0,
    VMBIN_CELL_FLAG_DIRTY = 1U << 0,
    VMBIN_CELL_FLAG_MATERIALIZED = 1U << 1,
    VMBIN_CELL_FLAG_PINNED = 1U << 2,
    VMBIN_CELL_FLAG_BORROWED = 1U << 3
} vmbin_cell_flags_t;

typedef enum vmbin_binding_access {
    VMBIN_BINDING_ACCESS_NONE = 0,
    VMBIN_BINDING_ACCESS_READ = 1U << 0,
    VMBIN_BINDING_ACCESS_WRITE = 1U << 1
} vmbin_binding_access_t;

typedef enum vmbin_binding_flags {
    VMBIN_BINDING_FLAG_NONE = 0,
    VMBIN_BINDING_FLAG_PARAMETER = 1U << 0,
    VMBIN_BINDING_FLAG_LOCAL = 1U << 1
} vmbin_binding_flags_t;

typedef struct vmbin_cell {
    vmbin_value_slot_t value;      /* staged 值（32 字节目标中立槽） */
    uint64_t storage_identifier;   /* canonical logical storage 身份 */
    uint64_t storage_generation;   /* 外部写/relocation 后变化 */
    uint64_t mutation_version;     /* 写后递增，读缓存据此失效 */
    uint16_t state;                /* vmbin_cell_state_t */
    uint16_t flags;                /* vmbin_cell_flags_t（dirty/pinned 等） */
    uint32_t reserved_32_bits;
} vmbin_cell_t;

typedef struct vmbin_binding {
    vmbin_cell_t *cell;            /* canonical Cell；可被多个 Binding 共享 */
    uint64_t observed_version;     /* 上次读到的 mutation_version */
    uint32_t register_identifier;  /* 程序中有方向的寄存器标识符 */
    uint16_t access;               /* vmbin_binding_access_t */
    uint16_t flags;                /* vmbin_binding_flags_t */
} vmbin_binding_t;

runtimec_status_t vmbin_cell_reset(vmbin_cell_t *cell);
runtimec_status_t vmbin_cell_invalidate(vmbin_cell_t *cell);
runtimec_status_t vmbin_cell_query(const vmbin_cell_t *cell,
                                   vmbin_value_slot_t *output_value,
                                   uint64_t *output_mutation_version);
runtimec_status_t vmbin_binding_reset(vmbin_binding_t *binding);
runtimec_status_t vmbin_binding_bind(vmbin_binding_t *binding,
                                     uint32_t register_identifier,
                                     vmbin_binding_access_t access,
                                     vmbin_cell_t *cell);
runtimec_status_t vmbin_binding_read(vmbin_binding_t *binding,
                                     vmbin_value_slot_t *output_value);
runtimec_status_t vmbin_binding_write(vmbin_binding_t *binding,
                                      const vmbin_value_slot_t *value);
runtimec_status_t vmbin_binding_invalidate(vmbin_binding_t *binding);
_Static_assert(sizeof(vmbin_cell_t) == 64U,
               "vmbin cells must remain 64 bytes");

#ifdef __cplusplus
}
#endif

#endif
