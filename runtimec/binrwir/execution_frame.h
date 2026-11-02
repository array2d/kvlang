#ifndef BINRWIR_EXECUTION_FRAME_H
#define BINRWIR_EXECUTION_FRAME_H

/*
 * 组件：binrwir/execution_frame
 * 作用：保存可从 kvspace 重建的执行帧镜像。
 * 联系：engine route 创建 Frame；vmbin 与 BinRWIR JIT 消费。
 * 约束：Frame 不是持久事实源，每条源指令边界必须可观察。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "binrwir/execution_binding.h"
#include "binrwir/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_execution_frame_flags {
    BINRWIR_EXECUTION_FRAME_FLAG_NONE = 0,
    BINRWIR_EXECUTION_FRAME_FLAG_INITIALIZED = 1U << 0,
    BINRWIR_EXECUTION_FRAME_FLAG_OBSERVABLE = 1U << 1,
    BINRWIR_EXECUTION_FRAME_FLAG_RESUMED = 1U << 2
} binrwir_execution_frame_flags_t;

typedef struct binrwir_execution_frame {
    const binrwir_program_t *program;
    struct binrwir_execution_frame *parent;
    binrwir_cell_t *cells;
    size_t cell_count;
    binrwir_binding_t *bindings;
    size_t binding_count;
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t logical_program_counter;
    uint32_t flags;
} binrwir_execution_frame_t;

#define BINRWIR_EXECUTION_FRAME_ALIGNMENT \
    ((uint32_t)_Alignof(binrwir_execution_frame_t))

runtimec_status_t binrwir_execution_frame_initialize(
    binrwir_execution_frame_t *frame, const binrwir_program_t *program,
    binrwir_execution_frame_t *parent, binrwir_cell_t *cells,
    size_t cell_count,
    binrwir_binding_t *bindings, size_t binding_count,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter);
runtimec_status_t binrwir_execution_frame_reset(
    binrwir_execution_frame_t *frame);
runtimec_status_t binrwir_execution_frame_destroy(
    binrwir_execution_frame_t *frame);
runtimec_status_t binrwir_execution_frame_lookup_cell(
    binrwir_execution_frame_t *frame, uint32_t cell_identifier,
    binrwir_cell_t **output_cell);
runtimec_status_t binrwir_execution_frame_lookup_binding(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    binrwir_binding_t **output_binding);
runtimec_status_t binrwir_execution_frame_bind_register(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    uint32_t cell_identifier,
    binrwir_binding_access_t access);
runtimec_status_t binrwir_execution_frame_materialize_register(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    binrwir_value_slot_t *output_value);

#ifdef __cplusplus
}
#endif

#endif
