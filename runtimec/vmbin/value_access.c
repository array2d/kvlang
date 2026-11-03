/*
 * 组件：vmbin/value_access 的实现
 * 作用：按 reference_kind 找值体地址（或池内字面量），并直接读写值体字节。
 * 约束：值头/值体的几何只经合同——基址 → 值体用 runtimec_xvalue_body_address，
 *       值体 → 当前长度用 runtimec_value_body_length；本文件不写任何偏移算术。
 *       实参是字面量时绑定表给的是内联字节，直接借出，不推导任何地址。
 *       写只接受与当前值体等长的值：改长度要连带改值头与 attr 位置，那是存储层
 *       的事，vmbin 不做，也不回退成按 key 写。
 */

#include "value_access.h"

#include <string.h>

#include "frame_slot.h"

typedef enum vmbin_value_access_diagnostic {
    VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST = 1,
    VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE = 2,
    VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT = 3,
    VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL = 4,
    VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY = 5,
    VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH = 6
} vmbin_value_access_diagnostic_t;

static runtimec_status_t vmbin_value_access_fail(
    runtimec_error_t *error, runtimec_status_t status,
    vmbin_value_access_diagnostic_t code, const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                       (int64_t)code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t vmbin_value_access_locate(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    runtimec_bytes_view_t literal;
    runtimec_string_view_t langtype;
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_value_body_address_t body_address = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    runtimec_status_t status;

    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){NULL, NULL, 0U};
    }
    if (frame == NULL || frame->program == NULL || argument == NULL ||
        output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        status = binrwir_program_get_literal(frame->program,
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
        return RUNTIMEC_STATUS_SUCCESS;
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_STATIC:
        /* 静态参数的 payload 已经是值体地址；值头在它前面。 */
        if (argument->payload == BINRWIR_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
                "static argument carries no body address");
        }
        body_address = (runtimec_value_body_address_t)argument->payload;
        break;
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
    case BINRWIR_REFERENCE_LOCAL: {
        const binrwir_frame_binding_t *binding = NULL;
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

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
            /* 内联实参：字节就在 Program 字面量池里，只有读视图。 */
            output_location->read_bytes = binding->bytes;
            output_location->write_bytes = NULL;
            output_location->size = binding->length;
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
        /* FUNCTION 是调用目标，不是值；未知 kind 同样没有可写位置。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "argument has no value body");
    }
    /* 地址的有效性由来源证明（STATIC 的 payload 经参数校验非零，绑定只在解析
     * 成功后写入）；长度 0 是合法的空值体（None），不是坏地址的哨兵。 */
    output_location->read_bytes = (const uint8_t *)(uintptr_t)body_address;
    output_location->write_bytes = (uint8_t *)(uintptr_t)body_address;
    output_location->size = (size_t)runtimec_value_body_length(body_address);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_read(
    const binrwir_execution_frame_t *frame,
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

runtimec_status_t vmbin_value_access_write(
    const binrwir_execution_frame_t *frame,
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
        /* 改长度要连带改值头与 attr 位置：那是存储层的事。 */
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
