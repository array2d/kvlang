#include <kvlang/xvalue.h>

#include <stdlib.h>
#include <string.h>

static int fail(uint8_t **out) {
    if (out)
        *out = NULL;
    return -1;
}

static int build(const uint8_t *body, uint64_t body_len, char langtype_prefix,
                 const char *langtype, uint8_t **out, uint64_t *out_len) {
    if (!out || !out_len)
        return fail(out);
    *out = NULL;
    const uint64_t ltl = langtype ? (uint64_t)strlen(langtype) : 0;
    const uint64_t record_len = ltl + (langtype_prefix ? 1u : 0u);
    const uint64_t alen = record_len ? 3 + record_len : 0;
    if (alen > UINT16_MAX)
        return -1;
    if (body_len > UINT64_MAX - KVSPACE_XVALUE_HEAD_LEN ||
        alen > UINT64_MAX - KVSPACE_XVALUE_HEAD_LEN - body_len)
        return -1;
    const uint64_t n = (uint64_t)KVSPACE_XVALUE_HEAD_LEN + body_len + alen;
    if (n > SIZE_MAX)
        return -1;
    uint8_t *p = (uint8_t *)calloc(1, (size_t)n);
    if (!p)
        return -1;
    kvspaceXvalueHead h = {.padding = {0u, 0u, 0u, 0u, 0u, 0u}};
    kvspaceXvalueHeadSetLen(&h, body_len);
    kvspaceXvalueHeadSetAttrsLen(&h, (uint16_t)alen);
    memcpy(p, &h, sizeof h);
    if (body && body_len)
        memcpy(p + KVSPACE_XVALUE_HEAD_LEN, body, body_len);
    uint8_t *a = p + KVSPACE_XVALUE_HEAD_LEN + body_len;
    if (record_len) {
        a[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
        a[1] = (uint8_t)record_len;
        a[2] = (uint8_t)(record_len >> 8);
        uint64_t at = 3;
        if (langtype_prefix)
            a[at++] = (uint8_t)langtype_prefix;
        if (ltl)
            memcpy(a + at, langtype, ltl);
    }
    *out = p;
    *out_len = n;
    return 0;
}

int kvlangXvalueHeadNewNone(uint8_t **out, uint64_t *out_len) {
    return build(NULL, 0, 0, NULL, out, out_len);
}

int kvlangXvalueHeadNewScalar(const uint8_t *raw, uint32_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len) {
    if (raw_len && !raw)
        return fail(out);
    return build(raw, raw_len, 0, langtype, out, out_len);
}

int kvlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_len,
                             const char *langtype,
                             uint8_t **out, uint64_t *out_len) {
    if (data_len && !data)
        return fail(out);
    return build(data, data_len, 0, langtype, out, out_len);
}

int kvlangXvalueHeadNewTensor(const uint64_t *dims, uint32_t ndim,
                              const uint8_t *raw, uint64_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len) {
    
    if (!dims || ndim == 0 || ndim > 64 || (raw_len && !raw))
        return fail(out);
    return build(raw, raw_len, 0, langtype, out, out_len);
}

int kvlangXvalueHeadNewPtr(const uint8_t *path, uint32_t path_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len) {
    if (path_len && !path)
        return fail(out);
    if (langtype && (langtype[0] == '*' || langtype[0] == '@'))
        return fail(out);
    return build(path, path_len, '*', langtype, out, out_len);
}

int kvlangXvalueHeadNewExt(const uint8_t *locator, uint32_t locator_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len) {
    if (!locator_len || !locator)
        return fail(out);
    if (langtype && (langtype[0] == '*' || langtype[0] == '@'))
        return fail(out);
    return build(locator, locator_len, '@', langtype, out, out_len);
}

int kvlangXvalueHeadNewCode(const uint8_t *body, uint64_t body_len,
                            const char *langtype,
                            uint8_t **out, uint64_t *out_len) {
    return kvlangXvalueHeadNewSlack(body, body_len, langtype, out, out_len);
}

int kvlangXvalueHeadReserve(uint64_t body_len, const char *langtype,
                            uint8_t **out, uint64_t *out_len) {
    
    return build(NULL, body_len, 0, langtype, out, out_len);
}
