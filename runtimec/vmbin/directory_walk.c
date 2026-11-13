#include "vmbin/directory_walk.h"

#include <errno.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>

#include "contract/key_resolution.h"
#include "contract/pathspace_interface.h"

static void *walk_libc_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size != 0U ? size : 1U);
}

static void walk_libc_deallocate(void *context, void *allocation, size_t size,
                                 size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static runtimec_allocator_t walk_allocator_or_libc(
    const runtimec_allocator_t *allocator) {
    if (allocator != NULL && runtimec_allocator_is_valid(allocator)) {
        return *allocator;
    }
    runtimec_allocator_t libc;
    libc.context = NULL;
    libc.allocate = walk_libc_allocate;
    libc.deallocate = walk_libc_deallocate;
    return libc;
}

typedef struct walk_source_context {
    void *handle;
    char directory[RUNTIMEC_DIRECTORY_WALK_KEY_CAPACITY];
} walk_source_context_t;

static runtimec_status_t walk_source(void *context, const char *directory,
                                     char *names, uint32_t capacity,
                                     uint32_t *output_length,
                                     uint32_t *output_count,
                                     uint64_t *output_generation) {
    const walk_source_context_t *source = (const walk_source_context_t *)context;
    (void)directory; 
    if (source == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int status =
        pathspaceList(source->handle, source->directory, names, capacity,
                    output_length, output_count, output_generation, error_text,
                    (uint32_t)sizeof(error_text));
    if (status == 0) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (status == -ENOSPC) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    return RUNTIMEC_STATUS_ERROR;
}

static runtimec_status_t walk_reader(void *context, const char *key,
                                     const char **output_langtype,
                                     size_t *output_langtype_size,
                                     const char **output_body,
                                     size_t *output_body_size) {
    void *handle = context;
    if (output_langtype != NULL) {
        *output_langtype = NULL;
    }
    if (output_langtype_size != NULL) {
        *output_langtype_size = 0U;
    }
    if (output_body != NULL) {
        *output_body = NULL;
    }
    if (output_body_size != NULL) {
        *output_body_size = 0U;
    }
    if (handle == NULL || key == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    char error_text[128];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    const int resolve_status =
        pathspaceResolveOffset(handle, key, 0U, &value_offset, error_text,
                       (uint32_t)sizeof(error_text));
    if (resolve_status != 0) {
        return RUNTIMEC_STATUS_INVALID; 
    }
    const uint8_t *base = pathspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return RUNTIMEC_STATUS_ERROR; 
    }
    const uint8_t *wire = base + value_offset;
    const uint64_t wire_size =
        pathspaceXvalueHeadTotalLen((const pathspaceXvalueHead *)(const void *)wire);
    if (wire_size < PATHSPACE_XVALUE_HEAD_LEN) {
        return RUNTIMEC_STATUS_ERROR;
    }
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)wire;
    if (!pathspaceXvalueHeadPaddingIsZero(head)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (output_body != NULL) {
        *output_body = (const char *)wire + PATHSPACE_XVALUE_HEAD_LEN;
    }
    if (output_body_size != NULL) {
        *output_body_size = (size_t)pathspaceXvalueHeadLen(head);
    }
    const uint8_t *langtype = NULL;
    const uint16_t langtype_size =
        pathlangXvalueHeadLangtypeBounded(wire, (uint64_t)wire_size, &langtype);
    if (output_langtype != NULL && langtype != NULL) {
        *output_langtype = (const char *)langtype;
    }
    if (output_langtype_size != NULL) {
        *output_langtype_size = (size_t)langtype_size;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

bool runtimec_directory_walk_open(runtimec_directory_walk_t *walk,
                                  const runtimec_allocator_t *allocator,
                                  void *pathspace_handle, const char *directory,
                                  bool resolve_pointer, bool expand_extension) {
    
    if (walk == NULL) {
        return false;
    }
    memset(walk, 0, sizeof(*walk));

    if (pathspace_handle == NULL || directory == NULL) {
        return false;
    }

    if (expand_extension) {
        return false;
    }

    walk_source_context_t *source = malloc(sizeof(*source));
    if (source == NULL) {
        return false;
    }
    memset(source, 0, sizeof(*source));
    source->handle = pathspace_handle;

    if (resolve_pointer) {
        if (runtimec_key_resolve_prefix(walk_reader, pathspace_handle, directory,
                                        source->directory,
                                        sizeof(source->directory)) !=
            RUNTIMEC_STATUS_SUCCESS) {
            free(source);
            return false;
        }
    } else {
        const size_t size = strlen(directory);
        if (size + 1U > sizeof(source->directory)) {
            free(source);
            return false;
        }
        memcpy(source->directory, directory, size + 1U);
    }

    const runtimec_allocator_t resolved_allocator = walk_allocator_or_libc(allocator);
    runtimec_key_directory_reset(&walk->directory, &resolved_allocator, source,
                                 walk_source);
    walk->opened = true;
    if (runtimec_key_directory_refresh(&walk->directory, source->directory) !=
        RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_directory_walk_close(walk);
        return false;
    }
    return true;
}

bool runtimec_directory_walk_count(const runtimec_directory_walk_t *walk,
                                   int64_t *output_count) {
    if (walk == NULL || !walk->opened || output_count == NULL) {
        return false;
    }
    *output_count = (int64_t)walk->directory.count;
    return true;
}

bool runtimec_directory_walk_at(const runtimec_directory_walk_t *walk,
                                int64_t index, const char **output_name,
                                uint32_t *output_size) {
    if (walk == NULL || !walk->opened || index < 0) {
        return false;
    }
    return runtimec_key_directory_at(&walk->directory, (uint32_t)index,
                                     output_name, output_size) ==
           RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_directory_walk_close(runtimec_directory_walk_t *walk) {
    if (walk == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    void *source = walk->directory.source_context;
    const runtimec_status_t status =
        runtimec_key_directory_destroy(&walk->directory);
    
    walk->directory.source_context = NULL;
    free(source);
    walk->opened = false;
    return status;
}
