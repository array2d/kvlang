
#include "interpreter.h"

#include <stdio.h>
#include <string.h>

#include "contract/error.h"
#include "contract/pathspace_access.h"
#include "contract/program_counter.h"
#include "core_operations.h"
#include "frame_slot.h"
#include "frame_target_key.h"
#include "value_access.h"
#include "vthread_status.h"

#define VMBIN_SCRATCH_INITIAL_CAPACITY 32U

typedef struct vmbin_execution {
    const binrwir_binfunc_t *program;
    const binrwir_registry_t *builtin_registry;
    vmbin_profile_t *profile;
    binrwir_binvthread_frame_t *frame;
    size_t instruction_count;
    
    runtimec_error_t error;
} vmbin_execution_t;

static void vmbin_interpreter_error_exit(binrwir_execution_exit_t *output_exit,
                                         runtimec_status_t status,
                                         uint32_t detail_code,
                                         uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    output_exit->detail_code = detail_code;
}

static void vmbin_interpreter_committed_exit(
    binrwir_execution_exit_t *output_exit, uint16_t kind,
    uint32_t source_program_counter, uint32_t resume_program_counter,
    uint32_t target_identifier) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = kind;
    output_exit->status = RUNTIMEC_STATUS_SUCCESS;
    output_exit->flags = BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = resume_program_counter;
    output_exit->target_identifier = target_identifier;
}

static runtimec_status_t vmbin_interpreter_fail(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, runtimec_status_t status,
    uint32_t detail_code, binrwir_execution_exit_t *output_exit) {
    (void)execution;
    vmbin_interpreter_error_exit(output_exit, status, detail_code,
                                 instruction->source_logical_program_counter);
    return status;
}

static runtimec_status_t vmbin_interpreter_fail_with_error(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, runtimec_status_t status,
    uint32_t default_detail_code, binrwir_execution_exit_t *output_exit) {
    const runtimec_error_t *inner = &execution->error;
    const bool has_inner_diagnostic =
        inner->code != 0 || inner->message.size != 0U;

    if (!has_inner_diagnostic) {
        return vmbin_interpreter_fail(execution, instruction, status,
                                      default_detail_code, output_exit);
    }
    
    vmbin_interpreter_error_exit(
        output_exit, status, (uint32_t)(inner->code < 0 ? 0 : inner->code),
        instruction->source_logical_program_counter);
    
    output_exit->resume_logical_program_counter =
        instruction->source_logical_program_counter;
    return status;
}

static runtimec_status_t vmbin_interpreter_locate(
    vmbin_execution_t *execution, const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location) {
    
    return vmbin_value_access_locate_body_read(execution->frame, argument,
                                               output_location,
                                               &execution->error);
}

static int vmbin_interpreter_decode_unsigned(const uint8_t *bytes,
                                             size_t size,
                                             uint64_t *output_value) {
    uint64_t value = 0U;

    if (bytes == NULL) {
        return 0;
    }
    switch (size) {
    case 1U: {
        uint8_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 2U: {
        uint16_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 4U: {
        uint32_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 8U:
        memcpy(&value, bytes, sizeof(value));
        break;
    default:
        return 0;
    }
    *output_value = value;
    return 1;
}

static runtimec_status_t vmbin_interpreter_resolve_target(
    vmbin_execution_t *execution, const binrwir_argument_t *argument,
    uint32_t *output_instruction_identifier) {
    vmbin_value_location_t target_location;
    uint64_t target = 0U;
    runtimec_status_t status;

    *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    status = vmbin_interpreter_locate(execution, argument, &target_location);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!vmbin_interpreter_decode_unsigned(target_location.read_bytes,
                                           target_location.size, &target) ||
        target > UINT32_MAX) {
        runtimec_error_set(&execution->error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)VMBIN_INTERPRETER_DIAG_UNDECODABLE_TARGET,
                           runtimec_string_view_from_c_string(
                               "branch target is not a 64-bit counter"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_binfunc_find_instruction(
        execution->program, (uint32_t)target, output_instruction_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_set(&execution->error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)VMBIN_INTERPRETER_DIAG_UNRESOLVED_TARGET,
                           runtimec_string_view_from_c_string(
                               "branch target is not in the program"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    return status;
}

static void vmbin_interpreter_advance(vmbin_execution_t *execution,
                                      uint32_t instruction_identifier) {
    execution->frame->logical_program_counter = instruction_identifier + 1U;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
}

static bool vmbin_interpreter_copy_binds_reference(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    const binrwir_argument_t *read_argument) {
    runtimec_string_view_t source_langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_string_view_t target_langtype = runtimec_string_view_make(NULL, 0U);
    vmbin_value_location_t source_location;

    if (vmbin_value_access_langtype(execution->frame, read_argument,
                                    &source_langtype, NULL) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_langtype(
            execution->frame, &instruction->write_arguments.data[0],
            &target_langtype, NULL) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    
    if (!vmbin_value_access_langtype_is_container(target_langtype)) {
        return false;
    }
    
    if (!vmbin_value_access_langtype_is_container(source_langtype)) {
        return false;
    }
    
    {
        
        runtimec_error_t ignored_error;

        runtimec_error_reset(&ignored_error);
        if (vmbin_value_access_locate(execution->frame, read_argument,
                                      &source_location, &ignored_error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return false;
        }
    }
    return !source_location.is_whole_wire && source_location.size == 0U;
}

static runtimec_status_t vmbin_interpreter_execute_copy(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_locate(execution,
                                      &instruction->read_arguments.data[0],
                                      &source);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    
    if (vmbin_interpreter_copy_binds_reference(
            execution, instruction,
            &instruction->read_arguments.data[0])) {
        char key_buffer[512];
        const binrwir_argument_t *read_argument =
            &instruction->read_arguments.data[0];
        runtimec_status_t reference_status;

        if (read_argument->dereference != 0U) {
            
            binrwir_argument_t plain = *read_argument;
            vmbin_value_location_t pointer_value;

            plain.dereference = 0U;
            reference_status = vmbin_value_access_locate_body(
                execution->frame, &plain, &pointer_value, &execution->error);
            if (reference_status == RUNTIMEC_STATUS_SUCCESS &&
                pointer_value.read_bytes != NULL && pointer_value.size != 0U &&
                pointer_value.size + 1U <= sizeof(key_buffer)) {
                memcpy(key_buffer, pointer_value.read_bytes,
                       pointer_value.size);
                key_buffer[pointer_value.size] = '\0';
                reference_status = RUNTIMEC_STATUS_SUCCESS;
            } else if (reference_status == RUNTIMEC_STATUS_SUCCESS) {
                reference_status = RUNTIMEC_STATUS_INVALID;
            }
        } else if (read_argument->reference_kind ==
                   BINRWIR_REFERENCE_PARAMETER_READ) {
            
            const binrwir_frame_binding_t *binding = NULL;
            uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

            if (execution->frame->bindings == NULL ||
                vmbin_frame_slot_of_argument(execution->frame->program,
                                             read_argument,
                                             &binding_slot) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                binrwir_frame_bindings_get(
                    execution->frame->bindings, binding_slot,
                    execution->frame->snapshot_generation, &binding) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                reference_status = RUNTIMEC_STATUS_UNSUPPORTED;
            } else {
                const runtimec_key_view_t key =
                    binding->caller_key.size != 0U ? binding->caller_key
                                                   : binding->slot_key;

                if (key.size == 0U || key.size + 1U > sizeof(key_buffer)) {
                    reference_status = RUNTIMEC_STATUS_INVALID;
                } else {
                    memcpy(key_buffer, key.data, key.size);
                    key_buffer[key.size] = '\0';
                    reference_status = RUNTIMEC_STATUS_SUCCESS;
                }
            }
        } else {
            reference_status = vmbin_value_access_argument_key(
                execution->frame, read_argument, key_buffer,
                sizeof(key_buffer), &execution->error);
        }
        if (reference_status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_interpreter_fail_with_error(
                execution, instruction, reference_status,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        status = vmbin_value_access_write_whole_value(
            execution->frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(key_buffer, strlen(key_buffer)),
            &execution->error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_interpreter_fail_with_error(
                execution, instruction, status,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        vmbin_interpreter_advance(execution, identifier);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    {
        vmbin_value_location_t probe;

        const vmbin_value_fact_t none_fact =
            vmbin_value_access_locate_body_read(
                execution->frame, &instruction->read_arguments.data[0],
                &probe, &execution->error) == RUNTIMEC_STATUS_SUCCESS
                ? vmbin_value_location_is_none(&probe)
                : VMBIN_VALUE_FACT_UNKNOWN;

        if (none_fact == VMBIN_VALUE_FACT_UNKNOWN) {
            runtimec_error_set(
                &execution->error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                runtimec_string_view_from_c_string(
                    "cannot tell whether the copied value is None: its wire "
                    "facts are unavailable on this path"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return vmbin_interpreter_fail_with_error(
                execution, instruction, RUNTIMEC_STATUS_INVALID,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        if (none_fact == VMBIN_VALUE_FACT_YES) {
            
            runtimec_string_view_t target_langtype =
                runtimec_string_view_make(NULL, 0U);
            runtimec_error_t ignored;

            runtimec_error_reset(&ignored);
            const bool target_known =
                vmbin_value_access_langtype(
                    execution->frame, &instruction->write_arguments.data[0],
                    &target_langtype, &ignored) == RUNTIMEC_STATUS_SUCCESS &&
                runtimec_string_view_is_valid(target_langtype) &&
                target_langtype.size != 0U;
            const bool target_is_reference =
                target_known &&
                (target_langtype.data[0] == '*' ||
                 vmbin_value_access_langtype_is_structref(target_langtype));

            if (!target_is_reference) {
                runtimec_error_set(
                    &execution->error, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                    (int64_t)VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
                    runtimec_string_view_from_c_string(
                        "None cannot be stored in a declared slot: the value has "
                        "an empty body and no langtype; check existence first "
                        "(`base·*key != None`)"),
                    RUNTIMEC_PROGRAM_COUNTER_NONE);
                return vmbin_interpreter_fail_with_error(
                    execution, instruction, RUNTIMEC_STATUS_ERROR,
                    VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
            }
        }
    }
    status = vmbin_value_access_write_body(
        execution->frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(source.read_bytes, source.size),
        &execution->error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    vmbin_interpreter_advance(execution, identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_goto(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_resolve_target(
        execution, &instruction->read_arguments.data[0], &target_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    execution->frame->logical_program_counter = target_identifier;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_branch(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t condition_location;
    uint64_t condition = 0U;
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 3U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_locate(execution,
                                      &instruction->read_arguments.data[0],
                                      &condition_location);
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        !vmbin_interpreter_decode_unsigned(condition_location.read_bytes,
                                           condition_location.size,
                                           &condition)) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    status = vmbin_interpreter_resolve_target(
        execution, &instruction->read_arguments.data[condition != 0U ? 1U : 2U],
        &target_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    execution->frame->logical_program_counter = target_identifier;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_call(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    size_t argument_ordinal;
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    uint32_t resume_program_counter = BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;

    for (argument_ordinal = 0U;
         argument_ordinal < instruction->read_arguments.count;
         ++argument_ordinal) {
        const binrwir_argument_t *argument =
            &instruction->read_arguments.data[argument_ordinal];

        if (argument->reference_kind == BINRWIR_REFERENCE_FUNCTION) {
            target_identifier = (uint32_t)argument->slot;
            break;
        }
    }
    if (target_identifier == BINRWIR_IDENTIFIER_NONE) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    }
    if (identifier + 1U < execution->instruction_count) {
        binrwir_instruction_view_t next_instruction;

        if (binrwir_binfunc_get_instruction(execution->program, identifier + 1U,
                                            &next_instruction) ==
            RUNTIMEC_STATUS_SUCCESS) {
            resume_program_counter =
                next_instruction.source_logical_program_counter;
        }
    }
    vmbin_interpreter_advance(execution, identifier);
    vmbin_interpreter_committed_exit(
        output_exit, BINRWIR_EXECUTION_EXIT_CALL,
        instruction->source_logical_program_counter, resume_program_counter,
        target_identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_return(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    vmbin_interpreter_committed_exit(
        output_exit, BINRWIR_EXECUTION_EXIT_DONE,
        instruction->source_logical_program_counter,
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE, 0U);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_dispatch_builtin(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    const binrwir_registry_entry_t *entry = NULL;
    runtimec_status_t status = binrwir_registry_find(
        execution->builtin_registry,
        instruction->instruction->operation_identifier, &entry);

    if (status == RUNTIMEC_STATUS_INVALID) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY, output_exit);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    }
    
    *output_exit = (binrwir_execution_exit_t){0};
    {
        const runtimec_status_t handler_status =
            entry->handler(execution->frame, instruction, output_exit);
        
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_NONE &&
            handler_status != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_interpreter_error_exit(
                output_exit, handler_status,
                VMBIN_INTERPRETER_DIAG_HANDLER_FAILED,
                instruction->source_logical_program_counter);
        }
    }
    if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
        
        return RUNTIMEC_STATUS_SUCCESS;
    }
    vmbin_interpreter_advance(execution, identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_step(
    vmbin_execution_t *execution, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    binrwir_instruction_view_t instruction;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    status = binrwir_binfunc_get_instruction(execution->program, identifier,
                                             &instruction);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_exit = (binrwir_execution_exit_t){0};
        output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
        output_exit->status = RUNTIMEC_STATUS_INVALID;
        output_exit->detail_code = VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION;
        output_exit->logical_program_counter =
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
        output_exit->resume_logical_program_counter =
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_reset(&execution->error);
    switch (instruction.instruction->operation_identifier) {
    case VMBIN_OPERATION_IDENTIFIER_COPY:
        return vmbin_interpreter_execute_copy(execution, &instruction,
                                              identifier, output_exit);
    case VMBIN_OPERATION_IDENTIFIER_GOTO:
        return vmbin_interpreter_execute_goto(execution, &instruction,
                                              output_exit);
    case VMBIN_OPERATION_IDENTIFIER_BRANCH:
        return vmbin_interpreter_execute_branch(execution, &instruction,
                                                output_exit);
    case VMBIN_OPERATION_IDENTIFIER_CALL:
        return vmbin_interpreter_execute_call(execution, &instruction,
                                              identifier, output_exit);
    case VMBIN_OPERATION_IDENTIFIER_RETURN:
        return vmbin_interpreter_execute_return(execution, &instruction,
                                                output_exit);
    case BINRWIR_OPERATION_IDENTIFIER_FOREIGN:
        
        *output_exit = (binrwir_execution_exit_t){0};
        output_exit->kind = BINRWIR_EXECUTION_EXIT_HANDOFF;
        output_exit->status = RUNTIMEC_STATUS_UNSUPPORTED;
        output_exit->logical_program_counter =
            instruction.source_logical_program_counter;
        output_exit->resume_logical_program_counter =
            instruction.source_logical_program_counter;
        return RUNTIMEC_STATUS_SUCCESS;
    case VMBIN_OPERATION_IDENTIFIER_NONE:
        return vmbin_interpreter_fail(
            execution, &instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    default:
        return vmbin_interpreter_dispatch_builtin(execution, &instruction,
                                                  identifier, output_exit);
    }
}

static bool vmbin_interpreter_observable(
    const binrwir_binvthread_frame_t *frame) {
    return (frame->flags & BINRWIR_BINVTHREAD_FRAME_FLAG_OBSERVABLE) != 0U;
}

static bool vmbin_interpreter_status_continue(
    vmbin_execution_t *execution, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    char key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    runtimec_string_view_t text;
    runtimec_error_t error;
    binrwir_instruction_view_t instruction;

    if (!vmbin_interpreter_observable(execution->frame) ||
        execution->frame->pathspace_access == NULL ||
        execution->frame->pathspace_access->handle == NULL ||
        runtimec_vthread_status_key(
            execution->frame->frame_path,
            runtimec_string_view_from_c_string("/‥status"), key,
            sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    runtimec_error_reset(&error);
    if (runtimec_vthread_status_read(
            execution->frame->pathspace_access->handle, key, &text, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    if ((text.size == 4U && memcmp(text.data, "init", 4U) == 0) ||
        (text.size == 7U && memcmp(text.data, "running", 7U) == 0) ||
        (text.size == 4U && memcmp(text.data, "wait", 4U) == 0)) {
        return true;
    }
    if (binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    if (text.size == 6U && memcmp(text.data, "paused", 6U) == 0) {
        vmbin_interpreter_committed_exit(
            output_exit, BINRWIR_EXECUTION_EXIT_YIELD,
            instruction.source_logical_program_counter,
            instruction.source_logical_program_counter, 0U);
    } else if (text.size == 5U && memcmp(text.data, "error", 5U) == 0) {
        vmbin_interpreter_error_exit(
            output_exit, RUNTIMEC_STATUS_ERROR, 0U,
            instruction.source_logical_program_counter);
    } else {
        vmbin_interpreter_committed_exit(
            output_exit, BINRWIR_EXECUTION_EXIT_DONE,
            instruction.source_logical_program_counter,
            instruction.source_logical_program_counter, 0U);
    }
    return false;
}

static void vmbin_interpreter_write_error_status(
    const binrwir_binvthread_frame_t *frame) {
    char key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    static const char error_text[] = "error";

    if (frame->pathspace_access == NULL ||
        frame->pathspace_access->handle == NULL ||
        runtimec_vthread_status_key(
            frame->frame_path,
            runtimec_string_view_from_c_string("/‥status"), key,
            sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    (void)runtimec_vthread_status_write(
        frame->pathspace_access->handle, key,
        runtimec_bytes_view_make((const uint8_t *)error_text,
                                 sizeof(error_text) - 1U));
}

static void vmbin_interpreter_commit_pc(vmbin_execution_t *execution,
                                        uint32_t source_program_counter) {
    runtimec_program_counter_t counter;
    runtimec_error_t error;

    if (!vmbin_interpreter_observable(execution->frame) ||
        execution->frame->pathspace_access == NULL ||
        !runtimec_string_view_is_valid(execution->frame->vthread_identifier) ||
        execution->frame->vthread_identifier.size == 0U) {
        return;
    }
    runtimec_error_reset(&error);
    counter.stack_identifier = execution->frame->stack_identifier;
    counter.source_instruction_ordinal = (int32_t)source_program_counter;
    if (runtimec_pathspace_access_write_program_counter(
            execution->frame->pathspace_access,
            execution->frame->vthread_identifier, counter, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        vmbin_interpreter_write_error_status(execution->frame);
    }
}

static void vmbin_interpreter_commit_entry(vmbin_execution_t *execution) {
    binrwir_instruction_view_t instruction;
    const uint32_t identifier = execution->frame->logical_program_counter;

    if (!vmbin_interpreter_observable(execution->frame) ||
        identifier == BINRWIR_IDENTIFIER_NONE ||
        (size_t)identifier >= execution->instruction_count ||
        binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    vmbin_interpreter_commit_pc(
        execution, instruction.source_logical_program_counter);
}

static void vmbin_interpreter_commit_step(vmbin_execution_t *execution) {
    binrwir_instruction_view_t instruction;
    const uint32_t identifier = execution->frame->logical_program_counter;

    if (!vmbin_interpreter_observable(execution->frame) ||
        (size_t)identifier >= execution->instruction_count ||
        binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    vmbin_interpreter_commit_pc(
        execution, instruction.source_logical_program_counter);
}

runtimec_status_t
vmbin_execute(const binrwir_binfunc_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_binvthread_frame_t *frame,
              const binrwir_registry_t *builtin_registry,
              void *pathspace_handle,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit) {
    vmbin_execution_t execution;
    binrwir_binfunc_view_t program_view;
    runtimec_status_t status;

    (void)execution_plan;
    if (output_exit != NULL) {
        vmbin_interpreter_error_exit(output_exit, RUNTIMEC_STATUS_INVALID,
                                     VMBIN_INTERPRETER_DIAG_INVALID_REQUEST,
                                     BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE);
    }
    if (program == NULL || frame == NULL || builtin_registry == NULL ||
        output_exit == NULL || frame->program != program) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_binfunc_validate(program);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_view(program, &program_view) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(&execution, 0, sizeof(execution));
    runtimec_error_reset(&execution.error);
    execution.program = program;
    execution.builtin_registry = builtin_registry;
    execution.profile = profile;
    execution.frame = frame;
    
    frame->pathspace_handle = pathspace_handle;
    
    execution.instruction_count = program_view.instruction_count;

    vmbin_interpreter_commit_entry(&execution);
    for (;;) {
        uint32_t identifier = frame->logical_program_counter;

        if (identifier == BINRWIR_IDENTIFIER_NONE) {
            vmbin_interpreter_error_exit(
                output_exit, RUNTIMEC_STATUS_INVALID,
                VMBIN_INTERPRETER_DIAG_INVALID_CURSOR,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE);
            status = RUNTIMEC_STATUS_INVALID;
            break;
        }
        if ((size_t)identifier >= program_view.instruction_count) {
            
            vmbin_interpreter_committed_exit(
                output_exit, BINRWIR_EXECUTION_EXIT_DONE,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE, 0U);
            status = RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        
        if (!vmbin_interpreter_status_continue(&execution, identifier,
                                               output_exit)) {
            status = output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR
                         ? RUNTIMEC_STATUS_ERROR
                         : RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        
        (void)vmbin_interpreter_step(&execution, identifier, output_exit);
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
            binrwir_instruction_view_t failing_instruction;
            
            if ((output_exit->logical_program_counter == 0U ||
                 output_exit->logical_program_counter ==
                     BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) &&
                binrwir_binfunc_get_instruction(program, identifier,
                                                &failing_instruction) ==
                    RUNTIMEC_STATUS_SUCCESS) {
                output_exit->logical_program_counter =
                    failing_instruction.source_logical_program_counter;
                output_exit->resume_logical_program_counter =
                    failing_instruction.source_logical_program_counter;
            }
            vmbin_interpreter_write_error_status(frame);
            status = output_exit->status != RUNTIMEC_STATUS_SUCCESS
                         ? output_exit->status
                         : RUNTIMEC_STATUS_ERROR;
            break;
        }
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
            
            status = RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        
        vmbin_interpreter_commit_step(&execution);
    }
    return status;
}
