#ifndef RUNTIMEC_BINRWIR_JIT_EXECUTION_INTERFACE_H
#define RUNTIMEC_BINRWIR_JIT_EXECUTION_INTERFACE_H

/*
 * 组件：binrwir_compiler/jit/execution_interface
 * 作用：机器码入口与执行 binary interface 的最小描述：V.body 地址、参数序号、
 *       Frame 对齐与 safepoint 合同版本。骨架期不冻结最终寄存器约定、
 *       JIT frame/stack 布局或稳定机器 binary interface。
 * 联系：JIT compiler 与 backend 共用；BinRWIR argument 与 execution frame
 *       合同是唯一布局来源，机器码生成器不得手写第二份 offset。
 * 约束：版本变化必须进入 compilation repository key；本文件不定义语言语义。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_JIT_EXECUTION_BINARY_INTERFACE_MAJOR_VERSION 0u
#define BINRWIR_JIT_EXECUTION_BINARY_INTERFACE_MINOR_VERSION 3u

typedef runtimec_status_t (*binrwir_jit_machine_code_function_t)(
    void *execution_state,
    binrwir_execution_frame_t *frame,
    binrwir_execution_exit_t *exit);

typedef struct binrwir_jit_execution_interface_t {
    uint16_t binary_interface_major_version;
    uint16_t binary_interface_minor_version;
    uint16_t value_body_address_size_bytes;
    uint16_t argument_index_size_bytes;
    uint32_t frame_alignment_bytes;
    uint32_t safepoint_contract_revision;
} binrwir_jit_execution_interface_t;

runtimec_status_t binrwir_jit_execution_interface(
    binrwir_jit_execution_interface_t *output_interface);

#ifdef __cplusplus
}
#endif

#endif
