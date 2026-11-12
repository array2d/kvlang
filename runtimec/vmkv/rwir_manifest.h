#ifndef RUNTIMEC_VMKV_RWIR_MANIFEST_H
#define RUNTIMEC_VMKV_RWIR_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/instruction.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMKV_RWIR_TYPE_SET_NONE UINT32_C(0)
#define VMKV_RWIR_TYPE_SET_INT8 (UINT32_C(1) << 0)
#define VMKV_RWIR_TYPE_SET_INT16 (UINT32_C(1) << 1)
#define VMKV_RWIR_TYPE_SET_INT32 (UINT32_C(1) << 2)
#define VMKV_RWIR_TYPE_SET_INT64 (UINT32_C(1) << 3)
#define VMKV_RWIR_TYPE_SET_UINT8 (UINT32_C(1) << 4)
#define VMKV_RWIR_TYPE_SET_UINT16 (UINT32_C(1) << 5)
#define VMKV_RWIR_TYPE_SET_UINT32 (UINT32_C(1) << 6)
#define VMKV_RWIR_TYPE_SET_UINT64 (UINT32_C(1) << 7)
#define VMKV_RWIR_TYPE_SET_FLOAT32 (UINT32_C(1) << 8)
#define VMKV_RWIR_TYPE_SET_FLOAT64 (UINT32_C(1) << 9)
#define VMKV_RWIR_TYPE_SET_BOOLEAN (UINT32_C(1) << 10)
#define VMKV_RWIR_TYPE_SET_CHARACTER_UTF8 (UINT32_C(1) << 11)
#define VMKV_RWIR_TYPE_SET_CHARACTER_UTF32 (UINT32_C(1) << 12)
#define VMKV_RWIR_TYPE_SET_CHARACTER_ASCII (UINT32_C(1) << 13)
#define VMKV_RWIR_TYPE_SET_ARRAY (UINT32_C(1) << 14)
#define VMKV_RWIR_TYPE_SET_STRING_KEY_MAP (UINT32_C(1) << 15)
#define VMKV_RWIR_TYPE_SET_STRUCT_REFERENCE (UINT32_C(1) << 16)
#define VMKV_RWIR_TYPE_SET_EXTENSION (UINT32_C(1) << 17)
#define VMKV_RWIR_TYPE_SET_POINTER (UINT32_C(1) << 18)

#define VMKV_RWIR_TYPE_SET_ANY_SINGLE (UINT32_C(1) << 19)

#define VMKV_RWIR_TYPE_SET_WIDTH 20U
#define VMKV_RWIR_TYPE_SET_LIMIT (UINT32_C(1) << VMKV_RWIR_TYPE_SET_WIDTH)
#define VMKV_RWIR_TYPE_SET_RESERVED_MASK (~(VMKV_RWIR_TYPE_SET_LIMIT - UINT32_C(1)))

#define VMKV_RWIR_MANIFEST_VERSION 1U

typedef enum runtimec_rwir_constraint {
    RUNTIMEC_RWIR_CONSTRAINT_INVALID = 0,
    RUNTIMEC_RWIR_CONSTRAINT_NONE = 1,
    RUNTIMEC_RWIR_CONSTRAINT_SAME_CLASS = 2,
    RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPATIBLE = 3,
    RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPARE = 4
} runtimec_rwir_constraint_t;

typedef enum runtimec_rwir_result_rule {
    RUNTIMEC_RWIR_RESULT_INVALID = 0,
    RUNTIMEC_RWIR_RESULT_NONE = 1,
    RUNTIMEC_RWIR_RESULT_SAME_FIRST = 2,
    RUNTIMEC_RWIR_RESULT_SAME_SECOND = 3,
    RUNTIMEC_RWIR_RESULT_WIDEN = 4,
    RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER = 5,
    RUNTIMEC_RWIR_RESULT_BOOLEAN = 6,
    RUNTIMEC_RWIR_RESULT_FLOAT64 = 7,
    RUNTIMEC_RWIR_RESULT_TARGET_TYPE = 8,
    RUNTIMEC_RWIR_RESULT_IDENTITY = 9,
    RUNTIMEC_RWIR_RESULT_CHARACTER_UTF8 = 10
} runtimec_rwir_result_rule_t;

typedef enum runtimec_rwir_alias_policy {
    RUNTIMEC_RWIR_ALIAS_INVALID = 0,
    RUNTIMEC_RWIR_ALIAS_FORBIDDEN = 1,
    RUNTIMEC_RWIR_ALIAS_DECLARED = 2
} runtimec_rwir_alias_policy_t;

typedef enum vmkv_rwir_capability_identifier {

#define RUNTIMEC_RWIR_OPERATION(identifier, operation_name, numeric_identifier, \
                                read_mask, write_mask,                         \
                                read_roles, write_roles,                       \
                                constraint_value, result_value, alias_value,    \
                                decisions)                                      \
    identifier,
#include "specification/rwir_catalog.def"
#undef RUNTIMEC_RWIR_OPERATION
    VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT
} vmkv_rwir_capability_identifier_t;

typedef struct vmkv_rwir_capability {
    runtimec_string_view_t operator_name;
    runtimec_string_view_t behavior_decisions;
    binrwir_operation_identifier_t operation_identifier;
    uint32_t read_parameter_type_set;
    uint32_t write_parameter_type_set;
    runtimec_rwir_constraint_t constraint;
    runtimec_rwir_result_rule_t result_rule;
    runtimec_rwir_alias_policy_t alias_policy;
    
    runtimec_string_view_t read_parameter_roles;
    runtimec_string_view_t write_parameter_roles;
} vmkv_rwir_capability_t;

typedef struct vmkv_rwir_alias {
    runtimec_string_view_t surface_name;
    vmkv_rwir_capability_identifier_t capability_identifier;
} vmkv_rwir_alias_t;

typedef struct vmkv_rwir_specialization {
    runtimec_string_view_t surface_name;
    uint32_t first_parameter_type_set;
    uint32_t second_parameter_type_set;
    bool result_inverted;
    vmkv_rwir_capability_identifier_t capability_identifier;
} vmkv_rwir_specialization_t;

typedef enum vmkv_rwir_specialization_result {
    VMKV_RWIR_SPECIALIZATION_RESULT_INVALID = 0,
    
    VMKV_RWIR_SPECIALIZATION_RESULT_NONE = 1,
    
    VMKV_RWIR_SPECIALIZATION_RESULT_SELECTED = 2,
    
    VMKV_RWIR_SPECIALIZATION_RESULT_INVERTED = 3
} vmkv_rwir_specialization_result_t;

const vmkv_rwir_capability_t *vmkv_rwir_manifest_find_operation(
    runtimec_string_view_t surface_name);

const vmkv_rwir_capability_t *vmkv_rwir_manifest_capability(
    vmkv_rwir_capability_identifier_t identifier);

bool vmkv_rwir_manifest_find_operation_by_identifier(
    binrwir_operation_identifier_t operation_identifier, runtimec_string_view_t *output_name);

bool vmkv_rwir_manifest_is_call(runtimec_string_view_t operation_name);

bool vmkv_rwir_manifest_is_rwfunc_call(
    runtimec_string_view_t operation_langtype);

vmkv_rwir_specialization_result_t vmkv_rwir_manifest_find_specialization(
    runtimec_string_view_t surface_name,
    const uint32_t *parameter_type_sets, size_t parameter_count,
    const vmkv_rwir_capability_t **output_capability);

bool vmkv_rwir_type_set_is_single(uint32_t type_set);

#ifdef __cplusplus
}
#endif

#endif
