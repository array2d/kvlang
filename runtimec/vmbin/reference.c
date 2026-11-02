/*
 * 组件：vmbin/reference 的实现
 * 作用：引用创建、校验与读取入口；业务语义尚未实现。
 * 约束：所有输出先进入明确无效状态，再返回未实现。
 */

#include "vmbin/reference.h"

#include <stddef.h>

runtimec_status_t vmbin_reference_create(
    vmbin_reference_kind_t kind, uint32_t identifier,
    vmbin_reference_t *output_reference) {
    if (output_reference != NULL) {
        *output_reference = VMBIN_REFERENCE_INVALID;
    }
    if (output_reference == NULL || kind <= VMBIN_REFERENCE_KIND_INVALID ||
        kind > VMBIN_REFERENCE_KIND_SYMBOL ||
        identifier > VMBIN_REFERENCE_IDENTIFIER_MASK) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_reference_validate(vmbin_reference_t reference) {
    if (reference == VMBIN_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_reference_get_kind(
    vmbin_reference_t reference, vmbin_reference_kind_t *output_kind) {
    if (output_kind != NULL) {
        *output_kind = VMBIN_REFERENCE_KIND_INVALID;
    }
    if (output_kind == NULL || reference == VMBIN_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_reference_get_identifier(
    vmbin_reference_t reference, uint32_t *output_identifier) {
    if (output_identifier != NULL) {
        *output_identifier = UINT32_MAX;
    }
    if (output_identifier == NULL || reference == VMBIN_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
