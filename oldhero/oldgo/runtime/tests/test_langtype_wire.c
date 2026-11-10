/* Unit tests for wire trailing-attr `langtype` parsing and `None`/scalar predicates:
 * bounded attr parsing on truncated/corrupt wires, non-None empty-body typed markers,
 * and rejection of empty-body fixed-width scalars in `kvlangXvalueScalar`. */
#include "runtime_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

static bool lt_is(const kvlangXvalue_t *v, const char *want) {
    return v->langtype_len == strlen(want) &&
           memcmp(v->langtype, want, v->langtype_len) == 0 &&
           v->langtype[v->langtype_len] == 0;
}

int main(void) {
    uint8_t *w = NULL;
    uint64_t wl = 0;
    kvlangXvalue_t v;

    /* Scalar wire populates cached `langtype`; any truncation yields empty `langtype`. */
    uint8_t raw[8] = {42, 0, 0, 0, 0, 0, 0, 0};
    CHECK(kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, raw, 8, "int64",
                                     &w, &wl) == 0);
    kvlangXvalueSetWire(&v, w, (uint32_t)wl);
    CHECK(lt_is(&v, "int64"));
    CHECK(!kvlangXvalueNone(&v));
    CHECK(kvlangXvalueScalar(&v).kind_id == KVLANG_LT_INT64);
    for (uint32_t cut = 0; cut < (uint32_t)wl; cut++) {
        /* Exact `cut`-byte allocation so ASan catches any out-of-bounds read. */
        uint8_t *t = malloc(cut ? cut : 1);
        memcpy(t, w, cut);
        kvlangXvalueSetWire(&v, t, cut);
        CHECK(v.langtype_len == 0);
        free(t);
    }
    /* Out-of-bounds `attr_len` or record length yields empty `langtype` safely. */
    uint8_t *bad = malloc(wl);
    memcpy(bad, w, wl);
    bad[KVSPACE_XVALUE_HEAD_LEN + 8] = 0xff;
    bad[KVSPACE_XVALUE_HEAD_LEN + 9] = 0x7f;
    kvlangXvalueSetWire(&v, bad, (uint32_t)wl);
    CHECK(v.langtype_len == 0);
    memcpy(bad, w, wl);
    bad[KVSPACE_XVALUE_HEAD_LEN + 8 + 3] = 0xff; /* low byte of tag=1 record len */
    kvlangXvalueSetWire(&v, bad, (uint32_t)wl);
    CHECK(v.langtype_len == 0);
    /* Claimed `body_len` exceeding wire length yields empty `langtype`. */
    memcpy(bad, w, wl);
    bad[0] = 0xf0;
    kvlangXvalueSetWire(&v, bad, (uint32_t)wl);
    CHECK(v.langtype_len == 0);
    free(bad);
    free(w);

    /* `None`: empty body and empty attr. */
    CHECK(kvspaceXvalueHeadNewNone(&w, &wl) == 0);
    kvlangXvalueSetWire(&v, w, (uint32_t)wl);
    CHECK(v.langtype_len == 0);
    CHECK(kvlangXvalueNone(&v));
    CHECK(kvlangXvalueScalar(&v).kind_id == KVLANG_LT_NONE);
    free(w);

    /* Empty string `""`: empty body + `langtype` is not `None`. */
    CHECK(kvspaceXvalueHeadNewSlack((const uint8_t *)"", 0, 0, "char/utf32",
                                    &w, &wl) == 0);
    kvlangXvalueSetWire(&v, w, (uint32_t)wl);
    CHECK(lt_is(&v, "char/utf32"));
    CHECK(!kvlangXvalueNone(&v));
    CHECK(strcmp(kvlangXvalueKind(&v), "char/utf32") == 0);
    free(w);

    /* `def struct` / map marker: empty body + `langtype` is not `None`. */
    CHECK(kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, NULL, 0,
                                     "def struct", &w, &wl) == 0);
    kvlangXvalueSetWire(&v, w, (uint32_t)wl);
    CHECK(lt_is(&v, "def struct"));
    CHECK(!kvlangXvalueNone(&v));
    free(w);

    /* Empty-body `int64` is not a valid scalar (must not read 8B past body). */
    CHECK(kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, NULL, 0, "int64",
                                     &w, &wl) == 0);
    kvlangXvalueSetWire(&v, w, (uint32_t)wl);
    CHECK(!kvlangXvalueNone(&v));
    kvlangScalar_t s = kvlangXvalueScalar(&v);
    CHECK(s.kind_id != KVLANG_LT_INT64 && s.bytes == NULL);
    free(w);

    /* Stack-built scalar wire (`kvlangScalarToWire`) carries trailing attr too. */
    uint8_t buf[64];
    uint32_t bl = kvlangScalarToWire("float64", raw, 8, buf, sizeof buf);
    kvlangXvalueSetWire(&v, buf, bl);
    CHECK(lt_is(&v, "float64"));

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("langtype_wire: ok\n");
    return 0;
}
