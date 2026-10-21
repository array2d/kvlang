#include "rwir_internal.h"

/* ── 数组打包 / 散 key 辅助 ──────────────────────────────────────── */

static void pack_typed_array(const char *kind, const kvlangXvalue_t *elems,
                             int n, kvlangXvalue_t *out) {
    int sz = kvlangXvalueElemSize(kind);
    uint8_t *raw = malloc((size_t)sz * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        const uint8_t *b;
        int32_t blen;
        b = xh_body_of(&elems[i]);
        blen = xh_content_len_of(&elems[i]);
        int c = blen < sz ? blen : sz;
        memcpy(raw + i * sz, b, (size_t)c);
        for (int j = c; j < sz; j++)
            raw[i * sz + j] = 0;
    }
    kvlangXvalueNewTlv(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static int separated_len(kvspace_t *kv, const char *base) {
    char key[KV_KEY_MAX];
    for (int i = 0;; i++) {
        kvlangKeyFormat(key, sizeof key, "%s%s[%d]", base, MEMBER_SEP, i);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(kv, key, &v);
        bool none = kvlangXvalueNone(&v);
        if (none)
            return i;
    }
}

static int64_t separated_next(kvspace_t *kv, const char *base) {
    char *dir = kvlangKeytreeMember(base, "");
    char **names = NULL;
    int count = 0;
    int64_t next = 0;
    if (kvlangKvspaceList(kv, dir, false, false, &names, &count) == 0) {
        for (int i = 0; i < count; i++) {
            char *end = NULL;
            if (names[i][0] == '[') {
                long long index = strtoll(names[i] + 1, &end, 10);
                if (end && *end == ']' && end[1] == 0 && index >= 0 &&
                    index < INT64_MAX && index + 1 > next)
                    next = index + 1;
            }
            free(names[i]);
        }
        free(names);
    }
    free(dir);
    return next;
}

static int clear_members(kvlangFrame_t *f, const char *base) {
    char *prefix = kvlangKeytreeMember(base, "");
    char **names = NULL;
    int count = 0;
    if (kvlangKvspaceList(f->kvspace, prefix, false, false, &names, &count) != 0) {
        free(prefix);
        return kvlangBuiltinSetError(f, "array.scatter: cannot list members");
    }
    char err[256];
    int rc = 0;
    for (int i = 0; i < count; i++) {
        if (rc == 0) {
            char key[KV_KEY_MAX];
            kvlangKeyFormat(key, sizeof key, "%s%s", prefix, names[i]);
            if (kvlangKvspaceDeleteTree(f->kvspace, key, err, sizeof err) != 0)
                rc = kvlangBuiltinSetError(f, "%s", err);
        }
        free(names[i]);
    }
    free(names);
    free(prefix);
    return rc;
}

/* 坐标段 key：base·[s0,s1,...]。1 维即 base·[s0]。 */
char *kvlangBuiltinScatterKey(const char *base, const int64_t *coords,
                              int ncoord) {
    char buf[KV_KEY_MAX];
    size_t cap = sizeof buf;
    int n = snprintf(buf, cap, "%s%s[", base, MEMBER_SEP);
    if (n < 0 || (size_t)n >= cap)
        kvlangDie("scatter key exceeds %zu bytes", cap);
    size_t off = (size_t)n;
    for (int i = 0; i < ncoord; i++) {
        n = snprintf(buf + off, cap - off, "%s%lld", i ? "," : "",
                     (long long)coords[i]);
        if (n < 0 || (size_t)n >= cap - off)
            kvlangDie("scatter key exceeds %zu bytes", cap);
        off += (size_t)n;
    }
    if (off + 2 > cap)
        kvlangDie("scatter key exceeds %zu bytes", cap);
    buf[off++] = ']';
    buf[off] = 0;
    return kvlangStringCopyN(buf, off);
}

static int ensure_scattered(kvlangFrame_t *f, const char *base, const char *fallback_kind) {
    kvlangXvalue_t arr;
    kvlangXvalueZero(&arr);
    kvlangKvspaceGetOne(f->kvspace, base, &arr);
    /* base 即将被 marker 覆盖；shm 的读是 mmap 视图，写会使其失效，先固化。 */
    kvlangXvalueCopyToArena(&arr);
    bool missing = kvlangXvalueNone(&arr);
    const char *kind = missing ? fallback_kind : kvlangXvalueKind(&arr);
    if (!kind || kvlangXvalueElemSize(kind) <= 0) {
        return missing ? kvlangBuiltinSetError(f, "array: missing element type") : 0;
    }
    char ty[256];
    snprintf(ty, sizeof ty, "[int64]%s%s", MEMBER_SEP, kind);
    kvlangXvalue_t mark;
    kvlangBuiltinMapMarker(&mark, ty);
    kvlangKvPair_t head = {(char *)base, mark};
    char err[256];
    int rc = kvlangKvspaceSet(f->kvspace, &head, 1, err, sizeof err);
    if (rc != 0) {
        return kvlangBuiltinSetError(f, "%s", err);
    }
    if (missing) {
        return 0;
    }
    int n = kvlangXvalueArrayLen(&arr);
    for (int i = 0; i < n; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(base, c, 1);
        kvlangXvalue_t e;
        kvlangXvalueElementAt(&arr, i, &e);
        kvlangKvPair_t p = {k, e};
        rc = kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
        free(k);
        if (rc != 0) {
            return kvlangBuiltinSetError(f, "%s", err);
        }
    }
    return 0;
}

int kvlangBuiltinArray(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    if (f->instruction->write_count == 0 || n == 0) {
        kvlangBuiltinNextPc(f);
        kvlangBuiltinFreeInputs(in, n);
        return 0;
    }
    const char *kind = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(kind) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "array: unsupported element kind %s",
                                   kind);
    }
    for (int i = 1; i < n; i++)
        if (strcmp(kvlangXvalueKind(&in[i]), kind) != 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetError(f, "array: mixed kinds %s and %s", kind,
                                       kvlangXvalueKind(&in[i]));
        }
    kvlangXvalue_t arr;
    pack_typed_array(kind, in, n, &arr);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    free(fr);
    kvlangKvPair_t p = {key, arr};
    char err[256];
    kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    free(key);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

/* array·fill(langtype[, value]) -> a：按 langtype 的定长维度一次成型 compact 数组，
 * 每元素填 value（缺省全零）。用于大数组初始化，无需逐元素赋值。 */
int kvlangBuiltinArrayFill(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: array.fill requires a write param (-> a)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f,
                                   "TypeError: array.fill requires a langtype");
    }
    char *ke = kvlangXvalueValueString(&in[0]);
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)ke, (int32_t)strlen(ke), &kx);
    int sz = kvlangXvalueElemSize(kx.kind);
    if (sz <= 0 || sz > 8 || kx.ndim < 1 || kx.array_len < 0) {
        int rc = kvlangBuiltinSetError(
            f, "TypeError: array.fill: not a fixed-length array type %s", ke);
        free(ke);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    uint8_t elem[8] = {0};
    if (n >= 2 && !kvlangXvalueNone(&in[1])) {
        kvlangScalar_t s = kvlangXvalueScalar(&in[1]);
        int tid = kvlangLangtypeId(kx.kind, kx.kind_len);
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
    free(ke);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* ── array· 系列 ────────────────────────────────────────────────── */

int kvlangBuiltinScatter(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
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
        return kvlangBuiltinSetError(
            f,
            "TypeError: array.scatter requires a compact array ([]T), got %s",
            kvlangXvalueKind(&in[0]));
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    int rc = clear_members(f, dst);
    if (rc != 0) {
        free(dst);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    int al = kvlangXvalueArrayLen(&in[0]);
    char err[256];
    char ty[256];
    snprintf(ty, sizeof ty, "[int64]%s%s", MEMBER_SEP,
             kvlangXvalueKind(&in[0]));
    kvlangXvalue_t mark;
    kvlangBuiltinMapMarker(&mark, ty);
    kvlangKvPair_t p0 = {dst, mark};
    rc = kvlangKvspaceSet(f->kvspace, &p0, 1, err, sizeof err);
    if (rc != 0) {
        free(dst);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "%s", err);
    }
    for (int i = 0; i < al; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e;
        kvlangXvalueElementAt(&in[0], i, &e);
        kvlangKvPair_t p = {k, e};
        rc = kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
        free(k);
        if (rc != 0) {
            free(dst);
            free(fr);
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetError(f, "%s", err);
        }
    }
    free(dst);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinCompact(kvlangFrame_t *f) {
    if (f->instruction->read_count == 0 || f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: array.compact requires read and write params");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *src =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->reads[0].name);
    kvlangXvalue_t elems[1024];
    int n = 0;
    for (int i = 0;; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(src, c, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(f->kvspace, k, &v);
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
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    kvlangKvPair_t p = {dst, arr};
    char err[256];
    kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    free(dst);
    free(src);
    free(fr);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinAppend(kvlangFrame_t *f) {
    if (f->instruction->read_count < 2)
        return kvlangBuiltinSetError(
            f, "TypeError: array.append requires array and element");
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: array.append requires a write param (-> arr)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    int rc = ensure_scattered(f, base, n >= 2 ? kvlangXvalueKind(&in[1]) : NULL);
    if (rc != 0) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    int64_t c[1] = {separated_next(f->kvspace, base)};
    char *k = kvlangBuiltinScatterKey(base, c, 1);
    kvlangKvPair_t p = {k, in[1]}; /* nr >= 2 已由上面拦下，ReadInputs(cap=2) 必返 2 */
    char err[256];
    rc = kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    free(k);
    free(base);
    free(fr);
    if (rc == 0)
        kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return rc == 0 ? 0 : kvlangBuiltinSetError(f, "%s", err);
}

int kvlangBuiltinSlice(kvlangFrame_t *f) {
    if (f->instruction->read_count < 3)
        return kvlangBuiltinSetError(
            f, "TypeError: array.slice requires array, start, end");
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: array.slice requires a write param (-> arr)");
    kvlangXvalue_t in[3];
    int n = kvlangBuiltinReadInputs(f, in, 3);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    int rc = ensure_scattered(f, base, NULL);
    if (rc != 0) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    int al = separated_len(f->kvspace, base);
    int lo = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])),
        hi = (int)kvlangScalarI64(kvlangXvalueScalar(&in[2]));
    if (lo < 0 || hi < lo || hi > al) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)",
            lo, hi, al);
    }
    for (int i = lo; i < hi; i++) {
        int64_t sc[1] = {i}, dc[1] = {i - lo};
        char *sk = kvlangBuiltinScatterKey(base, sc, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(f->kvspace, sk, &v);
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        kvlangKvPair_t p = {dk, v};
        char err[256];
        kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
        free(sk);
        free(dk);
    }
    for (int i = hi - lo; i < al; i++) {
        int64_t dc[1] = {i};
        char *dk = kvlangBuiltinScatterKey(base, dc, 1);
        char err[256];
        kvlangKvspaceDelete(f->kvspace, dk, err, sizeof err);
        free(dk);
    }
    free(base);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}
