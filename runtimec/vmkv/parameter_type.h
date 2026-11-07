#ifndef RUNTIMEC_VMKV_PARAMETER_TYPE_H
#define RUNTIMEC_VMKV_PARAMETER_TYPE_H

/*
 * 组件：vmkv/parameter_type
 * 作用：把 RWIR 槽上的具体 langtype 文本收敛成 TypeSet 的**唯一一位**，供
 *       能力行掩码判定；顺便识别并拒绝并集（`A|B`）与空（None）两种非单值形态。
 * 联系：vmkv/encoder 对每个实参先做本判定，再与能力行读写掩码求交。
 * 约束：本组件只读类型文本，不猜测、不归一化、不把未知种类折成 any/保留位。
 *       形状 `[dims]kind` 按基种类归类：字符基是字符串（char 位），其余基是
 *       数组（array 位）——这与 spec「以字符为基的数组即字符串」一致。
 */

#include <stdint.h>

#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmkv_parameter_type_result {
    VMKV_PARAMETER_TYPE_RESULT_INVALID = 0,
    /* 空 langtype（None）：类型集为空，任何标量掩码都不接受。 */
    VMKV_PARAMETER_TYPE_RESULT_NONE = 1,
    /* 恰好一个具体 langtype；output_type_set 是它唯一的那一位。 */
    VMKV_PARAMETER_TYPE_RESULT_SINGLE = 2,
    /* 声明了并集 `A|B`：BinRWIR 参数必须单一具体，一律失败。 */
    VMKV_PARAMETER_TYPE_RESULT_UNION = 3,
    /* 语法合法但不在 19 位类别表内（低精度浮点、time、scope 等）。 */
    VMKV_PARAMETER_TYPE_RESULT_UNMAPPED = 4
} vmkv_parameter_type_result_t;

vmkv_parameter_type_result_t vmkv_parameter_type_classify(
    runtimec_string_view_t langtype, uint32_t *output_type_set);

/* 单值类型集 → 规范种类名（`int64`、`bool`、`char/utf8`…）；非单值返回空视图。 */
runtimec_string_view_t vmkv_parameter_type_name(uint32_t type_set);

#ifdef __cplusplus
}
#endif

#endif
