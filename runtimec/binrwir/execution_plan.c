/* execution plan 生命周期与查询的失败封闭骨架。 */

#include "binrwir/execution_plan.h"
#include "binrwir/execution_plan_internal.h"

#include <string.h>

runtimec_status_t binrwir_execution_plan_validate(
    const binrwir_execution_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_execution_plan_get_view(
    const binrwir_execution_plan_t *plan,
    binrwir_execution_plan_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        output_view->identity.snapshot_generation = UINT64_MAX;
        output_view->identity.program_generation = UINT64_MAX;
        output_view->identity.plan_generation = UINT64_MAX;
        output_view->identity.binrwir_binary_interface_generation = UINT32_MAX;
        output_view->identity.compiler_revision = UINT32_MAX;
    }
    if (plan == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_execution_plan_destroy(
    binrwir_execution_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
