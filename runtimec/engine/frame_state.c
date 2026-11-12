
#include "engine/frame_state.h"

#include <string.h>

static runtimec_status_t runtimec_frame_state_fail(runtimec_error_t *error,
                                                   runtimec_status_t status,
                                                   int64_t code,
                                                   const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static bool runtimec_frame_state_view_equals(const char *left, size_t left_size,
                                             const char *right,
                                             size_t right_size) {
    if (left_size != right_size) {
        return false;
    }
    if (left_size == 0U) {
        return true;
    }
    return memcmp(left, right, left_size) == 0;
}

static bool runtimec_frame_state_matches(const runtimec_frame_state_t *state,
                                         runtimec_key_view_t frame_path) {
    return runtimec_frame_state_view_equals(state->frame_path_text,
                                            state->frame_path.size,
                                            frame_path.data, frame_path.size);
}

static ptrdiff_t runtimec_frame_state_find(
    const runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < table->count; ++ordinal) {
        if (runtimec_frame_state_matches(table->entries[ordinal], frame_path)) {
            return (ptrdiff_t)ordinal;
        }
    }
    return -1;
}

static void runtimec_frame_state_discard(
    runtimec_frame_state_t *state, const runtimec_allocator_t *allocator) {
    if (state->bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(state->bindings);
        state->bindings = NULL;
    }
    if (state->frame_path_text != NULL) {
        (void)runtimec_allocator_deallocate(allocator, state->frame_path_text,
                                            state->frame_path.size + 1U, 1U);
        state->frame_path_text = NULL;
    }
    (void)runtimec_allocator_deallocate(allocator, state, sizeof(*state),
                                        _Alignof(runtimec_frame_state_t));
}

static void runtimec_frame_state_remove(runtimec_frame_state_table_t *table,
                                        size_t ordinal) {
    size_t index;

    for (index = ordinal + 1U; index < table->count; ++index) {
        table->entries[index - 1U] = table->entries[index];
    }
    table->count -= 1U;
}

static runtimec_status_t runtimec_frame_state_reserve(
    runtimec_frame_state_table_t *table, runtimec_error_t *error) {
    size_t capacity;
    runtimec_frame_state_t **grown;

    if (table->count < table->capacity) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    capacity = table->capacity == 0U ? 8U : table->capacity * 2U;
    grown = (runtimec_frame_state_t **)runtimec_allocator_allocate(
        &table->allocator, capacity * sizeof(*grown),
        _Alignof(runtimec_frame_state_t *));
    if (grown == NULL) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 1,
            "frame state table allocation failed");
    }
    if (table->count != 0U) {
        memcpy(grown, table->entries, table->count * sizeof(*grown));
    }
    if (table->entries != NULL) {
        (void)runtimec_allocator_deallocate(
            &table->allocator, table->entries,
            table->capacity * sizeof(*grown),
            _Alignof(runtimec_frame_state_t *));
    }
    table->entries = grown;
    table->capacity = capacity;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_frame_state_insert(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_error_t *error) {
    runtimec_frame_state_t *state;
    runtimec_status_t status;
    char *text;

    text = (char *)runtimec_allocator_allocate(&table->allocator,
                                              frame_path.size + 1U, 1U);
    if (text == NULL) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
            "frame state path copy allocation failed");
    }
    memcpy(text, frame_path.data, frame_path.size);
    text[frame_path.size] = '\0';

    state = (runtimec_frame_state_t *)runtimec_allocator_allocate(
        &table->allocator, sizeof(*state), _Alignof(runtimec_frame_state_t));
    if (state == NULL) {
        (void)runtimec_allocator_deallocate(&table->allocator, text,
                                            frame_path.size + 1U, 1U);
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 3,
            "frame state allocation failed");
    }
    memset(state, 0, sizeof(*state));
    state->frame_path = runtimec_key_view_make(text, frame_path.size);
    state->frame_path_text = text;
    state->function_path = function_path;
    state->generation = storage_generation;

    if (binrwir_frame_bindings_create(
            &table->allocator,
            binding_capacity == 0U ? 1U : binding_capacity,
            &state->bindings) != RUNTIMEC_STATUS_SUCCESS) {
        state->bindings = NULL;
        runtimec_frame_state_discard(state, &table->allocator);
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 4,
            "frame bindings allocation failed");
    }

    status = runtimec_frame_state_reserve(table, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_frame_state_discard(state, &table->allocator);
        return status;
    }
    table->entries[table->count++] = state;
    *output_state = state;
    return RUNTIMEC_STATUS_SUCCESS;
}

void runtimec_frame_state_table_initialize(runtimec_frame_state_table_t *table,
                                           runtimec_allocator_t allocator) {
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
    table->allocator = allocator;
}

runtimec_status_t runtimec_frame_state_open(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_frame_state_result_t *output_result, runtimec_error_t *error) {
    ptrdiff_t ordinal;
    
    runtimec_frame_state_t *state = NULL;
    runtimec_status_t status;

    if (output_state != NULL) {
        *output_state = NULL;
    }
    if (output_result != NULL) {
        *output_result = RUNTIMEC_FRAME_STATE_INVALID;
    }
    if (table == NULL || error == NULL || output_state == NULL ||
        output_result == NULL || !runtimec_allocator_is_valid(&table->allocator) ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U ||
        !runtimec_string_view_is_valid(function_path) ||
        function_path.size == 0U ||
        storage_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 5,
            "invalid frame state open request");
    }
    ordinal = runtimec_frame_state_find(table, frame_path);
    if (ordinal < 0) {
        status = runtimec_frame_state_insert(table, frame_path, function_path,
                                             storage_generation,
                                             binding_capacity, &state, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        *output_state = state;
        *output_result = RUNTIMEC_FRAME_STATE_CREATED;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    state = table->entries[(size_t)ordinal];
    
    if (!runtimec_frame_state_view_equals(
            state->function_path.data, state->function_path.size,
            function_path.data, function_path.size)) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 6,
            "frame path is already bound to another function");
    }
    if (state->generation == storage_generation) {
        *output_state = state;
        *output_result = RUNTIMEC_FRAME_STATE_REUSED;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    {
        binrwir_frame_bindings_t *rebuilt = NULL;
        if (binrwir_frame_bindings_create(
                &table->allocator,
                binding_capacity == 0U ? 1U : binding_capacity,
                &rebuilt) != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_frame_state_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 7,
                "frame bindings allocation failed");
        }
        (void)binrwir_frame_bindings_destroy(state->bindings);
        state->bindings = rebuilt;
        state->function_path = function_path;
        state->generation = storage_generation;
    }
    *output_state = state;
    *output_result = RUNTIMEC_FRAME_STATE_REBUILT;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_frame_state_close(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_error_t *error) {
    ptrdiff_t ordinal;

    if (table == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (!runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 8,
            "invalid frame state close request");
    }
    ordinal = runtimec_frame_state_find(table, frame_path);
    if (ordinal < 0) {
        
        return RUNTIMEC_STATUS_SUCCESS;
    }
    runtimec_frame_state_discard(table->entries[(size_t)ordinal],
                                       &table->allocator);
    runtimec_frame_state_remove(table, (size_t)ordinal);
    return RUNTIMEC_STATUS_SUCCESS;
}

size_t runtimec_frame_state_count(const runtimec_frame_state_table_t *table) {
    return table != NULL ? table->count : 0U;
}

void runtimec_frame_state_table_release(runtimec_frame_state_table_t *table) {
    if (table == NULL) {
        return;
    }
    while (table->count != 0U) {
        table->count -= 1U;
        runtimec_frame_state_discard(table->entries[table->count],
                                          &table->allocator);
    }
    if (table->entries != NULL) {
        (void)runtimec_allocator_deallocate(
            &table->allocator, table->entries,
            table->capacity * sizeof(*table->entries),
            _Alignof(runtimec_frame_state_t *));
    }
    table->entries = NULL;
    table->capacity = 0U;
}
