// builtin_coll —— array/ndarray/xv/obj/map 集合与容器 rwir

#include "builtin_internal.h"

static void pack_typed_array(const char *kind, const kvlangXvalue_t *elems, int n, kvlangXvalue_t *out) {
    int sz = kvlangXvalueElemSize(kind);
    uint8_t *raw = malloc((size_t)sz * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        const uint8_t *b; int32_t blen;
        kvspaceHead_t h; kvspaceDecodeHead(elems[i].data, elems[i].len, &h);
        b = elems[i].data + h.body_offset; blen = h.body_len;
        int c = blen < sz ? blen : sz;
        memcpy(raw + i * sz, b, (size_t)c);
        for (int j = c; j < sz; j++) raw[i * sz + j] = 0;
    }
    kvlangXvalueNewTlv(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static int separated_len(kvlangKv_t *kv, const char *base) {
    for (int i = 0; ; i++) {
        kvlangStrbuf_t k; kvlangStrbufInit(&k);
        kvlangStrbufPuts(&k, base); kvlangStrbufPuts(&k, MEMBER_SEP);
        kvlangStrbufPrintf(&k, "[%d]", i);
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, k.p, &v);
        bool none = kvlangXvalueNone(&v);
        kvlangXvalueFree(&v); kvlangStrbufFree(&k);
        if (none) return i;
    }
}

/* 坐标段 key：base·[s0,s1,...]。1 维即 base·[s0]。 */
char *kvlangBuiltinScatterKey(const char *base, const int64_t *coords, int ncoord) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, base); kvlangStrbufPuts(&b, MEMBER_SEP); kvlangStrbufPutc(&b, '[');
    for (int i = 0; i < ncoord; i++) {
        if (i) kvlangStrbufPutc(&b, ',');
        kvlangStrbufPrintf(&b, "%lld", (long long)coords[i]);
    }
    kvlangStrbufPutc(&b, ']');
    return kvlangStrbufDetach(&b);
}

/* kvlangBuiltinMemindex（p·）：kind=index，body=[4B count LE][name\n...]，成员列表唯一权威。 */
void kvlangBuiltinMemindex(kvlangXvalue_t *out, const char *const *names, int n) {
    kvlangStrbuf_t body; kvlangStrbufInit(&body);
    char count[4] = { (char)(n & 0xFF), (char)((n >> 8) & 0xFF), (char)((n >> 16) & 0xFF), (char)((n >> 24) & 0xFF) };
    kvlangStrbufPutn(&body, count, 4);
    for (int i = 0; i < n; i++) {
        if (i) kvlangStrbufPutc(&body, '\n');
        kvlangStrbufPuts(&body, names[i]);
    }
    kvlangXvalueNewTlv(out, KVSPACE_KIND_INDEX, (const uint8_t *)body.p, (uint32_t)body.len, 1);
    kvlangStrbufFree(&body);
}

/* stringkeymap 容器值（p）：body 空，dims 落 head。 */
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const int32_t *dims, int ndim) {
    kvlangXvalueNewTlvDims(out, KVSPACE_KIND_MAP, (const uint8_t *)"", 0, dims, ndim);
}

static void ensure_scattered(kvlangFrame_t *f, const char *base) {
    kvlangXvalue_t arr; kvlangXvalueZero(&arr);
    kvlangKvGetOne(f->kv, base, &arr);
    if (kvlangXvalueNone(&arr) || kvlangXvalueElemSize(kvlangXvalueKind(&arr)) <= 0) { kvlangXvalueFree(&arr); return; }
    int n = kvlangXvalueArrayLen(&arr);
    for (int i = 0; i < n; i++) {
        int64_t c[1] = { i };
        char *k = kvlangBuiltinScatterKey(base, c, 1);
        kvlangXvalue_t e; kvlangBuiltinXvalueAt(&arr, i, &e);
        kvlangKvPair_t p = { k, e };
        char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k);
    }
    kvlangXvalueFree(&arr);
    char err[256]; kvlangKvDel(f->kv, base, err, sizeof err);
}

int kvlangBuiltinArray(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = kvlangBuiltinReadInputs(f, in, 64);
    if (f->inst->nw == 0 || n == 0) { kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0; }
    const char *kind = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(kind) <= 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "array: unsupported element kind %s", kind); }
    for (int i = 1; i < n; i++) if (strcmp(kvlangXvalueKind(&in[i]), kind) != 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "array: mixed kinds %s and %s", kind, kvlangXvalueKind(&in[i])); }
    kvlangXvalue_t arr; pack_typed_array(kind, in, n, &arr);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    kvlangKvPair_t p = { key, arr };
    char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key); kvlangXvalueFree(&arr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinNdarrayNumel(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    int64_t n_el = 0;
    if (n > 0) n_el = kvlangXvalueArrayLen(&in[0]);
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, n_el);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinNdarrayDim(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    int64_t ndim = 0;
    if (n > 0) {
        kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        ndim = kx.ndim;
    }
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, ndim);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinNdarrayShape(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    int32_t dims[8]; int32_t ndim = 0;
    if (n > 0 && !kvlangXvalueNone(&in[0])) {
        kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        ndim = kx.ndim;
        for (int i = 0; i < ndim && i < 8; i++) dims[i] = kx.dims[i];
    }
    uint8_t raw[64]; uint32_t raw_len = 0;
    for (int i = 0; i < ndim; i++) {
        int64_t d = dims[i];
        for (int j = 0; j < 8; j++) raw[i * 8 + j] = (d >> (j * 8)) & 0xFF;
        raw_len += 8;
    }
    int32_t sd[1] = { ndim };
    kvlangXvalue_t r; kvlangXvalueNewTlvDims(&r, KVSPACE_KIND_INT64, raw, raw_len, sd, 1);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* 计算多维下标 (i0..i_{n-1}) 的 row-major 扁平索引，越界返回 -1。 */
static int64_t flat_index(const kvlang_kindexpr_t *kx, const kvlangXvalue_t *in, int nidx) {
    int64_t flat = 0;
    for (int i = 0; i < nidx; i++) {
        int64_t idx = kvlangXvalueAsInt64(&in[i + 1]);
        if (idx < 0 || idx >= kx->dims[i]) return -1;
        flat = flat * kx->dims[i] + idx;
    }
    return flat;
}

int kvlangBuiltinXvAt(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 1;
    if (nidx < 1) return kvlangBuiltinSetErr(f, "TypeError: xv.at requires array and indices");
    kvlangXvalue_t in[MAX_PARAMS]; int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    int sz = kvlangXvalueElemSize(k);
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (sz <= 0 || kx.ndim == 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: xv.at requires a compact array, got %s", k); }
    if (nidx != kx.ndim) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.at: %d-dim array needs %d indices, got %d", kx.ndim, kx.ndim, nidx); }
    int64_t flat = flat_index(&kx, in, nidx);
    if (flat < 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.at: index out of bounds"); }
    const uint8_t *body = in[0].data + h.body_offset;
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, k, body + flat * sz, (uint32_t)sz, 1);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinXvSet(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 2;
    if (nidx < 1) return kvlangBuiltinSetErr(f, "TypeError: xv.set requires array, indices, value");
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: xv.set requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS]; int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    int sz = kvlangXvalueElemSize(k);
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (sz <= 0 || kx.ndim == 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: xv.set requires a compact array, got %s", k); }
    if (nidx != kx.ndim) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.set: %d-dim array needs %d indices, got %d", kx.ndim, kx.ndim, nidx); }
    int64_t flat = flat_index(&kx, in, nidx);
    if (flat < 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.set: index out of bounds"); }
    const uint8_t *body = in[0].data + h.body_offset;
    uint8_t *nb = malloc((size_t)h.body_len);
    memcpy(nb, body, (size_t)h.body_len);
    kvspaceHead_t vh; kvspaceDecodeHead(in[nidx + 1].data, in[nidx + 1].len, &vh);
    const uint8_t *vb = in[nidx + 1].data + vh.body_offset;
    int c = vh.body_len < sz ? vh.body_len : sz;
    memcpy(nb + flat * sz, vb, (size_t)c);
    kvlangXvalue_t nv; kvlangXvalueNewTlvDims(&nv, k, nb, (uint32_t)h.body_len, kx.dims, kx.ndim);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv); free(nb); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinXvReshape(kvlangFrame_t *f) {
    int ndims = f->inst->nr - 1;
    if (ndims < 1) return kvlangBuiltinSetErr(f, "TypeError: xv.reshape requires array and >=1 dims");
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: xv.reshape requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS]; int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(k) <= 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: xv.reshape requires a compact array, got %s", k); }
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (kx.ndim < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: xv.reshape requires a compact array, got scalar %s", k); }
    if (ndims > X_MAX_NDIM) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.reshape: at most %d dims, got %d", X_MAX_NDIM, ndims); }
    int32_t dims[X_MAX_NDIM]; int64_t numel = 1;
    for (int i = 0; i < ndims; i++) {
        dims[i] = (int32_t)kvlangXvalueAsInt64(&in[i + 1]);
        if (dims[i] < 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.reshape: negative dim %d", dims[i]); }
        numel *= dims[i];
    }
    if (numel != kx.array_len) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: xv.reshape: cannot reshape %d elements into %lld", kx.array_len, (long long)numel); }
    const uint8_t *body = in[0].data + h.body_offset;
    kvlangXvalue_t nv; kvlangXvalueNewTlvDims(&nv, k, body, (uint32_t)h.body_len, dims, ndims);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·reinterpret(arr, kindexpr) -> a：body 字节原样，整个 kindexpr 换成传入的（kind+dims 一起），不做校验。 */
int kvlangBuiltinXvReinterpret(kvlangFrame_t *f) {
    if (f->inst->nr < 2) return kvlangBuiltinSetErr(f, "TypeError: xv.reinterpret requires array and kindexpr");
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: xv.reinterpret requires a write param (-> a)");
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *ke = kvlangXvalueValueString(&in[1]);
    kvlang_kindexpr_t nkx; kvlang_kindexpr_parse((const uint8_t *)ke, &nkx);
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    const uint8_t *body = in[0].data + h.body_offset;
    /* 动态 "[]kind"（parse 得 ndim0 但带方括号）：按 body 字节数补出一维长度，与落盘数组表示一致。 */
    int32_t ndim = nkx.ndim;
    if (nkx.ndim == 0 && strchr(ke, '[')) {
        int32_t es = kvlangXvalueElemSize(nkx.kind);
        nkx.dims[0] = es > 0 ? (int32_t)(h.body_len / es) : (int32_t)h.body_len;
        ndim = 1;
    }
    kvlangXvalue_t nv; kvlangXvalueNewTlvDims(&nv, nkx.kind, body, (uint32_t)h.body_len, nkx.dims, ndim);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv); free(ke); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·kindexpr(v) -> s：返回 v 的 head kindexpr 串（含 ref 前缀与 [dims]），作为字符串。 */
int kvlangBuiltinXvKindexpr(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: xv.kindexpr requires a write param (-> s)");
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
    kvlangXvalue_t r; kvlangXvalueNewCharUtf8(&r, (const char *)h.kindexpr);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·bodylen(v) -> n：返回 v 的 body 字节数（int64）。 */
int kvlangBuiltinXvBodylen(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: xv.bodylen requires a write param (-> n)");
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, h.body_len);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinScatter(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: array.scatter requires a write param");
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n == 0 || kvlangXvalueNone(&in[0])) { kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0; }
    if (kvlangXvalueElemSize(kvlangXvalueKind(&in[0])) <= 0) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: array.scatter requires a compact array ([]T), got %s", kvlangXvalueKind(&in[0])); }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    int al = kvlangXvalueArrayLen(&in[0]);
    if (al > 0) {
        char err[256];
        int32_t dims[1] = { al };
        kvlangXvalue_t mark; kvlangBuiltinMapMarker(&mark, dims, 1);
        kvlangKvPair_t p0 = { dst, mark }; kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
    }
    for (int i = 0; i < al; i++) {
        int64_t c[1] = { i };
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e; kvlangBuiltinXvalueAt(&in[0], i, &e);
        kvlangKvPair_t p = { k, e }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k);
    }
    free(dst); free(fr);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

int kvlangBuiltinCompact(kvlangFrame_t *f) {
    if (f->inst->nr == 0 || f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: array.compact requires read and write params");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *src = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[0].name);
    kvlangXvalue_t elems[1024]; int n = 0;
    for (int i = 0; ; i++) {
        int64_t c[1] = { i };
        char *k = kvlangBuiltinScatterKey(src, c, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v); free(k);
        if (none) break;
        elems[n++] = v;
    }
    if (n == 0) { free(src); free(fr); kvlangBuiltinNextPc(f); return 0; }
    kvlangXvalue_t arr; pack_typed_array(kvlangXvalueKind(&elems[0]), elems, n, &arr);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    kvlangKvPair_t p = { dst, arr }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    for (int i = 0; i < n; i++) kvlangXvalueFree(&elems[i]);
    kvlangXvalueFree(&arr);
    free(dst); free(src); free(fr);
    kvlangBuiltinNextPc(f); return 0;
}

int kvlangBuiltinAppend(kvlangFrame_t *f) {
    if (f->inst->nr < 2) return kvlangBuiltinSetErr(f, "TypeError: array.append requires array and element");
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: array.append requires a write param (-> arr)");
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int len = separated_len(f->kv, base);
    int64_t c[1] = { len };
    char *k = kvlangBuiltinScatterKey(base, c, 1);
    kvlangKvPair_t p = { k, n >= 2 ? in[1] : in[0] };
    char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(k); free(base); free(fr);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

int kvlangBuiltinSlice(kvlangFrame_t *f) {
    if (f->inst->nr < 3) return kvlangBuiltinSetErr(f, "TypeError: array.slice requires array, start, end");
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: array.slice requires a write param (-> arr)");
    kvlangXvalue_t in[3]; int n = kvlangBuiltinReadInputs(f, in, 3);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int al = separated_len(f->kv, base);
    int lo = (int)kvlangXvalueAsInt64(&in[1]), hi = (int)kvlangXvalueAsInt64(&in[2]);
    if (lo < 0 || hi < lo || hi > al) { free(base); free(fr); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)", lo, hi, al); }
    for (int i = lo; i < hi; i++) {
        int64_t sc[1] = { i }, dc[1] = { i - lo };
        char *sk = kvlangBuiltinScatterKey(base, sc, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, sk, &v);
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        kvlangKvPair_t p = { dk, v }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v); free(sk); free(dk);
    }
    for (int i = hi - lo; i < al; i++) {
        int64_t dc[1] = { i };
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        char err[256]; kvlangKvDel(f->kv, dk, err, sizeof err); free(dk);
    }
    free(base); free(fr);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

int kvlangBuiltinObj(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = kvlangBuiltinReadInputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name），容器值随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL; int oc = 0;
        kvlangKvList(f->kv, dir, false, false, &old, &oc);
        for (int i = 0; i < oc; i++) {
            char *mk = kvlangKeytreeMember(ok, old[i]);
            kvlangKvDel(f->kv, mk, err, sizeof err);
            free(mk); free(old[i]);
        }
        free(old); free(dir);
        /* 收集成员名（跳过 None）。 */
        int cnt = 0;
        for (int i = 0; i + 1 < n; i += 2) if (!kvlangXvalueNone(&in[i + 1])) cnt++;
        char **names = malloc(sizeof(char *) * (size_t)(cnt > 0 ? cnt : 1));
        for (int i = 0, j = 0; i + 1 < n; i += 2) {
            if (kvlangXvalueNone(&in[i + 1])) continue;
            names[j++] = kvlangXvalueValueString(&in[i]);
        }
        /* 容器值 p：kind=object，body 空。 */
        kvlangXvalue_t mark; kvlangXvalueNewTlv(&mark, KVSPACE_KIND_OBJ, (const uint8_t *)"", 0, 1);
        kvlangKvPair_t p0 = { ok, mark };
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        /* kvlangBuiltinMemindex p·：kind=index，body=[4B count][names]。 */
        char *mip = kvlangKeytreeMember(ok, "");
        kvlangXvalue_t mi; kvlangBuiltinMemindex(&mi, (const char *const *)names, cnt);
        kvlangKvPair_t p1 = { mip, mi };
        kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
        kvlangXvalueFree(&mi); free(mip);
        for (int i = 0, j = 0; i + 1 < n; i += 2) {
            if (kvlangXvalueNone(&in[i + 1])) continue;
            char *mk = kvlangKeytreeMember(ok, names[j]);
            kvlangKvPair_t p = { mk, in[i + 1] };
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(mk); free(names[j]); j++;
        }
        free(names);
        free(ok);
    }
    free(fr);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

static char *dupn(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* 在 "name:kindexpr\n..." 声明串里查字段名，返回其类型（malloc）或 NULL（无此字段）。 */
static char *struct_field_type(const char *decl, const char *fname) {
    size_t fl = strlen(fname);
    const char *p = decl;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        const char *colon = memchr(p, ':', linelen);
        if (colon) {
            size_t nlen = (size_t)(colon - p);
            if (nlen == fl && memcmp(p, fname, fl) == 0)
                return dupn(colon + 1, linelen - nlen - 1);
        }
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

/* struct·new：克隆 /lib/Name 原型子树到写槽，覆盖给定字段（校验字段存在性+类型）。
 * in[0]=structref（"/lib/Name"），其后成对 (字段名, 值)。实例基值 kind=structref。 */
int kvlangBuiltinStructNew(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = kvlangBuiltinReadInputs(f, in, 64);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: struct.new requires a type"); }
    char *ref = kvlangXvalueValueString(&in[0]);
    kvlangXvalue_t proto; kvlangXvalueZero(&proto); kvlangKvGetOne(f->kv, ref, &proto);
    if (kvlangXvalueNone(&proto) || strcmp(kvlangXvalueKind(&proto), KVSPACE_KIND_STRUCT) != 0) {
        int e = kvlangBuiltinSetErr(f, "TypeError: %s is not a struct type", ref);
        kvlangXvalueFree(&proto); free(ref); kvlangBuiltinFreeInputs(in, n); return e;
    }
    kvspaceHead_t ph; kvlangXvalueHead(&proto, &ph);
    int32_t dclen = 0; const uint8_t *dcl = kvlangXvalueBody(&proto, &ph, &dclen);
    char *decl = dupn((const char *)dcl, (size_t)(dclen > 0 ? dclen : 0));
    kvlangXvalueFree(&proto);

    char err[256]; int rc = 0;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw && rc == 0; w++) {
        char *ok = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        kvlangKvDelTree(f->kv, ok, err, sizeof err);
        if (kvlangKvCpTree(f->kv, ref, ok, err, sizeof err) != 0) { rc = kvlangBuiltinSetErr(f, "%s", err); free(ok); break; }
        kvlangXvalue_t mark; kvlangXvalueNewTlv(&mark, ref, (const uint8_t *)"", 0, 1);
        kvlangKvPair_t p0 = { ok, mark }; kvlangKvSet(f->kv, &p0, 1, err, sizeof err); kvlangXvalueFree(&mark);
        for (int i = 1; i + 1 < n && rc == 0; i += 2) {
            char *fname = kvlangXvalueValueString(&in[i]);
            char *ftype = struct_field_type(decl, fname);
            if (!ftype) { rc = kvlangBuiltinSetErr(f, "TypeError: struct %s has no field %s", ref, fname); free(fname); break; }
            const char *vk = kvlangXvalueKind(&in[i + 1]);
            kvspaceHead_t vh; kvlangXvalueHead(&in[i + 1], &vh);
            kvlang_kindexpr_t vkx; kvlang_kindexpr_parse(vh.kindexpr, &vkx);
            if (ftype[0] && !kvlang_rwirextKindexprMatch(ftype, vk, vkx.ndim, vkx.dims)) {
                rc = kvlangBuiltinSetErr(f, "TypeError: field %s: expected %s, got %s", fname, ftype, vk[0] ? vk : "None");
                free(ftype); free(fname); break;
            }
            char *mk = kvlangKeytreeMember(ok, fname);
            kvlangKvPair_t p = { mk, in[i + 1] }; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(mk); free(ftype); free(fname);
        }
        free(ok);
    }
    free(fr); free(decl); free(ref);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0) return rc;
    kvlangBuiltinNextPc(f); return 0;
}

int kvlangBuiltinMap(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = kvlangBuiltinReadInputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name）与旧容器值，随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL; int oc = 0;
        kvlangKvList(f->kv, dir, false, false, &old, &oc);
        for (int i = 0; i < oc; i++) {
            char *mk = kvlangKeytreeMember(ok, old[i]);
            kvlangKvDel(f->kv, mk, err, sizeof err);
            free(mk); free(old[i]);
        }
        free(old); free(dir);
        kvlangKvDel(f->kv, ok, err, sizeof err);

        char **names = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPrintf(&s, "[%d]", i);
            names[i] = kvlangStrbufDetach(&s);
        }
        /* 容器值 p：stringkeymap，body 空，dims=[n] 落 head。 */
        int32_t dims[1] = { n };
        kvlangXvalue_t mark; kvlangBuiltinMapMarker(&mark, dims, 1);
        kvlangKvPair_t p0 = { ok, mark };
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        /* kvlangBuiltinMemindex p·：kind=index，body=[4B count][[0]\n[1]...]。 */
        char *mip = kvlangKeytreeMember(ok, "");
        kvlangXvalue_t mi; kvlangBuiltinMemindex(&mi, (const char *const *)names, n);
        kvlangKvPair_t p1 = { mip, mi };
        kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
        kvlangXvalueFree(&mi); free(mip);
        for (int i = 0; i < n; i++) {
            int64_t c[1] = { i };
            char *k = kvlangBuiltinScatterKey(ok, c, 1);
            kvlangKvPair_t p = { k, in[i] };
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(k); free(names[i]);
        }
        free(names); free(ok);
    }
    free(fr);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}
