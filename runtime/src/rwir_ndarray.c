#include "rwir_internal.h"

/* ndarray 形状算子只认带形状段的 langtype。map 容器（langtype=`{keylt}·{valt}`）无形状段——
 * 成员数在 memindex（`p·`）而非容器值，用 ndarray·numel 取是范畴错误，直接报错逼用 kvspace·listlen。 */
static int ndarray_shape_guard(kvlangFrame_t *f, const char *op,
                               const kvspaceHead_t *h) {
    kvlangLangtype kx;
    kvlangLangtypeParse(h->langtype, &kx);
    if (kx.ndim > 0)
        return 0;
    if (kvlangKindIsMap(kx.kind))
        return kvlangBuiltinSetErr(f,
                                   "TypeError: %s: container %s has no shape; "
                                   "use kvspace·listlen for member count",
                                   op, kx.kind);
    return 0;
}

int kvlangBuiltinNdarrayNumel(kvlangFrame_t *f) {
    kvspaceHead_t h;
    int64_t n_el = 0;
    if (xv_head1(f, &h) == 0) {
        int g = ndarray_shape_guard(f, "ndarray·numel", &h);
        if (g)
            return g;
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, &kx);
        n_el = kx.array_len;
    }
    kvlangXvalue_t r;
    kvlangXvalueNewInt64(&r, n_el);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}

int kvlangBuiltinNdarrayDim(kvlangFrame_t *f) {
    kvspaceHead_t h;
    int64_t ndim = 0;
    if (xv_head1(f, &h) == 0) {
        int g = ndarray_shape_guard(f, "ndarray·dim", &h);
        if (g)
            return g;
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, &kx);
        ndim = kx.ndim;
    }
    kvlangXvalue_t r;
    kvlangXvalueNewInt64(&r, ndim);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}

int kvlangBuiltinNdarrayShape(kvlangFrame_t *f) {
    int32_t dims[8];
    int32_t ndim = 0;
    kvspaceHead_t h;
    if (xv_head1(f, &h) == 0) {
        int g = ndarray_shape_guard(f, "ndarray·shape", &h);
        if (g)
            return g;
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, &kx);
        ndim = kx.ndim;
        for (int i = 0; i < ndim && i < 8; i++)
            dims[i] = kx.dims[i];
    }
    uint8_t raw[64];
    uint32_t raw_len = 0;
    for (int i = 0; i < ndim; i++) {
        int64_t d = dims[i];
        for (int j = 0; j < 8; j++)
            raw[i * 8 + j] = (d >> (j * 8)) & 0xFF;
        raw_len += 8;
    }
    int32_t sd[1] = {ndim};
    kvlangXvalue_t r;
    kvlangXvalueNewTlvDims(&r, KVSPACE_KIND_INT64, raw, raw_len, sd, 1);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}
