
#ifndef PATHSPACE_C_XVALUE_INTERNAL_H
#define PATHSPACE_C_XVALUE_INTERNAL_H

#include <pathspace/pathspace_xvalue.h>

#define PATHSPACE_XVALUE_REFERENCE_INVALID 0
#define PATHSPACE_XVALUE_REFERENCE_INLINE 1
#define PATHSPACE_XVALUE_REFERENCE_PTR 2
#define PATHSPACE_XVALUE_REFERENCE_EXT 3

#define PATHSPACE_C_ATTR_TAG_RETIRED_REALLEN 2u

#define PATHSPACE_C_ATTR_TAG_LANGTYPE 1u

static inline int pathspace_c_scan_attrs(const uint8_t *wire, uint64_t wire_len,
                                       const uint8_t **out_langtype,
                                       uint16_t *out_langtype_len) {
    if (out_langtype)
        *out_langtype = NULL;
    if (out_langtype_len)
        *out_langtype_len = 0;
    if (!wire || wire_len < PATHSPACE_XVALUE_HEAD_LEN)
        return -1;
    const pathspaceXvalueHead *h = (const pathspaceXvalueHead *)(const void *)wire;
    const uint64_t total = pathspaceXvalueHeadTotalLen(h);
    if (total == 0 || total > wire_len)
        return -1;
    const uint8_t *p = wire + PATHSPACE_XVALUE_HEAD_LEN + pathspaceXvalueHeadLen(h);
    const uint8_t *end = p + pathspaceXvalueHeadAttrsLen(h);
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    while (p + 3 <= end) {
        const uint8_t tag = p[0];
        const uint16_t rlen = (uint16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
        if (p + 3 + rlen > end)
            return -1;
        if (tag == PATHSPACE_C_ATTR_TAG_RETIRED_REALLEN)
            return -1; 
        if (tag == PATHSPACE_C_ATTR_TAG_LANGTYPE && !langtype) {
            langtype = p + 3;
            langtype_len = rlen;
        }
        p += 3 + rlen;
    }
    if (out_langtype)
        *out_langtype = langtype;
    if (out_langtype_len)
        *out_langtype_len = langtype_len;
    return 0;
}

static inline int pathspace_c_reference_of(const uint8_t *wire, uint64_t wire_len) {
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;

    if (pathspace_c_scan_attrs(wire, wire_len, &langtype, &langtype_len) != 0)
        return PATHSPACE_XVALUE_REFERENCE_INVALID;
    if (langtype == NULL || langtype_len == 0)
        return PATHSPACE_XVALUE_REFERENCE_INLINE; 
    if (langtype[0] == '*')
        return PATHSPACE_XVALUE_REFERENCE_PTR;
    if (langtype[0] == '@')
        return PATHSPACE_XVALUE_REFERENCE_EXT;
    return PATHSPACE_XVALUE_REFERENCE_INLINE;
}

static inline int pathspace_c_effective_len(const uint8_t *wire, uint64_t total,
                                          uint64_t *out) {
    return pathspaceXvalueHeadEffectiveLen(wire, total, out);
}

static inline int pathspace_c_same_shape(const uint8_t *left, uint64_t left_len,
                                       const uint8_t *right, uint64_t right_len) {
    const pathspaceXvalueHead *left_head;
    const pathspaceXvalueHead *right_head;
    uint64_t left_total;
    uint64_t right_total;

    if (!left || !right || left_len < PATHSPACE_XVALUE_HEAD_LEN ||
        right_len < PATHSPACE_XVALUE_HEAD_LEN)
        return 0;
    left_head = (const pathspaceXvalueHead *)(const void *)left;
    right_head = (const pathspaceXvalueHead *)(const void *)right;
    left_total = pathspaceXvalueHeadTotalLen(left_head);
    right_total = pathspaceXvalueHeadTotalLen(right_head);
    if (left_total == 0 || right_total == 0 || left_total > left_len ||
        right_total > right_len)
        return 0;
    if (pathspaceXvalueHeadLen(left_head) != pathspaceXvalueHeadLen(right_head))
        return 0;
    if (pathspaceXvalueHeadAttrsLen(left_head) != pathspaceXvalueHeadAttrsLen(right_head))
        return 0;
    return memcmp(left, right, PATHSPACE_XVALUE_HEAD_LEN) == 0 &&
           memcmp(left + PATHSPACE_XVALUE_HEAD_LEN + pathspaceXvalueHeadLen(left_head),
                  right + PATHSPACE_XVALUE_HEAD_LEN + pathspaceXvalueHeadLen(right_head),
                  pathspaceXvalueHeadAttrsLen(left_head)) == 0;
}

#endif
