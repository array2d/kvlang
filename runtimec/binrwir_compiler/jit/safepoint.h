#ifndef RUNTIMEC_BINRWIR_JIT_SAFEPOINT_H
#define RUNTIMEC_BINRWIR_JIT_SAFEPOINT_H

/*
 * 组件：binrwir_compiler/jit/safepoint
 * 作用：定义 JIT 已完成状态提交与未完成机器状态丢弃的恢复/可观察边界。
 * 联系：lowering_plan 以 register_allocation 的精确来源映射建立 safepoint；
 *       backend 发出写回序列；executor 只允许在写回完整成功后推进 Frame 的
 *       logical program counter。
 * 约束：每条 logical instruction completion 都是强制边界。只有完整完成指令
 *       产生的 dirty state 可以提交到原 body_address；当前未完成指令的
 *       speculative/partial register 或 spill 在 guard failure/error 时必须丢弃，
 *       禁止 materialize。提交任一项失败时 logical program counter 与 checkpoint
 *       均不得推进。recovery/resume 必须丢弃全部机器状态，只从 KVspace V.body
 *       重建；执行错误不得转交 VMBIN 重试。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_frame.h"
#include "binrwir_compiler/jit/register_allocation.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_jit_safepoint_boundary {
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_INVALID = 0,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION = 1,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_PAUSE = 2,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_HANDOFF = 3,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_CALL = 4,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_EXTERNAL_OBSERVATION = 5,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_RECOVERY = 6,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_RESUME = 7,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_GUARD_FAILURE = 8,
    BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR = 9
} binrwir_jit_safepoint_boundary_t;

typedef enum binrwir_jit_safepoint_action {
    BINRWIR_JIT_SAFEPOINT_ACTION_INVALID = 0,
    BINRWIR_JIT_SAFEPOINT_ACTION_COMMIT_COMPLETED_STATE = 1,
    BINRWIR_JIT_SAFEPOINT_ACTION_DISCARD_INCOMPLETE_STATE = 2
} binrwir_jit_safepoint_action_t;

typedef enum binrwir_jit_materialization_flags {
    BINRWIR_JIT_MATERIALIZATION_FLAG_NONE = 0,
    BINRWIR_JIT_MATERIALIZATION_FLAG_COMPLETED_DIRTY = 1U << 0
} binrwir_jit_materialization_flags_t;

typedef struct binrwir_jit_materialization_entry {
    binrwir_jit_body_origin_t body_origin;
    uint32_t location_identifier;
    uint16_t location_kind;
    uint16_t flags;
} binrwir_jit_materialization_entry_t;

typedef struct binrwir_jit_safepoint_descriptor {
    uint32_t logical_program_counter;
    /* discard action 的 entry count 必须为零，禁止写回未完成状态。 */
    uint32_t materialization_entry_offset;
    uint32_t materialization_entry_count;
    uint16_t boundary;
    uint16_t action;
} binrwir_jit_safepoint_descriptor_t;

typedef struct binrwir_jit_safepoint_plan binrwir_jit_safepoint_plan_t;

typedef struct binrwir_jit_safepoint_plan_view {
    const binrwir_jit_safepoint_descriptor_t *safepoints;
    size_t safepoint_count;
    /* 这里只允许完整完成 logical instruction 的 dirty state。 */
    const binrwir_jit_materialization_entry_t *materialization_entries;
    size_t materialization_entry_count;
} binrwir_jit_safepoint_plan_view_t;

runtimec_status_t binrwir_jit_safepoint_plan_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_jit_register_allocation_t *register_allocation,
    const runtimec_allocator_t *allocator,
    binrwir_jit_safepoint_plan_t **output_plan,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_safepoint_plan_get_view(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_plan_view_t *output_view);
runtimec_status_t binrwir_jit_safepoint_commit_completed_state(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_boundary_t boundary,
    uint32_t completed_logical_program_counter,
    uint32_t next_logical_program_counter,
    void *execution_state,
    binrwir_execution_frame_t *frame,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_safepoint_discard_incomplete_state(
    const binrwir_jit_safepoint_plan_t *plan,
    binrwir_jit_safepoint_boundary_t boundary,
    uint32_t current_logical_program_counter,
    void *execution_state,
    binrwir_execution_frame_t *frame,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_safepoint_plan_destroy(
    binrwir_jit_safepoint_plan_t *plan);

_Static_assert(sizeof(binrwir_jit_materialization_entry_t) == 24U,
               "JIT materialization entries must remain 24 bytes");
_Static_assert(sizeof(binrwir_jit_safepoint_descriptor_t) == 16U,
               "JIT safepoint descriptors must remain 16 bytes");

#ifdef __cplusplus
}
#endif

#endif
