
#include "contract/pathspace_interface.h"
#include "contract/langtype.h"
#include "value_access.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/frame_bindings.h"
#include "frame_slot.h"
#include "frame_target_key.h"
#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>

static runtimec_status_t vmbin_value_access_fail(
    runtimec_error_t *error, runtimec_status_t status,
    vmbin_value_access_diagnostic_t code, const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                       (int64_t)code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static runtimec_status_t vmbin_value_access_locate_member(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t member_name,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    char key_text[512];
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    runtimec_status_t status;

    (void)argument;
    if (frame->pathspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
            "frame carries no pathspace access for a member key");
    }
    
    status = vmbin_value_access_argument_key(frame, argument, key_text,
                                             sizeof(key_text), error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key cannot be built");
    }
    
    {
        
        runtimec_string_view_t base_name = runtimec_string_view_make(
            member_name.data, member_name.size);
        vmbin_value_location_t base_value;
        const char *separator = NULL;
        (void)base_value;

        for (size_t offset = 0U; offset + 2U <= member_name.size; ++offset) {
            if ((uint8_t)member_name.data[offset] == 0xC2U &&
                (uint8_t)member_name.data[offset + 1U] == 0xB7U) {
                separator = member_name.data + offset;
                break;
            }
        }
        if (separator != NULL) {
            base_name = runtimec_string_view_make(
                member_name.data, (size_t)(separator - member_name.data));
        }
        
        if (separator != NULL) {
            char base_key[512];
            const int written = snprintf(
                base_key, sizeof(base_key), "%.*s/%.*s",
                (int)frame->frame_path.size, frame->frame_path.data,
                (int)base_name.size, base_name.data);

            if (written > 0 && (size_t)written < sizeof(base_key)) {
                runtimec_xvalue_address_t base_address =
                    RUNTIMEC_XVALUE_ADDRESS_NONE;
                uint64_t base_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;

                if (runtimec_pathspace_access_resolve_frame_xvalue_address(
                        frame->pathspace_access, &frame->snapshot,
                        runtimec_key_view_from_c_string(base_key),
                        &base_address, &base_generation, NULL) ==
                        RUNTIMEC_STATUS_SUCCESS &&
                    base_address != RUNTIMEC_XVALUE_ADDRESS_NONE) {
                    const runtimec_value_body_address_t base_body =
                        runtimec_xvalue_body_address(base_address);
                    const size_t base_size =
                        (size_t)runtimec_value_body_length(base_body);

                    if (base_body != RUNTIMEC_VALUE_BODY_ADDRESS_NONE &&
                        base_size != 0U &&
                        ((const uint8_t *)(uintptr_t)base_body)[0] == '/' &&
                        base_size + member_name.size + 1U <= sizeof(key_text)) {
                        const size_t tail_size =
                            member_name.size -
                            (size_t)(separator - member_name.data);

                        memcpy(key_text, (const void *)(uintptr_t)base_body,
                               base_size);
                        memcpy(key_text + base_size, separator, tail_size);
                        key_text[base_size + tail_size] = '\0';
                    }
                }
            }
        }
        (void)base_value;
    }
    status = runtimec_pathspace_access_resolve_frame_xvalue_address(
        frame->pathspace_access, &frame->snapshot,
        runtimec_key_view_from_c_string(key_text), &xvalue_address,
        &generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        static _Thread_local char message[512];
        static const char prefix[] =
            "member key is not present at execution time: ";
        const size_t prefix_size = sizeof(prefix) - 1U;
        const size_t key_size = strlen(key_text);
        const size_t bounded =
            key_size < sizeof(message) - prefix_size - 1U
                ? key_size
                : sizeof(message) - prefix_size - 1U;

        memcpy(message, prefix, prefix_size);
        memcpy(message + prefix_size, key_text, bounded);
        message[prefix_size + bounded] = '\0';
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT, (const char *)message);
    }
    const runtimec_value_body_address_t body_address =
        runtimec_xvalue_body_address(xvalue_address);
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
            "member address has no value body");
    }
    output_location->read_bytes = (const uint8_t *)(uintptr_t)body_address;
    
    output_location->write_bytes = (uint8_t *)(uintptr_t)body_address;
    output_location->size = (size_t)runtimec_value_body_length(body_address);
    output_location->is_whole_wire = false;
    (void)member_name;
    return RUNTIMEC_STATUS_SUCCESS;
}

vmbin_value_fact_t vmbin_value_location_is_none(
    const vmbin_value_location_t *location) {
    if (location == NULL || location->body_is_empty == VMBIN_VALUE_FACT_UNKNOWN ||
        location->has_langtype_record == VMBIN_VALUE_FACT_UNKNOWN ||
        location->is_directory_key == VMBIN_VALUE_FACT_UNKNOWN) {
        return VMBIN_VALUE_FACT_UNKNOWN;
    }
    if (location->is_directory_key == VMBIN_VALUE_FACT_YES) {
        return VMBIN_VALUE_FACT_NO;
    }
    return (location->body_is_empty == VMBIN_VALUE_FACT_YES &&
            location->has_langtype_record == VMBIN_VALUE_FACT_NO)
               ? VMBIN_VALUE_FACT_YES
               : VMBIN_VALUE_FACT_NO;
}

static void vmbin_value_access_fill_none_facts(
    vmbin_value_location_t *location) {
    const void *wire = NULL;

    if (location == NULL) {
        return;
    }
    if (location->is_whole_wire) {
        wire = location->read_bytes;
    } else if (location->read_bytes != NULL) {
        const runtimec_xvalue_address_t base =
            runtimec_value_body_xvalue_address(
                (runtimec_value_body_address_t)(uintptr_t)location->read_bytes);
        if (base != RUNTIMEC_XVALUE_ADDRESS_NONE) {
            wire = (const void *)(uintptr_t)base;
        }
    }
    if (wire == NULL) {
        return; 
    }
    {
        const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)wire;
        const uint64_t total = pathspaceXvalueHeadTotalLen(head);

        if (total == 0U) {
            return; 
        }
        location->body_is_empty = pathspaceXvalueHeadLen(head) == 0U
                                      ? VMBIN_VALUE_FACT_YES
                                      : VMBIN_VALUE_FACT_NO;
        location->has_langtype_record =
            pathspaceXvalueHeadAttrsLen(head) == 0U ? VMBIN_VALUE_FACT_NO
                                                  : VMBIN_VALUE_FACT_YES;
    }
    
}

static runtimec_status_t vmbin_value_access_locate_plain(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

static runtimec_status_t vmbin_value_access_dereference_ex(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, bool missing_target_is_none,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;
    vmbin_value_location_t pointer_location;
    runtimec_xvalue_address_t target_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_value_body_address_t target_body = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    uint8_t form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    runtimec_status_t status;

    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "a literal has no address to follow");
    }
    plain.dereference = 0U;
    status = vmbin_value_access_locate_plain(frame, &plain, &pointer_location,
                                             error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_form(frame, &plain, &form, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (form != VMBIN_VALUE_ACCESS_FORM_POINTER) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER,
            "dereferenced operand does not hold a pointer");
    }
    if (pointer_location.read_bytes == NULL || pointer_location.size == 0U ||
        frame->pathspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "pointer value does not carry a resolvable target key");
    }
    {
        const runtimec_key_view_t key = runtimec_key_view_make(
            (const char *)pointer_location.read_bytes, pointer_location.size);

        if (pointer_location.size >= sizeof("/vthread/") - 1U &&
            memcmp(pointer_location.read_bytes, "/vthread/",
                   sizeof("/vthread/") - 1U) == 0) {
            uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
            status = runtimec_pathspace_access_resolve_frame_xvalue_address(
                frame->pathspace_access, &frame->snapshot, key, &target_address,
                &generation, error);
            if (status == RUNTIMEC_STATUS_SUCCESS &&
                generation != frame->snapshot_generation) {
                return vmbin_value_access_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                    "dereference target is not in the frame generation");
            }
        } else {
            status = runtimec_pathspace_access_resolve_static_xvalue_address(
                frame->pathspace_access, &frame->snapshot, key, &target_address,
                error);
        }
    }
    
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        target_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        runtimec_xvalue_body_address(target_address) ==
            RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
        if (missing_target_is_none) {
            output_location->read_bytes = NULL;
            output_location->write_bytes = NULL;
            output_location->size = 0U;
            output_location->is_whole_wire = false;
            
            output_location->body_is_empty = VMBIN_VALUE_FACT_YES;
            output_location->has_langtype_record = VMBIN_VALUE_FACT_NO;
            output_location->is_directory_key = VMBIN_VALUE_FACT_NO;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return status != RUNTIMEC_STATUS_SUCCESS
                   ? status
                   : vmbin_value_access_fail(
                         error, RUNTIMEC_STATUS_INVALID,
                         VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                         "dereference target has no derivable body");
    }
    target_body = runtimec_xvalue_body_address(target_address);
    output_location->read_bytes = (const uint8_t *)(uintptr_t)target_body;
    output_location->write_bytes = (uint8_t *)(uintptr_t)target_body;
    output_location->size = runtimec_value_body_length(target_body);
    output_location->is_whole_wire = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || argument == NULL || output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    runtimec_status_t status;
    if (argument->dereference != 0U) {
        status = vmbin_value_access_dereference_ex(frame, argument, false,
                                                   output_location, error);
    } else {
        status = vmbin_value_access_locate_plain(frame, argument,
                                                 output_location, error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        vmbin_value_access_fill_none_facts(output_location);
    }
    return status;
}

static runtimec_status_t vmbin_value_access_locate_plain(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    runtimec_bytes_view_t literal;
    runtimec_string_view_t langtype;
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_value_body_address_t body_address = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    runtimec_status_t status;

    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || frame->program == NULL || argument == NULL ||
        output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        status = binrwir_binfunc_get_literal(frame->program,
                                             (uint32_t)argument->payload,
                                             &literal, &langtype);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL,
                "literal index is not in the program pool");
        }
        output_location->read_bytes = literal.data;
        output_location->write_bytes = NULL;
        output_location->size = literal.size;
        output_location->is_whole_wire = true;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_STATIC:
        
        if (argument->payload == BINRWIR_BODY_ADDRESS_NONE) {
            
            runtimec_string_view_t key_text = runtimec_string_view_make(NULL, 0U);
            runtimec_string_view_t key_langtype;
            runtimec_xvalue_address_t resolved = RUNTIMEC_XVALUE_ADDRESS_NONE;
            if (frame->pathspace_access == NULL ||
                binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                           &key_text, &key_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                runtimec_pathspace_access_resolve_static_xvalue_address(
                    frame->pathspace_access, &frame->snapshot,
                    runtimec_key_view_make(key_text.data, key_text.size),
                    &resolved, error) != RUNTIMEC_STATUS_SUCCESS ||
                resolved == RUNTIMEC_XVALUE_ADDRESS_NONE) {
                {
                    
                    static const char prefix[] =
                        "static key is unresolved at execution time: ";
                    static _Thread_local char message[512];
                    const size_t prefix_size = sizeof(prefix) - 1U;
                    const size_t key_size =
                        key_text.size < sizeof(message) - prefix_size - 1U
                            ? key_text.size
                            : sizeof(message) - prefix_size - 1U;
                    
                    if (key_size == 0U) {
                        static const char bare[] =
                            "static key is unresolved at execution time";
                        memcpy(message, bare, sizeof(bare));
                    } else {
                        memcpy(message, prefix, prefix_size);
                        memcpy(message + prefix_size, key_text.data, key_size);
                        message[prefix_size + key_size] = '\0';
                    }
                    return vmbin_value_access_fail(
                        error, RUNTIMEC_STATUS_INVALID,
                        VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, message);
                }
            }
            body_address = runtimec_xvalue_body_address(resolved);
        } else {
            body_address = (runtimec_value_body_address_t)argument->payload;
        }
        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
                "static argument has no derivable value body");
        }
        break;
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
    case BINRWIR_REFERENCE_LOCAL: {
        const binrwir_frame_binding_t *binding = NULL;
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

        {
            runtimec_string_view_t member_name;
            runtimec_string_view_t member_langtype;
            if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                           &member_name, &member_langtype) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                binrwir_symbol_name_is_member(member_name)) {
                return vmbin_value_access_locate_member(frame, argument,
                                                        member_name,
                                                        output_location, error);
            }
        }
        if (frame->bindings == NULL ||
            vmbin_frame_slot_of_argument(frame->program, argument,
                                         &binding_slot) !=
                RUNTIMEC_STATUS_SUCCESS ||
            binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                       frame->snapshot_generation,
                                       &binding) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "argument slot is not bound");
        }
        if (binding->kind == (uint8_t)BINRWIR_BINDING_INLINE) {
            
            output_location->read_bytes = binding->bytes;
            output_location->write_bytes = NULL;
            output_location->size = binding->length;
            output_location->is_whole_wire = true;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (binding->kind != (uint8_t)BINRWIR_BINDING_ADDRESS ||
            binding->xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "binding carries no XValue address");
        }
        xvalue_address = binding->xvalue_address;
    }
        body_address = runtimec_xvalue_body_address(xvalue_address);
        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "binding address has no value body");
        }
        break;
    default:
        
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "argument has no value body");
    }
    
    output_location->read_bytes = (const uint8_t *)(uintptr_t)body_address;
    output_location->write_bytes = (uint8_t *)(uintptr_t)body_address;
    output_location->size = (size_t)runtimec_value_body_length(body_address);
    output_location->is_whole_wire = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_buffer_is_valid(output)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid read buffer");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->size = location.size;
    if (output->data == NULL || output->capacity < location.size) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    if (location.size != 0U) {
        memcpy(output->data, location.read_bytes, location.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate_body_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || argument == NULL || output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    if (argument->dereference != 0U) {
        status = vmbin_value_access_dereference_ex(frame, argument, true,
                                                   output_location, error);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            vmbin_value_access_fill_none_facts(output_location);
        }
        return status;
    }
    status = vmbin_value_access_locate_plain(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    vmbin_value_access_fill_none_facts(&location);
    if (!location.is_whole_wire) {
        *output_location = location;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    {
        const runtimec_value_body_address_t body_address =
            runtimec_xvalue_body_address(
                (runtimec_xvalue_address_t)(uintptr_t)location.read_bytes);

        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "wire has no derivable value body");
        }
        *output_location = location;
        output_location->read_bytes =
            (const uint8_t *)(uintptr_t)body_address;
        output_location->write_bytes = NULL;
        output_location->size =
            (size_t)runtimec_value_body_length(body_address);
        output_location->is_whole_wire = false;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status =
        vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (!location.is_whole_wire) {
        *output_location = location;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    {
        const runtimec_value_body_address_t body_address =
            runtimec_xvalue_body_address(
                (runtimec_xvalue_address_t)(uintptr_t)location.read_bytes);

        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "wire has no derivable value body");
        }
        output_location->read_bytes =
            (const uint8_t *)(uintptr_t)body_address;
        
        output_location->write_bytes = NULL;
        output_location->size = (size_t)runtimec_value_body_length(
            body_address);
        output_location->is_whole_wire = false;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_read_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_buffer_is_valid(output)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid read buffer");
    }
    
    status = vmbin_value_access_locate_body_read(frame, argument, &location,
                                                 error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->size = location.size;
    if (output->data == NULL || output->capacity < location.size) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    if (location.size != 0U) {
        memcpy(output->data, location.read_bytes, location.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_value_access_create_member(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    runtimec_string_view_t declared;
    char langtype_text[256];
    char key_text[512];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype does not fit the member slot");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    status = vmbin_frame_target_key(frame, argument, key_text,
                                    sizeof(key_text));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key cannot be built");
    }
    
    if (pathlangXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "member value cannot be encoded as an XValue");
    }
    message[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(frame->pathspace_handle, key_text,
                                                 wire, (uint32_t)wire_size,
                                                 message, sizeof(message));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key could not be created in storage");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool vmbin_value_access_writes_through_binding_slot(
    const binrwir_argument_t *argument) {
    if (argument->dereference != 0U) {
        return false;
    }
    return argument->reference_kind == BINRWIR_REFERENCE_LOCAL ||
           argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE;
}

static runtimec_status_t vmbin_value_access_create_unbound_local(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    const binrwir_frame_binding_t *binding = NULL;
    runtimec_string_view_t declared;
    runtimec_key_view_t target_key;
    char langtype_text[256];
    char key_text[512];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    runtimec_status_t status;

    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
        if (argument->reference_kind != BINRWIR_REFERENCE_PARAMETER_WRITE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
                "this reference kind has no binding slot to create or verify");
        }
        if (frame->bindings == NULL ||
            vmbin_frame_slot_of_argument(frame->program, argument,
                                         &binding_slot) !=
                RUNTIMEC_STATUS_SUCCESS ||
            binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                       frame->snapshot_generation,
                                       &binding) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "write parameter has no binding; the caller side never bound "
                "it");
        }
        
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (frame->bindings == NULL || frame->pathspace_access == NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;
        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            return vmbin_value_access_create_member(frame, argument,
                                                     value, error);
        }
    }
    if (vmbin_frame_slot_of_argument(frame->program, argument, &binding_slot) !=
        RUNTIMEC_STATUS_SUCCESS) {
        
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "local slot index is not in the program pool");
    }
    if (binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                   frame->snapshot_generation,
                                   &binding) == RUNTIMEC_STATUS_SUCCESS) {
        
        return RUNTIMEC_STATUS_SUCCESS;
    }
    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (declared.size == 0U) {
        if (!runtimec_bytes_view_is_valid(value) || value.data == NULL ||
            value.size < PATHSPACE_XVALUE_HEAD_LEN) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "local slot has no declared langtype and its written value "
                "carries no readable head");
        }
        const uint8_t *head_langtype_bytes = NULL;
        const uint16_t head_langtype_size = pathlangXvalueHeadLangtypeBounded(
            value.data, (uint64_t)value.size, &head_langtype_bytes);
        const runtimec_string_view_t head_langtype =
            head_langtype_size != 0U && head_langtype_bytes != NULL
                ? runtimec_string_view_make(
                      (const char *)(const void *)head_langtype_bytes,
                      (size_t)head_langtype_size)
                : runtimec_string_view_make(NULL, 0U);
        if (!runtimec_string_view_is_valid(head_langtype) ||
            head_langtype.size == 0U) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "local slot has no declared langtype and the written wire's "
                "head carries none either");
        }
        declared = head_langtype;
    }
    if (declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID, VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype does not fit the frame slot");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    status = vmbin_frame_target_key(frame, argument, key_text, sizeof(key_text));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID, VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "local slot key cannot be built");
    }
    if (pathlangXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR, VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "written value cannot be encoded as an XValue");
    }
    message[0] = '\0';
    if (!runtimec_pathspace_write_wire(frame->pathspace_handle, key_text, wire,
                                   (uint32_t)wire_size, message,
                                   sizeof(message))) {
        free(wire);
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR, VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "local slot could not be created in storage");
    }
    free(wire);
    target_key = runtimec_key_view_make(key_text, strlen(key_text));
    return vmbin_value_access_bind_write_target(frame, argument, target_key,
                                                error);
}

static runtimec_status_t vmbin_value_access_create_static_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    runtimec_string_view_t key_text;
    runtimec_string_view_t key_langtype;
    char langtype_text[256];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    if (frame->pathspace_handle == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target has no pathspace handle; the key cannot be "
            "created or written");
    }
    status = binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                        &key_text, &key_langtype);
    if (status != RUNTIMEC_STATUS_SUCCESS || key_text.size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target has no key in the symbol pool");
    }
    
    if (!runtimec_string_view_is_valid(key_langtype) ||
        key_langtype.size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "static write target has no declared langtype; a key cannot be "
            "created without one");
    }
    if (key_langtype.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "static write target's langtype does not fit the buffer");
    }
    memcpy(langtype_text, key_langtype.data, key_langtype.size);
    langtype_text[key_langtype.size] = '\0';
    if (value.size != 0U && value.data == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write value has no bytes");
    }
    if (pathlangXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "static write value cannot be encoded as an XValue");
    }
    char key_c_string[512];
    if (key_text.size + 1U > sizeof(key_c_string)) {
        free(wire);
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target key does not fit the buffer");
    }
    memcpy(key_c_string, key_text.data, key_text.size);
    key_c_string[key_text.size] = '\0';
    message[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        frame->pathspace_handle, key_c_string, wire, (uint32_t)wire_size, message,
        sizeof(message));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            message[0] != '\0' ? message
                                : "static write target could not be written");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, vmbin_value_location_t *output_location,
    runtimec_error_t *error) {
    runtimec_status_t status =
        vmbin_value_access_locate_body(frame, argument, output_location, error);

    if (status == RUNTIMEC_STATUS_SUCCESS) {
        if (output_location->write_bytes != NULL) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL &&
        frame->program != NULL && frame->pathspace_access != NULL) {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            char key_text[512];

            status = vmbin_value_access_argument_key(
                frame, argument, key_text, sizeof(key_text), error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            if (!runtimec_pathspace_access_langtype_has_fixed_zero_width(
                    member_langtype)) {
                
                return status;
            }
            status = runtimec_pathspace_access_ensure_zero_value(
                frame->pathspace_access, &frame->snapshot,
                runtimec_key_view_from_c_string(key_text), member_langtype,
                error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            return vmbin_value_access_locate_body(frame, argument,
                                                  output_location, error);
        }
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC ||
        frame->program == NULL || frame->pathspace_access == NULL) {
        return status;
    }
    {
        runtimec_string_view_t key_text;
        runtimec_string_view_t key_langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &key_text, &key_langtype) !=
                RUNTIMEC_STATUS_SUCCESS ||
            key_text.size == 0U) {
            return status;
        }
        if (!runtimec_string_view_is_valid(key_langtype) ||
            key_langtype.size == 0U) {
            
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "static write target has no declared langtype; the key cannot "
                "be created without one");
        }
        status = runtimec_pathspace_access_ensure_zero_value(
            frame->pathspace_access, &frame->snapshot,
            runtimec_key_view_make(key_text.data, key_text.size), key_langtype,
            error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    
    status = vmbin_value_access_locate_body(frame, argument, output_location,
                                            error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (output_location->write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_value_access_create_dereference_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;
    vmbin_value_location_t pointer_value;
    runtimec_string_view_t declared = runtimec_string_view_make(NULL, 0U);
    char langtype_text[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char message[256];
    runtimec_status_t status;

    if (frame->pathspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "frame carries no pathspace access to create a dereference target");
    }
    
    plain.dereference = 0U;
    status = vmbin_value_access_locate_body(frame, &plain, &pointer_value,
                                            error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (pointer_value.read_bytes == NULL || pointer_value.size == 0U ||
        pointer_value.read_bytes[0] != '/') {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "dereference target key is not an absolute key");
    }
    
    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (declared.size != 0U && declared.data[0] == '*') {
        declared = runtimec_string_view_make(declared.data + 1U,
                                             declared.size - 1U);
    }
    if (declared.size == 0U || declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "dereference write target has no usable declared langtype");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    if (pathlangXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "dereference write target cannot be encoded as an XValue");
    }
    message[0] = '\0';
    {
        char key_text[512];
        const size_t key_size = pointer_value.size < sizeof(key_text) - 1U
                                    ? pointer_value.size
                                    : sizeof(key_text) - 1U;

        memcpy(key_text, pointer_value.read_bytes, key_size);
        key_text[key_size] = '\0';
        if (!runtimec_pathspace_write_wire(frame->pathspace_handle, key_text, wire,
                                       (uint32_t)wire_size, message,
                                       sizeof(message))) {
            free(wire);
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
                "dereference write target could not be created in storage");
        }
    }
    free(wire);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_view_is_valid(value)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write value");
    }
    
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC &&
        argument->payload == BINRWIR_BODY_ADDRESS_NONE) {
        return vmbin_value_access_create_static_target(frame, argument, value,
                                                       error);
    }
    status = vmbin_value_access_writes_through_binding_slot(argument)
                 ? vmbin_value_access_create_unbound_local(frame, argument,
                                                           value, error)
                 : RUNTIMEC_STATUS_SUCCESS;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_locate_body(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        
        if (argument->dereference != 0U) {
            status = vmbin_value_access_create_dereference_target(
                frame, argument, value, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            status = vmbin_value_access_locate_body(frame, argument, &location,
                                                    error);
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    
    if (value.size != location.size) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
            "value does not match the current body size");
    }
    if (value.size != 0U) {
        memcpy(location.write_bytes, value.data, value.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_view_is_valid(value)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write value");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    if (value.size != location.size) {
        
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
            "value does not match the current body size");
    }
    if (value.size != 0U) {
        memcpy(location.write_bytes, value.data, value.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write_whole_value(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t body,
    runtimec_error_t *error) {
    if (!runtimec_bytes_view_is_valid(body)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write body");
    }
    
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC) {
        return vmbin_value_access_create_static_target(frame, argument, body,
                                                       error);
    }
    return vmbin_value_access_create_unbound_local(frame, argument, body, error);
}

bool vmbin_value_access_langtype_is_structref(runtimec_string_view_t langtype) {
    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    
    if (langtype.data[0] == '*') {
        return false;
    }
    if (langtype.size >= 10U &&
        memcmp(langtype.data, "def struct", 10U) == 0) {
        return true;
    }
    return langtype.size > sizeof("/lib/") - 1U &&
           memcmp(langtype.data, "/lib/", sizeof("/lib/") - 1U) == 0;
}

bool vmbin_value_access_langtype_is_container(runtimec_string_view_t langtype) {
    runtimec_langtype_t parsed;

    if (vmbin_value_access_langtype_is_structref(langtype)) {
        return true;
    }
    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    if (runtimec_langtype_parse(langtype, &parsed) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    return parsed.form == RUNTIMEC_LANGTYPE_FORM_MAPPING;
}

bool vmbin_value_access_langtype_is_path_string(runtimec_string_view_t langtype) {
    runtimec_string_view_t rest = langtype;

    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    if (langtype.data[0] == '*') {
        return false;
    }
    
    while (rest.size != 0U && rest.data[0] == '[') {
        const char *closing = memchr(rest.data, ']', rest.size);

        if (closing == NULL) {
            return false;
        }
        rest = runtimec_string_view_make(closing + 1,
                                         rest.size - (size_t)(closing - rest.data) - 1U);
    }
    return rest.size >= 5U && memcmp(rest.data, "char/", 5U) == 0;
}

runtimec_status_t vmbin_value_access_langtype(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, runtimec_error_t *error) {
    const runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);

    if (output_langtype != NULL) {
        *output_langtype = empty;
    }
    if (frame == NULL || frame->program == NULL || argument == NULL ||
        output_langtype == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid langtype request");
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LITERAL: {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;

        if (binrwir_binfunc_get_literal(frame->program,
                                        (uint32_t)argument->payload, &wire,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "literal index is not in the program pool");
        }
        *output_langtype = langtype;
        break;
    }
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC: {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "symbol index is not in the program pool");
        }
        *output_langtype = langtype;
        break;
    }
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE: {
        binrwir_binfunc_view_t view;
        const runtimec_string_view_t *types = NULL;
        size_t count = 0U;

        if (binrwir_binfunc_get_view(frame->program, &view) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "program view is unavailable");
        }
        if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_READ) {
            types = view.signature.read_parameter_types;
            count = view.signature.read_parameter_count;
        } else {
            types = view.signature.write_parameter_types;
            count = view.signature.write_parameter_count;
        }
        if (types == NULL || (size_t)argument->slot >= count) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "parameter index is outside the signature type table");
        }
        *output_langtype = types[argument->slot];
        break;
    }
    default:
        
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "argument has no declared langtype");
    }
    if (output_langtype->size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype is empty");
    }
    
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_form_unfollowed(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;

    plain.dereference = 0U;
    return vmbin_value_access_form(frame, &plain, output_form, error);
}

runtimec_status_t vmbin_value_access_form(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    const uint8_t *wire = NULL;
    runtimec_status_t status;

    if (output_form != NULL) {
        *output_form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    }
    if (output_form == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid value form request");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.is_whole_wire) {
        
        wire = location.read_bytes;
    } else {
        
        const runtimec_xvalue_address_t base =
            runtimec_value_body_xvalue_address(
                (runtimec_value_body_address_t)(uintptr_t)location.read_bytes);

        if (base == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "value body has no derivable XValue head");
        }
        wire = (const uint8_t *)(uintptr_t)base;
    }
    if (wire == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "argument has no readable XValue head");
    }
    
    {
        const pathspaceXvalueHead *head =
            (const pathspaceXvalueHead *)(const void *)wire;
        const uint64_t total = pathspaceXvalueHeadTotalLen(head);

        if (total == 0) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "value head is not a well-formed XValue");
        }
        const uint8_t *langtype = NULL;
        const uint16_t langtype_size = pathlangXvalueHeadLangtypeBounded(
            wire, total, &langtype);
        if (langtype != NULL && langtype_size != 0U) {
            if (langtype[0] == '*') {
                *output_form = VMBIN_VALUE_ACCESS_FORM_POINTER;
            } else if (langtype[0] == '@') {
                *output_form = VMBIN_VALUE_ACCESS_FORM_EXTENSION;
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_bind_write_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_key_view_t target_key,
    runtimec_error_t *error) {
    uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    runtimec_status_t status;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        frame->bindings == NULL || frame->pathspace_access == NULL ||
        !runtimec_key_view_is_valid(target_key) || target_key.size == 0U ||
        error == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid bind-write-target request");
    }
    
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;
        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL &&
        argument->reference_kind != BINRWIR_REFERENCE_PARAMETER_WRITE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "only local and parameter-write slots are bound after creation");
    }
    if (frame->snapshot_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "frame has no storage generation");
    }
    if (vmbin_frame_slot_of_argument(frame->program, argument, &binding_slot) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "local slot index is not in the program pool");
    }
    status = runtimec_pathspace_access_resolve_frame_xvalue_address(
        frame->pathspace_access, &frame->snapshot, target_key, &xvalue_address,
        &generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        generation != frame->snapshot_generation) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "created value is not in the frame storage generation");
    }
    if (binrwir_frame_bindings_bind(frame->bindings, binding_slot, target_key,
                                    target_key, xvalue_address,
                                    generation) != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "created value could not be bound to its slot");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
