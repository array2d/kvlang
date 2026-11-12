#ifndef RUNTIMEC_CONTRACT_ALLOCATOR_H
#define RUNTIMEC_CONTRACT_ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*runtimec_allocate_function_t)(void *context, size_t size,
                                              size_t alignment);
typedef void (*runtimec_deallocate_function_t)(void *context,
                                               void *allocation,
                                               size_t size,
                                               size_t alignment);

typedef struct runtimec_allocator {
    void *context;
    runtimec_allocate_function_t allocate;
    runtimec_deallocate_function_t deallocate;
} runtimec_allocator_t;

void runtimec_allocator_reset(runtimec_allocator_t *allocator);

bool runtimec_allocator_is_valid(const runtimec_allocator_t *allocator);

void *runtimec_allocator_allocate(const runtimec_allocator_t *allocator,
                                  size_t size, size_t alignment);

runtimec_status_t runtimec_allocator_deallocate(
    const runtimec_allocator_t *allocator, void *allocation,
    size_t size, size_t alignment);

#ifdef __cplusplus
}
#endif

#endif
