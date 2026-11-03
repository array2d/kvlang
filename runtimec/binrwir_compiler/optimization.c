/*
 * 组件：BinRWIR Compiler optimization 的实现
 * 作用：从已地址化的 canonical Program 派生只读 execution plan。本阶段只派生
 *       source logical program counter 映射——它是指令标识符与 source pc 之间
 *       唯一的对应关系，供恢复与观察使用；类型特化已在编码期完成，dispatch
 *       融合与控制流派生属于 JIT 阶段，本阶段不产出。
 * 联系：只由 binrwir_compiler/compiler.c 调用；vmbin 与 JIT 只读。
 * 约束：plan 不复制 instruction、不解析或缓存地址、不建立中间值存储；它借用
 *       调用方的 Program，生命周期由 compilation 保证。
 */

#include "binrwir_compiler/optimization.h"

#include <string.h>

#include "binrwir/execution_plan_internal.h"
#include "contract/error.h"

static runtimec_status_t binrwir_compiler_optimization_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t binrwir_compiler_build_execution_plan(
    const runtimec_allocator_t *allocator,
    const binrwir_program_t *canonical_program, uint64_t plan_generation,
    uint32_t compiler_revision,
    binrwir_execution_plan_t **output_execution_plan,
    runtimec_error_t *error) {
    binrwir_program_view_t program_view;
    binrwir_source_program_counter_mapping_t *source_mappings = NULL;
    binrwir_execution_plan_t *plan = NULL;
    size_t instruction_count;
    size_t mapping_ordinal;

    if (output_execution_plan != NULL) {
        *output_execution_plan = NULL;
    }
    if (!runtimec_allocator_is_valid(allocator) || canonical_program == NULL ||
        output_execution_plan == NULL || compiler_revision == 0U ||
        plan_generation == UINT64_MAX) {
        return binrwir_compiler_optimization_fail(
            error, RUNTIMEC_STATUS_INVALID, 1,
            "invalid execution plan request");
    }
    if (binrwir_program_get_view(canonical_program, &program_view) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return binrwir_compiler_optimization_fail(
            error, RUNTIMEC_STATUS_INVALID, 2, "canonical program is not usable");
    }
    instruction_count = program_view.instruction_count;
    if (instruction_count != 0U) {
        source_mappings = (binrwir_source_program_counter_mapping_t *)
            runtimec_allocator_allocate(
                allocator,
                sizeof(*source_mappings) * instruction_count,
                _Alignof(binrwir_source_program_counter_mapping_t));
        if (source_mappings == NULL) {
            return binrwir_compiler_optimization_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 3,
                "source mapping allocation failed");
        }
        /*
         * 本阶段的映射是恒等映射：第 i 条指令自成一段。它不是占位——vmbin 的
         * 稠密游标与 source pc 就是靠这张表对应，恢复时只认它。
         */
        for (mapping_ordinal = 0U; mapping_ordinal < instruction_count;
             ++mapping_ordinal) {
            binrwir_instruction_view_t instruction;

            if (binrwir_program_get_instruction(canonical_program,
                                                (uint32_t)mapping_ordinal,
                                                &instruction) !=
                RUNTIMEC_STATUS_SUCCESS) {
                (void)runtimec_allocator_deallocate(
                    allocator, source_mappings,
                    sizeof(*source_mappings) * instruction_count,
                    _Alignof(binrwir_source_program_counter_mapping_t));
                return binrwir_compiler_optimization_fail(
                    error, RUNTIMEC_STATUS_INVALID, 4,
                    "instruction is not readable");
            }
            source_mappings[mapping_ordinal].execution_dispatch_identifier =
                (uint32_t)mapping_ordinal;
            source_mappings[mapping_ordinal].source_logical_program_counter =
                instruction.source_logical_program_counter;
            source_mappings[mapping_ordinal].first_instruction_identifier =
                (uint32_t)mapping_ordinal;
            source_mappings[mapping_ordinal].instruction_count = 1U;
        }
    }

    plan = (binrwir_execution_plan_t *)runtimec_allocator_allocate(
        allocator, sizeof(*plan), _Alignof(binrwir_execution_plan_t));
    if (plan == NULL) {
        if (source_mappings != NULL) {
            (void)runtimec_allocator_deallocate(
                allocator, source_mappings,
                sizeof(*source_mappings) * instruction_count,
                _Alignof(binrwir_source_program_counter_mapping_t));
        }
        return binrwir_compiler_optimization_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 5,
            "execution plan allocation failed");
    }
    memset(plan, 0, sizeof(*plan));
    plan->allocator = *allocator;
    plan->view.identity.snapshot_generation =
        program_view.identity.snapshot_generation;
    plan->view.identity.program_generation =
        program_view.identity.program_generation;
    plan->view.identity.plan_generation = plan_generation;
    plan->view.identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    plan->view.identity.compiler_revision = compiler_revision;
    plan->view.program = canonical_program;
    plan->view.source_mappings = source_mappings;
    plan->view.source_mapping_count = instruction_count;
    plan->view.flags = instruction_count != 0U
                           ? (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_HAS_SOURCE_MAPPING
                           : (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_NONE;
    if (instruction_count != 0U) {
        plan->owned_storage = source_mappings;
        plan->owned_storage_size = sizeof(*source_mappings) * instruction_count;
        plan->owned_storage_alignment =
            _Alignof(binrwir_source_program_counter_mapping_t);
    }

    if (binrwir_execution_plan_validate(plan) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_execution_plan_destroy(plan);
        return binrwir_compiler_optimization_fail(
            error, RUNTIMEC_STATUS_INVALID, 6,
            "derived execution plan is not usable");
    }
    *output_execution_plan = plan;
    return RUNTIMEC_STATUS_SUCCESS;
}
