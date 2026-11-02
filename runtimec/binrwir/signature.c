/*
 * 组件：binrwir/signature 的实现
 * 作用：签名校验与别名策略查询；当前明确返回未实现。
 * 约束：默认输出仅是无效前置状态，不表示已经证明不重叠。
 */

#include "binrwir/signature.h"

runtimec_status_t
binrwir_signature_validate(const binrwir_signature_view_t *signature) {
    if (signature == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (signature->read_parameter_count > UINT32_MAX ||
        signature->write_parameter_count > UINT32_MAX ||
        signature->alias_rule_count > UINT32_MAX ||
        (signature->read_parameter_count != 0U &&
         signature->read_parameter_argument_indices == NULL) ||
        (signature->write_parameter_count != 0U &&
         signature->write_parameter_argument_indices == NULL) ||
        (signature->alias_rule_count != 0U &&
         signature->alias_rules == NULL)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_signature_get_alias_policy(
    const binrwir_signature_view_t *signature, uint32_t read_parameter_index,
    uint32_t write_parameter_index, binrwir_alias_policy_t *output_policy) {
    if (output_policy != NULL) {
        *output_policy = BINRWIR_ALIAS_POLICY_INVALID;
    }
    if (signature == NULL || output_policy == NULL ||
        read_parameter_index >= signature->read_parameter_count ||
        write_parameter_index >= signature->write_parameter_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
