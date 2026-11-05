/*
 * 组件：binrwir/argument 的实现
 * 作用：按引用种类构造与校验参数项；只有静态 key 携带 body 地址。
 * 约束：所有输出先进入无效状态。槽号只属于参数/局部引用，payload 只属于
 *       静态地址与字面量池序号；两者不得互相顶替，避免 payload 被当地址用。
 */

#include "binrwir/argument.h"

#include <stddef.h>
#include <string.h>

static int binrwir_reference_kind_is_known(uint8_t reference_kind) {
    return reference_kind == BINRWIR_REFERENCE_PARAMETER_READ ||
           reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE ||
           reference_kind == BINRWIR_REFERENCE_LOCAL ||
           reference_kind == BINRWIR_REFERENCE_STATIC ||
           reference_kind == BINRWIR_REFERENCE_LITERAL ||
           reference_kind == BINRWIR_REFERENCE_FUNCTION;
}

/* 按 kind 分支：槽引用不带 payload，payload 引用不带 slot。 */
static int binrwir_argument_shape_is_invalid(
    uint8_t reference_kind, uint16_t slot, uint64_t payload) {
    switch (reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_FUNCTION:
        return payload != 0U;
    case BINRWIR_REFERENCE_STATIC:
        /*
         * slot 是符号池下标（key 文本），任意值都留待 program 层查池。
         *
         * payload 允许 NONE：**写目标**的 key 允许在编码期尚不存在——地址由
         * 执行期按 key 解析（不存在则创建）。读目标的 key 不存在仍是真实错误，
         * 由值访问层拒绝；本函数不区分读/写，只保证结构自洽。
         */
        return 0;
    case BINRWIR_REFERENCE_LITERAL:
        return slot != 0U;
    default:
        return 1;
    }
}

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, binrwir_reference_kind_t reference_kind,
    uint16_t slot, uint64_t payload,
    binrwir_argument_t *output_argument) {
    if (output_argument == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(output_argument, 0, sizeof(*output_argument));
    if (argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        !binrwir_reference_kind_is_known((uint8_t)reference_kind) ||
        binrwir_argument_shape_is_invalid((uint8_t)reference_kind, slot,
                                          payload)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_argument->argument_index = argument_index;
    output_argument->reference_kind = (uint8_t)reference_kind;
    output_argument->reserved_8_bits = 0U;
    output_argument->slot = slot;
    output_argument->payload = payload;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument) {
    if (argument == NULL ||
        argument->argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        argument->reserved_8_bits != 0U ||
        !binrwir_reference_kind_is_known(argument->reference_kind) ||
        binrwir_argument_shape_is_invalid(argument->reference_kind,
                                          argument->slot,
                                          argument->payload)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
