#ifndef BINRWIR_INSTRUCTION_H
#define BINRWIR_INSTRUCTION_H

/*
 * 组件：binrwir/instruction
 * 作用：定义只含 opid 的 BinRWIR 二进制指令头，以及非二进制只读视图。
 * 联系：Program 私有存储负责 framing；vmbin 与 BinRWIR JIT 通过 view 消费。
 * 约束：二进制指令头不得携带 offset、count、key、类型、名称或值。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_OPERATION_IDENTIFIER_NONE UINT32_MAX

typedef struct binrwir_instruction {
    uint32_t operation_identifier;
} binrwir_instruction_t;

typedef struct binrwir_argument_span {
    const binrwir_argument_t *data;
    size_t count;
} binrwir_argument_span_t;

typedef struct binrwir_instruction_build_request {
    uint32_t source_logical_program_counter;
    uint32_t operation_identifier;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
} binrwir_instruction_build_request_t;

typedef struct binrwir_instruction_view {
    const binrwir_instruction_t *instruction;
    uint32_t source_logical_program_counter;
    uint32_t reserved_32_bits;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
} binrwir_instruction_view_t;

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request);
runtimec_status_t binrwir_instruction_get_view(
    const binrwir_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    binrwir_argument_span_t read_arguments,
    binrwir_argument_span_t write_arguments,
    binrwir_instruction_view_t *output_view);

_Static_assert(sizeof(binrwir_instruction_t) == 4U,
               "binrwir instruction heads must contain only opid");
_Static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U,
               "binrwir opid must begin each instruction");

#ifdef __cplusplus
}
#endif

#endif
