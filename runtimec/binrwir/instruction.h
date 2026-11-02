#ifndef BINRWIR_INSTRUCTION_H
#define BINRWIR_INSTRUCTION_H

/*
 * 组件：binrwir/instruction
 * 作用：热指令的固定宽度表示，以及读取和写入引用的分区视图。
 * 联系：Program 拥有指令和引用池；vmbin 与 vmjit 只读消费。
 * 约束：当前窄式和宽式布局不是冻结的持久格式。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "binrwir/reference.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_OPERATION_CODE_NONE UINT16_MAX

typedef enum binrwir_instruction_flags {
    BINRWIR_INSTRUCTION_FLAG_NONE = 0,
    BINRWIR_INSTRUCTION_FLAG_CONTROL = 1U << 0,
    BINRWIR_INSTRUCTION_FLAG_EFFECT = 1U << 1
} binrwir_instruction_flags_t;

typedef enum binrwir_instruction_form {
    BINRWIR_INSTRUCTION_FORM_INVALID = 0,
    BINRWIR_INSTRUCTION_FORM_NARROW = 1,
    BINRWIR_INSTRUCTION_FORM_WIDE = 2
} binrwir_instruction_form_t;

typedef struct binrwir_instruction {
    uint32_t operand_offset;
    uint32_t auxiliary;
    uint16_t operation_code;
    uint16_t flags;
    uint8_t read_count;
    uint8_t write_count;
    uint8_t form;
    uint8_t reserved_byte;
} binrwir_instruction_t;

typedef struct binrwir_reference_span {
    const binrwir_reference_t *data;
    size_t count;
} binrwir_reference_span_t;

typedef struct binrwir_instruction_build_request {
    uint32_t source_logical_program_counter;
    uint32_t auxiliary;
    uint16_t operation_code;
    uint16_t flags;
    uint8_t form;
    uint8_t reserved_bytes[3];
    binrwir_reference_span_t reads;
    binrwir_reference_span_t writes;
} binrwir_instruction_build_request_t;

typedef struct binrwir_instruction_view {
    const binrwir_instruction_t *instruction;
    uint32_t source_logical_program_counter;
    uint32_t reserved_32_bits;
    binrwir_reference_span_t reads;
    binrwir_reference_span_t writes;
} binrwir_instruction_view_t;

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request);
runtimec_status_t binrwir_instruction_get_view(
    const binrwir_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    const binrwir_reference_t *references, size_t reference_count,
    binrwir_instruction_view_t *output_view);

_Static_assert(sizeof(binrwir_instruction_t) == 16U,
               "binrwir instructions must remain 16 bytes");

#ifdef __cplusplus
}
#endif

#endif
