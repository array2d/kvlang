#ifndef VMBIN_HANDLERS_STRING_H
#define VMBIN_HANDLERS_STRING_H

/*
 * 组件：vmbin/handlers/string
 * 作用：字符串算子的 handler 表。
 * 联系：由 engine/session 的装配流程取走条目并装进 `binrwir_registry_t`；
 *       值是 compact 的字符序列（`[N]char/utf8` / `[N]char/utf32` / `[N]char/ascii`），
 *       经 `vmbin/value_access` 按地址读写。
 * 约束：
 *   - **不按字符串内容分支**：编码差异只由 langtype 决定，转换一律走
 *     `contract/char_codec`（全系统唯一转换点），本模块不写第二份编解码。
 *   - **不按 langtype 分叉**：所有位置/长度都先统一成**码点序列**再算，
 *     所以「怎么算是第 3 个字符」对三种编码只有一条路径。
 *   - 变长结果走 `kvspaceSetValue` 写整条 wire，分配归存储层；
 *     定长缓冲 512 字节，超限用 `RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER`
 *     同一个细节码（不新造）。
 *
 * 本批实现（opid / 名字 / catalog 行 / 分组）：
 *
 *   观测（只读）
 *     0x0125 string·len      rwir_catalog.def:541  码点数，不是字节数
 *
 *   读元素
 *     0x0121 string·char     rwir_catalog.def:513  第 i 个码点，返回单字符字符串
 *
 *   比较（返回 -1 / 0 / 1）
 *     0x0123 string·cmp      rwir_catalog.def:527  按码点逐位比较
 *
 *   创建（造新 wire 写目标 key）
 *     0x0127 string·concat   rwir_catalog.def:555  结果恒为 char/utf8
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
const binrwir_registry_entry_t *runtimec_string_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
