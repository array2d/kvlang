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
    kvlangXvalueNew(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static int separated_len(kvspace_t *kv, const char *base) {
    /* 一次 ListLen 取代逐 key GetOne 探测（数组元素为 base·[i]）。 */
    char *dir = kvlangKeytreeMember(base, "");
    int count = 0;
    if (kvlangKvspaceListLen(kv, dir, false, false, &count) != 0)
        count = 0;
    free(dir);
    return count;
}

static int64_t separated_next(kvspace_t *kv, const char *base) {
    char *dir = kvlangKeytreeMember(base, "");
    int count = 0;
    /* memindex 的坐标段按 cmp_coord 升序（spec 07），故末元素即最大坐标：
     * next = max_index + 1。两次后端调用，且天然覆盖用户直写 [100] 造成的洞。 */
    if (kvlangKvspaceListLen(kv, dir, false, false, &count) == 0 && count > 0) {
        char *last = NULL;
        if (kvlangKvspaceListAt(kv, dir, false, false, count - 1, &last) == 0 &&
            last) {
            int64_t next = -1;
            if (last[0] == '[') {
                char *end = NULL;
                long long idx = strtoll(last + 1, &end, 10);
                if (end && *end == ']' && end[1] == 0 && idx >= 0 &&
                    idx < INT64_MAX)
                    next = (int64_t)idx + 1;
            }
            free(last);
            if (next >= 0) {
                free(dir);
                return next;
            }
        }
    }
    char **names = NULL;
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
    if (count > 0) {
        /* 一次多键 Del 取代 N 次逐键删除（成员均为标量，无子树）。 */
        char **keys = malloc(sizeof *keys * (size_t)count);
        for (int i = 0; i < count; i++) {
            keys[i] = kvlangStringFormat("%s%s", prefix, names[i]);
            kvlangBodyInvalidate(keys[i]);
        }
        if (kvspaceDel(f->kvspace->handle, (const char *const *)keys, (uint32_t)count,
                       err, sizeof err) != 0)
            rc = kvlangBuiltinSetError(f, "%s", err);
        for (int i = 0; i < count; i++)
            free(keys[i]);
        free(keys);
    }
    for (int i = 0; i < count; i++)
        free(names[i]);
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
    /* base 即将被 marker 覆盖；shm 的读是 mmap 视图，写会使其失效，先显式 copy。 */
    kvlangXvalueCopy(&arr);
    bool missing = kvlangXvalueNone(&arr);
    const char *kind = missing ? fallback_kind : kvlangXvalueKind(&arr);
    if (!kind || kvlangXvalueElemSize(kind) <= 0) {
        int rc = missing ? kvlangBuiltinSetError(f, "array: missing element type")
                         : 0;
        kvlangXvalueFree(&arr);
        return rc;
    }
    char ty[256];
    snprintf(ty, sizeof ty, "[int64]%s%s", MEMBER_SEP, kind);
    kvlangXvalue_t mark;
    kvlangBuiltinMapMarker(&mark, ty);
    kvlangKvPair_t head = {(char *)base, mark};
    char err[256];
    int rc = kvlangKvspaceSet(f->kvspace, &head, 1, err, sizeof err);
    kvlangXvalueFree(&mark);
    if (rc != 0) {
        int e = kvlangBuiltinSetError(f, "%s", err);
        kvlangXvalueFree(&arr);
        return e;
    }
    if (missing) {
        kvlangXvalueFree(&arr);
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
        kvlangXvalueFree(&e);
        free(k);
        if (rc != 0) {
            int err_rc = kvlangBuiltinSetError(f, "%s", err);
            kvlangXvalueFree(&arr);
            return err_rc;
        }
    }
    kvlangXvalueFree(&arr);
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
    char dbuf[64];
    char kbuf[64];
    const char *kind =
        kvlangDeclElemKind(f->instruction->writes[0].type, dbuf, sizeof dbuf);
    if (!kind) {
        snprintf(kbuf, sizeof kbuf, "%s", kvlangXvalueKind(&in[0]));
        kind = kbuf;
    }
    if (kvlangXvalueElemSize(kind) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "array: unsupported element kind %s",
                                   kind);
    }
    uint8_t cbuf[64][64];
    kvlangXvalue_t cv[64];
    if (kvlangCoerceAll(f, kind, in, n, cbuf, cv) != 0) {
        kvlangBuiltinFreeInputs(in, n);
        return -1;
    }
    kvlangXvalue_t arr;
    pack_typed_array(kind, cv, n, &arr);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    free(fr);
    kvlangKvPair_t p = {key, arr};
    char err[256];
    kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    kvlangXvalueFree(&arr);
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
        char ek[65];
        snprintf(ek, sizeof ek, "%.*s", kx.kind_len, kx.kind);
        uint8_t cbuf[64];
        kvlangXvalue_t cv;
        if (kvlangCoerceAssign(f, ek, &in[1], cbuf, sizeof cbuf, &cv) != 0) {
            free(ke);
            kvlangBuiltinFreeInputs(in, n);
            return -1;
        }
        kvlangScalar_t s = kvlangXvalueScalar(&cv);
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
    kvlangXvalueNewDims(&arr, kx.kind, raw, (uint32_t)total, kx.dims,
                           kx.ndim);
    free(raw);
    int rc = kvlangBuiltinEmitResult(f, &arr);
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
    kvlangXvalueFree(&mark);
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
        kvlangXvalueFree(&e);
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
    /* 源必须是散 key 形态（map 标记）。已经是 compact 值（数组/字符串）就没有目录
     * 可打包：显式拒绝，不静默空转。 */
    kvlangXvalue_t sin[1];
    int sn = kvlangBuiltinReadInputs(f, sin, 1);
    if (sn >= 1 && !kvlangXvalueNone(&sin[0]) &&
        !kvlangKindIsMap(kvlangXvalueKind(&sin[0]))) {
        char lt[256];
        xh_langtype_copy_of(&sin[0], lt, sizeof lt);
        return kvlangBuiltinSetError(
            f, "TypeError: array.compact requires a scattered array, got %s",
            lt);
    }
    const char *src_name = f->instruction->reads[0].name;
    if (!src_name)
        return kvlangBuiltinSetError(
            f, "TypeError: array.compact requires a scattered array variable");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *src = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, src_name);
    /* ListLen 给出元素数，逐元素读；省掉结尾那一次“再探一个空槽”的后端调用。 */
    char *cdir = kvlangKeytreeMember(src, "");
    int ccnt = 0;
    kvlangKvspaceListLen(f->kvspace, cdir, false, false, &ccnt);
    free(cdir);
    kvlangXvalue_t elems[1024];
    int n = 0;
    for (int i = 0; i < ccnt && n < 1024; i++) {
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
    char dbuf[64];
    const char *kind =
        kvlangDeclElemKind(f->instruction->writes[0].type, dbuf, sizeof dbuf);
    int rc = ensure_scattered(
        f, base, kind ? kind : (n >= 2 ? kvlangXvalueKind(&in[1]) : NULL));
    if (rc != 0) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    uint8_t cbuf[64];
    kvlangXvalue_t ev = in[1];
    if (kind && kvlangCoerceAssign(f, kind, &in[1], cbuf, sizeof cbuf, &ev) !=
                    0) {
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return -1;
    }
    int64_t c[1] = {separated_next(f->kvspace, base)};
    char *k = kvlangBuiltinScatterKey(base, c, 1);
    kvlangKvPair_t p = {k, ev}; /* nr >= 2 已由上面拦下，ReadInputs(cap=2) 必返 2 */
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

/* 一维坐标段 "[i]" 解析；非该形式（多维/非坐标）返回 -1。 */
static int64_t coord_of_name(const char *name) {
    if (!name || name[0] != '[')
        return -1;
    char *end = NULL;
    long long idx = strtoll(name + 1, &end, 10);
    if (!end || *end != ']' || end[1] != 0 || idx < 0)
        return -1;
    return (int64_t)idx;
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
    int lo = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])),
        hi = (int)kvlangScalarI64(kvlangXvalueScalar(&in[2]));
    const char *rkey = f->instruction->reads[0].name;
    bool compact_src = rkey && strcmp(rkey, base) == 0 &&
                       kvlangXvalueElemSize(kvlangXvalueKind(&in[0])) > 0;
    char *dir = NULL, **names = NULL;
    int al = 0;
    bool rank_path = false;
    if (compact_src) {
        al = kvlangXvalueArrayLen(&in[0]);
    } else {
        dir = kvlangKeytreeMember(base, "");
        if (kvlangKvspaceListLen(f->kvspace, dir, false, false, &al) != 0)
            al = 0;
        if (al > 0) {
            /* 末元素坐标 == al-1 ⇒ dense（坐标连续），可按坐标切片；
             * 否则带洞，需枚举名字走元素序列路径。 */
            char *last = NULL;
            if (kvlangKvspaceListAt(f->kvspace, dir, false, false, al - 1,
                                    &last) == 0 &&
                last) {
                rank_path = coord_of_name(last) != (int64_t)al - 1;
                free(last);
            } else {
                rank_path = true;
            }
        }
        if (rank_path &&
            kvlangKvspaceList(f->kvspace, dir, false, false, &names, &al) != 0)
            al = 0;
    }
    if (lo < 0 || hi < lo || hi > al) {
        int e = kvlangBuiltinSetError(
            f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)",
            lo, hi, al);
        for (int i = 0; i < al; i++)
            free(names ? names[i] : NULL);
        free(names);
        free(dir);
        free(base);
        free(fr);
        kvlangBuiltinFreeInputs(in, n);
        return e;
    }
    char err[256];
    if (rank_path) {
        /* 带洞数组：洞不是元素。按 memindex 排序后的元素序列取 [lo,hi)，
         * 写连续坐标 [0,hi-lo)；源中坐标 >= hi-lo 的旧元素删除。 */
        int cut = al;
        for (int r = 0; r < al; r++) {
            int64_t c = coord_of_name(names[r]);
            if (c < 0 || c >= (int64_t)(hi - lo)) {
                cut = r;
                break;
            }
        }
        for (int r = lo; r < hi; r++) {
            char *sk = kvlangStringFormat("%s%s", dir, names[r]);
            kvlangXvalue_t v;
            kvlangXvalueZero(&v);
            kvlangKvspaceGetOne(f->kvspace, sk, &v);
            int64_t dc[1] = {r - lo};
            char *dk = kvlangBuiltinScatterKey(base, dc, 1);
            kvlangKvPair_t p = {dk, v};
            kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
            free(sk);
            free(dk);
        }
        if (cut < al) {
            int tn = al - cut;
            char **tk = malloc(sizeof *tk * (size_t)tn);
            for (int i = 0; i < tn; i++) {
                tk[i] = kvlangStringFormat("%s%s", dir, names[cut + i]);
                kvlangBodyInvalidate(tk[i]);
            }
            kvspaceDel(f->kvspace->handle, (const char *const *)tk,
                       (uint32_t)tn, err, sizeof err);
            for (int i = 0; i < tn; i++)
                free(tk[i]);
            free(tk);
        }
    } else {
        for (int i = lo; i < hi; i++) {
            int64_t sc[1] = {i}, dc[1] = {i - lo};
            char *sk = kvlangBuiltinScatterKey(base, sc, 1);
            kvlangXvalue_t v;
            kvlangXvalueZero(&v);
            kvlangKvspaceGetOne(f->kvspace, sk, &v);
            char *dk = kvlangBuiltinScatterKey(base, dc, 1);
            kvlangKvPair_t p = {dk, v};
            kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
            free(sk);
            free(dk);
        }
        int tail = al - (hi - lo);
        if (tail > 0) {
            char **dk = malloc(sizeof *dk * (size_t)tail);
            for (int i = 0; i < tail; i++) {
                int64_t dc[1] = {hi - lo + i};
                dk[i] = kvlangBuiltinScatterKey(base, dc, 1);
                kvlangBodyInvalidate(dk[i]);
            }
            kvspaceDel(f->kvspace->handle, (const char *const *)dk,
                       (uint32_t)tail, err, sizeof err);
            for (int i = 0; i < tail; i++)
                free(dk[i]);
            free(dk);
        }
    }
    for (int i = 0; i < al; i++)
        free(names ? names[i] : NULL);
    free(names);
    free(dir);
    free(base);
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}
