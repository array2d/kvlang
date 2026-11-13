
#include "engine/execution_window.h"
#include "contract/pathspace_interface.h"
#include "contract/diagnostic_band.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>

#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/binfunc.h"
#include "binrwir/specialize.h"
#include "binrwir_compiler/compiler.h"
#include "binrwir_compiler/jit/executor.h"
#include "contract/checkpoint.h"
#include "engine/compilation_repository.h"
#include "engine/frame_state.h"
#include "engine/session.h"
#include "vmbin/frame_slot.h"
#include "vmbin/interpreter.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir.h"
#include "vmkv/rwir_manifest.h"

static void runtimec_execution_window_reset_result(runtimec_execution_window_result_t *result,
                                        runtimec_status_t status,
                                        uint32_t detail_code) {
    *result = (runtimec_execution_window_result_t){0};
    result->status = status;
    result->execution_exit.kind = BINRWIR_EXECUTION_EXIT_ERROR;
    result->execution_exit.status = status;
    result->execution_exit.detail_code = detail_code;
    result->execution_exit.logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    result->execution_exit.resume_logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
}

static runtimec_status_t runtimec_execution_window_fail(runtimec_execution_window_result_t *result,
                                             runtimec_error_t *error,
                                             runtimec_status_t status,
                                             runtimec_execution_window_diagnostic_t detail,
                                             const char *message) {
    runtimec_execution_window_reset_result(result, status, (uint32_t)detail);
    
    if (error != NULL && error->message.size != 0U) {
        error->status = status;
        return status;
    }
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, detail,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static bool runtimec_execution_window_vthread_is_numeric(
    runtimec_string_view_t vthread_identifier) {
    size_t ordinal;

    if (vthread_identifier.size == 0U) {
        return false;
    }
    for (ordinal = 0U; ordinal < vthread_identifier.size; ++ordinal) {
        const char character = vthread_identifier.data[ordinal];

        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

static runtimec_program_counter_t runtimec_execution_window_make_program_counter(
    int32_t stack_identifier, uint32_t source_program_counter) {
    runtimec_program_counter_t counter;

    counter.stack_identifier = stack_identifier;
    counter.source_instruction_ordinal =
        source_program_counter > (uint32_t)INT32_MAX
            ? RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE
            : (int32_t)source_program_counter;
    return counter;
}

static runtimec_status_t runtimec_execution_window_materialize_parameter_pointer(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const binrwir_binfunc_t *program, const runtimec_string_view_t *parameter_type,
    uint32_t ordinal, const runtimec_string_view_t *caller_key,
    runtimec_execution_window_diagnostic_t *output_detail,
    runtimec_error_t *error) {
    char slot_key_text[256];
    char langtype_text[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char message[256];
    size_t slot_length;

    slot_length = binrwir_frame_parameter_key(
        runtimec_string_view_make(request->frame_path.data,
                                  request->frame_path.size),
        (uint16_t)ordinal, false, slot_key_text, sizeof(slot_key_text));
    if (slot_length == SIZE_MAX) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (parameter_type->size == 0U ||
        parameter_type->size + 1U > sizeof(langtype_text)) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(langtype_text, parameter_type->data, parameter_type->size);
    langtype_text[parameter_type->size] = '\0';
    
    if (langtype_text[0] != '*') {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (pathlangXvalueHeadNewPtr(
            (const uint8_t *)caller_key->data, (uint32_t)caller_key->size,
            langtype_text, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    if (!runtimec_pathspace_write_wire(host->handle, slot_key_text, wire,
                                   (uint32_t)wire_size, message,
                                   sizeof(message))) {
        free(wire);
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return RUNTIMEC_STATUS_ERROR;
    }
    free(wire);
    (void)snapshot;
    (void)program;
    (void)error;
    return RUNTIMEC_STATUS_SUCCESS;
}

#define RUNTIMEC_EXECUTION_WINDOW_MAX_PARAMETERS 64U

#define RUNTIMEC_EXECUTION_WINDOW_KEY_CAPACITY 256U

static runtimec_status_t runtimec_execution_window_bind_slot(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    binrwir_frame_bindings_t *bindings, uint16_t binding_slot,
    runtimec_string_view_t key_text, uint64_t *generation_in_out,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    const runtimec_key_view_t slot_key =
        runtimec_key_view_make(key_text.data, key_text.size);
    runtimec_status_t status =
        runtimec_pathspace_access_resolve_frame_xvalue_address(
            host, snapshot, slot_key, &xvalue_address, &generation, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return status;
    }
    
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE || generation == 0U) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return RUNTIMEC_STATUS_INVALID;
    }
    if (*generation_in_out == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        *generation_in_out = generation;
    } else if (*generation_in_out != generation) {
        
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return RUNTIMEC_STATUS_ERROR;
    }
    if (binrwir_frame_bindings_bind(bindings, binding_slot, slot_key, slot_key,
                                    xvalue_address, generation) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_execution_window_materialize_argument(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const runtimec_argument_description_t *description, size_t ordinal,
    char *scratch, size_t scratch_capacity, runtimec_string_view_t *output_key,
    runtimec_execution_window_diagnostic_t *output_detail,
    runtimec_error_t *error) {
    *output_key = runtimec_string_view_make(NULL, 0U);
    switch (description->source) {
    case RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY:
        if (!runtimec_string_view_is_valid(description->key) ||
            description->key.size == 0U) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = description->key;
        return RUNTIMEC_STATUS_SUCCESS;
    case RUNTIMEC_ARGUMENT_SOURCE_LITERAL: {
        if (!runtimec_string_view_is_valid(description->literal_langtype) ||
            description->literal_langtype.size == 0U ||
            description->literal_bytes.data == NULL) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        
        char langtype_text[128];
        const int langtype_length =
            snprintf(langtype_text, sizeof(langtype_text), "%.*s",
                     (int)description->literal_langtype.size,
                     description->literal_langtype.data);
        if (langtype_length < 0 ||
            (size_t)langtype_length >= sizeof(langtype_text)) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        const int key_length =
            snprintf(scratch, scratch_capacity, "%.*s/._lit%zu",
                     (int)request->frame_path.size, request->frame_path.data,
                     ordinal);
        if (key_length < 0 || (size_t)key_length >= scratch_capacity) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        
        if (runtimec_pathspace_access_write(
                host, snapshot,
                runtimec_key_view_make(scratch, strlen(scratch)),
                runtimec_bytes_view_make(description->literal_bytes.data,
                                         description->literal_bytes.size),
                error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = runtimec_string_view_from_c_string(scratch);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET: {
        if (!runtimec_string_view_is_valid(description->key) ||
            description->key.size == 0U ||
            !runtimec_string_view_is_valid(description->literal_langtype) ||
            description->literal_langtype.size == 0U) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        
        if (runtimec_pathspace_access_ensure_zero_value(
                host, snapshot,
                                    runtimec_key_view_make(description->key.data,
                                                           description->key.size),
                                    description->literal_langtype,
                                    error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = description->key;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    default:
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
        return RUNTIMEC_STATUS_INVALID;
    }
}

static runtimec_status_t runtimec_execution_window_bind_parameters(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const binrwir_binfunc_t *program, const runtimec_execution_window_request_t *request,
    binrwir_frame_bindings_t *bindings, uint64_t *output_generation,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    binrwir_binfunc_view_t program_view;
    size_t read_count;
    size_t write_count;
    size_t ordinal;
    
    char materialized_key[RUNTIMEC_EXECUTION_WINDOW_KEY_CAPACITY];

    *output_generation = snapshot->source_revision.generation;
    if (binrwir_binfunc_get_view(program, &program_view) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    read_count = program_view.signature.read_parameter_count;
    write_count = program_view.signature.write_parameter_count;
    
    if (read_count > SIZE_MAX - write_count ||
        request->argument_count != read_count + write_count) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT;
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (read_count + write_count >
        (size_t)RUNTIMEC_EXECUTION_WINDOW_MAX_PARAMETERS) {
        static _Thread_local char parameter_count_message[160];
        (void)snprintf(parameter_count_message,
                       sizeof(parameter_count_message),
                       "function takes %zu parameters (read %zu + write %zu), "
                       "exceeding the supported limit of %u",
                       read_count + write_count, read_count, write_count,
                       (unsigned)RUNTIMEC_EXECUTION_WINDOW_MAX_PARAMETERS);
        runtimec_error_set(error, RUNTIMEC_STATUS_UNSUPPORTED,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT,
                           runtimec_string_view_from_c_string(
                               parameter_count_message),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT;
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    for (ordinal = 0U; ordinal < read_count; ++ordinal) {
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
        if (vmbin_frame_slot_of_parameter(program,
                                          BINRWIR_PARAMETER_DIRECTION_READ,
                                          (uint32_t)ordinal,
                                          &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        runtimec_string_view_t resolved_key;
        runtimec_status_t status = runtimec_execution_window_materialize_argument(
            host, snapshot, request, &request->arguments[ordinal], ordinal,
            materialized_key, sizeof(materialized_key), &resolved_key,
            output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        
        status = runtimec_execution_window_materialize_parameter_pointer(
            host, snapshot, request, program,
            &program_view.signature.read_parameter_types[ordinal], ordinal,
            &resolved_key, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot, resolved_key,
            output_generation, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    for (ordinal = 0U; ordinal < write_count; ++ordinal) {
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
        if (vmbin_frame_slot_of_parameter(program,
                                          BINRWIR_PARAMETER_DIRECTION_WRITE,
                                          (uint32_t)ordinal,
                                          &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        runtimec_string_view_t resolved_key;
        runtimec_status_t status = runtimec_execution_window_materialize_argument(
            host, snapshot, request,
            &request->arguments[read_count + ordinal], read_count + ordinal,
            materialized_key, sizeof(materialized_key), &resolved_key,
            output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot, resolved_key,
            output_generation, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_execution_window_operation_name(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const char *function_path, uint32_t source_program_counter,
    char *output_key, size_t key_capacity,
    runtimec_string_view_t *output_operation, runtimec_error_t *error) {
    *output_operation = runtimec_string_view_make(NULL, 0U);
    const int key_length = snprintf(output_key, key_capacity, "%s/[%u,0]",
                                    function_path, source_program_counter);
    if (key_length < 0 || (size_t)key_length >= key_capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    uint8_t storage[512];
    runtimec_bytes_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    runtimec_status_t status = runtimec_pathspace_access_read(
        host, snapshot,
        runtimec_key_view_make(output_key, (size_t)key_length), &buffer, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    return vmkv_rwir_operation_name_from_wire(buffer.data, buffer.size,
                                              output_operation, error);
}

static runtimec_string_view_t runtimec_execution_window_persistent_status(
    uint16_t exit_kind, int32_t stack_identifier) {
    switch (exit_kind) {
    case BINRWIR_EXECUTION_EXIT_DONE:
        
        return runtimec_string_view_from_c_string(
            stack_identifier > 1 ? "running" : "done");
    case BINRWIR_EXECUTION_EXIT_YIELD:
        return runtimec_string_view_from_c_string("paused");
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return runtimec_string_view_from_c_string("waiting");
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return runtimec_string_view_from_c_string("error");
    case BINRWIR_EXECUTION_EXIT_CALL:
    case BINRWIR_EXECUTION_EXIT_BUILTIN:
    default:
        return runtimec_string_view_from_c_string("running");
    }
}

static runtimec_status_t runtimec_execution_window_commit_checkpoint(
    runtimec_session_t *session, runtimec_pathspace_access_t *host,
    const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const binrwir_execution_exit_t *execution_exit, runtimec_error_t *error) {
    runtimec_checkpoint_t checkpoint;
    uint32_t source_program_counter;
    runtimec_status_t status;

    if ((execution_exit->flags &
         BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED) == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (execution_exit->kind == BINRWIR_EXECUTION_EXIT_DONE) {
        source_program_counter = execution_exit->logical_program_counter;
    } else if (execution_exit->resume_logical_program_counter !=
               BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        source_program_counter = execution_exit->resume_logical_program_counter;
    } else {
        source_program_counter = execution_exit->logical_program_counter;
    }
    
    if (source_program_counter == BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED,
                           runtimec_string_view_from_c_string(
                               "completed exit carries no source "
                               "program counter"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_checkpoint_reset(&checkpoint);
    checkpoint.contract_version = snapshot->contract_version;
    checkpoint.source_revision = snapshot->source_revision;
    status = runtimec_session_take_checkpoint_sequence(session,
                                                       &checkpoint.sequence);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    checkpoint.vthread_identifier = request->vthread_identifier;
    checkpoint.logical_program_counter =
        runtimec_execution_window_make_program_counter(
            request->stack_identifier, source_program_counter);
    checkpoint.persistent_status = runtimec_execution_window_persistent_status(
        execution_exit->kind, request->stack_identifier);
    checkpoint.outcome = execution_exit->status;
    return runtimec_pathspace_access_checkpoint(host, &checkpoint, error);
}

static runtimec_status_t runtimec_execution_window_materialize_locals(
    runtimec_pathspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const binrwir_binfunc_t *program,
    const runtimec_execution_window_request_t *request,
    binrwir_frame_bindings_t *bindings, size_t binding_capacity,
    uint64_t *generation_in_out,
    runtimec_execution_window_diagnostic_t *output_detail,
    runtimec_error_t *error) {
    binrwir_binfunc_view_t program_view;
    size_t symbol_count = 0U;
    bool *is_local = NULL;
    size_t instruction_ordinal;
    size_t symbol_ordinal;
    runtimec_status_t status;

    if (binrwir_binfunc_get_view(program, &program_view) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    
    status = runtimec_pathspace_access_frame_generation(
        host, snapshot, runtimec_key_view_make(request->frame_path.data,
                                               request->frame_path.size),
        generation_in_out, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return status;
    }
    if (symbol_count == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    is_local = (bool *)runtimec_allocator_allocate(
        &host->allocator, sizeof(*is_local) * symbol_count,
        _Alignof(bool));
    if (is_local == NULL) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY;
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(is_local, 0, sizeof(*is_local) * symbol_count);

    for (instruction_ordinal = 0U;
         instruction_ordinal < program_view.instruction_count;
         ++instruction_ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_argument_span_t spans[2];
        size_t span_ordinal;

        if (binrwir_binfunc_get_instruction(program,
                                            (uint32_t)instruction_ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        spans[0] = instruction.read_arguments;
        spans[1] = instruction.write_arguments;
        for (span_ordinal = 0U; span_ordinal < 2U; ++span_ordinal) {
            size_t argument_ordinal;

            for (argument_ordinal = 0U;
                 argument_ordinal < spans[span_ordinal].count;
                 ++argument_ordinal) {
                const binrwir_argument_t *argument =
                    &spans[span_ordinal].data[argument_ordinal];

                if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL &&
                    (size_t)argument->slot < symbol_count) {
                    is_local[argument->slot] = true;
                }
            }
        }
    }

    for (symbol_ordinal = 0U; symbol_ordinal < symbol_count;
         ++symbol_ordinal) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;
        char *key_text;
        size_t key_size;

        if (!is_local[symbol_ordinal]) {
            continue;
        }
        if (binrwir_binfunc_get_symbol(program, (uint16_t)symbol_ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            name.size == 0U) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        
        if (langtype.size == 0U) {
            continue;
        }
        
        if (binrwir_symbol_name_is_member(name)) {
            continue;
        }
        if (!runtimec_pathspace_access_langtype_has_fixed_zero_width(langtype)) {
            continue;
        }
        key_size = request->frame_path.size + 1U + name.size;
        key_text = (char *)runtimec_allocator_allocate(
            &host->allocator, key_size + 1U, 1U);
        if (key_text == NULL) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY;
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
        memcpy(key_text, request->frame_path.data, request->frame_path.size);
        key_text[request->frame_path.size] = '/';
        memcpy(key_text + request->frame_path.size + 1U, name.data, name.size);
        key_text[key_size] = '\0';

        const runtimec_key_view_t slot_key =
            runtimec_key_view_make(key_text, key_size);
        status = runtimec_pathspace_access_ensure_zero_value(
            host, snapshot, slot_key,
                                         langtype, error);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            
            runtimec_xvalue_address_t xvalue_address =
                RUNTIMEC_XVALUE_ADDRESS_NONE;
            uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
            
            uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

            status = vmbin_frame_slot_of_local_symbol(
                program, (uint16_t)symbol_ordinal, &binding_slot);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
            }
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                status =
                    runtimec_pathspace_access_resolve_frame_xvalue_address(
                        host, snapshot, slot_key, &xvalue_address,
                    &generation, error);
            }
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
                    generation == BINRWIR_FRAME_BINDING_GENERATION_NONE ||
                    binding_slot >= binding_capacity) {
                    *output_detail =
                        RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                    status = RUNTIMEC_STATUS_INVALID;
                } else if (*generation_in_out != generation) {
                    
                    *output_detail =
                        RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                    status = RUNTIMEC_STATUS_ERROR;
                }
            }
            if (status == RUNTIMEC_STATUS_SUCCESS &&
                binrwir_frame_bindings_bind(bindings, binding_slot, slot_key,
                                            slot_key, xvalue_address,
                                            generation) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                status = RUNTIMEC_STATUS_INVALID;
            }
        }
        (void)runtimec_allocator_deallocate(&host->allocator, key_text,
                                            key_size + 1U, 1U);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
            return status;
        }
    }
    (void)runtimec_allocator_deallocate(&host->allocator, is_local,
                                        sizeof(*is_local) * symbol_count,
                                        _Alignof(bool));
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_execution_window_find_instruction(
    const binrwir_binfunc_t *program, uint32_t source_program_counter,
    uint32_t *output_instruction_identifier,
    runtimec_execution_window_diagnostic_t *output_detail) {
    *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    if (binrwir_binfunc_find_instruction(program, source_program_counter,
                                         output_instruction_identifier) !=
            RUNTIMEC_STATUS_SUCCESS ||
        *output_instruction_identifier == BINRWIR_IDENTIFIER_NONE) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND;
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_execution_window_compile_and_insert(
    const runtimec_session_runtime_components_t *components,
    runtimec_pathspace_access_t *host, const runtimec_execution_window_request_t *request,
    const runtimec_snapshot_t *snapshot,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    vmkv_encode_request_t encode_request;
    binrwir_binfunc_t *program = NULL;
    binrwir_compilation_t *compilation = NULL;
    runtimec_status_t status;

    encode_request.pathspace_access = host;
    encode_request.function_path = request->function_path;
    encode_request.expected_snapshot = *snapshot;
    status = vmkv_encode(components->vmkv_encoder, &encode_request, &program,
                         error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED;
        return status;
    }
    
    {
        binrwir_binfunc_t *specialized = NULL;

        status = binrwir_binfunc_specialize(program, &host->allocator,
                                            &specialized, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)binrwir_binfunc_destroy(program);
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED;
            return status;
        }
        
        (void)binrwir_binfunc_destroy(program);
        program = specialized;
    }
    
    status = binrwir_compiler_compile(components->binrwir_compiler, program,
                                      &compilation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED;
        return status;
    }
    status = runtimec_compilation_repository_insert(
        components->compilation_repository, key, compilation, output_lease,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compilation_destroy(compilation, &ignored);
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED;
        return status;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_execution_window_execute(
    runtimec_session_t *session, const runtimec_execution_window_request_t *request,
    runtimec_execution_window_result_t *output_result, runtimec_error_t *error) {
    runtimec_pathspace_access_t *host = NULL;
    runtimec_session_runtime_components_t components;
    runtimec_snapshot_t snapshot;
    runtimec_compilation_repository_key_t key;
    runtimec_compilation_repository_lease_t lease;
    runtimec_compilation_repository_lookup_result_t lookup_result =
        RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
    binrwir_compilation_view_t compilation_view;
    binrwir_binfunc_view_t program_view;
    runtimec_frame_state_table_t *frame_state_table = NULL;
    runtimec_frame_state_t *frame_state = NULL;
    runtimec_frame_state_result_t frame_state_result = RUNTIMEC_FRAME_STATE_INVALID;
    bool frame_state_opened = false;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_binvthread_frame_t frame;
    binrwir_execution_exit_t execution_exit;
    runtimec_execution_window_diagnostic_t detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST;
    uint64_t binding_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    
    uint64_t frame_storage_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    size_t binding_capacity = 0U;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    bool lease_held = false;
    bool frame_ready = false;
    
    void *pathspace_handle = NULL;
    runtimec_status_t status;

    if (output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_execution_window_reset_result(output_result, RUNTIMEC_STATUS_INVALID,
                                (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST);
    if (session == NULL || request == NULL || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    runtimec_error_reset(error);
    if (!runtimec_string_view_is_valid(request->function_path) ||
        request->function_path.size == 0U ||
        !runtimec_string_view_is_valid(request->vthread_identifier) ||
        !runtimec_execution_window_vthread_is_numeric(
            request->vthread_identifier) ||
        !runtimec_string_view_is_valid(request->frame_path) ||
        request->frame_path.size == 0U ||
        (request->argument_count != 0U && request->arguments == NULL)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "invalid execution window request");
    }
    if (runtimec_session_get_pathspace_access(session, &host) !=
            RUNTIMEC_STATUS_SUCCESS ||
        host == NULL ||
        runtimec_session_get_runtime_components(session, &components) !=
            RUNTIMEC_STATUS_SUCCESS ||
        !runtimec_allocator_is_valid(&host->allocator)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY,
            "host services lack a capability this route requires");
    }
    
    if (runtimec_session_get_pathspace_handle(session, &pathspace_handle) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "cannot read the pathspace handle from the session");
    }
    if (runtimec_session_get_frame_state(session, &frame_state_table) !=
            RUNTIMEC_STATUS_SUCCESS ||
        frame_state_table == NULL) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "session has no frame state table");
    }

    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key =
        runtimec_key_view_make(request->function_path.data, request->function_path.size);
    status = runtimec_pathspace_access_source_revision(
        host, snapshot.function_key,
                                   &snapshot.source_revision, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error, status,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED,
                                   "source revision is unavailable");
    }
    
    if (snapshot.source_revision.generation == 0U ||
        snapshot.source_revision.instance_identifier == 0U) {
        return runtimec_execution_window_fail(output_result, error,
                                   RUNTIMEC_STATUS_UNSUPPORTED,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED,
                                   "source revision is not addressable");
    }

    memset(&key, 0, sizeof(key));
    key.snapshot = snapshot;
    key.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    key.binrwir_binary_interface_generation = BINRWIR_BINARY_INTERFACE_GENERATION;
    if (vmkv_encoder_get_canonical_encoding_revision(
            components.vmkv_encoder, &key.canonical_encoding_revision) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_compiler_get_revision(components.binrwir_compiler,
                                      &key.compiler_revision) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error,
                                   RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "component revision is unavailable");
    }
    memset(&lease, 0, sizeof(lease));
    status = runtimec_compilation_repository_find(
        components.compilation_repository, &key, &lookup_result, &lease, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error, status,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "compilation lookup failed");
    }
    if (lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND) {
        lease_held = true;
    } else if (lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND) {
        status = runtimec_execution_window_compile_and_insert(
            &components, host, request, &snapshot, &key, &lease, &detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_execution_window_fail(output_result, error, status, detail,
                                       "compilation was not produced");
        }
        lease_held = true;
    } else {
        return runtimec_execution_window_fail(output_result, error, RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "compilation lookup returned no verdict");
    }

    status = binrwir_compilation_get_view(lease.compilation, &compilation_view,
                                          error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_result->status = status;
        goto release_lease;
    }
    
    if (compilation_view.canonical_program == NULL ||
        (compilation_view.execution_target != BINRWIR_EXECUTION_TARGET_JIT &&
         compilation_view.execution_target != BINRWIR_EXECUTION_TARGET_VMBIN) ||
        (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_JIT &&
         compilation_view.binrwir_jit_code == NULL) ||
        (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_VMBIN &&
         compilation_view.binrwir_jit_code != NULL)) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID,
                                     "compilation is not executable");
        goto release_lease;
    }
    output_result->program = compilation_view.canonical_program;
    if (binrwir_binfunc_get_view(compilation_view.canonical_program,
                                 &program_view) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_frame_slot_capacity(compilation_view.canonical_program,
                                  &binding_capacity) !=
            RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED,
                                     "program view is unavailable");
        goto release_lease;
    }

    status = runtimec_pathspace_access_ensure_frame(
        host, &snapshot,
        runtimec_key_view_make(request->frame_path.data,
                               request->frame_path.size),
        request->function_path, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, status,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED,
            "frame could not be established");
        goto release_lease;
    }
    
    status = runtimec_pathspace_access_frame_generation(
        host, &snapshot,
        runtimec_key_view_make(request->frame_path.data,
                               request->frame_path.size),
        &frame_storage_generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, status,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED,
            "frame storage generation is unavailable");
        goto release_lease;
    }
    
    status = runtimec_frame_state_open(
        frame_state_table,
        runtimec_key_view_make(request->frame_path.data,
                               request->frame_path.size),
        request->function_path, frame_storage_generation, binding_capacity,
        &frame_state, &frame_state_result, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, status,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED,
            "frame state could not be opened");
        goto release_lease;
    }
    frame_state_opened = true;
    bindings = frame_state->bindings;
    
    binding_generation = frame_state->generation;
    if (frame_state_result != RUNTIMEC_FRAME_STATE_REUSED) {
        status = runtimec_execution_window_materialize_locals(
            host, &snapshot, compilation_view.canonical_program, request,
            bindings, binding_capacity, &binding_generation, &detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_execution_window_fail(
                output_result, error, status, detail,
                "local variables could not be materialized");
            goto release_frame_state;
        }
        status = runtimec_execution_window_bind_parameters(
            host, &snapshot, compilation_view.canonical_program, request,
            bindings, &binding_generation, &detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_execution_window_fail(
                output_result, error, status, detail,
                "frame parameter binding failed");
            goto release_frame_state;
        }
        if (binding_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
            status = runtimec_execution_window_fail(
                output_result, error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED,
                "frame binding has no storage generation");
            goto release_frame_state;
        }
        
        frame_state->generation = binding_generation;
    }
    if (runtimec_execution_window_find_instruction(
            compilation_view.canonical_program,
            request->source_logical_program_counter, &instruction_identifier,
            &detail) != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID, detail,
            "source program counter is not an instruction of this program");
        goto release_frame_state;
    }
    if (binrwir_binvthread_frame_initialize(
            &frame, compilation_view.canonical_program, NULL, binding_generation,
            program_view.identity.program_generation,
            instruction_identifier) != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED,
                                     "execution frame initialization failed");
        goto release_frame_state;
    }
    
    frame.pathspace_access = host;
    frame.snapshot = snapshot;
    
    frame.stack_identifier = request->stack_identifier;
    frame.vthread_identifier = request->vthread_identifier;
    frame.flags |= BINRWIR_BINVTHREAD_FRAME_FLAG_OBSERVABLE;
    
    frame.frame_path = runtimec_key_view_make(request->frame_path.data,
                                              request->frame_path.size);
    frame.bindings = bindings;
    frame_ready = true;

    memset(&execution_exit, 0, sizeof(execution_exit));
    if (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_JIT) {
        status = binrwir_jit_execute(compilation_view.binrwir_jit_code, &frame,
                                     &execution_exit, error);
    } else {
        status = vmbin_execute(compilation_view.canonical_program,
                               compilation_view.execution_plan, &frame,
                               components.builtin_registry, pathspace_handle, NULL,
                               &execution_exit);
    }
    output_result->execution_exit = execution_exit;
    output_result->status = execution_exit.status;
    
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        execution_exit.kind == BINRWIR_EXECUTION_EXIT_HANDOFF) {
        char function_path_text[512];
        char operation_key[512];
        runtimec_string_view_t operation;
        const int function_path_length =
            snprintf(function_path_text, sizeof(function_path_text), "%.*s",
                     (int)request->function_path.size,
                     request->function_path.data);
        if (function_path_length < 0 ||
            (size_t)function_path_length >= sizeof(function_path_text)) {
            status = RUNTIMEC_STATUS_INVALID;
        } else {
            status = runtimec_execution_window_operation_name(
                host, &snapshot, function_path_text,
                execution_exit.logical_program_counter, operation_key,
                sizeof(operation_key), &operation, error);
        }
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            runtimec_handoff_request_t handoff_request;
            memset(&handoff_request, 0, sizeof(handoff_request));
            handoff_request.operation = operation;
            runtimec_checkpoint_reset(&handoff_request.checkpoint);
            handoff_request.checkpoint.source_revision = snapshot.source_revision;
            handoff_request.checkpoint.vthread_identifier =
                request->vthread_identifier;
            handoff_request.checkpoint.frame_identifier = request->frame_path;
            handoff_request.checkpoint.persistent_status =
                runtimec_execution_window_persistent_status(
                    execution_exit.kind, request->stack_identifier);
            handoff_request.checkpoint.outcome = execution_exit.status;
            status = runtimec_pathspace_access_handoff(
                host, &handoff_request, error);
        }
        
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            
            if (status == RUNTIMEC_STATUS_WAIT &&
                operation.data != NULL && operation.size != 0U) {
                char detail_text[768];
                
                char operation_text[512];
                size_t operation_length = operation.size;

                if (operation_length >= sizeof(operation_text)) {
                    operation_length = sizeof(operation_text) - 1U;
                }
                memcpy(operation_text, operation.data, operation_length);
                operation_text[operation_length] = '\0';
                const int written = snprintf(
                    detail_text, sizeof(detail_text),
                    "no extension claimed this instruction (0x%08X) at pc %u in %.*s "
                    "— the opcode slot reads `%s`; is a handler missing from this build?",
                    (unsigned)BINRWIR_OPERATION_IDENTIFIER_FOREIGN,
                    (unsigned)execution_exit.logical_program_counter,
                    (int)request->function_path.size,
                    request->function_path.data, operation_text);

                if (written > 0 && (size_t)written < sizeof(detail_text) &&
                    error != NULL) {
                    runtimec_error_set(
                        error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                        (int64_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_HANDOFF_UNCLAIMED,
                        runtimec_string_view_make(detail_text, (size_t)written),
                        RUNTIMEC_PROGRAM_COUNTER_NONE);
                }
            }
            output_result->status = status;
            output_result->execution_exit.status = status;
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        
        output_result->status = status;
        output_result->execution_exit.status = status;
        if (error != NULL && error->message.size == 0U) {
            
            static _Thread_local char execution_message[96];
            (void)snprintf(execution_message, sizeof(execution_message),
                           "instruction execution failed (execution exit detail=%u)",
                           (unsigned)execution_exit.detail_code);
            runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)execution_exit.detail_code,
                               runtimec_string_view_from_c_string(
                                   execution_message),
                               (runtimec_program_counter_t){
                                   .stack_identifier =
                                       (int32_t)request->stack_identifier,
                                   .source_instruction_ordinal =
                                       (int32_t)execution_exit
                                           .logical_program_counter});
        }
        goto release_frame;
    }

    status = runtimec_execution_window_commit_checkpoint(session, host, &snapshot, request,
                                              &execution_exit, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_result->status = status;
        output_result->execution_exit.status = status;
        if (error != NULL && error->message.size == 0U) {
            static _Thread_local char checkpoint_message[96];
            (void)snprintf(checkpoint_message, sizeof(checkpoint_message),
                           "checkpoint commit failed (exit detail=%u)",
                           (unsigned)execution_exit.detail_code);
            runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)execution_exit.detail_code,
                               runtimec_string_view_from_c_string(
                                   checkpoint_message),
                               (runtimec_program_counter_t){
                                   .stack_identifier =
                                       (int32_t)request->stack_identifier,
                                   .source_instruction_ordinal =
                                       (int32_t)execution_exit
                                           .logical_program_counter});
        }
        goto release_frame;
    }
    status = RUNTIMEC_STATUS_SUCCESS;

release_frame:
    if (frame_ready) {
        (void)binrwir_binvthread_frame_destroy(&frame);
    }
release_frame_state:
    
    if (frame_state_opened) {
        
        const bool terminal =
            status != RUNTIMEC_STATUS_SUCCESS ||
            output_result->execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE ||
            output_result->execution_exit.kind == BINRWIR_EXECUTION_EXIT_ERROR;
        if (terminal) {
            runtimec_error_t ignored;
            runtimec_error_reset(&ignored);
            (void)runtimec_frame_state_close(
                frame_state_table,
                runtimec_key_view_make(request->frame_path.data,
                                       request->frame_path.size),
                &ignored);
        }
    }
release_lease:
    if (lease_held) {
        (void)runtimec_compilation_repository_release(
            components.compilation_repository, &lease);
    }
    return status;
}
