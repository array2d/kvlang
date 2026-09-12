#include "rwir_internal.h"

/* ── 数组打包 / 散 key 辅助 ──────────────────────────────────────── */

static void pack_typed_array(const char *kind, const kvlangXvalue_t *elems,
                             int n, kvlangXvalue_t *out) {
    int sz = kvlangXvalueElemSize(kind);
    uint8_t *raw = malloc((size_t)sz * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        const uint8_t *b;
        int32_t blen;
        kvspaceHead_t h;
        kvspaceDecodeHead(elems[i].data, elems[i].len, &h);
        b = elems[i].data + h.body_offset;
        blen = h.body_len;
        int c = blen < sz ? blen : sz;
        memcpy(raw + i * sz, b, (size_t)c);
        for (int j = c; j < sz; j++)
            raw[i * sz + j] = 0;
    }
    kvlangXvalueNewTlv(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static int separated_len(kvlangKv_t *kv, const char *base) {
    for (int i = 0;; i++) {
        kvlangStrbuf_t k;
        kvlangStrbufInit(&k);
        kvlangStrbufPuts(&k, base);
        kvlangStrbufPuts(&k, MEMBER_SEP);
        kvlangStrbufPrintf(&k, "[%d]", i);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, k.p, &v);
        bool none = kvlangXvalueNone(&v);
        kvlangXvalueFree(&v);
        kvlangStrbufFree(&k);
        if (none)
            return i;
    }
}

/* 坐标段 key：base·[s0,s1,...]。1 维即 base·[s0]。 */
char *kvlangBuiltinScatterKey(const char *base, const int64_t *coords,
                              int ncoord) {
    kvlangStrbuf_t b;
    kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, base);
    kvlangStrbufPuts(&b, MEMBER_SEP);
    kvlangStrbufPutc(&b, '[');
    for (int i = 0; i < ncoord; i++) {
        if (i)
            kvlangStrbufPutc(&b, ',');
        kvlangStrbufPrintf(&b, "%lld", (long long)coords[i]);
    }
    kvlangStrbufPutc(&b, ']');
    return kvlangStrbufDetach(&b);
}

static void ensure_scattered(kvlangFrame_t *f, const char *base) {
    kvlangXvalue_t arr;
    kvlangXvalueZero(&arr);
    kvlangKvGetOne(f->kv, base, &arr);
    if (kvlangXvalueNone(&arr) ||
        kvlangXvalueElemSize(kvlangXvalueKind(&arr)) <= 0) {
        kvlangXvalueFree(&arr);
        return;
    }
    int n = kvlangXvalueArrayLen(&arr);
    for (int i = 0; i < n; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(base, c, 1);
        kvlangXvalue_t e;
        kvlangBuiltinXvalueAt(&arr, i, &e);
        kvlangKvPair_t p = {k, e};
        char err[256];
        kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e);
        free(k);
    }
    kvlangXvalueFree(&arr);
    char err[256];
    kvlangKvDel(f->kv, base, err, sizeof err);
}

int kvlangBuiltinArray(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    if (f->inst->nw == 0 || n == 0) {
        kvlangBuiltinNextPc(f);
        kvlangBuiltinFreeInputs(in, n);
        return 0;
    }
    const char *kind = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(kind) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "array: unsupported element kind %s",
                                   kind);
    }
    for (int i = 1; i < n; i++)
        if (strcmp(kvlangXvalueKind(&in[i]), kind) != 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetErr(f, "array: mixed kinds %s and %s", kind,
                                       kvlangXvalueKind(&in[i]));
        }
    kvlangXvalue_t arr;
    pack_typed_array(kind, in, n, &arr);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    kvlangKvPair_t p = {key, arr};
    char err[256];
    kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key);
    kvlangXvalueFree(&arr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

/* array·fill(langtype[, value]) -> a：按 langtype 的定长维度一次成型 compact 数组，
 * 每元素填 value（缺省全零）。用于大数组初始化，无需逐元素赋值。 */
int kvlangBuiltinArrayFill(kvlangFrame_t *f) {
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.fill requires a write param (-> a)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f,
                                   "TypeError: array.fill requires a langtype");
    }
    char *ke = kvlangXvalueValueString(&in[0]);
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)ke, &kx);
    int sz = kvlangXvalueElemSize(kx.kind);
    if (sz <= 0 || sz > 8 || kx.ndim < 1 || kx.array_len < 0) {
        int rc = kvlangBuiltinSetErr(
            f, "TypeError: array.fill: not a fixed-length array type %s", ke);
        free(ke);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    uint8_t elem[8] = {0};
    if (n >= 2 && !kvlangXvalueNone(&in[1])) {
        kvlangScalar_t s = kvlangXvalueScalar(&in[1]);
        int tid = kvlangLangTypeId(kx.kind, kx.kind_len);
        if (tid == KVLANG_LT_FLOAT32) {
            float v = (float)kvlangScalarF64(s);
            memcpy(elem, &v, 4);
        } else if (tid == KVLANG_LT_FLOAT64) {
            double v = kvlangScalarF64(s);
            memcpy(elem, &v, 8);
        } else {
            int64_t v = kvlangScalarI64(s);
            memcpy(elem, &v, (size_t)sz);
        }
    }
    size_t total = (size_t)sz * (size_t)kx.array_len;
    uint8_t *raw = malloc(total > 0 ? total : 1);
    for (int i = 0; i < kx.array_len; i++)
        memcpy(raw + (size_t)i * sz, elem, (size_t)sz);
    kvlangXvalue_t arr;
    kvlangXvalueNewTlvDims(&arr, kx.kind, raw, (uint32_t)total, kx.dims,
                           kx.ndim);
    free(raw);
    int rc = kvlangBuiltinWriteResult(f, &arr);
    kvlangXvalueFree(&arr);
    free(ke);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* ── array· 系列 ────────────────────────────────────────────────── */

int kvlangBuiltinScatter(kvlangFrame_t *f) {
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.scatter requires a write param");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n == 0 || kvlangXvalueNone(&in[0])) {
        kvlangBuiltinNextPc(f);
        kvlangBuiltinFreeInputs(in, n);
        return 0;
    }
    if (kvlangXvalueElemSize(kvlangXvalueKind(&in[0])) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f,
            "TypeError: array.scatter requires a compact array ([]T), got %s",
            kvlangXvalueKind(&in[0]));
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    int al = kvlangXvalueArrayLen(&in[0]);
    if (al > 0) {
        char err[256];
        int32_t dims[1] = {al};
        /* 散 key 化的容器值类型：键是坐标段 `[i]`，值是原 compact 元素类型（逐元素原样搬）。 */
        char ty[256];
        snprintf(ty, sizeof ty, "[int64]%s%s", MEMBER_SEP,
                 kvlangXvalueKind(&in[0]));
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, ty, dims, 1);
        kvlangKvPair_t p0 = {dst, mark};
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
    }
    for (int i = 0; i < al; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e;
        kvlangBuiltinXvalueAt(&in[0], i, &e);
        kvlangKvPair_t p = {k, e};
        char err[256];
        kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e);
        free(k);
    }
    free(dst);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinCompact(kvlangFrame_t *f) {
    if (f->inst->nr == 0 || f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.compact requires read and write params");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *src =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[0].name);
    kvlangXvalue_t elems[1024];
    int n = 0;
    for (int i = 0;; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(src, c, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v);
        free(k);
        if (none)
            break;
        elems[n++] = v;
    }
    if (n == 0) {
        free(src);
        free(fr);
        kvlangBuiltinNextPc(f);
        return 0;
    }
    kvlangXvalue_t arr;
    pack_typed_array(kvlangXvalueKind(&elems[0]), elems, n, &arr);
    char *dst =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    kvlangKvPair_t p = {dst, arr};
    char err[256];
    kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    for (int i = 0; i < n; i++)
        kvlangXvalueFree(&elems[i]);
    kvlangXvalueFree(&arr);
    free(dst);
    free(src);
    free(fr);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinAppend(kvlangFrame_t *f) {
    if (f->inst->nr < 2)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.append requires array and element");
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.append requires a write param (-> arr)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int len = separated_len(f->kv, base);
    int64_t c[1] = {len};
    char *k = kvlangBuiltinScatterKey(base, c, 1);
    kvlangKvPair_t p = {k, n >= 2 ? in[1] : in[0]};
    char err[256];
    kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(k);
    free(base);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinSlice(kvlangFrame_t *f) {
    if (f->inst->nr < 3)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.slice requires array, start, end");
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: array.slice requires a write param (-> arr)");
    kvlangXvalue_t in[3];
    int n = kvlangBuiltinReadInputs(f, in, 3);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int al = separated_len(f->kv, base);
    int lo = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])),
        hi = (int)kvlangScalarI64(kvlangXvalueScalar(&in[2]));
    if (lo < 0 || hi < lo || hi > al) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)",
            lo, hi, al);
    }
    for (int i = lo; i < hi; i++) {
        int64_t sc[1] = {i}, dc[1] = {i - lo};
        char *sk = kvlangBuiltinScatterKey(base, sc, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(f->kv, sk, &v);
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        kvlangKvPair_t p = {dk, v};
        char err[256];
        kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v);
        free(sk);
        free(dk);
    }
    for (int i = hi - lo; i < al; i++) {
        int64_t dc[1] = {i};
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        char err[256];
        kvlangKvDel(f->kv, dk, err, sizeof err);
        free(dk);
    }
    free(base);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

