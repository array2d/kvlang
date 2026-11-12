
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/extension_interface.h"
#include "contract/kvspace_access.h"

#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>

#include "binrwir/binvthread_frame.h"
#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "contract/kvspace_interface.h"
#include "engine/session.h"
#include "vmbin/value_access.h"

typedef struct extension_call {
    binrwir_binvthread_frame_t *frame;
    const binrwir_instruction_view_t *instruction;
    binrwir_execution_exit_t *output_exit;
} extension_call_t;

static runtimec_status_t extension_call_from(const void *frame_handle,
                                             const void *instruction_handle,
                                             void *output_exit_handle,
                                             bool needs_exit, extension_call_t *output,
                                             kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_text(
    const void *frame, const void *instruction, size_t index,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, false, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->read_arguments.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    runtimec_bytes_buffer_t buffer;
    runtimec_bytes_buffer_reset(&buffer);
    buffer.data = (uint8_t *)output->data;
    buffer.capacity = output->capacity;
    
    status = vmbin_value_access_read_body(
        call.frame, &call.instruction->read_arguments.data[index], &buffer,
        &internal);
    if (status == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY) {
        output->required_length = buffer.size;
        return status;
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->length = buffer.size;
    output->required_length = buffer.size;
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_integer(
    const void *frame, const void *instruction, size_t index, int64_t *output,
    kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, false, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->read_arguments.count ||
        output == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const binrwir_argument_t *argument = &call.instruction->read_arguments.data[index];
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    runtimec_string_view_t langtype;
    status = vmbin_value_access_langtype(call.frame, argument, &langtype, &internal);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    runtimec_bytes_buffer_t buffer;
    runtimec_bytes_buffer_reset(&buffer);
    uint8_t storage[8];
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    
    status = vmbin_value_access_read_body(call.frame, argument, &buffer,
                                          &internal);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    unsigned char bytes[8];
    memcpy(bytes, storage, buffer.size);
    switch (buffer.size) {
    case 1: *output = (int64_t)(int8_t)bytes[0]; return RUNTIMEC_STATUS_SUCCESS;
    case 2: *output = (int64_t)(int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8)); return RUNTIMEC_STATUS_SUCCESS;
    case 4: {
        const uint32_t raw = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
        *output = (int64_t)(int32_t)raw;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case 8: {
        uint64_t raw = 0U;
        for (size_t at = 0U; at < 8U; ++at) {
            raw |= (uint64_t)bytes[at] << (8U * at);
        }
        *output = (int64_t)raw;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    default:
        return RUNTIMEC_STATUS_INVALID;
    }
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_wire(
    const void *frame, const void *instruction, size_t index,
    const uint8_t **output_data, size_t *output_size,
    kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, false, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->read_arguments.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    vmbin_value_location_t location;
    status = vmbin_value_access_locate(
        call.frame, &call.instruction->read_arguments.data[index], &location,
        &internal);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (location.read_bytes == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const runtimec_xvalue_address_t xvalue_address =
        runtimec_value_body_xvalue_address(
            (runtimec_value_body_address_t)(uintptr_t)location.read_bytes);
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const uint8_t *wire = (const uint8_t *)(uintptr_t)xvalue_address;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0U || total > (uint64_t)SIZE_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (output_data != NULL) {
        *output_data = wire;
    }
    if (output_size != NULL) {
        *output_size = (size_t)total;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_langtype(
    const void *frame, const void *instruction, size_t index,
    const char **output_data, size_t *output_size,
    kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, false, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->read_arguments.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    runtimec_string_view_t langtype;
    status = vmbin_value_access_langtype(
        call.frame, &call.instruction->read_arguments.data[index], &langtype,
        &internal);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (output_data != NULL) {
        *output_data = langtype.data;
    }
    if (output_size != NULL) {
        *output_size = langtype.size;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_wire(
    void *frame, const void *instruction, size_t index, const uint8_t *data,
    size_t size, kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, true, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->write_arguments.count ||
        data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    return vmbin_value_access_write_whole_value(
        call.frame, &call.instruction->write_arguments.data[index],
        runtimec_bytes_view_make(data, size), &internal);
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_text(
    void *frame, const void *instruction, size_t index, const char *data,
    size_t size, kvlang_runtime_error_t *error) {
    if (data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, true, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->write_arguments.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    return vmbin_value_access_write_body(
        call.frame, &call.instruction->write_arguments.data[index],
        runtimec_bytes_view_make((const uint8_t *)data, size), &internal);
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_integer(
    void *frame, const void *instruction, size_t index, int64_t value,
    kvlang_runtime_error_t *error) {
    extension_call_t call;
    runtimec_status_t status = extension_call_from(frame, instruction, NULL, true, &call, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (call.instruction == NULL || index >= call.instruction->write_arguments.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_t internal;
    runtimec_error_reset(&internal);
    
    runtimec_string_view_t langtype;
    status = vmbin_value_access_langtype(
        call.frame, &call.instruction->write_arguments.data[index], &langtype,
        &internal);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    uint8_t bytes[8];
    size_t width = 0U;
    if (langtype.size == 4U && memcmp(langtype.data, "int8", 4U) == 0) {
        width = 1U;
        bytes[0] = (uint8_t)(int8_t)value;
    } else if (langtype.size == 4U && memcmp(langtype.data, "int16", 5U) == 0) {
        width = 2U;
    } else if (langtype.size == 4U && memcmp(langtype.data, "int32", 5U) == 0) {
        width = 4U;
    } else if (langtype.size == 4U && memcmp(langtype.data, "int64", 5U) == 0) {
        width = 8U;
    }
    if (width == 0U) {
        
        width = 8U;
    }
    for (size_t at = 0U; at < width; ++at) {
        bytes[at] = (uint8_t)(((uint64_t)value) >> (8U * at));
    }
    return vmbin_value_access_write_body(
        call.frame, &call.instruction->write_arguments.data[index],
        runtimec_bytes_view_make(bytes, width), &internal);
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_set_exit(
    void *output_exit, kvlang_runtime_exit_kind_t kind, int64_t detail_code,
    const char *message, kvlang_runtime_error_t *error) {
    (void)error;
    binrwir_execution_exit_t *exit_handle =
        (binrwir_execution_exit_t *)output_exit;
    if (exit_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (kind == KVLANG_RUNTIME_EXIT_NONE) {
        *exit_handle = (binrwir_execution_exit_t){0};
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (kind == KVLANG_RUNTIME_EXIT_ERROR) {
        exit_handle->kind = BINRWIR_EXECUTION_EXIT_ERROR;
        exit_handle->detail_code = (uint32_t)detail_code;
    } else {
        exit_handle->kind = BINRWIR_EXECUTION_EXIT_YIELD;
        exit_handle->detail_code = (uint32_t)detail_code;
    }
    (void)message;
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_make_pointer(
    const char *target_key, size_t target_size, const char *target_langtype,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error) {
    (void)error;
    if (target_key == NULL || target_langtype == NULL || output == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvlangXvalueHeadNewPtr((const uint8_t *)target_key,
                                (uint32_t)target_size, target_langtype, &wire,
                                &wire_size) != 0 ||
        wire == NULL) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (output->capacity < (size_t)wire_size) {
        output->required_length = (size_t)wire_size;
        free(wire);
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(output->data, wire, (size_t)wire_size);
    output->length = (size_t)wire_size;
    output->required_length = (size_t)wire_size;
    free(wire);
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_dereference_pointer(
    const uint8_t *wire, size_t size, kvlang_runtime_text_buffer_t *output_key,
    kvlang_runtime_text_buffer_t *output_langtype,
    kvlang_runtime_error_t *error) {
    (void)error;
    if (wire == NULL || size < KVSPACE_XVALUE_HEAD_LEN) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (!kvspaceXvalueHeadPaddingIsZero(head)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const uint64_t body_length = kvspaceXvalueHeadLen(head);
    if (KVSPACE_XVALUE_HEAD_LEN + body_length > (uint64_t)size) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const uint8_t *langtype = NULL;
    const uint16_t langtype_size =
        kvlangXvalueHeadLangtypeBounded(wire, (uint64_t)size, &langtype);
    
    if (langtype == NULL || langtype_size == 0U || langtype[0] != '*') {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (output_key != NULL) {
        if (output_key->capacity < (size_t)body_length) {
            output_key->required_length = (size_t)body_length;
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        memcpy(output_key->data, wire + KVSPACE_XVALUE_HEAD_LEN,
               (size_t)body_length);
        output_key->length = (size_t)body_length;
        output_key->required_length = (size_t)body_length;
    }
    if (output_langtype != NULL) {
        
        const size_t target_size = (size_t)langtype_size - 1U;
        if (output_langtype->capacity < target_size) {
            output_langtype->required_length = target_size;
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        memcpy(output_langtype->data, langtype + 1, target_size);
        output_langtype->length = target_size;
        output_langtype->required_length = target_size;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t extension_call_from(const void *frame_handle,
                                             const void *instruction_handle,
                                             void *output_exit_handle,
                                             bool needs_exit,
                                             extension_call_t *output,
                                             kvlang_runtime_error_t *error) {
    (void)needs_exit;
    (void)error;
    if (output == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (frame_handle == NULL || instruction_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output->frame = (binrwir_binvthread_frame_t *)frame_handle;
    output->instruction =
        (const binrwir_instruction_view_t *)instruction_handle;
    output->output_exit = (binrwir_execution_exit_t *)output_exit_handle;
    return RUNTIMEC_STATUS_SUCCESS;
}
