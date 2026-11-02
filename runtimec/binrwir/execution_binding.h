#ifndef BINRWIR_EXECUTION_BINDING_H
#define BINRWIR_EXECUTION_BINDING_H

/*
 * 组件：binrwir/execution_binding
 * 作用：Binding 将一个有方向的寄存器映射到 canonical Cell。
 * 联系：execution frame 持有 Cell 与 Binding；vmbin 与 vmjit 按同一
 *       签名检查显式别名。
 * 约束：不同读写寄存器可以指向同一 Cell；Cell 可从 kvspace 重建。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/value.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_CELL_IDENTIFIER_NONE UINT32_MAX
#define BINRWIR_REGISTER_IDENTIFIER_NONE UINT32_MAX

typedef enum binrwir_cell_state {
    BINRWIR_CELL_UNKNOWN = 0,
    BINRWIR_CELL_ABSENT = 1,
    BINRWIR_CELL_PRESENT = 2,
    BINRWIR_CELL_INVALID = 3
} binrwir_cell_state_t;

typedef enum binrwir_cell_flags {
    BINRWIR_CELL_FLAG_NONE = 0,
    BINRWIR_CELL_FLAG_DIRTY = 1U << 0,
    BINRWIR_CELL_FLAG_MATERIALIZED = 1U << 1,
    BINRWIR_CELL_FLAG_PINNED = 1U << 2,
    BINRWIR_CELL_FLAG_BORROWED = 1U << 3
} binrwir_cell_flags_t;

typedef enum binrwir_binding_access {
    BINRWIR_BINDING_ACCESS_NONE = 0,
    BINRWIR_BINDING_ACCESS_READ = 1U << 0,
    BINRWIR_BINDING_ACCESS_WRITE = 1U << 1
} binrwir_binding_access_t;

typedef enum binrwir_binding_flags {
    BINRWIR_BINDING_FLAG_NONE = 0,
    BINRWIR_BINDING_FLAG_PARAMETER = 1U << 0,
    BINRWIR_BINDING_FLAG_LOCAL = 1U << 1
} binrwir_binding_flags_t;

typedef struct binrwir_cell {
    binrwir_value_slot_t value;      /* staged 值（32 字节目标中立槽） */
    uint64_t storage_identifier;   /* canonical logical storage 身份 */
    uint64_t storage_generation;   /* 外部写/relocation 后变化 */
    uint64_t mutation_version;     /* 写后递增，读缓存据此失效 */
    uint16_t state;                /* binrwir_cell_state_t */
    uint16_t flags;                /* binrwir_cell_flags_t（dirty/pinned 等） */
    uint32_t reserved_32_bits;
} binrwir_cell_t;

typedef struct binrwir_binding {
    binrwir_cell_t *cell;            /* canonical Cell；可被多个 Binding 共享 */
    uint64_t observed_version;     /* 上次读到的 mutation_version */
    uint32_t register_identifier;  /* 程序中有方向的寄存器标识符 */
    uint16_t access;               /* binrwir_binding_access_t */
    uint16_t flags;                /* binrwir_binding_flags_t */
} binrwir_binding_t;

runtimec_status_t binrwir_cell_reset(binrwir_cell_t *cell);
runtimec_status_t binrwir_cell_invalidate(binrwir_cell_t *cell);
runtimec_status_t binrwir_cell_query(const binrwir_cell_t *cell,
                                   binrwir_value_slot_t *output_value,
                                   uint64_t *output_mutation_version);
runtimec_status_t binrwir_binding_reset(binrwir_binding_t *binding);
runtimec_status_t binrwir_binding_bind(binrwir_binding_t *binding,
                                     uint32_t register_identifier,
                                     binrwir_binding_access_t access,
                                     binrwir_cell_t *cell);
runtimec_status_t binrwir_binding_read(binrwir_binding_t *binding,
                                     binrwir_value_slot_t *output_value);
runtimec_status_t binrwir_binding_write(binrwir_binding_t *binding,
                                      const binrwir_value_slot_t *value);
runtimec_status_t binrwir_binding_invalidate(binrwir_binding_t *binding);
_Static_assert(sizeof(binrwir_cell_t) == 64U,
               "binrwir cells must remain 64 bytes");

#ifdef __cplusplus
}
#endif

#endif
