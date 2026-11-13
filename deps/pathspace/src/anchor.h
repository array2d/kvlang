#ifndef PATHSPACE_ANCHOR_H
#define PATHSPACE_ANCHOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHSPACE_ANCHOR_ROOT UINT64_C(0)

static inline uint64_t pathspaceAnchorMake(uint32_t parent_id, uint32_t depth) {
    return ((uint64_t)parent_id << 32) | (uint64_t)depth;
}

static inline uint32_t pathspaceAnchorParentId(uint64_t anchor) {
    return (uint32_t)(anchor >> 32);
}

static inline uint32_t pathspaceAnchorDepth(uint64_t anchor) {
    return (uint32_t)(anchor & UINT32_MAX);
}

#ifdef __cplusplus
}
#endif

#endif
