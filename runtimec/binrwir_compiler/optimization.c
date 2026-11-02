/* 已地址化 BinRWIR 通用派生优化的失败封闭骨架。 */

#include "binrwir_compiler/optimization.h"

#include "binrwir/execution_plan_internal.h"

runtimec_status_t binrwir_compiler_build_execution_plan(
    const runtimec_allocator_t *allocator,
    const binrwir_program_t *canonical_program,
    uint64_t plan_generation,
    uint32_t compiler_revision,
    binrwir_execution_plan_t **output_execution_plan,
    runtimec_error_t *error) {
    (void)plan_generation;
    (void)compiler_revision;
    (void)error;
    if (output_execution_plan != NULL) {
        *output_execution_plan = NULL;
    }
    if (!runtimec_allocator_is_valid(allocator) || canonical_program == NULL ||
        output_execution_plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
