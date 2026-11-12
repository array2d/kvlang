
#include <kvlang/xvalue.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int finish(uint8_t *value, uint64_t len, uint8_t **out, uint32_t *out_len) {
    if (!value || len > UINT32_MAX) {
        free(value);
        return 1;
    }
    *out = value;
    *out_len = (uint32_t)len;
    return 0;
}

int kvlangXvalueNewPtr(const char *target_kindexpr, const char *target,
                       uint8_t **out, uint32_t *out_len) {
    if (!target_kindexpr || !target || !out || !out_len)
        return 1;
    
    if (strcmp(target_kindexpr, "any") == 0)
        return 1;
    
    if (*target == '\0')
        return 1;
    
    uint8_t *value = NULL;
    uint64_t total = 0;
    if (kvlangXvalueHeadNewPtr((const uint8_t *)target,
                               (uint32_t)strlen(target), target_kindexpr,
                               &value, &total) != 0)
        return 1;
    return finish(value, total, out, out_len);
}

int kvlangXvalueNewChar(const uint8_t *bytes, uint32_t len, const char *langtype,
                        uint8_t **out, uint32_t *out_len) {
    if (!out || !out_len || (len && !bytes))
        return 1;
    uint8_t *value = NULL;
    uint64_t total = 0;
    if (kvlangXvalueHeadNewSlack(bytes, len, langtype, &value, &total) != 0)
        return 1;
    return finish(value, total, out, out_len);
}

int kvlangXvalueNewBool(uint8_t v, const char *langtype,
                        uint8_t **out, uint32_t *out_len) {
    if (!out || !out_len)
        return 1;
    uint8_t b = v ? 1 : 0;
    uint8_t *value = NULL;
    uint64_t total = 0;
    if (kvlangXvalueHeadNewScalar(&b, 1, langtype, &value, &total) != 0)
        return 1;
    return finish(value, total, out, out_len);
}

int kvlangXvalueNewInt64(int64_t v, const char *langtype,
                         uint8_t **out, uint32_t *out_len) {
    if (!out || !out_len)
        return 1;
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)((uint64_t)v >> (8 * i));
    uint8_t *value = NULL;
    uint64_t total = 0;
    if (kvlangXvalueHeadNewScalar(raw, 8, langtype, &value, &total) != 0)
        return 1;
    return finish(value, total, out, out_len);
}

int kvlangXvalueNewFloat64(double v, const char *langtype,
                           uint8_t **out, uint32_t *out_len) {
    if (!out || !out_len)
        return 1;
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)(bits >> (8 * i));
    uint8_t *value = NULL;
    uint64_t total = 0;
    if (kvlangXvalueHeadNewScalar(raw, 8, langtype, &value, &total) != 0)
        return 1;
    return finish(value, total, out, out_len);
}
