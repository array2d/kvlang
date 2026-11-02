#ifndef RUNTIMEC_VMJIT_EXECUTION_INTERFACE_H
#define RUNTIMEC_VMJIT_EXECUTION_INTERFACE_H

/*
 * 组件：vmjit/execution_interface
 * 作用：机器码入口与执行 binary interface 的最小描述：入口函数签名、
 *       ValueSlot 大小与 Frame 对齐。骨架期不冻结最终寄存器约定、vmjit
 *       frame/stack 布局或稳定机器 binary interface。
 * 联系：vmjit compiler 与 backend 共用；vmbin 的 ValueSlot 与 Frame 是
 *       唯一布局来源，机器码生成器不得手写第二份 offset。
 * 约束：版本变化必须进入 code cache key；本文件不定义语言语义。
 */

#include <stdint.h>

#include "contract/status.h"
#include "vmbin/exit.h"
#include "vmbin/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMJIT_EXECUTION_BINARY_INTERFACE_MAJOR_VERSION 0u
#define VMJIT_EXECUTION_BINARY_INTERFACE_MINOR_VERSION 1u

typedef void (*vmjit_machine_code_function_t)(
    void *execution_state,
    vmbin_frame_t *frame,
    vmbin_exit_t *exit);

typedef struct vmjit_execution_interface_t {
    uint16_t binary_interface_major_version;
    uint16_t binary_interface_minor_version;
    uint32_t value_slot_size_bytes;
    uint32_t frame_alignment_bytes;
} vmjit_execution_interface_t;

runtimec_status_t vmjit_execution_interface(
    vmjit_execution_interface_t *output_interface);

#ifdef __cplusplus
}
#endif

#endif
