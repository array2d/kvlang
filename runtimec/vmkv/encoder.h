#ifndef RUNTIMEC_VMKV_ENCODER_H
#define RUNTIMEC_VMKV_ENCODER_H

#include "binrwir/argument.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmkv/local_types.h"
#include "vmkv/rwir.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_encoder_t vmkv_encoder_t;

typedef struct vmkv_encode_request_t {
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_string_view_t function_path;
    runtimec_snapshot_t expected_snapshot;
} vmkv_encode_request_t;

typedef struct vmkv_encode_function {
    uint32_t read_parameter_count;
    uint32_t write_parameter_count;
    uint32_t next_operand_argument_index;
} vmkv_encode_function_t;

typedef struct vmkv_canonical_instruction {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t logical_program_counter;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
    
    const runtimec_string_view_t *read_parameter_types;
    const runtimec_string_view_t *write_parameter_types;
    
    bool aliasing_declared;
} vmkv_canonical_instruction_t;

runtimec_status_t vmkv_encoder_create(
    runtimec_allocator_t allocator,
    vmkv_encoder_t **output_encoder,
    runtimec_error_t *error);

runtimec_status_t vmkv_encoder_destroy(vmkv_encoder_t *encoder);

runtimec_status_t vmkv_encoder_get_canonical_encoding_revision(
    const vmkv_encoder_t *encoder,
    uint32_t *output_canonical_encoding_revision);

runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index,
    binrwir_argument_t *output_argument,
    runtimec_error_t *error);

runtimec_status_t vmkv_encode_instruction(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder,
    const vmkv_local_types_t *local_types,
    vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error);

runtimec_status_t vmkv_encode(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_binfunc_t **output_program,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
