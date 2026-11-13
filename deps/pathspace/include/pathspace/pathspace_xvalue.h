
#ifndef PATHSPACE_XVALUE_H
#define PATHSPACE_XVALUE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define PATHSPACE_PUBLIC __declspec(dllexport)
#elif defined(__GNUC__)
#define PATHSPACE_PUBLIC __attribute__((visibility("default")))
#else
#define PATHSPACE_PUBLIC
#endif

#if defined(__cplusplus)
#define PATHSPACE_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define PATHSPACE_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PATHSPACE_XVALUE_HEAD_LEN 16u

typedef struct pathspaceXvalueHead {
    uint64_t bodylen;      
    uint16_t attrslen;     
    uint8_t  padding[6];   
} pathspaceXvalueHead;

PATHSPACE_STATIC_ASSERT(sizeof(pathspaceXvalueHead) == PATHSPACE_XVALUE_HEAD_LEN,
               "XValue head must be exactly 16 bytes");
PATHSPACE_STATIC_ASSERT(offsetof(pathspaceXvalueHead, bodylen) == 0U, "bodylen@0");
PATHSPACE_STATIC_ASSERT(offsetof(pathspaceXvalueHead, attrslen) == 8U, "attrslen@8");
PATHSPACE_STATIC_ASSERT(offsetof(pathspaceXvalueHead, padding) == 10U, "padding@10");

static inline uint64_t pathspaceXvalueHeadLen(const pathspaceXvalueHead *h) {
    return h->bodylen;
}
static inline void pathspaceXvalueHeadSetLen(pathspaceXvalueHead *h, uint64_t v) {
    h->bodylen = v;
}
static inline uint16_t pathspaceXvalueHeadAttrsLen(const pathspaceXvalueHead *h) {
    return h->attrslen;
}
static inline void pathspaceXvalueHeadSetAttrsLen(pathspaceXvalueHead *h, uint16_t v) {
    h->attrslen = v;
}

static inline int pathspaceXvalueHeadPaddingIsZero(const pathspaceXvalueHead *h) {
    return h->padding[0] == 0u && h->padding[1] == 0u &&
           h->padding[2] == 0u && h->padding[3] == 0u &&
           h->padding[4] == 0u && h->padding[5] == 0u;
}

static inline uint64_t pathspaceXvalueHeadTotalLen(const pathspaceXvalueHead *h) {
    if (!pathspaceXvalueHeadPaddingIsZero(h))
        return 0;
    const uint64_t body = pathspaceXvalueHeadLen(h);
    const uint64_t attrs = pathspaceXvalueHeadAttrsLen(h);
    if (body > UINT64_MAX - PATHSPACE_XVALUE_HEAD_LEN ||
        attrs > UINT64_MAX - PATHSPACE_XVALUE_HEAD_LEN - body)
        return 0;
    return (uint64_t)PATHSPACE_XVALUE_HEAD_LEN + body + attrs;
}

static inline const uint8_t *pathspaceXvalueHeadAttrs(const uint8_t *wire) {
    const pathspaceXvalueHead *h = (const pathspaceXvalueHead *)(const void *)wire;
    return wire + PATHSPACE_XVALUE_HEAD_LEN + (uint64_t)h->bodylen;
}

#define PATHSPACE_XVALUE_ATTR_TAG_RETIRED_REALLEN 2u

int pathspaceXvalueGetAttr(const uint8_t *wire, uint64_t wire_len, uint8_t tag,
                          const uint8_t **out_data, uint16_t *out_len);
int pathspaceXvalueSetAttr(uint8_t *wire, uint64_t wire_capacity, uint8_t tag,
                          const uint8_t *data, uint16_t len,
                          uint64_t *out_new_length);

int pathspaceXvalueHeadEffectiveLen(const uint8_t *wire, uint64_t wire_len,
                                    uint64_t *out);

#ifdef __cplusplus
}
#endif

#endif
