#ifndef RUNTIMEC_CONTRACT_KEY_RESOLUTION_H
#define RUNTIMEC_CONTRACT_KEY_RESOLUTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_KEY_RESOLUTION_HOP_LIMIT 16U

typedef runtimec_status_t (*runtimec_key_reader_t)(
    void *context, const char *key, const char **output_langtype,
    size_t *output_langtype_size, const char **output_body,
    size_t *output_body_size);

runtimec_status_t runtimec_key_resolve_prefix(
    const runtimec_key_reader_t reader, void *reader_context, const char *key,
    char *output_key, size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif
