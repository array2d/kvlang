
#ifndef PATHLANG_XVALUE_H
#define PATHLANG_XVALUE_H

#include <pathspace/pathspace_xvalue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATHLANG_XVALUE_ATTR_TAG_LANGTYPE 1u

static inline uint16_t pathlangXvalueHeadLangtype(const uint8_t *wire,
                                                const uint8_t **out) {
    if (out)
        *out = NULL;
    const pathspaceXvalueHead *h = (const pathspaceXvalueHead *)(const void *)wire;
    const uint64_t total = pathspaceXvalueHeadTotalLen(h);
    if (total == 0)
        return 0;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (pathspaceXvalueGetAttr(wire, total, PATHLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

static inline uint16_t pathlangXvalueHeadLangtypeBounded(const uint8_t *wire,
                                                       uint64_t wire_len,
                                                       const uint8_t **out) {
    if (out)
        *out = NULL;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (pathspaceXvalueGetAttr(wire, wire_len, PATHLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

int pathlangXvalueHeadNewNone(uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewScalar(const uint8_t *raw, uint32_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_len,
                             const char *langtype,
                             uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewTensor(const uint64_t *dims, uint32_t ndim,
                              const uint8_t *raw, uint64_t raw_len,
                              const char *langtype,
                              uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewPtr(const uint8_t *path, uint32_t path_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewExt(const uint8_t *locator, uint32_t locator_len,
                           const char *langtype,
                           uint8_t **out, uint64_t *out_len);
int pathlangXvalueHeadNewCode(const uint8_t *body, uint64_t body_len,
                            const char *langtype,
                            uint8_t **out, uint64_t *out_len);

int pathlangXvalueHeadReserve(uint64_t body_len, const char *langtype,
                            uint8_t **out, uint64_t *out_len);

int pathlangXvalueNewPtr(const char *target_kindexpr, const char *target,
                       uint8_t **out, uint32_t *out_len);
int pathlangXvalueNewChar(const uint8_t *bytes, uint32_t len, const char *langtype,
                        uint8_t **out, uint32_t *out_len);
int pathlangXvalueNewBool(uint8_t v, const char *langtype, uint8_t **out,
                        uint32_t *out_len);
int pathlangXvalueNewInt64(int64_t v, const char *langtype, uint8_t **out,
                         uint32_t *out_len);
int pathlangXvalueNewFloat64(double v, const char *langtype, uint8_t **out,
                           uint32_t *out_len);

int pathlangCoordIsCoord(const char *name);
int pathlangParseCoord(const char *name, int64_t *coords, int maxn);
int pathlangCoordCmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
