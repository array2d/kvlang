#include "contract/key_resolution.h"

#include <string.h>

static bool copy_key(char *output, size_t output_capacity, const char *source,
                     size_t length) {
    if (length + 1U > output_capacity) {
        return false;
    }
    memcpy(output, source, length);
    output[length] = '\0';
    return true;
}

runtimec_status_t runtimec_key_resolve_prefix(
    const runtimec_key_reader_t reader, void *reader_context, const char *key,
    char *output_key, size_t output_capacity) {
    if (reader == NULL || key == NULL || output_key == NULL ||
        output_capacity == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const size_t key_length = strlen(key);
    if (!copy_key(output_key, output_capacity, key, key_length)) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }

    for (unsigned hop = 0U; hop < RUNTIMEC_KEY_RESOLUTION_HOP_LIMIT; ++hop) {
        const char *langtype = NULL;
        const char *body = NULL;
        size_t langtype_size = 0U;
        size_t body_size = 0U;

        const runtimec_status_t status = reader(
            reader_context, output_key, &langtype, &langtype_size, &body,
            &body_size);
        if (status == RUNTIMEC_STATUS_INVALID) {
            
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }

        const bool is_pointer = langtype != NULL && langtype_size != 0U &&
                                langtype[0] == '*';
        if (!is_pointer) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (body == NULL) {
            
            return RUNTIMEC_STATUS_INVALID;
        }
        if (!copy_key(output_key, output_capacity, body, body_size)) {
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
    }
    
    return RUNTIMEC_STATUS_ERROR;
}
