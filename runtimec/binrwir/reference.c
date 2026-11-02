/*
 * 组件：binrwir/reference 的实现
 * 作用：引用创建、校验与读取入口；业务语义尚未实现。
 * 约束：所有输出先进入明确无效状态，再返回未实现。
 */

#include "binrwir/reference.h"

#include <stddef.h>

runtimec_status_t binrwir_reference_create(
    binrwir_reference_kind_t kind, uint32_t identifier,
    binrwir_reference_t *output_reference) {
    if (output_reference != NULL) {
        *output_reference = BINRWIR_REFERENCE_INVALID;
    }
    if (output_reference == NULL || kind <= BINRWIR_REFERENCE_KIND_INVALID ||
        kind > BINRWIR_REFERENCE_KIND_SYMBOL ||
        identifier > BINRWIR_REFERENCE_IDENTIFIER_MASK) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_reference_validate(binrwir_reference_t reference) {
    if (reference == BINRWIR_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_reference_get_kind(
    binrwir_reference_t reference, binrwir_reference_kind_t *output_kind) {
    if (output_kind != NULL) {
        *output_kind = BINRWIR_REFERENCE_KIND_INVALID;
    }
    if (output_kind == NULL || reference == BINRWIR_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_reference_get_identifier(
    binrwir_reference_t reference, uint32_t *output_identifier) {
    if (output_identifier != NULL) {
        *output_identifier = UINT32_MAX;
    }
    if (output_identifier == NULL || reference == BINRWIR_REFERENCE_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
