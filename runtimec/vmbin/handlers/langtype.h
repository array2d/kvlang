#ifndef VMBIN_HANDLERS_LANGTYPE_H
#define VMBIN_HANDLERS_LANGTYPE_H

/*
 * 组件：vmbin/handlers/langtype
 * 作用：`parselangtype` 算子——把 XValue head 的 langtype 串解析成
 *       「基名 + 维数」，返回 `(kind[, ndim])`。
 * 联系：由 engine/session 的 builtin 装配流程取走条目并装进 `binrwir_registry_t`；
 *       值与类型都经 `vmbin/value_access` 按地址读。
 * 约束：这是**值侧**解析器，只处理实例 head 上能出现的形状——单一具体 kind、
 *       定长 dims、`*`/`@` 前缀。并集与轴量词属 `def` 声明侧，这里一律拒绝。
 *       条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 返回本模块的静态条目数组与条目数；output_count 为 NULL 时返回 NULL。 */
const binrwir_registry_entry_t *runtimec_langtype_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
