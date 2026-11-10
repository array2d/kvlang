#ifndef RUNTIMEC_TOOLS_DRIVER_OUTPUT_H
#define RUNTIMEC_TOOLS_DRIVER_OUTPUT_H

/*
 * 组件：tools/driver_output
 * 作用：**驱动侧**的输出算子扩展——给 `kv_runner` / `kvlangrun` 注册 0xFF01 /
 *       0xFF02 / 0xFF03 三个 opid，让它们能跑用输出算子的 `.kv` 源码。
 *
 * 为什么在驱动侧而不在 runtimec 库内：runtimec 最终**不实现任何输出算子**
 * （TASK-051）。输出是「宿主」的事——byteseek 有它自己的 `term.rs`，而 benchmark
 * 驱动需要一份 C 侧的。放在 `tools/` 意味着它**只被 `kv_runner` / `kvlangrun` 链接**，
 * 不进 `libkvlang_runtimec_scaffold.so`。
 *
 * 为什么复用 `vmbin/temp_output` 的渲染函数而不重写：输出格式只有一份定义
 * （分隔符语义、utf32 解码、宽度渲染都在那边）。重写一份的症状是「有的 case
 * 多一个空格」——那种差异不会被任何断言抓住，只会让 benchmark 数字看起来在漂。
 *
 * 注册方式：走 TASK-035 的扩展表（`kvlang_runtime_create_request_t.extensions`
 * 或 `runtimec_session_options_t.extension_modules`），**不依赖编译宏**。
 * 这一点与 `vmbin/temp_output` 不同——那个靠 `RUNTIMEC_TEMP_OUTPUT` 开关，
 * 关掉时整段不存在；本模块是驱动自己的源文件，只要驱动在它就在。
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 三条输出算子的注册表条目（0xFF01 / 0xFF02 / 0xFF03）。 */
const binrwir_registry_entry_t *runtimec_driver_output_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
