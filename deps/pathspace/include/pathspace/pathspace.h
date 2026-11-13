#ifndef PATHSPACE_PATHSPACE_H
#define PATHSPACE_PATHSPACE_H

#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHSPACE_ANCHOR_ROOT UINT64_C(0)

typedef enum pathspace_status {
    PATHSPACE_OK = 0,
    PATHSPACE_INVALID = -EINVAL,
    PATHSPACE_MISSING = -ENOENT,
    PATHSPACE_EXISTS = -EEXIST,
    PATHSPACE_ANCHOR_INVALID = -ESTALE,
    PATHSPACE_CAPACITY = -ENOSPC,
    PATHSPACE_NOT_INDEX = -ENOTDIR,
    PATHSPACE_HAS_CHILDREN = -ENOTEMPTY,
    PATHSPACE_UNSUPPORTED = -ENOTSUP,
    PATHSPACE_BACKEND = -EIO
} pathspace_status_t;

void *pathspaceConnect(const char *dsn);
void pathspaceClose(void *h);

uint8_t *pathspaceXvalueInProcessAddress(void *handle);

int pathspaceResolveOffset(void *handle, const char *key, int64_t parent_key_anchor,
                   uint64_t *out_value_offset, char *err, uint32_t err_cap);

int pathspaceRealloc(void *handle, const char *key, uint64_t body_length,
                   uint64_t attr_length, uint64_t *out_value_offset,
                   char *err, uint32_t err_cap);

int pathspaceFree(void *handle, const char *key, char *err, uint32_t err_cap);

int pathspaceCp(void *handle, const char *old_key, const char *new_key, char *err,
              uint32_t err_cap);

int pathspaceMkindex(void *handle, const char *key, char *err, uint32_t err_cap);

int pathspaceDeltree(void *handle, const char *key, char *err, uint32_t err_cap);

int pathspaceCpdir(void *handle, const char *source_key,
                 const char *destination_key, char *err, uint32_t err_cap);

int pathspaceListlen(void *handle, const char *key, int64_t *out_count,
                   uint32_t *out_max_name_length, char *err, uint32_t err_cap);

int pathspaceList(void *handle, const char *key, char *out_names,
                uint32_t out_capacity, uint32_t *out_length,
                uint32_t *out_count, uint64_t *out_generation, char *err,
                uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
