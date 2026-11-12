
#include "contract/allocator.h"

static bool runtimec_alignment_is_valid(size_t alignment) {
    return alignment != 0U && (alignment & (alignment - 1U)) == 0U;
}

void runtimec_allocator_reset(runtimec_allocator_t *allocator) {
    if (allocator != NULL) {
        allocator->context = NULL;
        allocator->allocate = NULL;
        allocator->deallocate = NULL;
    }
}

bool runtimec_allocator_is_valid(const runtimec_allocator_t *allocator) {
    return allocator != NULL && allocator->allocate != NULL &&
           allocator->deallocate != NULL;
}

void *runtimec_allocator_allocate(const runtimec_allocator_t *allocator,
                                  size_t size, size_t alignment) {
    if (!runtimec_allocator_is_valid(allocator) || size == 0U ||
        !runtimec_alignment_is_valid(alignment)) {
        return NULL;
    }
    return allocator->allocate(allocator->context, size, alignment);
}

runtimec_status_t runtimec_allocator_deallocate(
    const runtimec_allocator_t *allocator, void *allocation,
    size_t size, size_t alignment) {
    if (allocation == NULL || !runtimec_allocator_is_valid(allocator) ||
        size == 0U || !runtimec_alignment_is_valid(alignment)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator->deallocate(allocator->context, allocation, size, alignment);
    return RUNTIMEC_STATUS_SUCCESS;
}
