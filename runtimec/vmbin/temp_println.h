#ifndef VMBIN_TEMP_PRINTLN_H
#define VMBIN_TEMP_PRINTLN_H

/*
 * 组件：vmbin/temp_println（**临时**）
 *
 * 作用：`println` / `print` / `cerr` 三个输出算子的就地实现，行为对齐
 *       `byteseek/src/rwir/term.rs`：
 *
 *           println  读参按 " " 拼接，写完输出一个换行
 *           print    无分隔、无换行
 *           cerr     空格分隔、写 stderr
 *
 * **它是临时的。** 正式实现归 byteseek 扩展 runtime 所有（issue #622 决策 D）——
 * 这三个算子的进程内 opcode 刻意不进 runtimec 的 myrwircaps 能力表。这里实现它们
 * 只为让 tutorial 的 runtimec 分组能被 runtimec 自己跑通。
 *
 * 排除方式：整体由 `RUNTIMEC_TEMP_PRINTLN` 编译期开关控制，**默认 OFF**。
 * 关闭时（尤其 .so 产物）本文件不参与编译，三个算子回到「未知算子 → FOREIGN」的
 * 原行为，产物里 grep 不到任何 println 痕迹。
 *
 * opid 取保留区 `0xFFFF0001..0xFFFF0003`——不是能力表编号，专供本临时实现。
 */

#include <stdint.h>

#include "contract/status.h"
#include "vmbin/builtin/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 保留区 opid：临时，非能力表编号。`BINRWIR_OPERATION_IDENTIFIER_FOREIGN` 是
 * 0xFFFFFFFE，这里取它下方一段连续值，避免与任何现有保留值相撞。
 */
#define RUNTIMEC_TEMP_PRINTLN_OPERATION_IDENTIFIER UINT32_C(0xFFFF0001)
#define RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER UINT32_C(0xFFFF0002)
#define RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER UINT32_C(0xFFFF0003)

/*
 * 栈上的 builtin 条目表。调用方（session）在开启开关时把这张表交给
 * `vmbin_builtin_registry_initialize`；关闭时整块不存在。
 */
const vmbin_builtin_entry_t *runtimec_temp_println_entries(size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
