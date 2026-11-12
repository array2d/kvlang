
#include "binrwir/signature.h"

#include <string.h>

#define BINRWIR_SIGNATURE_ARGUMENT_INDEX_NONE UINT32_MAX

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

static runtimec_status_t binrwir_signature_validate_with(
    const binrwir_signature_view_t *signature, void *context,
    bool (*empty_type_is_legal)(void *context, int is_write, size_t ordinal)) {
    if (signature == NULL || binrwir_signature_shape_is_invalid(signature)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_argument_index_list_is_invalid(
            signature->read_parameter_argument_indices,
            signature->read_parameter_count) ||
        binrwir_argument_index_list_is_invalid(
            signature->write_parameter_argument_indices,
            signature->write_parameter_count)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (int is_write = 0; is_write <= 1; ++is_write) {
        const runtimec_string_view_t *types =
            is_write != 0 ? signature->write_parameter_types
                          : signature->read_parameter_types;
        const size_t count = is_write != 0 ? signature->write_parameter_count
                                           : signature->read_parameter_count;

        for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
            if (binrwir_signature_validate_parameter_type(types[ordinal]) ==
                RUNTIMEC_STATUS_SUCCESS) {
                continue;
            }
            
            if (types[ordinal].size != 0U || empty_type_is_legal == NULL ||
                !empty_type_is_legal(context, is_write, ordinal)) {
                return RUNTIMEC_STATUS_INVALID;
            }
        }
    }
    if (binrwir_alias_rules_are_invalid(
            signature->read_parameter_count, signature->write_parameter_count,
            signature->alias_rules, signature->alias_rule_count)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t
binrwir_signature_validate(const binrwir_signature_view_t *signature) {
    return binrwir_signature_validate_with(signature, NULL, NULL);
}

runtimec_status_t
binrwir_signature_validate_permitting_empty_types(
    const binrwir_signature_view_t *signature, void *context,
    bool (*empty_type_is_legal)(void *context, int is_write, size_t ordinal)) {
    if (empty_type_is_legal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return binrwir_signature_validate_with(signature, context,
                                           empty_type_is_legal);
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

    return RUNTIMEC_STATUS_UNSUPPORTED;
}
