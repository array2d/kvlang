
#include "vmbin/vthread_status.h"

#include <string.h>

#include "contract/view.h"
#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>

#include "contract/pathspace_interface.h"

runtimec_status_t runtimec_vthread_status_handle(
    const binrwir_binvthread_frame_t *frame, void **output_handle) {
    if (output_handle != NULL) {
        *output_handle = NULL;
    }
    if (frame == NULL || output_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_handle = frame->pathspace_handle;
    
    return frame->pathspace_handle != NULL ? RUNTIMEC_STATUS_SUCCESS
                                         : RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_vthread_status_key(
    runtimec_key_view_t frame_path, runtimec_string_view_t suffix, char *buffer,
    size_t capacity) {
    size_t root_length = 0U;
    size_t slash_count = 0U;
    size_t ordinal;

    if (buffer == NULL || capacity == 0U ||
        !runtimec_string_view_is_valid(suffix) || suffix.size == 0U ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    buffer[0] = '\0';
    
    for (ordinal = 0U; ordinal < frame_path.size; ++ordinal) {
        if (frame_path.data[ordinal] == '/' && ordinal != 0U) {
            slash_count += 1U;
            if (slash_count == 2U) {
                root_length = ordinal;
                break;
            }
        }
    }
    if (root_length == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (root_length + suffix.size + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, frame_path.data, root_length);
    memcpy(buffer + root_length, suffix.data, suffix.size);
    buffer[root_length + suffix.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_vthread_status_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t runtimec_vthread_status_write(
    void *pathspace_handle, const char *status_key, runtimec_bytes_view_t status) {
    char message[256];

    if (pathspace_handle == NULL || status_key == NULL ||
        status_key[0] == '\0') {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (status.size > RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (status.size != 0U && status.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    message[0] = '\0';
    
    const size_t langtype_length = strlen(RUNTIMEC_VTHREAD_STATUS_LANGTYPE);
    const uint64_t attr_length = 3U + (uint64_t)langtype_length;
    uint64_t value_offset = 0U;
    if (pathspaceRealloc(pathspace_handle, status_key, (uint64_t)status.size,
                       attr_length, &value_offset, message,
                       (uint32_t)sizeof(message)) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    const uint8_t *base = pathspaceXvalueInProcessAddress(pathspace_handle);
    if (base == NULL) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    uint8_t *slot = (uint8_t *)(uintptr_t)(base + value_offset);
    if (status.size != 0U) {
        memcpy(slot + PATHSPACE_XVALUE_HEAD_LEN, status.data, status.size);
    }
    {
        uint8_t *attr = slot + PATHSPACE_XVALUE_HEAD_LEN + (uint64_t)status.size;
        attr[0] = (uint8_t)PATHLANG_XVALUE_ATTR_TAG_LANGTYPE;
        attr[1] = (uint8_t)(langtype_length & 0xFFU);
        attr[2] = (uint8_t)((langtype_length >> 8U) & 0xFFU);
        memcpy(attr + 3, RUNTIMEC_VTHREAD_STATUS_LANGTYPE, langtype_length);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_vthread_status_read(
    void *pathspace_handle, const char *status_key,
    runtimec_string_view_t *output_text, runtimec_error_t *error) {
    const uint8_t *wire = NULL;
    uint32_t wire_length = 0U;
    const uint8_t *body = NULL;
    uint64_t effective_length = 0U;

    if (output_text != NULL) {
        *output_text = runtimec_string_view_make(NULL, 0U);
    }
    if (pathspace_handle == NULL || status_key == NULL ||
        status_key[0] == '\0' || output_text == NULL) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_INVALID, 1,
            "invalid status read request");
    }
    
    if (!runtimec_pathspace_read_wire(pathspace_handle, status_key, &wire,
                                    &wire_length)) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_ERROR, 4, "status read failed");
    }
    if (wire == NULL || wire_length == 0U) {
        
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 2,
            "status key is absent");
    }
    {
        pathspaceXvalueHead head;
        if (wire_length < PATHSPACE_XVALUE_HEAD_LEN) {
            return runtimec_vthread_status_fail(
                error, RUNTIMEC_STATUS_ERROR, 5, "status wire is truncated");
        }
        memcpy(&head, wire, sizeof(head));
        
        if (!pathspaceXvalueHeadPaddingIsZero(&head) ||
            pathspaceXvalueHeadTotalLen(&head) == 0U ||
            pathspaceXvalueHeadTotalLen(&head) > (uint64_t)wire_length ||
            pathspaceXvalueHeadEffectiveLen(wire, (uint64_t)wire_length,
                                          &effective_length) != 0) {
            return runtimec_vthread_status_fail(
                error, RUNTIMEC_STATUS_INVALID, 3,
                "status slot does not match the fixed shape");
        }
    }
    if (effective_length > RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_INVALID, 3,
            "status slot does not match the fixed shape");
    }
    body = wire + PATHSPACE_XVALUE_HEAD_LEN;
    *output_text = runtimec_string_view_make((const char *)body,
                                             (size_t)effective_length);
    return RUNTIMEC_STATUS_SUCCESS;
}
