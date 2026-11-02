/*
 * 组件：binrwir/signature 的实现
 * 作用：校验读参、写参 argidx 列表、具体 langtype 与显式别名规则，并回答单对
 *       (read, write) 的策略。
 * 约束：未声明的 (read, write) 关系返回 UNSUPPORTED 与 INVALID 策略；输入本身
 *       不合法时返回 INVALID。两者都不猜测为不重叠。参数类型必须是唯一的
 *       具体 langtype：空类型与 union 一律 INVALID。
 */

#include "binrwir/signature.h"

#include <string.h>

#define BINRWIR_SIGNATURE_ARGUMENT_INDEX_NONE UINT32_MAX
/* langtype 文法里的并集分隔符（`int64|float64`）：BinRWIR 不接受。 */
#define BINRWIR_SIGNATURE_UNION_SEPARATOR '|'

static int binrwir_alias_policy_is_known(uint8_t policy) {
    return policy == BINRWIR_ALIAS_NO_ALIAS ||
           policy == BINRWIR_ALIAS_MAY_ALIAS ||
           policy == BINRWIR_ALIAS_MUST_ALIAS;
}

static int binrwir_alias_rule_flags_are_known(uint8_t flags) {
    return (flags & (uint8_t)~(uint8_t)BINRWIR_ALIAS_RULE_FLAG_PROJECTION) == 0U;
}

runtimec_status_t binrwir_signature_validate_alias_rule(
    const binrwir_alias_rule_t *rule) {
    if (rule == NULL || !binrwir_alias_policy_is_known(rule->policy) ||
        !binrwir_alias_rule_flags_are_known(rule->flags) ||
        rule->reserved_16_bits != 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* argidx 列表：每项必须是确定参数，且同一方向内不得重复声明。 */
static int binrwir_argument_index_list_is_invalid(const uint32_t *indices,
                                                 size_t count) {
    size_t outer_ordinal;
    size_t inner_ordinal;

    for (outer_ordinal = 0U; outer_ordinal < count; ++outer_ordinal) {
        if (indices[outer_ordinal] == BINRWIR_SIGNATURE_ARGUMENT_INDEX_NONE) {
            return 1;
        }
        for (inner_ordinal = outer_ordinal + 1U; inner_ordinal < count;
             ++inner_ordinal) {
            if (indices[outer_ordinal] == indices[inner_ordinal]) {
                return 1;
            }
        }
    }
    return 0;
}

/* 具体类型：每个参数恰好一个非空 langtype，出现并集分隔符即 union，拒绝。 */
runtimec_status_t binrwir_signature_validate_parameter_type(
    runtimec_string_view_t parameter_type) {
    if (parameter_type.size == 0U || parameter_type.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (memchr(parameter_type.data, BINRWIR_SIGNATURE_UNION_SEPARATOR,
               parameter_type.size) != NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static int binrwir_parameter_types_are_invalid(
    const runtimec_string_view_t *types, size_t count) {
    size_t type_ordinal;

    for (type_ordinal = 0U; type_ordinal < count; ++type_ordinal) {
        if (binrwir_signature_validate_parameter_type(types[type_ordinal]) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return 1;
        }
    }
    return 0;
}

/* 别名规则：两侧 parameter index 必须落在对应方向列表内（即该方向确有这个参数），
 * 策略与标志必须已知，且同一 (read, write) 只能声明一次。 */
static int binrwir_alias_rules_are_invalid(
    size_t read_count, size_t write_count,
    const binrwir_alias_rule_t *rules, size_t rule_count) {
    size_t outer_ordinal;
    size_t inner_ordinal;

    for (outer_ordinal = 0U; outer_ordinal < rule_count; ++outer_ordinal) {
        const binrwir_alias_rule_t *rule = &rules[outer_ordinal];

        if (rule->read_parameter_index >= read_count ||
            rule->write_parameter_index >= write_count ||
            binrwir_signature_validate_alias_rule(rule) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 1;
        }
        for (inner_ordinal = outer_ordinal + 1U; inner_ordinal < rule_count;
             ++inner_ordinal) {
            if (rules[inner_ordinal].read_parameter_index ==
                    rule->read_parameter_index &&
                rules[inner_ordinal].write_parameter_index ==
                    rule->write_parameter_index) {
                return 1;
            }
        }
    }
    return 0;
}

/* 指针与计数自洽：count 为零时指针可空，count 非零时指针必须有值。 */
static int binrwir_signature_shape_is_invalid(
    const binrwir_signature_view_t *signature) {
    return signature->read_parameter_count > UINT32_MAX ||
           signature->write_parameter_count > UINT32_MAX ||
           signature->alias_rule_count > UINT32_MAX ||
           (signature->read_parameter_count != 0U &&
            signature->read_parameter_argument_indices == NULL) ||
           (signature->write_parameter_count != 0U &&
            signature->write_parameter_argument_indices == NULL) ||
           (signature->read_parameter_count != 0U &&
            signature->read_parameter_types == NULL) ||
           (signature->write_parameter_count != 0U &&
            signature->write_parameter_types == NULL) ||
           (signature->alias_rule_count != 0U && signature->alias_rules == NULL);
}

runtimec_status_t
binrwir_signature_validate(const binrwir_signature_view_t *signature) {
    if (signature == NULL || binrwir_signature_shape_is_invalid(signature)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_argument_index_list_is_invalid(
            signature->read_parameter_argument_indices,
            signature->read_parameter_count) ||
        binrwir_argument_index_list_is_invalid(
            signature->write_parameter_argument_indices,
            signature->write_parameter_count) ||
        binrwir_parameter_types_are_invalid(signature->read_parameter_types,
                                            signature->read_parameter_count) ||
        binrwir_parameter_types_are_invalid(signature->write_parameter_types,
                                            signature->write_parameter_count) ||
        binrwir_alias_rules_are_invalid(
            signature->read_parameter_count, signature->write_parameter_count,
            signature->alias_rules, signature->alias_rule_count)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_signature_get_alias_policy(
    const binrwir_signature_view_t *signature, uint32_t read_parameter_index,
    uint32_t write_parameter_index, binrwir_alias_policy_t *output_policy) {
    runtimec_status_t status;
    size_t rule_ordinal;

    if (output_policy != NULL) {
        *output_policy = BINRWIR_ALIAS_POLICY_INVALID;
    }
    if (signature == NULL || output_policy == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_signature_validate(signature);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (read_parameter_index >= signature->read_parameter_count ||
        write_parameter_index >= signature->write_parameter_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (rule_ordinal = 0U; rule_ordinal < signature->alias_rule_count;
         ++rule_ordinal) {
        const binrwir_alias_rule_t *rule = &signature->alias_rules[rule_ordinal];

        if (rule->read_parameter_index == read_parameter_index &&
            rule->write_parameter_index == write_parameter_index) {
            *output_policy = (binrwir_alias_policy_t)rule->policy;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    /* 未声明的 (read, write) 关系不得被当作不重叠；签名本身合法，只是不提供该答案。 */
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
