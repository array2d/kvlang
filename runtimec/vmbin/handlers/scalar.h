#ifndef VMBIN_HANDLERS_SCALAR_H
#define VMBIN_HANDLERS_SCALAR_H

/*
 * 组件：vmbin/handlers/scalar
 * 作用：标量值算子的 handler 表——加减小乘除、取余、位运算。
 * 联系：由 engine/session 的 builtin 装配流程取走条目并装进 `binrwir_registry_t`；
 *       值经 `vmbin/value_access` 按地址读写，与 core 规则共用同一条数据面。
 * 约束：条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 *       落点不是 `vmbin/builtin/`——`builtin` 是被判错的归属词：handler 是实现
 *       （执行侧），归 vmbin；registry 契约归 binrwir。
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 返回本模块的静态条目数组，并把条目数写入 output_count。
 * output_count 为 NULL 时返回 NULL（调用方错用，不猜）。
 */
const binrwir_registry_entry_t *runtimec_scalar_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
