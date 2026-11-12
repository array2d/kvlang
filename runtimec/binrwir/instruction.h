#ifndef BINRWIR_INSTRUCTION_H
#define BINRWIR_INSTRUCTION_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t binrwir_operation_identifier_t;

#define BINRWIR_OPERATION_IDENTIFIER_NONE UINT16_MAX

#define BINRWIR_OPERATION_IDENTIFIER_FOREIGN UINT16_C(0xFFFE)
#define BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE UINT16_C(0xFF00)

typedef struct binrwir_instruction {
    binrwir_operation_identifier_t operation_identifier;
} binrwir_instruction_t;

typedef struct binrwir_argument_span {
    const binrwir_argument_t *data;
    size_t count;
} binrwir_argument_span_t;

typedef struct binrwir_instruction_build_request {
    uint32_t source_logical_program_counter;
    binrwir_operation_identifier_t operation_identifier;
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

_Static_assert(sizeof(binrwir_instruction_t) == 2U,
               "binrwir instruction heads must contain only a 16-bit opid");
_Static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U,
               "binrwir opid must begin each instruction");

#ifdef __cplusplus
}
#endif

#endif
