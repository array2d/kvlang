
#include "vmkv/rwir_manifest.h"

#include <string.h>

static const vmkv_rwir_capability_t vmkv_rwir_capabilities
    [VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT] = {

#define RUNTIMEC_RWIR_OPERATION(identifier, operation_name, numeric_identifier, \
                                read_mask, write_mask,                         \
                                read_roles, write_roles,                       \
                                constraint_value, result_value, alias_value,    \
                                decisions)                                      \
    [identifier] = {                                                            \
        .operator_name = {operation_name, sizeof(operation_name) - 1U},          \
        .behavior_decisions = {decisions, sizeof(decisions) - 1U},              \
        .operation_identifier = numeric_identifier,                             \
        .read_parameter_type_set = read_mask,                                   \
        .write_parameter_type_set = write_mask,                                 \
        .constraint = constraint_value,                                         \
        .result_rule = result_value,                                            \
        .alias_policy = alias_value,                                            \
        .read_parameter_roles = {read_roles, sizeof(read_roles) - 1U},          \
        .write_parameter_roles = {write_roles, sizeof(write_roles) - 1U},       \
    },
#include "specification/rwir_catalog.def"
#undef RUNTIMEC_RWIR_OPERATION
};

#define RUNTIMEC_RWIR_ALIAS(surface, row)                          \
    {                                                              \
        .surface_name = {surface, sizeof(surface) - 1U},            \
        .capability_identifier = row,                               \
    },
#define RUNTIMEC_RWIR_SPECIALIZATION(surface, first_mask, second_mask, \
                                     inverted, row)
static const vmkv_rwir_alias_t vmkv_rwir_alias_table[] = {
#include "specification/rwir_alias_table.def"
};
#undef RUNTIMEC_RWIR_SPECIALIZATION
#undef RUNTIMEC_RWIR_ALIAS

#define RUNTIMEC_RWIR_ALIAS(surface, row)
#define RUNTIMEC_RWIR_SPECIALIZATION(surface, first_mask, second_mask, \
                                     inverted, row)                     \
    {                                                                   \
        .surface_name = {surface, sizeof(surface) - 1U},                 \
        .first_parameter_type_set = first_mask,                          \
        .second_parameter_type_set = second_mask,                        \
        .result_inverted = inverted,                                     \
        .capability_identifier = row,                                    \
    },
static const vmkv_rwir_specialization_t vmkv_rwir_specializations[] = {
#include "specification/rwir_alias_table.def"
};
#undef RUNTIMEC_RWIR_SPECIALIZATION
#undef RUNTIMEC_RWIR_ALIAS

static bool vmkv_rwir_view_equals(runtimec_string_view_t left,
                                  runtimec_string_view_t right) {
    return left.size == right.size &&
           (left.size == 0U || memcmp(left.data, right.data, left.size) == 0);
}

bool vmkv_rwir_type_set_is_single(uint32_t type_set) {
    if (type_set == VMKV_RWIR_TYPE_SET_NONE ||
        (type_set & VMKV_RWIR_TYPE_SET_RESERVED_MASK) != 0U) {
        return false;
    }
    return (type_set & (type_set - UINT32_C(1))) == 0U;
}

const vmkv_rwir_capability_t *vmkv_rwir_manifest_capability(
    vmkv_rwir_capability_identifier_t identifier) {
    if (identifier >= VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT) {
        return NULL;
    }
    return &vmkv_rwir_capabilities[identifier];
}

bool vmkv_rwir_manifest_find_operation_by_identifier(
    binrwir_operation_identifier_t operation_identifier,
    runtimec_string_view_t *output_name) {
    if (output_name != NULL) {
        *output_name = runtimec_string_view_make(NULL, 0U);
    }
    if (output_name == NULL) {
        return false;
    }
    
    for (size_t identifier = 0U;
         identifier < (size_t)VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT;
         ++identifier) {
        const vmkv_rwir_capability_t *capability =
            &vmkv_rwir_capabilities[identifier];
        if (capability->operation_identifier != operation_identifier ||
            capability->operator_name.size == 0U) {
            continue;
        }
        *output_name = capability->operator_name;
        return true;
    }
    return false;
}

bool vmkv_rwir_manifest_is_call(runtimec_string_view_t operation_name) {
    return vmkv_rwir_manifest_find_operation(operation_name) ==
           &vmkv_rwir_capabilities[RUNTIMEC_RWIR_CALL];
}

bool vmkv_rwir_manifest_is_rwfunc_call(
    runtimec_string_view_t operation_langtype) {
    static const char rwfunc_kind[] = "rwfunc";
    return operation_langtype.size == sizeof(rwfunc_kind) - 1U &&
           operation_langtype.data != NULL &&
           memcmp(operation_langtype.data, rwfunc_kind,
                  sizeof(rwfunc_kind) - 1U) == 0;
}

const vmkv_rwir_capability_t *vmkv_rwir_manifest_find_operation(
    runtimec_string_view_t surface_name) {
    if (!runtimec_string_view_is_valid(surface_name) ||
        surface_name.size == 0U) {
        return NULL;
    }
    const size_t count = sizeof(vmkv_rwir_alias_table) /
                         sizeof(vmkv_rwir_alias_table[0]);
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const vmkv_rwir_alias_t *record = &vmkv_rwir_alias_table[ordinal];
        if (!vmkv_rwir_view_equals(record->surface_name, surface_name)) {
            continue;
        }
        if (record->capability_identifier >=
            VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT) {
            return NULL;
        }
        return &vmkv_rwir_capabilities[record->capability_identifier];
    }
    return NULL;
}

vmkv_rwir_specialization_result_t vmkv_rwir_manifest_find_specialization(
    runtimec_string_view_t surface_name,
    const uint32_t *parameter_type_sets, size_t parameter_count,
    const vmkv_rwir_capability_t **output_capability) {
    if (output_capability != NULL) {
        *output_capability = NULL;
    }
    if (!runtimec_string_view_is_valid(surface_name) ||
        surface_name.size == 0U || parameter_type_sets == NULL ||
        parameter_count != 2U) {
        return VMKV_RWIR_SPECIALIZATION_RESULT_INVALID;
    }
    const size_t count = sizeof(vmkv_rwir_specializations) /
                         sizeof(vmkv_rwir_specializations[0]);
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const vmkv_rwir_specialization_t *record =
            &vmkv_rwir_specializations[ordinal];
        if (!vmkv_rwir_view_equals(record->surface_name, surface_name) ||
            (parameter_type_sets[0] & ~record->first_parameter_type_set) != 0U ||
            (parameter_type_sets[1] & ~record->second_parameter_type_set) !=
                0U) {
            continue;
        }
        if (record->capability_identifier >=
            VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT) {
            return VMKV_RWIR_SPECIALIZATION_RESULT_INVALID;
        }
        if (output_capability != NULL) {
            *output_capability =
                &vmkv_rwir_capabilities[record->capability_identifier];
        }
        return record->result_inverted
                   ? VMKV_RWIR_SPECIALIZATION_RESULT_INVERTED
                   : VMKV_RWIR_SPECIALIZATION_RESULT_SELECTED;
    }
    return VMKV_RWIR_SPECIALIZATION_RESULT_NONE;
}
