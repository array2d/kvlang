
#include "vmbin/call.h"

#include <string.h>

#include "frame_slot.h"

typedef enum vmbin_call_diagnostic {
    VMBIN_CALL_DIAG_INVALID_REQUEST = UINT32_C(0x4200) + 1U,
    VMBIN_CALL_DIAG_INVALID_OPERATION = UINT32_C(0x4200) + 2U,
    VMBIN_CALL_DIAG_FRAME_NOT_CALLABLE = UINT32_C(0x4200) + 3U,
    VMBIN_CALL_DIAG_ARGUMENT_COUNT = UINT32_C(0x4200) + 4U,
    VMBIN_CALL_DIAG_UNSUPPORTED_ACTUAL = UINT32_C(0x4200) + 5U,
    VMBIN_CALL_DIAG_UNRESOLVED_ACTUAL = UINT32_C(0x4200) + 6U,
    VMBIN_CALL_DIAG_ALIAS_VIOLATION = UINT32_C(0x4200) + 7U,
    VMBIN_CALL_DIAG_MISSING_ALIAS = UINT32_C(0x4200) + 8U,
    VMBIN_CALL_DIAG_OUT_OF_MEMORY = UINT32_C(0x4200) + 9U
} vmbin_call_diagnostic_t;

struct vmbin_call_control {
    runtimec_allocator_t allocator;
    uint32_t call_instruction_identifier;
    uint32_t return_logical_program_counter;
};

static void vmbin_call_initialize_exit(binrwir_execution_exit_t *output_exit,
                                       runtimec_status_t status,
                                       uint32_t detail_code) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = detail_code;
    output_exit->logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    output_exit->resume_logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
}

static runtimec_status_t vmbin_call_fail(binrwir_execution_exit_t *output_exit,
                                         runtimec_status_t status,
                                         vmbin_call_diagnostic_t detail_code) {
    vmbin_call_initialize_exit(output_exit, status, (uint32_t)detail_code);
    return status;
}

typedef enum vmbin_call_actual_kind {
    VMBIN_CALL_ACTUAL_INVALID = 0,
    VMBIN_CALL_ACTUAL_ADDRESS = 1,
    VMBIN_CALL_ACTUAL_INLINE = 2
} vmbin_call_actual_kind_t;

typedef struct vmbin_call_actual {
    vmbin_call_actual_kind_t kind;
    runtimec_xvalue_address_t xvalue_address;
    runtimec_bytes_view_t inline_bytes;
    runtimec_key_view_t slot_key;
} vmbin_call_actual_t;

static runtimec_status_t vmbin_call_resolve_actual(
    const binrwir_binvthread_frame_t *caller,
    const binrwir_argument_t *actual, vmbin_call_actual_t *output_actual) {
    const binrwir_frame_binding_t *binding = NULL;
    uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

    *output_actual = (vmbin_call_actual_t){VMBIN_CALL_ACTUAL_INVALID,
                                           RUNTIMEC_XVALUE_ADDRESS_NONE,
                                           runtimec_bytes_view_make(NULL, 0U),
                                           runtimec_key_view_make(NULL, 0U)};
    if (actual->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        runtimec_bytes_view_t literal;
        runtimec_string_view_t langtype;

        if (binrwir_binfunc_get_literal(caller->program,
                                        (uint32_t)actual->payload, &literal,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return RUNTIMEC_STATUS_INVALID;
        }
        output_actual->kind = VMBIN_CALL_ACTUAL_INLINE;
        output_actual->inline_bytes = literal;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (actual->reference_kind == BINRWIR_REFERENCE_STATIC) {
        runtimec_string_view_t key_text;
        runtimec_string_view_t key_langtype;

        if (actual->payload == BINRWIR_BODY_ADDRESS_NONE) {
            return RUNTIMEC_STATUS_INVALID;
        }
        output_actual->xvalue_address = runtimec_value_body_xvalue_address(
            (runtimec_value_body_address_t)actual->payload);
        if (output_actual->xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return RUNTIMEC_STATUS_INVALID;
        }
        
        if (binrwir_binfunc_get_symbol(caller->program, actual->slot,
                                       &key_text, &key_langtype) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return RUNTIMEC_STATUS_INVALID;
        }
        output_actual->slot_key =
            runtimec_key_view_make(key_text.data, key_text.size);
        output_actual->kind = VMBIN_CALL_ACTUAL_ADDRESS;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (caller->bindings == NULL ||
        vmbin_frame_slot_of_argument(caller->program, actual, &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_frame_bindings_get(caller->bindings, binding_slot,
                                   caller->snapshot_generation,
                                   &binding) != RUNTIMEC_STATUS_SUCCESS) {
        
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    if (binding->kind == (uint8_t)BINRWIR_BINDING_INLINE) {
        output_actual->kind = VMBIN_CALL_ACTUAL_INLINE;
        output_actual->inline_bytes =
            runtimec_bytes_view_make(binding->bytes, binding->length);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (binding->kind != (uint8_t)BINRWIR_BINDING_ADDRESS ||
        binding->xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_actual->kind = VMBIN_CALL_ACTUAL_ADDRESS;
    output_actual->xvalue_address = binding->xvalue_address;
    output_actual->slot_key = binding->slot_key.size != 0U
                                  ? binding->slot_key
                                  : binding->caller_key;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_call_bind_direction(
    const binrwir_binvthread_frame_t *caller,
    const binrwir_argument_t *actuals, size_t formal_count,
    binrwir_parameter_direction_t direction, const binrwir_binfunc_t *callee,
    binrwir_frame_bindings_t *bindings,
    runtimec_xvalue_address_t *output_addresses,
    vmbin_call_diagnostic_t *output_detail_code) {
    size_t formal_ordinal;

    for (formal_ordinal = 0U; formal_ordinal < formal_count; ++formal_ordinal) {
        const binrwir_argument_t *actual = &actuals[formal_ordinal];
        vmbin_call_actual_t resolved;
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
        runtimec_status_t status =
            vmbin_call_resolve_actual(caller, actual, &resolved);

        output_addresses[formal_ordinal] = RUNTIMEC_XVALUE_ADDRESS_NONE;
        if (status == RUNTIMEC_STATUS_UNSUPPORTED) {
            *output_detail_code = VMBIN_CALL_DIAG_UNSUPPORTED_ACTUAL;
            return status;
        }
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail_code = VMBIN_CALL_DIAG_UNRESOLVED_ACTUAL;
            return status;
        }
        if (vmbin_frame_slot_of_parameter(callee, direction,
                                          (uint32_t)formal_ordinal,
                                          &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail_code = VMBIN_CALL_DIAG_INVALID_OPERATION;
            return RUNTIMEC_STATUS_INVALID;
        }
        if (resolved.kind == VMBIN_CALL_ACTUAL_INLINE) {
            if (binrwir_frame_bindings_bind_inline(
                    bindings, binding_slot, resolved.slot_key,
                    resolved.inline_bytes, caller->snapshot_generation) !=
            RUNTIMEC_STATUS_SUCCESS) {
                *output_detail_code = VMBIN_CALL_DIAG_UNRESOLVED_ACTUAL;
                return RUNTIMEC_STATUS_INVALID;
            }
            continue;
        }
        
        if (binrwir_frame_bindings_bind(
                bindings, binding_slot, resolved.slot_key, resolved.slot_key,
                resolved.xvalue_address, caller->snapshot_generation) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail_code = VMBIN_CALL_DIAG_UNRESOLVED_ACTUAL;
            return RUNTIMEC_STATUS_INVALID;
        }
        output_addresses[formal_ordinal] = resolved.xvalue_address;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_call_check_alias(
    const binrwir_signature_view_t *signature,
    const runtimec_xvalue_address_t *read_addresses, size_t read_count,
    const runtimec_xvalue_address_t *write_addresses, size_t write_count,
    vmbin_call_diagnostic_t *output_detail_code) {
    size_t read_ordinal;
    size_t write_ordinal;

    for (read_ordinal = 0U; read_ordinal < read_count; ++read_ordinal) {
        for (write_ordinal = 0U; write_ordinal < write_count; ++write_ordinal) {
            binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
            runtimec_status_t status = binrwir_signature_get_alias_policy(
                signature, (uint32_t)read_ordinal, (uint32_t)write_ordinal,
                &policy);
            
            if (read_addresses[read_ordinal] == RUNTIMEC_XVALUE_ADDRESS_NONE ||
                write_addresses[write_ordinal] ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE) {
                continue;
            }
            const bool same_address =
                read_addresses[read_ordinal] == write_addresses[write_ordinal];

            if (status == RUNTIMEC_STATUS_UNSUPPORTED) {
                policy = BINRWIR_ALIAS_POLICY_INVALID;
            } else if (status != RUNTIMEC_STATUS_SUCCESS) {
                *output_detail_code = VMBIN_CALL_DIAG_INVALID_REQUEST;
                return status;
            }
            if (policy == BINRWIR_ALIAS_MUST_ALIAS && !same_address) {
                *output_detail_code = VMBIN_CALL_DIAG_MISSING_ALIAS;
                return RUNTIMEC_STATUS_INVALID;
            }
            if (policy != BINRWIR_ALIAS_MAY_ALIAS &&
                policy != BINRWIR_ALIAS_MUST_ALIAS && same_address) {
                *output_detail_code = VMBIN_CALL_DIAG_ALIAS_VIOLATION;
                return RUNTIMEC_STATUS_INVALID;
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_call_control_create(
    const binrwir_binvthread_frame_t *caller,
    const binrwir_binfunc_t *callee,
    const vmbin_call_request_t *request,
    const runtimec_allocator_t *allocator,
    vmbin_call_control_t **output_control) {
    vmbin_call_control_t *control;
    binrwir_instruction_view_t instruction;

    if (output_control != NULL) {
        *output_control = NULL;
    }
    if (caller == NULL || callee == NULL || request == NULL ||
        !runtimec_allocator_is_valid(allocator) || output_control == NULL ||
        caller->program == NULL ||
        request->call_instruction_identifier == BINRWIR_IDENTIFIER_NONE ||
        request->return_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE ||
        binrwir_binfunc_validate(callee) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_instruction(
            caller->program, request->call_instruction_identifier,
            &instruction) != RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    control = (vmbin_call_control_t *)runtimec_allocator_allocate(
        allocator, sizeof(*control), _Alignof(vmbin_call_control_t));
    if (control == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    
    control->allocator = *allocator;
    control->call_instruction_identifier = request->call_instruction_identifier;
    control->return_logical_program_counter =
        request->return_logical_program_counter;
    *output_control = control;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_call_control_destroy(vmbin_call_control_t *control) {
    runtimec_allocator_t allocator;

    if (control == NULL || !runtimec_allocator_is_valid(&control->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = control->allocator;
    return runtimec_allocator_deallocate(&allocator, control, sizeof(*control),
                                         _Alignof(vmbin_call_control_t));
}

runtimec_status_t vmbin_call_enter(binrwir_binvthread_frame_t *caller,
                                   binrwir_binvthread_frame_t *callee,
                                   const vmbin_call_control_t *control,
                                   binrwir_execution_exit_t *output_exit) {
    binrwir_instruction_view_t instruction;
    binrwir_binfunc_view_t callee_view;
    binrwir_signature_view_t signature;
    runtimec_xvalue_address_t *addresses = NULL;
    binrwir_frame_bindings_t *bindings = NULL;
    size_t read_count = 0U;
    size_t write_count = 0U;
    size_t address_capacity = 0U;
    size_t bind_slot_capacity = 0U;
    vmbin_call_diagnostic_t detail_code = VMBIN_CALL_DIAG_INVALID_REQUEST;
    runtimec_status_t status;

    if (output_exit != NULL) {
        vmbin_call_initialize_exit(output_exit, RUNTIMEC_STATUS_INVALID,
                                   (uint32_t)detail_code);
    }
    if (caller == NULL || callee == NULL || control == NULL ||
        output_exit == NULL || caller->program == NULL ||
        callee->program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (callee->bindings != NULL) {
        
        return vmbin_call_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_CALL_DIAG_FRAME_NOT_CALLABLE);
    }
    if (binrwir_binfunc_get_view(callee->program, &callee_view) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_instruction(caller->program,
                                        control->call_instruction_identifier,
                                        &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_call_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_CALL_DIAG_INVALID_OPERATION);
    }
    signature = callee_view.signature;
    read_count = signature.read_parameter_count;
    write_count = signature.write_parameter_count;
    
    if (instruction.read_arguments.count == 0U ||
        instruction.read_arguments.data[0].reference_kind !=
            BINRWIR_REFERENCE_FUNCTION ||
        instruction.read_arguments.count - 1U != read_count ||
        instruction.write_arguments.count != write_count ||
        vmbin_frame_slot_capacity(callee->program, &bind_slot_capacity) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_call_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_CALL_DIAG_ARGUMENT_COUNT);
    }
    address_capacity = read_count + write_count;
    if (address_capacity != 0U) {
        addresses = (runtimec_xvalue_address_t *)runtimec_allocator_allocate(
            &control->allocator,
            sizeof(*addresses) * address_capacity,
            _Alignof(runtimec_xvalue_address_t));
        if (addresses == NULL) {
            return vmbin_call_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                   VMBIN_CALL_DIAG_OUT_OF_MEMORY);
        }
    }
    if (binrwir_frame_bindings_create(
            &control->allocator, bind_slot_capacity == 0U ? 1U
                                                          : bind_slot_capacity,
            &bindings) != RUNTIMEC_STATUS_SUCCESS) {
        status = RUNTIMEC_STATUS_OUT_OF_MEMORY;
        detail_code = VMBIN_CALL_DIAG_OUT_OF_MEMORY;
    } else {
        status = vmbin_call_bind_direction(
            caller, &instruction.read_arguments.data[1], read_count,
            BINRWIR_PARAMETER_DIRECTION_READ, callee->program, bindings,
            addresses, &detail_code);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_call_bind_direction(
            caller, instruction.write_arguments.data, write_count,
            BINRWIR_PARAMETER_DIRECTION_WRITE, callee->program, bindings,
            addresses + read_count, &detail_code);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_call_check_alias(&signature, addresses, read_count,
                                        addresses + read_count, write_count,
                                        &detail_code);
    }
    if (addresses != NULL) {
        (void)runtimec_allocator_deallocate(
            &control->allocator, addresses, sizeof(*addresses) * address_capacity,
            _Alignof(runtimec_xvalue_address_t));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        if (bindings != NULL) {
            (void)binrwir_frame_bindings_destroy(bindings);
        }
        return vmbin_call_fail(output_exit, status, detail_code);
    }
    callee->parent = caller;
    
    callee->snapshot_generation = caller->snapshot_generation;
    callee->bindings = bindings;
    
    callee->logical_program_counter = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_call_return(binrwir_binvthread_frame_t *caller,
                                    binrwir_binvthread_frame_t *callee,
                                    const vmbin_call_control_t *control,
                                    binrwir_execution_exit_t *output_exit) {
    (void)control;

    if (output_exit != NULL) {
        vmbin_call_initialize_exit(output_exit, RUNTIMEC_STATUS_INVALID,
                                   (uint32_t)VMBIN_CALL_DIAG_INVALID_REQUEST);
    }
    if (caller == NULL || callee == NULL || output_exit == NULL ||
        callee->bindings == NULL || callee->parent != caller) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (binrwir_frame_bindings_destroy(callee->bindings) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_call_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_CALL_DIAG_FRAME_NOT_CALLABLE);
    }
    callee->bindings = NULL;
    callee->parent = NULL;
    callee->snapshot_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    return RUNTIMEC_STATUS_SUCCESS;
}
