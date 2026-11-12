#ifndef RUNTIMEC_CONTRACT_KEY_DIRECTORY_H
#define RUNTIMEC_CONTRACT_KEY_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_key_directory_listing {
    const char *names;
    uint32_t length;
    uint32_t count;
    uint64_t generation;
} runtimec_key_directory_listing_t;

typedef runtimec_status_t (*runtimec_key_directory_source_t)(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation);

typedef struct runtimec_key_directory {
    runtimec_allocator_t allocator;
    void *source_context;
    runtimec_key_directory_source_t source;

    char *names;
    uint32_t names_capacity;
    uint32_t names_length;
    uint32_t *offsets;
    uint32_t offsets_capacity;
    uint32_t count;
    uint64_t generation;
    bool valid;
} runtimec_key_directory_t;

void runtimec_key_directory_reset(runtimec_key_directory_t *directory,
                                  const runtimec_allocator_t *allocator,
                                  void *source_context,
                                  runtimec_key_directory_source_t source);

runtimec_status_t runtimec_key_directory_destroy(
    runtimec_key_directory_t *directory);

runtimec_status_t runtimec_key_directory_refresh(
    runtimec_key_directory_t *directory, const char *key);

runtimec_status_t runtimec_key_directory_at(const runtimec_key_directory_t *directory,
                                            uint32_t index, const char **output_name,
                                            uint32_t *output_size);

runtimec_status_t runtimec_key_directory_find(const runtimec_key_directory_t *directory,
                                              const char *name, uint32_t name_size,
                                              uint32_t *output_index);

#ifdef __cplusplus
}
#endif

#endif
