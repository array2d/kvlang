#ifndef BINRWIR_SIGNATURE_H
#define BINRWIR_SIGNATURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_parameter_direction {
    BINRWIR_PARAMETER_DIRECTION_INVALID = 0,
    BINRWIR_PARAMETER_DIRECTION_READ = 1,
    BINRWIR_PARAMETER_DIRECTION_WRITE = 2
} binrwir_parameter_direction_t;

typedef enum binrwir_alias_policy {
    BINRWIR_ALIAS_POLICY_INVALID = 0,
    BINRWIR_ALIAS_NO_ALIAS = 1,
    BINRWIR_ALIAS_MAY_ALIAS = 2,
    BINRWIR_ALIAS_MUST_ALIAS = 3
} binrwir_alias_policy_t;

typedef enum binrwir_alias_rule_flags {
    BINRWIR_ALIAS_RULE_FLAG_NONE = 0,
    BINRWIR_ALIAS_RULE_FLAG_PROJECTION = 1U << 0
} binrwir_alias_rule_flags_t;

typedef struct binrwir_alias_rule {
    uint32_t read_parameter_index;
    uint32_t write_parameter_index;
    uint8_t policy;
    uint8_t flags;
    uint16_t reserved_16_bits;
} binrwir_alias_rule_t;

typedef struct binrwir_signature_view {
    const uint32_t *read_parameter_argument_indices;
    size_t read_parameter_count;
    const uint32_t *write_parameter_argument_indices;
    size_t write_parameter_count;
    const runtimec_string_view_t *read_parameter_types;
    const runtimec_string_view_t *write_parameter_types;
    const binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
} binrwir_signature_view_t;

runtimec_status_t
binrwir_signature_validate(const binrwir_signature_view_t *signature);

runtimec_status_t binrwir_signature_validate_permitting_empty_types(
    const binrwir_signature_view_t *signature, void *context,
    bool (*empty_type_is_legal)(void *context, int is_write, size_t ordinal));
runtimec_status_t binrwir_signature_get_alias_policy(
    const binrwir_signature_view_t *signature, uint32_t read_parameter_index,
    uint32_t write_parameter_index, binrwir_alias_policy_t *output_policy);

runtimec_status_t binrwir_signature_validate_parameter_type(
    runtimec_string_view_t parameter_type);

runtimec_status_t binrwir_signature_validate_alias_rule(
    const binrwir_alias_rule_t *rule);

_Static_assert(sizeof(binrwir_alias_rule_t) == 12U,
               "binrwir alias rules must remain 12 bytes");

#ifdef __cplusplus
}
#endif

#endif
