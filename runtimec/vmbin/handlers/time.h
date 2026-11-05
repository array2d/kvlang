#ifndef VMBIN_HANDLERS_TIME_H
#define VMBIN_HANDLERS_TIME_H

/*
 * 组件：vmbin/handlers/time
 * 作用：`time·` 与 `time/duration·` 两族内建的实现与条目表。
 * 联系：条目由 `engine/session` 装进 `binrwir_registry_t`；值经
 *       `vmbin/value_access` 按地址读写，与 core 规则共用同一条数据面。
 * 约束：纯整数运算，无分配、无缓存；时钟只经 `clock_gettime(CLOCK_REALTIME)`。
 *
 * ── 表示：单一 int64 纳秒（Go 阵营）────────────────────────────────────
 * spec `03-类型系统/02-种类-标量时间容器.kv`：
 *
 *   > 五家分裂，kvlang 取 **Go 阵营**：单一 `int64` 纳秒。理由：单标量最省，
 *   > 且纳秒精度高于毫秒、避免浮点累积误差。
 *   > 因此 `time` 与 `time/duration` 的值体都是 8 字节。
 *
 * ⇒ 两个 kind 都是 int64 标量，**没有任何结构体**。所有算子都是对同一个
 * 8 字节有符号整数做加/减/比较/整除。
 *
 * ── 时钟：墙上时间，不是单调钟 ────────────────────────────────────────
 * 五语言行为矩阵 `time-realtime-nanoseconds` 是 **consensus** 行：
 *
 *   > Read the **wall clock** and store nanoseconds since the Unix epoch
 *
 * C 的 `clock_gettime`、Python `time.time`、Rust `SystemTime`、Go `time.Now`、
 * JS `Date.now` **全是墙上时间**。按 p7（五家一致 → 必须遵从）用
 * `CLOCK_REALTIME`，与前辈 `runtime/src/rwir_time.c:8` 同源。
 *
 * **已知后果**：系统时钟回拨时 `time·sub` 会得负值。矩阵未规定该情形，
 * 前辈也不钳制——本实现照做。要改这条语义是另一件事，不藏在 handler 里。
 *
 * ── 溢出与截断（矩阵两行，都取 C 阵营）────────────────────────────────
 *   duration-overflow-wrap  : 「Follow C, Go and release mode Rust: **wrap at
 *                             the 64 bit boundary**」⇒ int64 自然回绕，不检查；
 *   duration-truncating-scale: 「integer division **toward zero**」⇒ 直接用 `/`。
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 返回本模块的静态条目数组与条目数；output_count 为 NULL 时返回 NULL。 */
const binrwir_registry_entry_t *runtimec_time_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
