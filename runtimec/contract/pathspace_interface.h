#ifndef RUNTIMEC_CONTRACT_PATHSPACE_INTERFACE_H
#define RUNTIMEC_CONTRACT_PATHSPACE_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pathspace/pathspace.h>
#include <pathspace/pathspace_xvalue.h>

static inline bool runtimec_pathspace_write_wire(void *handle, const char *key,
                                               const uint8_t *value,
                                               uint32_t value_size,
                                               char *message,
                                               uint32_t message_capacity) {
    if (handle == NULL || key == NULL || value == NULL ||
        value_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return false;
    }
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)value;
    const uint64_t body_length = pathspaceXvalueHeadLen(head);
    const uint64_t attr_length = (uint64_t)pathspaceXvalueHeadAttrsLen(head);
    if (PATHSPACE_XVALUE_HEAD_LEN + body_length + attr_length !=
        (uint64_t)value_size) {
        return false;
    }
    if (message != NULL && message_capacity != 0U) {
        message[0] = '\0';
    }
    uint64_t value_offset = 0U;
    if (pathspaceRealloc(handle, key, body_length, attr_length, &value_offset,
                       message, message_capacity) != 0) {
        return false;
    }
    uint8_t *base = pathspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return false;
    }
    uint8_t *slot = base + value_offset;
    if (body_length != 0U) {
        memcpy(slot + PATHSPACE_XVALUE_HEAD_LEN, value + PATHSPACE_XVALUE_HEAD_LEN,
               (size_t)body_length);
    }
    if (attr_length != 0U) {
        memcpy(slot + PATHSPACE_XVALUE_HEAD_LEN + body_length,
               value + PATHSPACE_XVALUE_HEAD_LEN + body_length,
               (size_t)attr_length);
    }
    return true;
}

static inline bool runtimec_pathspace_read_wire(void *handle, const char *key,
                                              const uint8_t **output_wire,
                                              uint32_t *output_size) {
    if (output_wire != NULL) {
        *output_wire = NULL;
    }
    if (output_size != NULL) {
        *output_size = 0U;
    }
    if (handle == NULL || key == NULL) {
        return false;
    }
    char message[128];
    message[0] = '\0';
    uint64_t value_offset = 0U;
    const int status = pathspaceResolveOffset(handle, key, 0U, &value_offset, message,
                                      (uint32_t)sizeof(message));
    if (status == PATHSPACE_MISSING) {
        return true;
    }
    if (status != 0) {
        return false;
    }
    const uint8_t *base = pathspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return false;
    }
    const uint8_t *wire = base + value_offset;
    const uint64_t total =
        pathspaceXvalueHeadTotalLen((const pathspaceXvalueHead *)(const void *)wire);
    if (total == 0U || total > UINT32_MAX) {
        return false;
    }
    if (output_wire != NULL) {
        *output_wire = wire;
    }
    if (output_size != NULL) {
        *output_size = (uint32_t)total;
    }
    return true;
}

#endif
