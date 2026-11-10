/*
 * 组件：tools/driver_output 的实现
 *
 * 三条输出算子各一行：差异只有三个参数（分隔符 / 流 / 结尾换行），渲染本身全在
 * `vmbin/temp_output` 的唯一实现里。
 *
 * **本文件不出现任何算子名**：它只按 opid 注册。名字→opid 的映射由 layout 独占
 * （`layout/src/builtin.rs` 把名字写成十进制编号），runtimec 侧一个名字也不认识
 * ——这条是 TASK-044 的验收口径（`grep` 必须为 0），也是「谁是输出算子」的知识
 * 只在一处存在的原因。所以下面的函数名按编号取（`emit_1/2/3`），不按语义取。
 */

#include "tools/driver_output.h"

#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "vmbin/temp_output.h"

/* 0xFF01：读参按单个空格拼接，写完补一个换行，写 stdout。 */
static runtimec_status_t driver_output_emit_1(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, " ", 0, 1, output_exit);
}

/* 0xFF02：无分隔、无换行，写 stdout。 */
static runtimec_status_t driver_output_emit_2(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, "", 0, 0, output_exit);
}

/* 0xFF03：单空格拼接 + 换行，写 stderr。 */
static runtimec_status_t driver_output_emit_3(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, " ", 1, 1, output_exit);
}

/*
 * opid 在保留区（`binrwir/instruction.h` 的占用记账），**不是**能力表编号。
 *
 * 与内建会不会撞？不会——session 的 registry 装载顺序是「内建先、扩展后」，
 * `binrwir_registry_append` 对重复 opid 具名拒绝而非覆盖。所以库若同时打开
 * `RUNTIMEC_TEMP_OUTPUT`，本模块的条目会被拒——那是**预期的**：一个 opid 只能
 * 有一个实现，谁先注册谁生效，第二次注册必须失败而不是悄悄顶掉。
 */
static const binrwir_registry_entry_t driver_output_entry_table[] = {
    {.operation_identifier = UINT16_C(0xFF01),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_1},
    {.operation_identifier = UINT16_C(0xFF02),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_2},
    {.operation_identifier = UINT16_C(0xFF03),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_3},
};

const binrwir_registry_entry_t *runtimec_driver_output_entries(
    size_t *output_count) {
    if (output_count != NULL) {
        *output_count = sizeof(driver_output_entry_table) /
                        sizeof(driver_output_entry_table[0]);
    }
    return driver_output_entry_table;
}
