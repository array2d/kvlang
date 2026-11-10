#ifndef VMBIN_TEMP_OUTPUT_H
#define VMBIN_TEMP_OUTPUT_H

/*
 * 组件：vmbin/temp_output（**临时**）
 *
 * 作用：输出算子的就地实现（print / cerr），行为对齐
 *       `byteseek/src/rwir/term.rs`：
 *
 *           0xFF01   读参按 " " 拼接，写完输出一个换行（**已摘除**，见下）
 *           print    无分隔、无换行
 *           cerr     空格分隔、写 stderr
 *
 * 现在只剩 print / cerr 两条：那个按空格拼接并换行的算子已按 TASK-044 从 runtimec
 * 彻底删除，benchmark 的计时行改由驱动侧扩展承担。
 *
 * **它是临时的。** 正式实现归 byteseek 扩展 runtime 所有（issue #622 决策 D）——
 * 这些算子的进程内 opcode 刻意不进 runtimec 的 myrwircaps 能力表。这里实现它们
 * 只为让 tutorial 的 runtimec 分组能被 runtimec 自己跑通。
 *
 * 排除方式：整体由 `RUNTIMEC_TEMP_OUTPUT` 编译期开关控制，**默认 OFF**。
 * 关闭时（尤其 .so 产物）本文件不参与编译，这两条算子回到「未知算子 → FOREIGN」
 * 的原行为。
 *
 * opid 在保留区（见 binrwir/instruction.h 的占用记账）——不是能力表编号，
 * 专供本临时实现。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 保留区 opid：临时，非能力表编号。指令头 opid 是 16 位，保留区 0xFF00–0xFFFF
 * （占用记账见 binrwir/instruction.h）：这里取 0xFF01–0xFF03，FOREIGN 是 0xFFFE。
 */
#define RUNTIMEC_TEMP_OUTPUT_OPERATION_IDENTIFIER_REMOVED UINT16_C(0xFF01)
#define RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER UINT16_C(0xFF02)
#define RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER UINT16_C(0xFF03)

/*
 * 栈上的 builtin 条目表。调用方（session）在开启开关时把这张表交给
 * `binrwir_registry_initialize`；关闭时整块不存在。
 */
const binrwir_registry_entry_t *runtimec_temp_output_entries(size_t *output_count);

/*
 * **输出算子的唯一渲染实现**：把读参按 `separator` 拼起来、写到指定流、
 * 可选结尾换行。
 *
 * 为什么导出而不是留在 static：驱动侧的 `tools/driver_output` 也要产出同样格式的
 * 输出（benchmark 的计时行、tutorial 的业务输出），而复用这一份就不会出现
 * 「同一份格式两个实现」——那种分叉的症状是「有的 case 多一个空格」。
 */
runtimec_status_t runtimec_temp_output_run(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *separator,
    int use_stderr, int trailing_newline,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
