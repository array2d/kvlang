#ifndef VMBIN_FRAME_SLOT_H
#define VMBIN_FRAME_SLOT_H

/*
 * 组件：vmbin/frame_slot
 * 作用：帧绑定表的槽空间划分——把参数项里的「方向内序号」或「符号池下标」换算成
 *       绑定表里的唯一槽号。
 * 联系：vmbin/value_access 按它取绑定，vmbin/call 按它写绑定；两边必须用同一套
 *       换算，否则同一个形参在绑定时与访问时会落到不同槽。
 * 约束：绑定表只有一张，读参与写参的方向内序号都从 0 起，直接当下标会互相覆盖。
 *       因此槽空间按方向分段：读参 0..nr-1、写参 nr..nr+nw-1、局部符号
 *       nr+nw+s。参数项的 slot 字段语义不变，隔离在这里换算。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 帧里最多需要的绑定槽数：读参 + 写参（局部槽由调用入口另行扩容）。 */
runtimec_status_t vmbin_frame_slot_capacity(const binrwir_program_t *program,
                                            size_t *output_capacity);

/* 参数项 → 绑定槽号；非帧槽引用的 kind（LITERAL / STATIC / FUNCTION）返回
 * INVALID，它们不占绑定表。 */
runtimec_status_t vmbin_frame_slot_of_argument(
    const binrwir_program_t *program, const binrwir_argument_t *argument,
    uint16_t *output_slot);

/* 形参的直接换算：读参见 j，写参见 nr + j。callee 侧绑定时用。 */
runtimec_status_t vmbin_frame_slot_of_parameter(
    const binrwir_program_t *program, binrwir_parameter_direction_t direction,
    uint32_t parameter_index, uint16_t *output_slot);

#ifdef __cplusplus
}
#endif

#endif
