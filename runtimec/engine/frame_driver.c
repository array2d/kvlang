
#include "engine/frame_driver.h"

#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "engine/execution_window.h"

static void effective_key_record(frame_driver_t *driver,
                                 const char *frame_path, uint16_t slot,
                                 const char *key) {
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (driver->effective_keys[ordinal].parameter_slot == slot &&
            strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            snprintf(driver->effective_keys[ordinal].key,
                     sizeof(driver->effective_keys[ordinal].key), "%s", key);
            return;
        }
    }
    if (driver->effective_key_count >= FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY) {
        return;
    }
    frame_driver_effective_key_t *entry = &driver->effective_keys[driver->effective_key_count++];
    snprintf(entry->frame_path, sizeof(entry->frame_path), "%s", frame_path);
    entry->parameter_slot = slot;
    snprintf(entry->key, sizeof(entry->key), "%s", key);
}

static const char *effective_key_lookup(const frame_driver_t *driver,
                                        const char *frame_path,
                                        uint16_t slot) {
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (driver->effective_keys[ordinal].parameter_slot == slot &&
            strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            return driver->effective_keys[ordinal].key;
        }
    }
    return NULL;
}

static void effective_key_forget_frame(frame_driver_t *driver,
                                       const char *frame_path) {
    size_t kept = 0U;
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            continue;
        }
        if (kept != ordinal) {
            driver->effective_keys[kept] = driver->effective_keys[ordinal];
        }
        kept += 1U;
    }
    driver->effective_key_count = kept;
}

runtimec_status_t frame_driver_initialize(frame_driver_t *driver,
                                          runtimec_session_t *session) {
    if (driver == NULL || session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(driver, 0, sizeof(*driver));
    driver->session = session;
    return RUNTIMEC_STATUS_SUCCESS;
}

frame_driver_outcome_t frame_driver_execute(
    frame_driver_t *driver, const char *function_path, uint32_t source_pc,
    int32_t depth, const char *frame_segment,
    const char *vthread_identifier,
    const runtimec_argument_description_t *arguments, size_t argument_count,
    binrwir_execution_exit_t *out_exit, char *output_message,
    size_t output_message_capacity) {
    
    char frame_path[128];
    
    const int frame_length =
        snprintf(frame_path, sizeof(frame_path), "/vthread/%s/[%s]",
                 vthread_identifier, frame_segment);
    if (frame_length < 0 || (size_t)frame_length >= sizeof(frame_path)) {
        return FRAME_DRIVER_OUTCOME_INVALID;
    }
    
    for (;;) {
    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string(function_path);
    request.vthread_identifier =
        runtimec_string_view_from_c_string(vthread_identifier);
    request.frame_path = runtimec_string_view_from_c_string(frame_path);
    request.stack_identifier = depth;
    request.source_logical_program_counter = source_pc;
    request.arguments = arguments;
    request.argument_count = argument_count;

    runtimec_execution_window_result_t result;
    memset(&result, 0, sizeof(result));
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    
    char message_copy[256];
    message_copy[0] = '\0';
    if (runtimec_execution_window_execute(driver->session, &request,
                                          &result, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *out_exit = result.execution_exit;
        if (error.message.data != NULL && error.message.size != 0U) {
            const size_t copy = error.message.size < sizeof(message_copy) - 1U
                                    ? error.message.size
                                    : sizeof(message_copy) - 1U;
            memcpy(message_copy, error.message.data, copy);
            message_copy[copy] = '\0';
        }
        
        if (output_message != NULL && output_message_capacity != 0U) {
            snprintf(output_message, output_message_capacity, "%s",
                     message_copy);
        }
        return FRAME_DRIVER_OUTCOME_EXECUTE_FAILED;
    }
    *out_exit = result.execution_exit;
    
    const binrwir_binfunc_t *frame_program = result.program;
    switch (result.execution_exit.kind) {
    case BINRWIR_EXECUTION_EXIT_DONE:
        return FRAME_DRIVER_OUTCOME_DONE;
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return FRAME_DRIVER_OUTCOME_HANDOFF;
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return FRAME_DRIVER_OUTCOME_ERROR;
    case BINRWIR_EXECUTION_EXIT_YIELD:
        
        return FRAME_DRIVER_OUTCOME_YIELD;
    case BINRWIR_EXECUTION_EXIT_CALL: {
        
        if (result.execution_exit.resume_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        runtimec_string_view_t callee_name =
            runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t callee_langtype =
            runtimec_string_view_make(NULL, 0U);
        
        if (binrwir_binfunc_get_symbol(
                frame_program,
                (uint16_t)result.execution_exit.target_identifier,
                &callee_name, &callee_langtype) != RUNTIMEC_STATUS_SUCCESS ||
            callee_name.size == 0U) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        char callee_path[FRAME_DRIVER_PATH_CAPACITY];
        const int callee_length =
            snprintf(callee_path, sizeof(callee_path), "/lib/%.*s",
                     (int)callee_name.size, callee_name.data);
        if (callee_length < 0 ||
            (size_t)callee_length >= sizeof(callee_path)) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        uint32_t call_instruction_identifier = 0U;
        binrwir_instruction_view_t call_instruction;
        memset(&call_instruction, 0, sizeof(call_instruction));
        if (binrwir_binfunc_find_instruction(
                frame_program,
                result.execution_exit.logical_program_counter,
                &call_instruction_identifier) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_get_instruction(frame_program,
                                            call_instruction_identifier,
                                            &call_instruction) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        runtimec_argument_description_t callee_arguments[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES];
        
        char read_local_keys[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        char write_target_keys[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        char write_target_langtypes[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        size_t callee_argument_count = 0U;
        memset(callee_arguments, 0, sizeof(callee_arguments));
        
        {
            size_t actual_argument_count = 0U;
            bool exceeds_limit = false;

            for (size_t ordinal = 0U;
                 ordinal < call_instruction.read_arguments.count; ++ordinal) {
                if (call_instruction.read_arguments.data[ordinal]
                        .reference_kind == BINRWIR_REFERENCE_FUNCTION) {
                    continue;
                }
                actual_argument_count += 1U;
            }
            actual_argument_count += call_instruction.write_arguments.count;
            if (actual_argument_count > (size_t)FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES) {
                exceeds_limit = true;
            }
            if (exceeds_limit) {
                (void)snprintf(
                    output_message, output_message_capacity,
                    "call passes %zu arguments but the driver supports at most "
                    "%u; arguments are never silently dropped",
                    actual_argument_count,
                    (unsigned)FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES);
                return FRAME_DRIVER_OUTCOME_INVALID;
            }
        }
        for (size_t ordinal = 0U;
             ordinal < call_instruction.read_arguments.count &&
             callee_argument_count < FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.read_arguments.data[ordinal];
            if (argument->reference_kind == BINRWIR_REFERENCE_FUNCTION) {
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
                runtimec_bytes_view_t wire;
                runtimec_string_view_t literal_langtype;
                memset(&wire, 0, sizeof(wire));
                if (binrwir_binfunc_get_literal(frame_program,
                                                argument->payload, &wire,
                                                &literal_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_LITERAL;
                callee_arguments[callee_argument_count].literal_bytes = wire;
                callee_arguments[callee_argument_count].literal_langtype =
                    literal_langtype;
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
                
                runtimec_string_view_t local_name =
                    runtimec_string_view_make(NULL, 0U);
                runtimec_string_view_t local_langtype =
                    runtimec_string_view_make(NULL, 0U);
                if (binrwir_binfunc_get_symbol(frame_program,
                                               (uint16_t)argument->slot,
                                               &local_name, &local_langtype) !=
                        RUNTIMEC_STATUS_SUCCESS ||
                    local_name.size == 0U) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                
                if (binrwir_frame_local_key(
                        runtimec_string_view_from_c_string(frame_path),
                        runtimec_string_view_make(local_name.data,
                                                  local_name.size),
                        read_local_keys[callee_argument_count],
                        sizeof(read_local_keys[callee_argument_count])) ==
                    SIZE_MAX) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        read_local_keys[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        for (size_t ordinal = 0U;
             ordinal < call_instruction.write_arguments.count &&
             callee_argument_count < FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.write_arguments.data[ordinal];
            
            if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE) {
                
                const char *effective =
                    effective_key_lookup(driver, frame_path, argument->slot);
                size_t parameter_key_size = SIZE_MAX;
                if (effective != NULL) {
                    const size_t effective_size = strlen(effective);
                    if (effective_size + 1U <=
                        sizeof(write_target_keys[callee_argument_count])) {
                        memcpy(write_target_keys[callee_argument_count],
                               effective, effective_size + 1U);
                        parameter_key_size = effective_size;
                    }
                }
                if (parameter_key_size == SIZE_MAX) {
                    parameter_key_size = binrwir_frame_parameter_key(
                        runtimec_string_view_from_c_string(frame_path),
                        argument->slot, true,
                        write_target_keys[callee_argument_count],
                        sizeof(write_target_keys[callee_argument_count]));
                }
                if (parameter_key_size == SIZE_MAX) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                
                runtimec_string_view_t parameter_type =
                    runtimec_string_view_make(NULL, 0U);
                {
                    binrwir_binfunc_view_t view;
                    if (binrwir_binfunc_get_view(frame_program, &view) !=
                            RUNTIMEC_STATUS_SUCCESS ||
                        (size_t)argument->slot >=
                            view.signature.write_parameter_count) {
                        return FRAME_DRIVER_OUTCOME_ERROR;
                    }
                    parameter_type =
                        view.signature.write_parameter_types[argument->slot];
                }
                const int type_written = snprintf(
                    write_target_langtypes[callee_argument_count],
                    sizeof(write_target_langtypes[callee_argument_count]),
                    "%.*s", (int)parameter_type.size, parameter_type.data);
                if (type_written < 0 ||
                    (size_t)type_written >=
                        sizeof(write_target_langtypes[callee_argument_count])) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        write_target_keys[callee_argument_count]);
                callee_arguments[callee_argument_count].literal_langtype =
                    runtimec_string_view_from_c_string(
                        write_target_langtypes[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            runtimec_string_view_t local_name =
                runtimec_string_view_make(NULL, 0U);
            runtimec_string_view_t local_langtype =
                runtimec_string_view_make(NULL, 0U);
            if (binrwir_binfunc_get_symbol(frame_program,
                                           (uint16_t)argument->slot,
                                           &local_name, &local_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                local_name.size == 0U) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            
            if (binrwir_frame_local_key(
                    runtimec_string_view_from_c_string(frame_path),
                    runtimec_string_view_make(local_name.data,
                                              local_name.size),
                    write_target_keys[callee_argument_count],
                    sizeof(write_target_keys[callee_argument_count])) ==
                SIZE_MAX) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            const int langtype_written = snprintf(
                write_target_langtypes[callee_argument_count],
                sizeof(write_target_langtypes[callee_argument_count]), "%.*s",
                (int)local_langtype.size, local_langtype.data);
            if (langtype_written < 0 ||
                (size_t)langtype_written >=
                    sizeof(write_target_langtypes[callee_argument_count])) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            callee_arguments[callee_argument_count].source =
                RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
            callee_arguments[callee_argument_count].key =
                runtimec_string_view_from_c_string(
                    write_target_keys[callee_argument_count]);
            callee_arguments[callee_argument_count].literal_langtype =
                runtimec_string_view_from_c_string(
                    write_target_langtypes[callee_argument_count]);
            callee_argument_count += 1U;
        }
        
        binrwir_execution_exit_t callee_exit;
        memset(&callee_exit, 0, sizeof(callee_exit));
        
        char callee_frame_segment[64];
        const int callee_segment_length = snprintf(
            callee_frame_segment, sizeof(callee_frame_segment), "%d.%u",
            (int)depth + 1,
            (unsigned)result.execution_exit.logical_program_counter);
        if (callee_segment_length < 0 ||
            (size_t)callee_segment_length >= sizeof(callee_frame_segment)) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        
        char callee_frame_path[128];
        const int callee_path_length = snprintf(
            callee_frame_path, sizeof(callee_frame_path), "/vthread/%s/[%s]",
            vthread_identifier, callee_frame_segment);
        if (callee_path_length > 0 &&
            (size_t)callee_path_length < sizeof(callee_frame_path)) {
            
            uint16_t write_ordinal = 0U;
            for (size_t ordinal = 0U; ordinal < callee_argument_count;
                 ++ordinal) {
                const runtimec_argument_description_t *description =
                    &callee_arguments[ordinal];
                if (description->source != RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET) {
                    continue;
                }
                char key_text[128];
                const size_t key_size = description->key.size;
                if (key_size == 0U || key_size + 1U > sizeof(key_text)) {
                    write_ordinal += 1U;
                    continue;
                }
                memcpy(key_text, description->key.data, key_size);
                key_text[key_size] = '\0';
                effective_key_record(driver, callee_frame_path, write_ordinal, key_text);
                write_ordinal += 1U;
            }
        }
        const frame_driver_outcome_t callee_outcome = frame_driver_execute(
            driver, callee_path, FRAME_DRIVER_CALLEE_ENTRY_SOURCE_PC,
            depth + 1, callee_frame_segment, vthread_identifier,
            callee_arguments, callee_argument_count, &callee_exit,
            output_message, output_message_capacity);
        
        if ((size_t)callee_path_length < sizeof(callee_frame_path)) {
            effective_key_forget_frame(driver, callee_frame_path);
        }
        if (callee_outcome != FRAME_DRIVER_OUTCOME_DONE) {
            
            *out_exit = callee_exit;
            return callee_outcome;
        }
        
        source_pc = result.execution_exit.resume_logical_program_counter;
        continue;
    }
    default:
        return FRAME_DRIVER_OUTCOME_INVALID;
    }
    }  
}
