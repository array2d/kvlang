#ifndef VMBIN_HANDLERS_SCATTER_H
#define VMBIN_HANDLERS_SCATTER_H

/*
 * 组件：vmbin/handlers/scatter
 * 作用：scatterarray（stringkeymap 散列形态）算子的 handler 表。
 * 联系：成员是**独立子 key**（`base·[i]`），不是 body 里的偏移；因此本模块
 *       不走 `vmbin/value_access` 的 body 路径，而是直接调 kvspace C ABI
 *       （句柄来自 `frame->kvspace_handle`）。
 * 约束：
 *   - **不按 langtype 分叉**：元素宽只用于坐标拼写与成员计数，成员值原样搬运。
 *   - 容器本体是一个**空 body 的 map 头**（langtype 以容器声明给出），目录由
 *     `kvspaceMkindex` 建——二者缺一不可：没有目录，`kvspaceListLen` 数不到成员。
 *   - 条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 *
 * 本批实现（opid / 名字 / catalog 行）：
 *   0x0110 scatterarray·new   rwir_catalog.def:450  空散列容器（marker + 目录）
 *   0x0111 scatterarray·len   rwir_catalog.def:457  成员数（memindex 权威）
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

const binrwir_registry_entry_t *runtimec_scatter_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
