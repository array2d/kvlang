#ifndef BINRWIR_SIGNATURE_H
#define BINRWIR_SIGNATURE_H

/*
 * 组件：binrwir/signature
 * 作用：定义读参数、写参数和跨方向显式别名规则。
 * 联系：vmkv 生成签名；BinRWIR Compiler 按同一签名校验参数别名关系。
 * 约束：签名是别名策略的唯一所有者；未知关系不得猜测为不重叠。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

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
    const binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
} binrwir_signature_view_t;

runtimec_status_t
binrwir_signature_validate(const binrwir_signature_view_t *signature);
runtimec_status_t binrwir_signature_get_alias_policy(
    const binrwir_signature_view_t *signature, uint32_t read_parameter_index,
    uint32_t write_parameter_index, binrwir_alias_policy_t *output_policy);

_Static_assert(sizeof(binrwir_alias_rule_t) == 12U,
               "binrwir alias rules must remain 12 bytes");

#ifdef __cplusplus
}
#endif

#endif
