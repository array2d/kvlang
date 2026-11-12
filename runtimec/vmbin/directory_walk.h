#ifndef RUNTIMEC_VMBIN_DIRECTORY_WALK_H
#define RUNTIMEC_VMBIN_DIRECTORY_WALK_H

#include <stdbool.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/key_directory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_DIRECTORY_WALK_KEY_CAPACITY 1024U

#define KVSPACE_KEY_RESOLUTION_MIN_WIRE 16U

typedef struct runtimec_directory_walk {
    runtimec_key_directory_t directory;
    runtimec_allocator_t allocator;
    bool opened;
} runtimec_directory_walk_t;

bool runtimec_directory_walk_open(runtimec_directory_walk_t *walk,
                                  const runtimec_allocator_t *allocator,
                                  void *kvspace_handle, const char *directory,
                                  bool resolve_pointer, bool expand_extension);

bool runtimec_directory_walk_count(const runtimec_directory_walk_t *walk,
                                   int64_t *output_count);

bool runtimec_directory_walk_at(const runtimec_directory_walk_t *walk,
                                int64_t index, const char **output_name,
                                uint32_t *output_size);

runtimec_status_t runtimec_directory_walk_close(runtimec_directory_walk_t *walk);

#ifdef __cplusplus
}
#endif

#endif
