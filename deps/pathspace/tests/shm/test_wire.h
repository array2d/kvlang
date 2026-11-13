
#ifndef PATHSPACE_TEST_WIRE_H
#define PATHSPACE_TEST_WIRE_H

#include <pathspace/pathspace_xvalue.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PATHSPACE_TEST_ATTR_TAG_LANGTYPE 1u

static inline int pathspace_test_wire_build(const uint8_t *body, uint64_t body_len,
                                          int prefix, const char *langtype,
                                          uint8_t **out, uint64_t *out_len) {
    if (!out || !out_len)
        return -1;
    *out = NULL;
    *out_len = 0;
    if (body_len && !body && prefix == 0)
        return -1;
    const uint64_t langtype_len = langtype ? (uint64_t)strlen(langtype) : 0;
    const uint64_t record_len = langtype_len + (prefix ? 1u : 0u);
    const uint64_t attrs_len = record_len ? 3u + record_len : 0u;
    if (attrs_len > UINT16_MAX)
        return -1;
    const uint64_t total = PATHSPACE_XVALUE_HEAD_LEN + body_len + attrs_len;
    uint8_t *wire = (uint8_t *)calloc(1, (size_t)total);
    if (!wire)
        return -1;
    pathspaceXvalueHead head = {.padding = {0u, 0u, 0u, 0u, 0u, 0u}};
    pathspaceXvalueHeadSetLen(&head, body_len);
    pathspaceXvalueHeadSetAttrsLen(&head, (uint16_t)attrs_len);
    memcpy(wire, &head, sizeof head);
    if (body && body_len)
        memcpy(wire + PATHSPACE_XVALUE_HEAD_LEN, body, (size_t)body_len);
    if (record_len) {
        uint8_t *record = wire + PATHSPACE_XVALUE_HEAD_LEN + body_len;
        record[0] = PATHSPACE_TEST_ATTR_TAG_LANGTYPE;
        record[1] = (uint8_t)record_len;
        record[2] = (uint8_t)(record_len >> 8);
        uint64_t at = 3;
        if (prefix)
            record[at++] = (uint8_t)prefix;
        if (langtype_len)
            memcpy(record + at, langtype, (size_t)langtype_len);
    }
    *out = wire;
    *out_len = total;
    return 0;
}

static inline int pathspace_test_wire_scalar(const uint8_t *raw, uint32_t raw_len,
                                           const char *langtype, uint8_t **out,
                                           uint64_t *out_len) {
    if (raw_len && !raw) {
        if (out)
            *out = NULL;
        return -1;
    }
    return pathspace_test_wire_build(raw, raw_len, 0, langtype, out, out_len);
}

static inline int pathspace_test_wire_slack(const uint8_t *data, uint64_t data_len,
                                          const char *langtype, uint8_t **out,
                                          uint64_t *out_len) {
    if (data_len && !data) {
        if (out)
            *out = NULL;
        return -1;
    }
    return pathspace_test_wire_build(data, data_len, 0, langtype, out, out_len);
}

static inline int pathspace_test_wire_ptr(const uint8_t *path, uint32_t path_len,
                                        const char *langtype, uint8_t **out,
                                        uint64_t *out_len) {
    if (path_len && !path) {
        if (out)
            *out = NULL;
        return -1;
    }
    if (langtype && (langtype[0] == '*' || langtype[0] == '@')) {
        if (out)
            *out = NULL;
        return -1;
    }
    return pathspace_test_wire_build(path, path_len, '*', langtype, out, out_len);
}

static inline int pathspace_test_wire_ext(const uint8_t *locator,
                                        uint32_t locator_len,
                                        const char *langtype, uint8_t **out,
                                        uint64_t *out_len) {
    if (!locator_len || !locator) {
        if (out)
            *out = NULL;
        return -1;
    }
    if (langtype && (langtype[0] == '*' || langtype[0] == '@')) {
        if (out)
            *out = NULL;
        return -1;
    }
    return pathspace_test_wire_build(locator, locator_len, '@', langtype, out,
                                   out_len);
}

#endif
