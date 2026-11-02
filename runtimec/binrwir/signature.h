#ifndef BINRWIR_SIGNATURE_H
#define BINRWIR_SIGNATURE_H

/*
 * 组件：binrwir/signature
 * 作用：定义读参数、写参数、每个参数的具体 langtype 和跨方向显式别名规则。
 * 联系：vmkv 把 capability 行的 union 掩码收敛成具体类型后生成签名；BinRWIR
 *       Compiler 与 vmbin 按同一签名校验别名关系并按具体类型分派。
 * 约束：签名是别名策略与参数类型的唯一所有者；未知关系不得猜测为不重叠。
 *       每个参数恰好一个具体 langtype，union 与空类型都不属于签名。
 */

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

/*
 * 签名视图：两个方向的 argidx 列表、与之一一对应的具体 langtype、以及显式
 * 别名规则。所有指针都借用调用方内存，视图不拥有、不释放。
 *
 * types 与 argidx 列表同长且同序：第 j 项是第 j 个参数的 langtype 文本。
 * 两者共用一个 count，不设第二个长度字段——类型表的长度恒等于参数个数，
 * 多存一份长度就是同一事实的第二处真相。
 */
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
runtimec_status_t binrwir_signature_get_alias_policy(
    const binrwir_signature_view_t *signature, uint32_t read_parameter_index,
    uint32_t write_parameter_index, binrwir_alias_policy_t *output_policy);

/*
 * 单个参数的「具体 langtype」判据：非空、且不含并集分隔符 `|`。
 * 签名整体校验与 Program builder 的 add_parameter 共用这一处定义，不得另写一份。
 */
runtimec_status_t binrwir_signature_validate_parameter_type(
    runtimec_string_view_t parameter_type);
/*
 * 单条别名规则的字段判据：策略与标志已知、保留位为零。索引是否落在参数范围内
 * 由签名级校验裁定，这里不代判。
 */
runtimec_status_t binrwir_signature_validate_alias_rule(
    const binrwir_alias_rule_t *rule);

_Static_assert(sizeof(binrwir_alias_rule_t) == 12U,
               "binrwir alias rules must remain 12 bytes");

#ifdef __cplusplus
}
#endif

#endif
