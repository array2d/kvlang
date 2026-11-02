/*
 * 组件：binrwir/argument 的实现
 * 作用：地址化参数的构造与校验入口；真实地址解析尚未实现。
 * 约束：所有输出先进入零地址无效状态，动态 key 不得伪装为已解析。
 */

#include "binrwir/argument.h"

#include <stddef.h>
#include <string.h>

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, uint64_t body_address,
    binrwir_argument_t *output_argument) {
    if (output_argument != NULL) {
        memset(output_argument, 0, sizeof(*output_argument));
    }
    if (output_argument == NULL ||
        argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        body_address == BINRWIR_BODY_ADDRESS_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument) {
    if (argument == NULL ||
        argument->argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        argument->body_address == BINRWIR_BODY_ADDRESS_NONE ||
        argument->reserved_32_bits != 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
