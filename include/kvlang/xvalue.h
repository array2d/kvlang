
#ifndef KVLANG_XVALUE_H
#define KVLANG_XVALUE_H

#include <kvspace/kvspace_xvalue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVLANG_XVALUE_ATTR_TAG_LANGTYPE 1u

static inline uint16_t kvlangXvalueHeadLangtype(const uint8_t *wire,
                                                const uint8_t **out) {
    if (out)
        *out = NULL;
    const kvspaceXvalueHead *h = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(h);
    if (total == 0)
        return 0;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, total, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

static inline uint16_t kvlangXvalueHeadLangtypeBounded(const uint8_t *wire,
                                                       uint64_t wire_len,
                                                       const uint8_t **out) {
    if (out)
        *out = NULL;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, wire_len, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

int kvlangXvalueHeadNewNone(uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewScalar(const uint8_t *raw, uint32_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_len,
                             const char *langtype,
                             uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewTensor(const uint64_t *dims, uint32_t ndim,
                              const uint8_t *raw, uint64_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewPtr(const uint8_t *path, uint32_t path_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewExt(const uint8_t *locator, uint32_t locator_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len);
int kvlangXvalueHeadNewCode(const uint8_t *body, uint64_t body_len,
                            const char *langtype,
                            uint8_t **out, uint64_t *out_len);

int kvlangXvalueHeadReserve(uint64_t body_len, const char *langtype,
                            uint8_t **out, uint64_t *out_len);

int kvlangXvalueNewPtr(const char *target_kindexpr, const char *target,
                       uint8_t **out, uint32_t *out_len);
int kvlangXvalueNewChar(const uint8_t *bytes, uint32_t len, const char *langtype,
                        uint8_t **out, uint32_t *out_len);
int kvlangXvalueNewBool(uint8_t v, const char *langtype, uint8_t **out,
                        uint32_t *out_len);
int kvlangXvalueNewInt64(int64_t v, const char *langtype, uint8_t **out,
                         uint32_t *out_len);
int kvlangXvalueNewFloat64(double v, const char *langtype, uint8_t **out,
                           uint32_t *out_len);

int kvlangCoordIsCoord(const char *name);
int kvlangParseCoord(const char *name, int64_t *coords, int maxn);
int kvlangCoordCmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
