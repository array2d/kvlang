#ifndef RUNTIMEC_CONTRACT_VIEW_H
#define RUNTIMEC_CONTRACT_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_BYTES_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_STRING_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_KEY_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_STRING_VIEW_LITERAL(value) { (value), sizeof(value) - 1U }
#define RUNTIMEC_KEY_VIEW_LITERAL(value) { (value), sizeof(value) - 1U }

typedef struct runtimec_bytes_view {
    const uint8_t *data;
    size_t size;
} runtimec_bytes_view_t;

typedef struct runtimec_bytes_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} runtimec_bytes_buffer_t;

typedef struct runtimec_string_view {
    const char *data;
    size_t size;
} runtimec_string_view_t;

typedef struct runtimec_key_view {
    const char *data;
    size_t size;
} runtimec_key_view_t;

runtimec_bytes_view_t runtimec_bytes_view_make(const void *data, size_t size);
runtimec_string_view_t runtimec_string_view_make(const char *data, size_t size);

runtimec_string_view_t runtimec_string_view_from_c_string(const char *value);
runtimec_key_view_t runtimec_key_view_make(const char *data, size_t size);
runtimec_key_view_t runtimec_key_view_from_c_string(const char *value);

void runtimec_bytes_buffer_reset(runtimec_bytes_buffer_t *buffer);

bool runtimec_bytes_view_is_valid(runtimec_bytes_view_t view);
bool runtimec_bytes_buffer_is_valid(const runtimec_bytes_buffer_t *buffer);
bool runtimec_string_view_is_valid(runtimec_string_view_t view);
bool runtimec_key_view_is_valid(runtimec_key_view_t view);

#ifdef __cplusplus
}
#endif

#endif
