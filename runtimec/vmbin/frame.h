#ifndef VMBIN_FRAME_H
#define VMBIN_FRAME_H

/*
 * 组件：vmbin/frame
 * 作用：保存可从 kvspace 重建的执行帧镜像。
 * 联系：engine route 创建 Frame；interpreter 与 vmjit 消费。
 * 约束：Frame 不是持久事实源，每条源指令边界必须可观察。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "vmbin/binding.h"
#include "vmbin/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmbin_frame_flags {
    VMBIN_FRAME_FLAG_NONE = 0,
    VMBIN_FRAME_FLAG_INITIALIZED = 1U << 0,
    VMBIN_FRAME_FLAG_OBSERVABLE = 1U << 1,
    VMBIN_FRAME_FLAG_RESUMED = 1U << 2
} vmbin_frame_flags_t;

typedef struct vmbin_frame {
    const vmbin_program_t *program;
    struct vmbin_frame *parent;
    vmbin_cell_t *cells;
    size_t cell_count;
    vmbin_binding_t *bindings;
    size_t binding_count;
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t logical_program_counter;
    uint32_t flags;
} vmbin_frame_t;

#define VMBIN_FRAME_ALIGNMENT ((uint32_t)_Alignof(vmbin_frame_t))

runtimec_status_t vmbin_frame_initialize(
    vmbin_frame_t *frame, const vmbin_program_t *program,
    vmbin_frame_t *parent, vmbin_cell_t *cells, size_t cell_count,
    vmbin_binding_t *bindings, size_t binding_count,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter);
runtimec_status_t vmbin_frame_reset(vmbin_frame_t *frame);
runtimec_status_t vmbin_frame_destroy(vmbin_frame_t *frame);
runtimec_status_t vmbin_frame_lookup_cell(vmbin_frame_t *frame,
                                          uint32_t cell_identifier,
                                          vmbin_cell_t **output_cell);
runtimec_status_t vmbin_frame_lookup_binding(
    vmbin_frame_t *frame, uint32_t register_identifier,
    vmbin_binding_t **output_binding);
runtimec_status_t vmbin_frame_bind_register(
    vmbin_frame_t *frame, uint32_t register_identifier,
    uint32_t cell_identifier,
    vmbin_binding_access_t access);
runtimec_status_t vmbin_frame_materialize_register(
    vmbin_frame_t *frame, uint32_t register_identifier,
    vmbin_value_slot_t *output_value);

#ifdef __cplusplus
}
#endif

#endif
