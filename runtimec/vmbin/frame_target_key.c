
#include "frame_target_key.h"

#include <string.h>

#include "binrwir/argument.h"
#include "contract/error.h"
#include "binrwir/binfunc.h"
#include "contract/view.h"

runtimec_status_t vmbin_frame_target_key(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;
    size_t length;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        buffer == NULL || capacity == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL ||
        !runtimec_key_view_is_valid(frame->frame_path) ||
        frame->frame_path.size == 0U ||
        binrwir_binfunc_get_symbol(frame->program, argument->slot, &name,
                                   &langtype) != RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    length = frame->frame_path.size + 1U + name.size;
    if (length + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, frame->frame_path.data, frame->frame_path.size);
    buffer[frame->frame_path.size] = '/';
    memcpy(buffer + frame->frame_path.size + 1U, name.data, name.size);
    buffer[length] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_argument_key(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity,
    runtimec_error_t *error) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        buffer == NULL || capacity == 0U || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
        return vmbin_frame_target_key(frame, argument, buffer, capacity);
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC) {
        
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "write target is neither a local slot nor a "
                               "static key"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (binrwir_binfunc_get_symbol(frame->program, argument->slot, &name,
                                   &langtype) != RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "static write target has no symbol name"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    if (name.size + 1U > capacity) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "write target key does not fit the buffer"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, name.data, name.size);
    buffer[name.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}
