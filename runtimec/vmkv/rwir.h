#ifndef RUNTIMEC_VMKV_RWIR_H
#define RUNTIMEC_VMKV_RWIR_H

#include <stddef.h>
#include <stdint.h>

#include "contract/view.h"
#include "contract/status.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmkv_key_addressability_t {
    VMKV_KEY_ADDRESSABILITY_INVALID = 0,
    VMKV_KEY_ADDRESSABILITY_STATIC = 1,
    VMKV_KEY_ADDRESSABILITY_DYNAMIC = 2
} vmkv_key_addressability_t;

typedef struct vmkv_rwir_literal_view_t {
    runtimec_bytes_view_t bytes;
    runtimec_string_view_t langtype;
} vmkv_rwir_literal_view_t;

typedef struct vmkv_rwir_argument_view_t {
    runtimec_string_view_t parameter_type_name;
    runtimec_key_view_t key;
    vmkv_key_addressability_t key_addressability;
    vmkv_rwir_literal_view_t literal;
} vmkv_rwir_argument_view_t;

typedef struct vmkv_rwir_instruction_view_t {
    runtimec_string_view_t operation_name;
    
    runtimec_string_view_t operation_langtype;
    uint32_t logical_program_counter;
    const vmkv_rwir_argument_view_t *read_arguments;
    size_t read_argument_count;
    const vmkv_rwir_argument_view_t *write_arguments;
    size_t write_argument_count;
} vmkv_rwir_instruction_view_t;

typedef enum vmkv_rwir_read_result_t {
    VMKV_RWIR_READ_RESULT_INVALID = 0,
    VMKV_RWIR_READ_RESULT_INSTRUCTION = 1,
    VMKV_RWIR_READ_RESULT_END = 2
} vmkv_rwir_read_result_t;

typedef struct vmkv_rwir_reader_t vmkv_rwir_reader_t;

runtimec_status_t vmkv_rwir_operation_name_from_wire(
    const uint8_t *wire, size_t wire_size, runtimec_string_view_t *output_operation,
    runtimec_error_t *error);

bool vmkv_rwir_key_is_member(runtimec_key_view_t key);

bool vmkv_rwir_key_is_dynamic_member(runtimec_key_view_t key);

runtimec_status_t vmkv_rwir_reader_create(
    vmkv_function_source_t *function,
    vmkv_rwir_reader_t **output_reader,
    runtimec_error_t *error);

runtimec_status_t vmkv_rwir_reader_next(
    vmkv_rwir_reader_t *reader,
    vmkv_rwir_read_result_t *output_result,
    vmkv_rwir_instruction_view_t *output_instruction,
    runtimec_error_t *error);

runtimec_status_t vmkv_rwir_reader_destroy(vmkv_rwir_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif
