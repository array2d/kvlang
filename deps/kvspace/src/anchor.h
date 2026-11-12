#ifndef KVSPACE_ANCHOR_H
#define KVSPACE_ANCHOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVSPACE_ANCHOR_ROOT UINT64_C(0)

static inline uint64_t kvspaceAnchorMake(uint32_t parent_id, uint32_t depth) {
    return ((uint64_t)parent_id << 32) | (uint64_t)depth;
}

static inline uint32_t kvspaceAnchorParentId(uint64_t anchor) {
    return (uint32_t)(anchor >> 32);
}

static inline uint32_t kvspaceAnchorDepth(uint64_t anchor) {
    return (uint32_t)(anchor & UINT32_MAX);
}

#ifdef __cplusplus
}
#endif

#endif
