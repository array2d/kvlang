/*
 * 组件：binrwir/execution_plan 的实现
 * 作用：验证与查询 execution plan，并释放它的 owned storage。
 * 联系：plan 由 binrwir_compiler/optimization 构造；vmbin 与 BinRWIR JIT 只读。
 * 约束：plan 不重复编码 instruction、不解析或缓存地址、不建立中间值存储；本组件
 *       只判形状与身份，不搬动任何数组。
 */

#include "binrwir/execution_plan.h"

#include <string.h>

#include "binrwir/argument.h"
#include "binrwir/execution_plan_internal.h"

static void binrwir_execution_plan_identity_invalidate(
    binrwir_execution_plan_identity_t *identity) {
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
        identity->snapshot_generation = UINT64_MAX;
        identity->program_generation = UINT64_MAX;
        identity->plan_generation = UINT64_MAX;
        identity->binrwir_binary_interface_generation = UINT32_MAX;
        identity->compiler_revision = UINT32_MAX;
    }
}

/* identity 可消费当且仅当每个字段都是显式值，且代次就是当前二进制接口代次。 */
static int binrwir_execution_plan_identity_is_usable(
    const binrwir_execution_plan_identity_t *identity) {
    return identity->snapshot_generation != UINT64_MAX &&
           identity->program_generation != UINT64_MAX &&
           identity->plan_generation != UINT64_MAX &&
           identity->binrwir_binary_interface_generation ==
               BINRWIR_BINARY_INTERFACE_GENERATION &&
           identity->compiler_revision != UINT32_MAX;
}

/* 规范形状：count 为零时指针必须是 NULL，count 非零时指针必须有值。 */
static int binrwir_execution_plan_array_shape_is_invalid(const void *array,
                                                         size_t count) {
    if (count == 0U) {
        return array != NULL;
    }
    return array == NULL;
}

/* flags 只允许四个已定义位；reserved_32_bits 必须为零。 */
static int binrwir_execution_plan_flags_are_invalid(uint32_t flags) {
    const uint32_t known_flags =
        (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_HAS_TYPE_SPECIALIZATION |
        (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_HAS_DISPATCH_FUSION |
        (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_HAS_CONTROL_FLOW |
        (uint32_t)BINRWIR_EXECUTION_PLAN_FLAG_HAS_SOURCE_MAPPING;

    return (flags & ~known_flags) != 0U;
}

/* owned storage 是 plan 私有派生数组的唯一来源，尺寸与对齐必须自洽。 */
static int binrwir_execution_plan_storage_is_invalid(
    const binrwir_execution_plan_t *plan) {
    if (plan->owned_storage_size == 0U) {
        return plan->owned_storage != NULL ||
               plan->owned_storage_alignment != 0U;
    }
    return plan->owned_storage == NULL ||
           plan->owned_storage_alignment == 0U ||
           (plan->owned_storage_alignment &
            (plan->owned_storage_alignment - 1U)) != 0U;
}

runtimec_status_t binrwir_execution_plan_validate(
    const binrwir_execution_plan_t *plan) {
    if (plan == NULL || !runtimec_allocator_is_valid(&plan->allocator) ||
        binrwir_execution_plan_storage_is_invalid(plan)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (!binrwir_execution_plan_identity_is_usable(&plan->view.identity) ||
        plan->view.program == NULL ||
        binrwir_execution_plan_flags_are_invalid(plan->view.flags) ||
        plan->view.reserved_32_bits != 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_execution_plan_array_shape_is_invalid(
            plan->view.type_specializations,
            plan->view.type_specialization_count) ||
        binrwir_execution_plan_array_shape_is_invalid(
            plan->view.dispatch_fusions, plan->view.dispatch_fusion_count) ||
        binrwir_execution_plan_array_shape_is_invalid(
            plan->view.control_flow_blocks,
            plan->view.control_flow_block_count) ||
        binrwir_execution_plan_array_shape_is_invalid(
            plan->view.control_flow_successors,
            plan->view.control_flow_successor_count) ||
        binrwir_execution_plan_array_shape_is_invalid(
            plan->view.source_mappings, plan->view.source_mapping_count)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_execution_plan_get_view(
    const binrwir_execution_plan_t *plan,
    binrwir_execution_plan_view_t *output_view) {
    runtimec_status_t status;

    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        binrwir_execution_plan_identity_invalidate(&output_view->identity);
    }
    if (output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_execution_plan_validate(plan);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    *output_view = plan->view;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_execution_plan_destroy(
    binrwir_execution_plan_t *plan) {
    runtimec_allocator_t allocator;

    if (plan == NULL || !runtimec_allocator_is_valid(&plan->allocator) ||
        binrwir_execution_plan_storage_is_invalid(plan)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = plan->allocator;
    if (plan->owned_storage != NULL) {
        (void)runtimec_allocator_deallocate(
            &allocator, plan->owned_storage, plan->owned_storage_size,
            plan->owned_storage_alignment);
    }
    (void)runtimec_allocator_deallocate(&allocator, plan, sizeof(*plan),
                                        _Alignof(binrwir_execution_plan_t));
    return RUNTIMEC_STATUS_SUCCESS;
}
