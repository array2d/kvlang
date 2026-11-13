
#ifndef PATHSPACE_SHM_H
#define PATHSPACE_SHM_H

#include <pathspace/pathspace.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pathspace pathspace_t;

#define PATHSPACE_FORMAT_VERSION 8

pathspace_t *pathspaceShmOpen(const char *path, size_t data_size);
void pathspaceShmClose(pathspace_t *kv);

uint8_t *pathspaceShmGet(pathspace_t *kv, const char *key, int resolve,
                       int32_t *out_len);
int pathspaceShmSetPart(pathspace_t *kv, const char *key, uint32_t offset,
                      const uint8_t *buf, uint32_t buf_len);

int pathspaceShmSet(pathspace_t *kv, const char *key, const uint8_t *val,
                  int32_t val_len);
int pathspaceShmSetValue(pathspace_t *kv, const char *key, const uint8_t *val,
                       int32_t val_len);

int pathspaceShmListLen(pathspace_t *kv, const char *prefix, bool expand_ext,
                      int resolve, int32_t *out_count);

#define PATHSPACE_ANCHOR_ROOT UINT64_C(0)

uint8_t *pathspaceShmStorageDataBase(pathspace_t *kv);
int pathspaceShmStorageResolve(pathspace_t *kv, const char *key,
                             uint64_t *out_offset);

typedef struct pathspaceAnchor {
    int64_t key_offset; 
    int32_t block_id;   
} pathspaceAnchor_t;

int pathspaceShmStorageResolveFrom(pathspace_t *kv, const char *key,
                                 pathspaceAnchor_t anchor, uint64_t *out_offset);

int pathspaceShmStorageRealloc(pathspace_t *kv, const char *key,
                             uint64_t body_len, uint64_t attr_len,
                             uint64_t *out_offset);
int pathspaceShmStorageFree(pathspace_t *kv, const char *key);
int pathspaceShmStorageMakeindex(pathspace_t *kv, const char *key);
int pathspaceShmStorageListlen(pathspace_t *kv, const char *key, int64_t *out_count,
                             uint32_t *out_max_name_length);
int pathspaceShmStorageList(pathspace_t *kv, const char *key, char *out_names,
                          uint32_t out_capacity, uint32_t *out_length,
                          uint32_t *out_count, uint64_t *out_generation);

int pathspaceShmList(pathspace_t *kv, const char *prefix, bool expand_ext,
                   int resolve, char ***out_names, int32_t *out_count);

int pathspaceShmDel(pathspace_t *kv, const char *key);

int pathspaceShmDeltree(pathspace_t *kv, const char *prefix);
int pathspaceShmCp(pathspace_t *kv, const char *src,
                 const char *dst); 
int pathspaceShmCptree(pathspace_t *kv, const char *src,
                     const char *dst); 
int pathspaceShmCplist(pathspace_t *kv, const char *src,
                     const char *dst); 

#ifdef P2_TEST_HOOKS

int64_t pathspaceShmArtAlignCheck(pathspace_t *kv);
void pathspaceShmTestSetFormatVersion(pathspace_t *kv, uint32_t v);

void pathspaceShmTestSetGarbageThresholdMegabytes(uint32_t megabytes);
void pathspaceShmTestSetBarrierMilliseconds(int milliseconds);
void pathspaceShmTestSetHoldDrainMilliseconds(uint32_t milliseconds);
void pathspaceShmTestSetHoldReserveMilliseconds(uint32_t milliseconds);
void pathspaceShmTestSetFailAlloc(bool enabled);
void pathspaceShmTestSetFailArtInsert(bool enabled);

int pathspaceShmTestAnchorOf(pathspace_t *kv, const char *parent_key,
                           int64_t *out_anchor);
#endif

#endif
