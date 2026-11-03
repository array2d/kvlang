/*
 * 组件：vmkv/rwir_manifest 的实现
 * 作用：唯一一次展开 rwir_catalog.def 与 rwir_alias_table.def，得到能力行表、
 *       别名表与改选规则表；其余查询都在这些表上做线性查找。
 * 约束：表中不出现第二个签名来源；未注册名字返回未找到，不猜、不回退。
 */

#include "vmkv/rwir_manifest.h"

#include <string.h>

static const vmkv_rwir_capability_t vmkv_rwir_capabilities
    [VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT] = {
/*
 * 宏形参一律避开结构体字段名：`.字段` 里的标识符也会被预处理器替换，
 * 形参一旦与字段同名，designator 就会被参数文本吃掉。
 */
#define RUNTIMEC_RWIR_OPERATION(identifier, operation_name, numeric_identifier, \
                                read_mask, write_mask, constraint_value,        \
                                result_value, alias_value, decisions)           \
    [identifier] = {                                                            \
        .operator_name = {operation_name, sizeof(operation_name) - 1U},          \
        .behavior_decisions = {decisions, sizeof(decisions) - 1U},              \
        .operation_identifier = numeric_identifier,                             \
        .read_parameter_type_set = read_mask,                                   \
        .write_parameter_type_set = write_mask,                                 \
        .constraint = constraint_value,                                         \
        .result_rule = result_value,                                            \
        .alias_policy = alias_value,                                            \
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

/* 改选只对二元规则成立：两个操作数必须各自整体落进对应掩码。 */
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
