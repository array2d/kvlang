#ifndef RUNTIMEC_ENGINE_FRAME_STATE_H
#define RUNTIMEC_ENGINE_FRAME_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/frame_bindings.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_frame_state_result {
    RUNTIMEC_FRAME_STATE_INVALID = 0,
    
    RUNTIMEC_FRAME_STATE_CREATED = 1,
    
    RUNTIMEC_FRAME_STATE_REUSED = 2,
    
    RUNTIMEC_FRAME_STATE_REBUILT = 3
} runtimec_frame_state_result_t;

typedef struct runtimec_frame_state {
    runtimec_key_view_t frame_path;
    char *frame_path_text;
    runtimec_string_view_t function_path;
    binrwir_frame_bindings_t *bindings;
    uint64_t generation;
} runtimec_frame_state_t;

typedef struct runtimec_frame_state_table {
    runtimec_allocator_t allocator;
    runtimec_frame_state_t **entries;
    size_t count;
    size_t capacity;
} runtimec_frame_state_table_t;

void runtimec_frame_state_table_initialize(runtimec_frame_state_table_t *table,
                                           runtimec_allocator_t allocator);

runtimec_status_t runtimec_frame_state_open(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_frame_state_result_t *output_result, runtimec_error_t *error);

runtimec_status_t runtimec_frame_state_close(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_error_t *error);

size_t runtimec_frame_state_count(const runtimec_frame_state_table_t *table);

void runtimec_frame_state_table_release(runtimec_frame_state_table_t *table);

#ifdef __cplusplus
}
#endif

#endif
