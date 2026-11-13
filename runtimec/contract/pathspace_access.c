
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "contract/pathspace_access.h"

#include <errno.h>
#include <time.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/key_directory.h"
#include "contract/pathspace_interface.h"
#include "contract/pathspace_interface.h"
#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>

#define RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR "\xE2\x80\xA5"

static const char runtimec_pathspace_member_program_counter[] = {'p', 'c', 0};
static const char runtimec_pathspace_member_status[] = "status";
static const char runtimec_pathspace_member_message[] = "msg";

static const char runtimec_pathspace_member_library[] = "lib";

static const char runtimec_pathspace_member_source_revision[] = "ctime";

#define RUNTIMEC_PATHSPACE_SYSTEM_LANGTYPE "[]char/utf8"

static runtimec_status_t runtimec_pathspace_access_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_STORAGE, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static bool runtimec_pathspace_access_key_text(runtimec_key_view_t key,
                                              char *buffer, size_t capacity,
                                              size_t *output_size) {
    if (!runtimec_key_view_is_valid(key) || key.size == 0U ||
        key.size + 1U > capacity || key.data[0] != '/') {
        return false;
    }
    memcpy(buffer, key.data, key.size);
    buffer[key.size] = '\0';
    if (output_size != NULL) {
        *output_size = key.size;
    }
    return true;
}

static runtimec_status_t runtimec_pathspace_access_read_wire(
    runtimec_pathspace_access_t *adapter, const char *key_text,
    const uint8_t **output_wire, uint32_t *output_size,
    runtimec_error_t *error) {
    if (output_wire != NULL) {
        *output_wire = NULL;
    }
    if (output_size != NULL) {
        *output_size = 0U;
    }
    if (adapter == NULL || key_text == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 3, "invalid read request");
    }
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    const int status = pathspaceResolveOffset(adapter->handle, key_text, 0U,
                                      &value_offset, error_text,
                                      (uint32_t)sizeof(error_text));
    if (status != 0) {
        
        if (status == PATHSPACE_MISSING) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 3, "pathspace read failed");
    }
    const uint8_t *base = pathspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 3,
            "the backend does not expose an in-process value area");
    }
    const uint8_t *wire = base + value_offset;
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)wire;
    
    const uint64_t total = pathspaceXvalueHeadTotalLen(head);
    if (total == 0U || total > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 4, "value head is malformed");
    }
    if (output_wire != NULL) {
        *output_wire = wire;
    }
    if (output_size != NULL) {
        *output_size = (uint32_t)total;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_pathspace_access_write_wire(
    runtimec_pathspace_access_t *adapter, const char *key_text,
    const uint8_t *wire, uint32_t wire_size, runtimec_error_t *error) {
    if (adapter == NULL || key_text == NULL || wire == NULL ||
        wire_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 3, "invalid write request");
    }
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)wire;
    const uint64_t body_length = pathspaceXvalueHeadLen(head);
    const uint64_t attr_length = (uint64_t)pathspaceXvalueHeadAttrsLen(head);
    if (PATHSPACE_XVALUE_HEAD_LEN + body_length + attr_length != (uint64_t)wire_size) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 4, "wire length does not match its head");
    }
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    if (pathspaceRealloc(adapter->handle, key_text, body_length, attr_length,
                       &value_offset, error_text,
                       (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 5, "pathspace write failed");
    }
    uint8_t *base = pathspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 5,
            "the backend does not expose an in-process value area");
    }
    
    uint8_t *slot = base + value_offset;
    if (body_length != 0U) {
        memcpy(slot + PATHSPACE_XVALUE_HEAD_LEN, wire + PATHSPACE_XVALUE_HEAD_LEN,
               (size_t)body_length);
    }
    if (attr_length != 0U) {
        memcpy(slot + PATHSPACE_XVALUE_HEAD_LEN + body_length,
               wire + PATHSPACE_XVALUE_HEAD_LEN + body_length,
               (size_t)attr_length);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_read(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_buffer_is_valid(output) ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 1, "invalid read request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 2, "invalid key view");
    }
    
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_pathspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
    }
    
    if (value == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 4, "key is missing");
    }
    if (value_size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 6, "key holds no bytes");
    }
    output->size = value_size;
    if (output->capacity < (size_t)value_size || output->data == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 5,
            "value does not fit the caller buffer");
    }
    memcpy(output->data, value, value_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_read_header(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_pathspace_value_header_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    runtimec_pathspace_value_header_reset(output);
    if (adapter == NULL || output == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 6, "invalid header request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 7, "invalid key view");
    }
    
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_pathspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
    }
    
    output->value_size = (value == NULL) ? 0U : (uint64_t)value_size;
    
    output->value_revision = 0U;
    output->flags = 0U;
    output->reserved = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_write(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 9, "invalid write request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 10, "invalid key view");
    }
    if (value.size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 11, "value is too large");
    }
    return runtimec_pathspace_access_write_wire(adapter, key_text, value.data,
                                              (uint32_t)value.size, error);
}

runtimec_status_t runtimec_pathspace_access_write_partial(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 13, "invalid partial write request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 14, "invalid key view");
    }
    if (offset > UINT32_MAX || value.size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 15,
            "partial write is outside the addressable range");
    }
    
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    if (pathspaceResolveOffset(adapter->handle, key_text, 0U, &value_offset, error_text,
                       (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 16, "pathspace partial write failed");
    }
    uint8_t *base = pathspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 16,
            "the backend does not expose an in-process value area");
    }
    uint8_t *slot = base + value_offset + PATHSPACE_XVALUE_HEAD_LEN;
    if (value.size != 0U) {
        memcpy(slot + offset, value.data, (size_t)value.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_remove(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 17, "invalid remove request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 18, "invalid key view");
    }
    
    char error_text[256];
    error_text[0] = '\0';
    if (pathspaceFree(adapter->handle, key_text, error_text,
                    (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 19,
            error_text[0] != '\0' ? error_text : "pathspace remove failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

#define RUNTIMEC_PATHSPACE_LIST_DEPTH_LIMIT 32U

static runtimec_status_t runtimec_pathspace_access_directory_source(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation);

static runtimec_status_t runtimec_pathspace_access_directory_source(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation) {
    runtimec_pathspace_access_t *adapter = (runtimec_pathspace_access_t *)context;
    if (adapter == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int status = pathspaceList(adapter->handle, directory, names, capacity,
                                   output_length, output_count,
                                   output_generation, error_text,
                                   (uint32_t)sizeof(error_text));
    if (status == 0) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (status == -ENOSPC) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    return RUNTIMEC_STATUS_ERROR;
}

static runtimec_status_t runtimec_pathspace_access_list_directory(
    runtimec_pathspace_access_t *adapter, const char *directory,
    size_t directory_size, const char *prefix, size_t prefix_size,
    runtimec_pathspace_list_visitor_function_t visitor, void *visitor_context,
    unsigned depth, runtimec_error_t *error) {
    if (depth > RUNTIMEC_PATHSPACE_LIST_DEPTH_LIMIT) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 20, "key tree is too deep");
    }
    
    runtimec_key_directory_t level;
    runtimec_key_directory_reset(&level, &adapter->allocator, adapter,
                                 runtimec_pathspace_access_directory_source);
    runtimec_status_t refresh = runtimec_key_directory_refresh(&level, directory);
    if (refresh != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_key_directory_destroy(&level);
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 21, "pathspace list failed");
    }
    const uint32_t entry_count = level.count;
    for (uint32_t index = 0U; index < entry_count; index++) {
        const char *name = NULL;
        uint32_t name_size = 0U;
        if (runtimec_key_directory_at(&level, index, &name,
                                      &name_size) != RUNTIMEC_STATUS_SUCCESS ||
            name == NULL) {
            (void)runtimec_key_directory_destroy(&level);
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 22, "pathspace list entry failed");
        }
        if (directory_size + (size_t)name_size >= RUNTIMEC_PATHSPACE_KEY_CAPACITY) {
            (void)runtimec_key_directory_destroy(&level);
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 23, "listed key is too long");
        }
        char full_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
        memcpy(full_key, directory, directory_size);
        memcpy(full_key + directory_size, name, name_size);
        const size_t full_size = directory_size + name_size;
        full_key[full_size] = '\0';
        if (full_size < prefix_size ||
            strncmp(full_key, prefix, prefix_size) != 0) {
            continue;
        }
        if (full_size != 0U && full_key[full_size - 1U] == '/') {
            runtimec_status_t status = runtimec_pathspace_access_list_directory(
                adapter, full_key, full_size, prefix, prefix_size, visitor,
                visitor_context, depth + 1U, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                (void)runtimec_key_directory_destroy(&level);
                return status;
            }
            continue;
        }
        
        if (full_size + 1U < RUNTIMEC_PATHSPACE_KEY_CAPACITY) {
            char child_directory[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
            memcpy(child_directory, full_key, full_size);
            child_directory[full_size] = '/';
            child_directory[full_size + 1U] = '\0';
            runtimec_key_directory_t probe;
            runtimec_key_directory_reset(&probe, &adapter->allocator, adapter,
                                         runtimec_pathspace_access_directory_source);
            const bool has_children =
                runtimec_key_directory_refresh(&probe, child_directory) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                probe.count > 0U;
            (void)runtimec_key_directory_destroy(&probe);
            if (has_children) {
                runtimec_status_t status =
                    runtimec_pathspace_access_list_directory(
                        adapter, child_directory, full_size + 1U, prefix,
                        prefix_size, visitor, visitor_context, depth + 1U,
                        error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    (void)runtimec_key_directory_destroy(&level);
                    return status;
                }
            }
        }
        runtimec_status_t status = visitor(
            visitor_context, runtimec_key_view_make(full_key, full_size),
            error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_key_directory_destroy(&level);
            return status;
        }
    }
    (void)runtimec_key_directory_destroy(&level);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_list(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_pathspace_list_visitor_function_t visitor, void *visitor_context,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || visitor == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 24, "invalid list request");
    }
    char prefix_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    size_t prefix_size = 0U;
    if (!runtimec_pathspace_access_key_text(prefix, prefix_text,
                                           sizeof(prefix_text), &prefix_size)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 25, "invalid list prefix");
    }
    while (prefix_size != 0U && prefix_text[prefix_size - 1U] == '/') {
        prefix_size -= 1U;
    }
    if (prefix_size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 26, "list prefix is empty");
    }
    prefix_text[prefix_size] = '\0';

    size_t directory_size = prefix_size;
    while (directory_size != 0U && prefix_text[directory_size] != '/') {
        directory_size -= 1U;
    }
    if (directory_size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 27, "list prefix has no parent");
    }
    
    char directory[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    memcpy(directory, prefix_text, directory_size + 1U);
    directory[directory_size + 1U] = '\0';
    return runtimec_pathspace_access_list_directory(
        adapter, directory, directory_size + 1U, prefix_text, prefix_size,
        visitor, visitor_context, 0U, error);
}

runtimec_status_t runtimec_pathspace_access_signal(
    runtimec_pathspace_access_t *access, runtimec_key_view_t key, runtimec_error_t *error) {
    (void)access;
    (void)key;
    
    return runtimec_pathspace_access_fail(
        error, RUNTIMEC_STATUS_NOT_IMPLEMENTED, 33,
        "pathspace has no explicit signal primitive");
}

static bool runtimec_pathspace_access_read_source_generation(
    runtimec_pathspace_access_t *adapter, const char *function_text,
    uint64_t *output_generation) {
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    size_t function_size = strlen(function_text);
    while (function_size != 0U && function_text[function_size - 1U] == '/') {
        function_size -= 1U;
    }
    const size_t separator_size = sizeof(RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    const size_t member_size = sizeof(runtimec_pathspace_member_source_revision) - 1U;
    if (function_size + 1U + separator_size + member_size + 1U >
        sizeof(key_text)) {
        return false;
    }
    memcpy(key_text, function_text, function_size);
    key_text[function_size] = '/';
    memcpy(key_text + function_size + 1U, RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
           separator_size);
    memcpy(key_text + function_size + 1U + separator_size,
           runtimec_pathspace_member_source_revision, member_size);
    key_text[function_size + 1U + separator_size + member_size] = '\0';

    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (runtimec_pathspace_access_read_wire(adapter, key_text, &value,
                                          &value_size, NULL) !=
            RUNTIMEC_STATUS_SUCCESS ||
        value == NULL || value_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return false;
    }
    pathspaceXvalueHead head;
    memcpy(&head, value, sizeof(head));
    
    if (pathspaceXvalueHeadLen(&head) != 8U ||
        pathspaceXvalueHeadTotalLen(&head) > (uint64_t)value_size) {
        return false;
    }
    uint64_t generation = 0U;
    for (size_t index = 0U; index < 8U; index++) {
        generation |= (uint64_t)value[PATHSPACE_XVALUE_HEAD_LEN + index]
                      << (8U * index);
    }
    *output_generation = generation;
    return true;
}

runtimec_status_t runtimec_pathspace_access_source_revision(
    runtimec_pathspace_access_t *access, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    runtimec_source_revision_reset(revision);
    if (adapter == NULL || revision == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 34, "invalid source revision request");
    }
    char function_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(function_key, function_text,
                                           sizeof(function_text), NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 35, "invalid function key");
    }
    uint64_t source_generation = 0U;
    if (!runtimec_pathspace_access_read_source_generation(
            adapter, function_text, &source_generation)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 36,
            "function has no readable source revision key");
    }
    
    bool changed = false;
    size_t index = 0U;
    while (index < adapter->resolved_address_capacity) {
        runtimec_pathspace_resolved_address_t *record =
            &adapter->resolved_addresses[index];

        if (record->key_size == 0U) {
            index += 1U; 
            continue;
        }
        char record_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
        memcpy(record_key, record->key, record->key_size);
        record_key[record->key_size] = '\0';
        
        char record_error[128];
        record_error[0] = '\0';
        uint64_t record_offset = 0U;
        const int read_status =
            pathspaceResolveOffset(adapter->handle, record_key, 0U, &record_offset,
                           record_error, (uint32_t)sizeof(record_error));
        const uint8_t *base = pathspaceXvalueInProcessAddress(adapter->handle);
        const uint64_t current_address =
            (read_status == 0 && base != NULL)
                ? (uint64_t)(uintptr_t)(base + record_offset)
                : 0U;
        const bool stale = read_status != 0 || base == NULL ||
                           current_address != record->address;
        if (!stale) {
            index += 1U;
            continue;
        }
        changed = true;
        if (read_status != 0 || base == NULL) {
            
            record->key_size = 0U;
            adapter->resolved_address_count -= 1U;
            index += 1U;
            continue;
        }
        
        record->address = current_address;
        index += 1U;
    }
    
    if (!adapter->generation_started || adapter->generation == 0U) {
        adapter->generation = 1U;
        adapter->generation_started = true;
    } else if (changed) {
        adapter->generation += 1U;
    }
    revision->instance_identifier = adapter->instance_identifier;
    revision->generation = adapter->generation;
    revision->schema_version = 0U;
    revision->instruction_set_version = source_generation > UINT32_MAX
                                            ? UINT32_MAX
                                            : (uint32_t)source_generation;
    return RUNTIMEC_STATUS_SUCCESS;
}

#define RUNTIMEC_PATHSPACE_LEDGER_SLOT_LIMIT 1024U

#define RUNTIMEC_PATHSPACE_LEDGER_LOAD_NUMERATOR 3U
#define RUNTIMEC_PATHSPACE_LEDGER_LOAD_DENOMINATOR 4U

static uint64_t runtimec_pathspace_access_key_hash(const char *key, size_t size) {
    
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= (uint64_t)(uint8_t)key[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t runtimec_pathspace_access_ledger_probe(
    const runtimec_pathspace_resolved_address_t *slots, size_t capacity,
    const char *key, uint16_t key_size, uint64_t key_hash) {
    size_t index = (size_t)(key_hash & (uint64_t)(capacity - 1U));

    for (;;) {
        const runtimec_pathspace_resolved_address_t *slot = &slots[index];

        if (slot->key_size == 0U) {
            return index; 
        }
        if (slot->key_hash == key_hash && slot->key_size == key_size &&
            memcmp(slot->key, key, key_size) == 0) {
            return index; 
        }
        index = (index + 1U) & (capacity - 1U);
    }
}

static runtimec_status_t runtimec_pathspace_access_ledger_grow(
    runtimec_pathspace_access_t *adapter, size_t new_capacity,
    runtimec_error_t *error) {
    runtimec_pathspace_resolved_address_t *grown =
        (runtimec_pathspace_resolved_address_t *)runtimec_allocator_allocate(
            &adapter->allocator,
            new_capacity * sizeof(*grown), _Alignof(runtimec_pathspace_resolved_address_t));

    if (grown == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 44,
            "address ledger allocation failed");
    }
    memset(grown, 0, new_capacity * sizeof(*grown));
    for (size_t ordinal = 0U; ordinal < adapter->resolved_address_capacity;
         ++ordinal) {
        const runtimec_pathspace_resolved_address_t *source =
            &adapter->resolved_addresses[ordinal];

        if (source->key_size == 0U) {
            continue;
        }
        grown[runtimec_pathspace_access_ledger_probe(
            grown, new_capacity, source->key, source->key_size,
            source->key_hash)] = *source;
    }
    if (adapter->resolved_addresses != NULL) {
        (void)runtimec_allocator_deallocate(
            &adapter->allocator, adapter->resolved_addresses,
            adapter->resolved_address_capacity * sizeof(*grown),
            _Alignof(runtimec_pathspace_resolved_address_t));
    }
    adapter->resolved_addresses = grown;
    adapter->resolved_address_capacity = new_capacity;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_pathspace_access_ledger_record(
    runtimec_pathspace_access_t *adapter, const char *key_text, size_t key_size,
    uint64_t address, uint64_t observed_size, runtimec_error_t *error) {
    const uint64_t key_hash =
        runtimec_pathspace_access_key_hash(key_text, key_size);
    size_t index;

    if (key_size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 66,
            "cannot record an empty key in the address ledger");
    }
    if (key_size >= RUNTIMEC_PATHSPACE_KEY_CAPACITY) {
        
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 67,
            "resolved key exceeds the address ledger capacity");
    }
    if (adapter->resolved_address_capacity == 0U) {
        runtimec_status_t status = runtimec_pathspace_access_ledger_grow(
            adapter, 16U, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if ((adapter->resolved_address_count + 1U) *
            RUNTIMEC_PATHSPACE_LEDGER_LOAD_DENOMINATOR >
        adapter->resolved_address_capacity *
            RUNTIMEC_PATHSPACE_LEDGER_LOAD_NUMERATOR) {
        if (adapter->resolved_address_capacity >=
            RUNTIMEC_PATHSPACE_LEDGER_SLOT_LIMIT) {
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 68,
                "address ledger is full; the key count must not grow with "
                "call count");
        }
        runtimec_status_t status = runtimec_pathspace_access_ledger_grow(
            adapter, adapter->resolved_address_capacity * 2U, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    index = runtimec_pathspace_access_ledger_probe(
        adapter->resolved_addresses, adapter->resolved_address_capacity,
        key_text, (uint16_t)key_size, key_hash);
    if (adapter->resolved_addresses[index].key_size == 0U) {
        adapter->resolved_address_count += 1U; 
        adapter->resolved_addresses[index].key_size = (uint16_t)key_size;
        adapter->resolved_addresses[index].key_hash = key_hash;
        memcpy(adapter->resolved_addresses[index].key, key_text, key_size);
    }
    adapter->resolved_addresses[index].address = address;
    adapter->resolved_addresses[index].observed_size = observed_size;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_pathspace_access_resolve_address(
    runtimec_pathspace_access_t *adapter, const char *key_text, bool record,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_pathspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
    }
    if (value == NULL || value_size < PATHSPACE_XVALUE_HEAD_LEN) {
        
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 41, "key holds no value");
    }
    pathspaceXvalueHead head;
    memcpy(&head, value, sizeof(head));
    if (pathspaceXvalueHeadTotalLen(&head) > (uint64_t)value_size) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 42,
            "XValue body and attrs exceed the value");
    }
    const uint64_t address = (uint64_t)(uintptr_t)value;
    if (address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 43, "pathspace returned a null address");
    }
    if (record) {
        const size_t key_size = strlen(key_text);
        const runtimec_status_t status = runtimec_pathspace_access_ledger_record(
            adapter, key_text, key_size, address, (uint64_t)value_size, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    *output_xvalue_address = address;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_resolve_static_xvalue_address(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (adapter == NULL || output_xvalue_address == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 37, "invalid address request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 38,
            "key is not a static absolute key");
    }
    
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) == 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 39,
            "keys under /vthread are rewritten during execution");
    }
    return runtimec_pathspace_access_resolve_address(
        adapter, key_text, true, output_xvalue_address, error);
}

static runtimec_status_t runtimec_pathspace_access_resolve_frame_address(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, bool record,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (output_generation != NULL) {
        *output_generation = 0U;
    }
    if (adapter == NULL || output_xvalue_address == NULL ||
        output_generation == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 63, "invalid frame address request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 64,
            "frame key is not an absolute key");
    }
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 65,
            "key is not a frame slot under /vthread");
    }
    runtimec_status_t status = runtimec_pathspace_access_resolve_address(
        adapter, key_text, record, output_xvalue_address, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        return status;
    }
    
    if (!adapter->generation_started || adapter->generation == 0U) {
        adapter->generation = 1U;
        adapter->generation_started = true;
    }
    *output_generation = adapter->generation;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_resolve_frame_xvalue_address(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    return runtimec_pathspace_access_resolve_frame_address(
        access, snapshot, key, true, output_xvalue_address, output_generation,
        error);
}

static runtimec_status_t runtimec_pathspace_access_write_system_member(
    runtimec_pathspace_access_t *adapter, runtimec_string_view_t vthread,
    const char *member, runtimec_string_view_t text, runtimec_error_t *error) {
    if (!runtimec_string_view_is_valid(text) || text.size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 45, "invalid member text");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(member) + 1U >
        sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 46, "member key is too long");
    }
    int written = snprintf(key_text, sizeof(key_text), "/vthread/%.*s/%s%s",
                           (int)vthread.size, vthread.data,
                           RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR, member);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 47, "member key formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (pathlangXvalueHeadNewSlack(
            (const uint8_t *)text.data, text.size,
            RUNTIMEC_PATHSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 48, "system value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 49,
            error_text[0] != '\0' ? error_text : "pathspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

typedef struct runtimec_pathspace_width_entry {
    const char *langtype;
    uint64_t width;
} runtimec_pathspace_width_entry_t;

static const runtimec_pathspace_width_entry_t runtimec_pathspace_width_table[] = {
    {"int8", 1U},    {"uint8", 1U},   {"bool", 1U},
    {"int16", 2U},   {"uint16", 2U},  {"int32", 4U},
    {"uint32", 4U},  {"float32", 4U}, {"int64", 8U},
    {"uint64", 8U},  {"float64", 8U}, {"time", 8U},
    {"time/duration", 8U},
    
    {"char/ascii", 1U}, {"char/utf32", 4U},
};

static bool runtimec_pathspace_base_width(runtimec_string_view_t langtype,
                                        uint64_t *output_width) {
    const size_t count = sizeof(runtimec_pathspace_width_table) /
                         sizeof(runtimec_pathspace_width_table[0]);

    *output_width = 0U;
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const char *name = runtimec_pathspace_width_table[ordinal].langtype;
        const size_t size = strlen(name);
        if (langtype.size == size &&
            memcmp(langtype.data, name, size) == 0) {
            *output_width = runtimec_pathspace_width_table[ordinal].width;
            return true;
        }
    }
    return false;
}

static bool runtimec_pathspace_array_element_count(runtimec_string_view_t shape,
                                                 uint64_t *output_count) {
    uint64_t total = 1U;
    size_t cursor = 0U;

    *output_count = 0U;
    while (cursor < shape.size && shape.data[cursor] == '[') {
        uint64_t dimension = 0U;
        bool have_digit = false;

        cursor += 1U;
        while (cursor < shape.size && shape.data[cursor] != ']') {
            const char character = shape.data[cursor];

            if (character == ',') {
                
                if (!have_digit) {
                    return false;
                }
                total *= dimension;
                dimension = 0U;
                have_digit = false;
                cursor += 1U;
                continue;
            }
            if (character < '0' || character > '9') {
                return false; 
            }
            
            if (dimension > (UINT64_MAX - (uint64_t)(character - '0')) / 10U) {
                return false;
            }
            dimension = dimension * 10U + (uint64_t)(character - '0');
            have_digit = true;
            cursor += 1U;
        }
        if (cursor >= shape.size || !have_digit) {
            return false; 
        }
        total *= dimension;
        cursor += 1U; 
    }
    if (cursor == 0U) {
        return false; 
    }
    *output_count = total;
    return true;
}

static bool runtimec_pathspace_zero_width(runtimec_string_view_t langtype,
                                        uint64_t *output_width) {
    uint64_t element_count = 0U;
    size_t cursor = 0U;
    runtimec_string_view_t base;
    uint64_t base_width = 0U;

    *output_width = 0U;
    
    if (langtype.size != 0U &&
        (langtype.data[0] == '*' || langtype.data[0] == '@')) {
        return false;
    }
    
    if (langtype.size != 0U && langtype.data[0] == '[') {
        
        while (cursor < langtype.size && langtype.data[cursor] == '[') {
            while (cursor < langtype.size && langtype.data[cursor] != ']') {
                cursor += 1U;
            }
            if (cursor >= langtype.size) {
                return false; 
            }
            cursor += 1U;
        }
        {
            const runtimec_string_view_t shape =
                runtimec_string_view_make(langtype.data, cursor);

            if (!runtimec_pathspace_array_element_count(shape, &element_count)) {
                return false;
            }
        }
        base = runtimec_string_view_make(langtype.data + cursor,
                                         langtype.size - cursor);
        if (!runtimec_pathspace_base_width(base, &base_width)) {
            return false;
        }
        if (element_count != 0U && base_width > UINT64_MAX / element_count) {
            return false;
        }
        *output_width = element_count * base_width;
        return true;
    }
    return runtimec_pathspace_base_width(langtype, output_width);
}

bool runtimec_pathspace_access_langtype_has_fixed_zero_width(
    runtimec_string_view_t langtype) {
    uint64_t width = 0U;

    return runtimec_pathspace_access_langtype_zero_width(langtype, &width);
}

bool runtimec_pathspace_access_langtype_zero_width(
    runtimec_string_view_t langtype, uint64_t *output_width) {
    if (output_width != NULL) {
        *output_width = 0U;
    }
    if (output_width == NULL || !runtimec_string_view_is_valid(langtype) ||
        langtype.size == 0U) {
        return false;
    }
    return runtimec_pathspace_zero_width(langtype, output_width);
}

runtimec_status_t runtimec_pathspace_access_ensure_frame(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;

    if (adapter == NULL || !runtimec_key_view_is_valid(frame_path) ||
        frame_path.size == 0U ||
        !runtimec_string_view_is_valid(function_key) ||
        function_key.size == 0U || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 70, "invalid ensure-frame request");
    }
    char frame_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(frame_path, frame_text,
                                           sizeof(frame_text), NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 71,
            "frame path is not an absolute key");
    }
    if (strncmp(frame_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 72,
            "frame path is not under /vthread");
    }

    char directory_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    const size_t frame_size = strlen(frame_text);
    if (frame_size + 2U > sizeof(directory_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 73, "frame path is too long");
    }
    memcpy(directory_text, frame_text, frame_size);
    size_t directory_size = frame_size;
    if (directory_text[directory_size - 1U] != '/') {
        directory_text[directory_size] = '/';
        ++directory_size;
    }
    directory_text[directory_size] = '\0';

    char pathspace_error[256];
    pathspace_error[0] = '\0';
    if (pathspaceMkindex(adapter->handle, directory_text, pathspace_error,
                       (uint32_t)sizeof(pathspace_error)) != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 74,
            pathspace_error[0] != '\0' ? pathspace_error
                                      : "pathspace frame directory failed");
    }

    char library_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (directory_size + separator_size +
            sizeof(runtimec_pathspace_member_library) > sizeof(library_key)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 75, "library key is too long");
    }
    int written = snprintf(library_key, sizeof(library_key), "%s%s%s",
                           directory_text,
                           RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
                           runtimec_pathspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 76, "library key formatting failed");
    }

    const uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (runtimec_pathspace_access_read_wire(adapter, library_key, &existing,
                                          &existing_size, error) ==
            RUNTIMEC_STATUS_SUCCESS &&
        existing != NULL) {
        const size_t head_size = PATHSPACE_XVALUE_HEAD_LEN;
        if (existing_size >= head_size) {
            pathspaceXvalueHead existing_head;
            memcpy(&existing_head, existing, sizeof(existing_head));
            const uint64_t existing_body = pathspaceXvalueHeadLen(&existing_head);
            if (existing_body == (uint64_t)function_key.size &&
                memcmp(existing + head_size, function_key.data,
                       function_key.size) == 0) {
                return RUNTIMEC_STATUS_SUCCESS;
            }
        }
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 77,
            "frame already runs a different function");
    }

    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (pathlangXvalueHeadNewSlack(
            (const uint8_t *)function_key.data, function_key.size,
            RUNTIMEC_PATHSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 78, "library value encoding failed");
    }
    pathspace_error[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        adapter->handle, library_key, wire, (uint32_t)wire_size, pathspace_error,
        (uint32_t)sizeof(pathspace_error));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 79,
            pathspace_error[0] != '\0' ? pathspace_error
                                     : "pathspace library write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_frame_generation(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, uint64_t *output_generation,
    runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    char frame_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    char library_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    size_t directory_size;
    int written;

    if (output_generation != NULL) {
        *output_generation = 0U;
    }
    if (adapter == NULL || output_generation == NULL || error == NULL ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 86,
            "invalid frame generation request");
    }
    if (!runtimec_pathspace_access_key_text(frame_path, frame_text,
                                           sizeof(frame_text), NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 87,
            "frame path is not an absolute key");
    }
    
    directory_size = strlen(frame_text);
    if (directory_size + 1U >= sizeof(frame_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 88, "frame path is too long");
    }
    if (frame_text[directory_size - 1U] != '/') {
        frame_text[directory_size] = '/';
        ++directory_size;
        frame_text[directory_size] = '\0';
    }
    written = snprintf(library_key, sizeof(library_key), "%s%s%s", frame_text,
                       RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
                       runtimec_pathspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 89, "library key formatting failed");
    }

    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status = runtimec_pathspace_access_resolve_frame_address(
        adapter, snapshot, runtimec_key_view_make(library_key, (size_t)written),
        false, &xvalue_address, output_generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_generation = 0U;
        return status;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_ensure_zero_value(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_string_view_t langtype, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_pathspace_access_t *adapter = access;

    if (adapter == NULL || !runtimec_key_view_is_valid(key) || key.size == 0U ||
        !runtimec_string_view_is_valid(langtype) || langtype.size == 0U ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 80,
            "invalid ensure-zero-value request");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (!runtimec_pathspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 82,
            "local slot key is not an absolute key");
    }

    uint64_t width = 0U;
    const bool fixed_width = runtimec_pathspace_zero_width(langtype, &width);

    const uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (runtimec_pathspace_access_read_wire(adapter, key_text, &existing,
                                          &existing_size, error) ==
            RUNTIMEC_STATUS_SUCCESS &&
        existing != NULL) {
        if (existing_size < PATHSPACE_XVALUE_HEAD_LEN) {
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 83,
                "existing local slot has no readable XValue head");
        }
        if (!fixed_width) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        pathspaceXvalueHead existing_head;
        memcpy(&existing_head, existing, sizeof(existing_head));
        if (pathspaceXvalueHeadLen(&existing_head) == width) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 84,
            "existing local slot has a different width than its langtype");
    }

    char langtype_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    if (langtype.size + 1U > sizeof(langtype_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 85, "langtype is too long");
    }
    memcpy(langtype_text, langtype.data, langtype.size);
    langtype_text[langtype.size] = '\0';

    const uint64_t body_length = fixed_width ? width : 0U;
    uint8_t *zero_body = body_length != 0U ? calloc(1U, (size_t)body_length) : NULL;
    if (body_length != 0U && zero_body == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 86,
            "zero body allocation failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    const int encode_status = pathlangXvalueHeadNewSlack(
        zero_body, body_length, langtype_text, &wire, &wire_size);
    free(zero_body);
    if (encode_status != 0 || wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 87,
            "zero value encoding failed");
    }
    char pathspace_error[256];
    pathspace_error[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, pathspace_error,
        (uint32_t)sizeof(pathspace_error));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 88,
            pathspace_error[0] != '\0' ? pathspace_error
                                      : "pathspace zero value write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_write_program_counter(
    runtimec_pathspace_access_t *adapter, runtimec_string_view_t vthread,
    runtimec_program_counter_t counter, runtimec_error_t *error) {
    if (counter.source_instruction_ordinal < 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 94,
            "program counter ordinal must not be negative");
    }
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(runtimec_pathspace_member_program_counter) + 1U >
        sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 90, "counter key is too long");
    }
    int written = snprintf(key_text, sizeof(key_text), "/vthread/%.*s/%s%s",
                           (int)vthread.size, vthread.data,
                           RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
                           runtimec_pathspace_member_program_counter);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 91,
            "counter key formatting failed");
    }

    uint8_t body[8];
    const uint32_t stack_bits = (uint32_t)counter.stack_identifier;
    const uint32_t ordinal_bits = (uint32_t)counter.source_instruction_ordinal;
    for (size_t index = 0U; index < 4U; ++index) {
        body[index] = (uint8_t)((stack_bits >> (8U * index)) & 0xFFU);
        body[4U + index] = (uint8_t)((ordinal_bits >> (8U * index)) & 0xFFU);
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    
    if (pathlangXvalueHeadNewSlack(body, sizeof(body),
                                            "[2]int32", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 92, "counter value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 93,
            error_text[0] != '\0' ? error_text : "pathspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_read_program_counter(
    runtimec_pathspace_access_t *access, runtimec_string_view_t vthread,
    runtimec_program_counter_t *output_counter, bool *output_present,
    runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    char key_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;

    if (output_counter != NULL) {
        *output_counter = RUNTIMEC_PROGRAM_COUNTER_NONE;
    }
    if (output_present != NULL) {
        *output_present = false;
    }
    if (adapter == NULL || !runtimec_string_view_is_valid(vthread) ||
        vthread.size == 0U || output_counter == NULL ||
        output_present == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 95,
            "invalid program counter read request");
    }
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(runtimec_pathspace_member_program_counter) + 1U >
        sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 96, "counter key is too long");
    }
    const int written = snprintf(
        key_text, sizeof(key_text), "/vthread/%.*s/%s%s", (int)vthread.size,
        vthread.data, RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
        runtimec_pathspace_member_program_counter);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 97,
            "counter key formatting failed");
    }

    const uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    if (runtimec_pathspace_access_read_wire(adapter, key_text, &wire, &wire_size,
                                          NULL) != RUNTIMEC_STATUS_SUCCESS ||
        wire == NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (wire_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 98, "counter wire is truncated");
    }
    pathspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    if (pathspaceXvalueHeadLen(&head) != 8U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 99,
            "counter body is not [2]int32");
    }
    const uint8_t *body = wire + PATHSPACE_XVALUE_HEAD_LEN;
    uint32_t stack_bits = 0U;
    uint32_t ordinal_bits = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        stack_bits |= (uint32_t)body[index] << (8U * index);
        ordinal_bits |= (uint32_t)body[4U + index] << (8U * index);
    }
    output_counter->stack_identifier = (int32_t)stack_bits;
    output_counter->source_instruction_ordinal = (int32_t)ordinal_bits;
    *output_present = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_read_frame_library(
    runtimec_pathspace_access_t *access, runtimec_key_view_t frame_path,
    char *output_text, size_t output_capacity, runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    char frame_text[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    char library_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    size_t directory_size;
    int written;

    if (output_text != NULL && output_capacity != 0U) {
        output_text[0] = '\0';
    }
    if (adapter == NULL || !runtimec_key_view_is_valid(frame_path) ||
        frame_path.size == 0U || output_text == NULL ||
        output_capacity == 0U || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 100,
            "invalid frame library read request");
    }
    if (!runtimec_pathspace_access_key_text(frame_path, frame_text,
                                          sizeof(frame_text), NULL)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 101,
            "frame path is not an absolute key");
    }
    directory_size = strlen(frame_text);
    if (directory_size + 1U >= sizeof(frame_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 102, "frame path is too long");
    }
    if (frame_text[directory_size - 1U] != '/') {
        frame_text[directory_size] = '/';
        ++directory_size;
        frame_text[directory_size] = '\0';
    }
    written = snprintf(library_key, sizeof(library_key), "%s%s%s", frame_text,
                       RUNTIMEC_PATHSPACE_RUNTIME_MEMBER_SEPARATOR,
                       runtimec_pathspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 103,
            "library key formatting failed");
    }

    const uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    if (runtimec_pathspace_access_read_wire(adapter, library_key, &wire,
                                          &wire_size, error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        wire == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 104,
            "frame has no library");
    }
    if (wire_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 105, "library wire is truncated");
    }
    pathspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    const uint64_t body_size = pathspaceXvalueHeadLen(&head);
    if (body_size + 1U > (uint64_t)output_capacity ||
        body_size > (uint64_t)UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 106,
            "library text exceeds the output buffer");
    }
    memcpy(output_text, wire + PATHSPACE_XVALUE_HEAD_LEN, (size_t)body_size);
    output_text[body_size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_checkpoint(
    runtimec_pathspace_access_t *access, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_checkpoint_is_valid(checkpoint) ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 50, "invalid checkpoint");
    }
    if (!runtimec_string_view_is_valid(checkpoint->vthread_identifier) ||
        checkpoint->vthread_identifier.size == 0U) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 51,
            "checkpoint has no vthread identifier");
    }
    runtimec_status_t status = runtimec_pathspace_access_write_program_counter(
        adapter, checkpoint->vthread_identifier,
        checkpoint->logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_pathspace_access_write_system_member(
        adapter, checkpoint->vthread_identifier,
        runtimec_pathspace_member_status, checkpoint->persistent_status, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (runtimec_string_view_is_valid(checkpoint->error.message) &&
        checkpoint->error.message.size != 0U) {
        return runtimec_pathspace_access_write_system_member(
            adapter, checkpoint->vthread_identifier,
            runtimec_pathspace_member_message, checkpoint->error.message, error);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

#define RUNTIMEC_PATHSPACE_HANDOFF_POLL_NANOSECONDS UINT64_C(1000000)
#define RUNTIMEC_PATHSPACE_HANDOFF_DEADLINE_NANOSECONDS UINT64_C(30000000000)

static void runtimec_pathspace_access_sleep(uint64_t duration_nanoseconds) {
    if (duration_nanoseconds == 0U) {
        return;
    }
    const struct timespec interval = {
        .tv_sec = (time_t)(duration_nanoseconds / UINT64_C(1000000000)),
        .tv_nsec = (long)(duration_nanoseconds % UINT64_C(1000000000)),
    };
    nanosleep(&interval, NULL);
}

runtimec_status_t runtimec_pathspace_access_handoff(
    runtimec_pathspace_access_t *access, const runtimec_handoff_request_t *request,
    runtimec_error_t *error) {
    runtimec_pathspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_handoff_request_is_valid(request) ||
        error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 52, "invalid handoff request");
    }
    if (request->operation.size + sizeof("/lib/") + sizeof("/vids/") +
            request->checkpoint.vthread_identifier.size + 1U >
        RUNTIMEC_PATHSPACE_KEY_CAPACITY) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 53, "handoff key is too long");
    }
    char queue_key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
    int written = snprintf(queue_key, sizeof(queue_key), "/lib/%.*s/vids/%.*s",
                           (int)request->operation.size, request->operation.data,
                           (int)request->checkpoint.vthread_identifier.size,
                           request->checkpoint.vthread_identifier.data);
    if (written <= 0 || (size_t)written >= sizeof(queue_key)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 54, "handoff key formatting failed");
    }
    
    char target_text[96];
    const int target_size = snprintf(
        target_text, sizeof(target_text), "[%d,%d]",
        (int)request->checkpoint.logical_program_counter.stack_identifier,
        (int)request->checkpoint.logical_program_counter
            .source_instruction_ordinal);
    if (target_size <= 0 || (size_t)target_size >= sizeof(target_text)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 55,
            "handoff program counter formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (pathlangXvalueHeadNewSlack(
            (const uint8_t *)target_text, (uint64_t)target_size,
            "char/utf8", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56, "handoff value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_pathspace_write_wire(
        adapter->handle, queue_key, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56,
            error_text[0] != '\0' ? error_text : "pathspace write failed");
    }
    uint64_t waited_nanoseconds = 0U;
    while (waited_nanoseconds <
           RUNTIMEC_PATHSPACE_HANDOFF_DEADLINE_NANOSECONDS) {
        const uint8_t *observed = NULL;
        uint32_t observed_size = 0U;
        if (runtimec_pathspace_access_read_wire(adapter, queue_key, &observed,
                                              &observed_size, error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 57, "handoff queue read failed");
        }
        if (observed == NULL || observed_size == 0U) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        
        runtimec_pathspace_access_sleep(RUNTIMEC_PATHSPACE_HANDOFF_POLL_NANOSECONDS);
        waited_nanoseconds += RUNTIMEC_PATHSPACE_HANDOFF_POLL_NANOSECONDS;
    }
    return runtimec_pathspace_access_fail(
        error, RUNTIMEC_STATUS_WAIT, 58, "handoff was not claimed in time");
}

bool runtimec_pathspace_access_key_is_static(runtimec_key_view_t key) {
    if (!runtimec_key_view_is_valid(key) || key.size == 0U ||
        key.data[0] != '/') {
        return false;
    }
    return strncmp(key.data, "/vthread/",
                   key.size < sizeof("/vthread/") - 1U ? key.size
                                                       : sizeof("/vthread/") -
                                                             1U) != 0;
}

runtimec_status_t runtimec_pathspace_access_create(
    const runtimec_pathspace_access_request_t *request,
    runtimec_pathspace_access_t **output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (request == NULL || output == NULL || error == NULL ||
        request->borrowed_pathspace_handle == NULL ||
        request->instance_identifier == 0U ||
        !runtimec_allocator_is_valid(&request->allocator)) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 58,
            "invalid pathspace access request");
    }
    runtimec_pathspace_access_t *access = runtimec_allocator_allocate(
        &request->allocator, sizeof(*access),
        alignof(runtimec_pathspace_access_t));
    if (access == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 59,
            "pathspace access allocation failed");
    }
    memset(access, 0, sizeof(*access));
    access->allocator = request->allocator;
    access->handle = request->borrowed_pathspace_handle;
    access->instance_identifier = request->instance_identifier;
    
    access->directory = runtimec_allocator_allocate(
        &request->allocator, sizeof(*access->directory),
        alignof(runtimec_key_directory_t));
    if (access->directory == NULL) {
        runtimec_status_t cleanup = runtimec_allocator_deallocate(
            &request->allocator, access, sizeof(*access),
            alignof(runtimec_pathspace_access_t));
        (void)cleanup;
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 59,
            "directory cache allocation failed");
    }
    runtimec_key_directory_reset(access->directory, &request->allocator, access,
                                 runtimec_pathspace_access_directory_source);
    *output = access;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_pathspace_access_destroy(
    runtimec_pathspace_access_t *access, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (access == NULL || error == NULL) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 60,
            "invalid pathspace access destroy request");
    }
    const runtimec_allocator_t allocator = access->allocator;
    if (access->directory != NULL) {
        runtimec_status_t released =
            runtimec_key_directory_destroy(access->directory);
        runtimec_allocator_deallocate(&allocator, access->directory,
                                      sizeof(*access->directory),
                                      alignof(runtimec_key_directory_t));
        access->directory = NULL;
        if (released != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 61,
                "directory cache deallocation violated the allocator contract");
        }
    }
    if (access->resolved_addresses != NULL) {
        runtimec_status_t status = runtimec_allocator_deallocate(
            &allocator, access->resolved_addresses,
            access->resolved_address_capacity *
                sizeof(runtimec_pathspace_resolved_address_t),
            alignof(runtimec_pathspace_resolved_address_t));
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_pathspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 61,
                "address ledger deallocation violated the allocator contract");
        }
    }
    memset(access, 0, sizeof(*access));
    runtimec_status_t status = runtimec_allocator_deallocate(
        &allocator, access, sizeof(*access),
        alignof(runtimec_pathspace_access_t));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_pathspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 62,
            "pathspace access deallocation violated the allocator contract");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address) {
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        xvalue_address > UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    }
    return xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

runtimec_xvalue_address_t
runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    return body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

static bool runtimec_value_effective_length(const pathspaceXvalueHead *head,
                                            uint64_t *output_length) {
    *output_length = 0U;
    if (!pathspaceXvalueHeadPaddingIsZero(head)) {
        return false;
    }
    *output_length = pathspaceXvalueHeadLen(head);
    return true;
}

uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return 0U;
    }
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(uintptr_t)(
        body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    uint64_t body_length = 0U;

    if (!runtimec_value_effective_length(head, &body_length)) {
        return 0U;
    }
    return body_length > UINT32_MAX ? 0U : (uint32_t)body_length;
}

runtimec_string_view_t runtimec_xvalue_body_from_wire(
    runtimec_bytes_view_t wire) {
    const size_t offset = (size_t)RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    if (wire.data == NULL || wire.size < offset) {
        return runtimec_string_view_make(NULL, 0U);
    }
    pathspaceXvalueHead head;
    memcpy(&head, wire.data, sizeof(head));
    if (!pathspaceXvalueHeadPaddingIsZero(&head)) {
        return runtimec_string_view_make(NULL, 0U);
    }
    const uint64_t body_length = pathspaceXvalueHeadLen(&head);
    const uint64_t attrs_length = (uint64_t)pathspaceXvalueHeadAttrsLen(&head);
    const uint64_t available = (uint64_t)wire.size - (uint64_t)offset;
    if (body_length > available || attrs_length > available - body_length) {
        return runtimec_string_view_make(NULL, 0U);
    }
    return runtimec_string_view_make((const char *)wire.data + offset,
                                     (size_t)body_length);
}

void runtimec_pathspace_value_header_reset(runtimec_pathspace_value_header_t *header) {
    if (header != NULL) {
        memset(header, 0, sizeof(*header));
    }
}

void runtimec_handoff_request_reset(runtimec_handoff_request_t *request) {
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }
}

bool runtimec_handoff_request_is_valid(
    const runtimec_handoff_request_t *request) {
    return request != NULL &&
           runtimec_string_view_is_valid(request->operation) &&
           request->operation.size != 0U;
}
