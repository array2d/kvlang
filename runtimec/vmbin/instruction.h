#ifndef VMBIN_INSTRUCTION_H
#define VMBIN_INSTRUCTION_H

/*
 * 组件：vmbin/instruction
 * 作用：热指令的固定宽度表示，以及读取和写入引用的分区视图。
 * 联系：Program 拥有指令和引用池；interpreter 与 vmjit 只读消费。
 * 约束：当前窄式和宽式布局不是冻结的持久格式。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "vmbin/reference.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_OPERATION_CODE_NONE UINT16_MAX

typedef enum vmbin_instruction_flags {
    VMBIN_INSTRUCTION_FLAG_NONE = 0,
    VMBIN_INSTRUCTION_FLAG_CONTROL = 1U << 0,
    VMBIN_INSTRUCTION_FLAG_EFFECT = 1U << 1
} vmbin_instruction_flags_t;

typedef enum vmbin_instruction_form {
    VMBIN_INSTRUCTION_FORM_INVALID = 0,
    VMBIN_INSTRUCTION_FORM_NARROW = 1,
    VMBIN_INSTRUCTION_FORM_WIDE = 2
} vmbin_instruction_form_t;

typedef struct vmbin_instruction {
    uint32_t operand_offset;
    uint32_t auxiliary;
    uint16_t operation_code;
    uint16_t flags;
    uint8_t read_count;
    uint8_t write_count;
    uint8_t form;
    uint8_t reserved_byte;
} vmbin_instruction_t;

typedef struct vmbin_reference_span {
    const vmbin_reference_t *data;
    size_t count;
} vmbin_reference_span_t;

typedef struct vmbin_instruction_build_request {
    uint32_t source_logical_program_counter;
    uint32_t auxiliary;
    uint16_t operation_code;
    uint16_t flags;
    uint8_t form;
    uint8_t reserved_bytes[3];
    vmbin_reference_span_t reads;
    vmbin_reference_span_t writes;
} vmbin_instruction_build_request_t;

typedef struct vmbin_instruction_view {
    const vmbin_instruction_t *instruction;
    uint32_t source_logical_program_counter;
    uint32_t reserved_32_bits;
    vmbin_reference_span_t reads;
    vmbin_reference_span_t writes;
} vmbin_instruction_view_t;

runtimec_status_t vmbin_instruction_validate_build_request(
    const vmbin_instruction_build_request_t *request);
runtimec_status_t vmbin_instruction_get_view(
    const vmbin_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    const vmbin_reference_t *references, size_t reference_count,
    vmbin_instruction_view_t *output_view);

_Static_assert(sizeof(vmbin_instruction_t) == 16U,
               "vmbin instructions must remain 16 bytes");

#ifdef __cplusplus
}
#endif

#endif
