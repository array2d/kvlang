
#ifndef KVSPACE_SHM_H
#define KVSPACE_SHM_H

#include <kvspace/kvspace.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct kvspace kvspace_t;

#define KVSPACE_FORMAT_VERSION 8

kvspace_t *kvspaceShmOpen(const char *path, size_t data_size);
void kvspaceShmClose(kvspace_t *kv);

uint8_t *kvspaceShmGet(kvspace_t *kv, const char *key, int resolve,
                       int32_t *out_len);
int kvspaceShmSetPart(kvspace_t *kv, const char *key, uint32_t offset,
                      const uint8_t *buf, uint32_t buf_len);

int kvspaceShmSet(kvspace_t *kv, const char *key, const uint8_t *val,
                  int32_t val_len);
int kvspaceShmSetValue(kvspace_t *kv, const char *key, const uint8_t *val,
                       int32_t val_len);

int kvspaceShmListLen(kvspace_t *kv, const char *prefix, bool expand_ext,
                      int resolve, int32_t *out_count);

#define KVSPACE_ANCHOR_ROOT UINT64_C(0)

uint8_t *kvspaceShmStorageDataBase(kvspace_t *kv);
int kvspaceShmStorageResolve(kvspace_t *kv, const char *key,
                             uint64_t *out_offset);

typedef struct kvspaceAnchor {
    int64_t key_offset; 
    int32_t block_id;   
} kvspaceAnchor_t;

int kvspaceShmStorageResolveFrom(kvspace_t *kv, const char *key,
                                 kvspaceAnchor_t anchor, uint64_t *out_offset);

int kvspaceShmStorageRealloc(kvspace_t *kv, const char *key,
                             uint64_t body_len, uint64_t attr_len,
                             uint64_t *out_offset);
int kvspaceShmStorageFree(kvspace_t *kv, const char *key);
int kvspaceShmStorageMakeindex(kvspace_t *kv, const char *key);
int kvspaceShmStorageListlen(kvspace_t *kv, const char *key, int64_t *out_count,
                             uint32_t *out_max_name_length);
int kvspaceShmStorageList(kvspace_t *kv, const char *key, char *out_names,
                          uint32_t out_capacity, uint32_t *out_length,
                          uint32_t *out_count, uint64_t *out_generation);

int kvspaceShmList(kvspace_t *kv, const char *prefix, bool expand_ext,
                   int resolve, char ***out_names, int32_t *out_count);

int kvspaceShmDel(kvspace_t *kv, const char *key);

int kvspaceShmDeltree(kvspace_t *kv, const char *prefix);
int kvspaceShmCp(kvspace_t *kv, const char *src,
                 const char *dst); 
int kvspaceShmCptree(kvspace_t *kv, const char *src,
                     const char *dst); 
int kvspaceShmCplist(kvspace_t *kv, const char *src,
                     const char *dst); 

#ifdef P2_TEST_HOOKS

int64_t kvspaceShmArtAlignCheck(kvspace_t *kv);
void kvspaceShmTestSetFormatVersion(kvspace_t *kv, uint32_t v);

void kvspaceShmTestSetGarbageThresholdMegabytes(uint32_t megabytes);
void kvspaceShmTestSetBarrierMilliseconds(int milliseconds);
void kvspaceShmTestSetHoldDrainMilliseconds(uint32_t milliseconds);
void kvspaceShmTestSetHoldReserveMilliseconds(uint32_t milliseconds);
void kvspaceShmTestSetFailAlloc(bool enabled);
void kvspaceShmTestSetFailArtInsert(bool enabled);

int kvspaceShmTestAnchorOf(kvspace_t *kv, const char *parent_key,
                           int64_t *out_anchor);
#endif

#endif
