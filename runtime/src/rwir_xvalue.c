#include "rwir_internal.h"

/* 计算多维下标的 row-major 扁平索引，越界返回 -1。 */
static int64_t flat_index(const kvlangLangtype *kx, const int64_t *idx,
                          int nidx) {
    int64_t flat = 0;
    for (int i = 0; i < nidx; i++) {
        if (idx[i] < 0 || idx[i] >= kx->dims[i])
            return -1;
        flat = flat * kx->dims[i] + idx[i];
    }
    return flat;
}

/* base kind（kx.kind 为非 NUL 终止子串）拷成 NUL 终止串，取元素字节大小；空 kind → 0。 */
static int xv_elem_size(const kvlangLangtype *kx) {
    if (!kx->kind || kx->kind_len <= 0)
        return 0;
    char kb[64];
    int kl = kx->kind_len < 63 ? kx->kind_len : 63;
    memcpy(kb, kx->kind, (size_t)kl);
    kb[kl] = 0;
    return kvlangXvalueElemSize(kb);
}

/* 读参 ri 的 head：变量走 GetHead 只读前缀（不借 body，*key=malloc'd 键），字面量数组取整块
 * 视图解 head（*key=NULL，view 指向内联/arena 数据，无需释放）。
 * 下标操作数须是 compact 数组**值**本身：Ptr（ref=1）是寻址间接层，不是数组（见 spec [[ptr]]）。
 * 返回 0 成功、-1 空/不存在、-2 是 Ptr（下标须先 `*p` 解引用）。 */
static int xv_read_head(kvlangFrame_t *f, const char *fr, int ri,
                        xv_head_t *h, char **key, kvlangXvalue_t *view) {
    kvlangXvalueZero(view);
    *key = NULL;
    char *k = kvlangBuiltinResolveReadKey(f->kvspace, fr, f->instruction->reads[ri].name,
                                          &f->instruction->reads[ri].value);
    if (k) {
        if (kvlangKvspaceGetHead(f->kvspace, k, &h->head) != 0) {
            free(k);
            return -1;
        }
        if (h->head.ref == KVSPACE_REF_PTR) {
            free(k);
            return -2;
        }
        xv_head_from_head(h);
        *key = k;
        return 0;
    }
    kvlangBuiltinResolveReadValue(f->kvspace, fr, f->instruction->reads[ri].name,
                                  &f->instruction->reads[ri].value, view);
    if (kvlangXvalueNone(view))
        return -1;
    if (kvlangXvalueIsPtr(view))
        return -2;
    xv_head_from_value(h, view);
    return 0;
}

/* 单读参 head：GetHead-only（变量）或原位视图（字面量），完毕只释放键。
 * 返回 0 并填 *h；空/不存在返回 -1（调用方给默认值）。 */
int xv_head1(kvlangFrame_t *f, xv_head_t *h) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key;
    kvlangXvalue_t view;
    int rc = xv_read_head(f, fr, 0, h, &key, &view);
    free(key);
    free(fr);
    return rc;
}

/* 读 first..first+nidx-1 的标量下标到 idx[]。 */
static void xv_read_indices(kvlangFrame_t *f, const char *fr, int first,
                            int nidx, int64_t *idx) {
    for (int i = 0; i < nidx && i < X_MAX_NDIM; i++) {
        kvlangXvalue_t iv;
        kvlangBuiltinResolveReadValue(f->kvspace, fr, f->instruction->reads[first + i].name,
                                      &f->instruction->reads[first + i].value, &iv);
        idx[i] = kvlangScalarI64(kvlangXvalueScalar(&iv));
    }
}

static bool xv_value_ready(const kvlangXvalue_t *v) {
    return !kvlangXvalueNone(v) && !kvlangXvalueKindIs(v, KVSPACE_KIND_RWIR) &&
           !kvlangXvalueKindIs(v, KVSPACE_KIND_RWFUNC);
}

static void xv_resolve_param(kvlangFrame_t *f, int ridx, kvlangXvalue_t *out) {
    const kvlangParam_t *p = &f->instruction->reads[ridx];
    if (xv_value_ready(&p->value)) {
        *out = p->value;
        return;
    }
    const char *fr = f->frame_root;
    char *owned = NULL;
    if (!fr) {
        owned = kvlangKeytreeFrameRoot(f->pc);
        fr = owned;
    }
    kvlangBuiltinResolveReadValue(f->kvspace, fr, p->name, &p->value, out);
    free(owned);
}

static void xv_read_indices_fast(kvlangFrame_t *f, int first, int nidx,
                                 int64_t *idx) {
    for (int i = 0; i < nidx && i < X_MAX_NDIM; i++) {
        kvlangXvalue_t iv;
        xv_resolve_param(f, first + i, &iv);
        if (iv.bytes && xh_len(iv.bytes) == 8 &&
            xh_storetype(iv.bytes) == KVSPACE_XVALUE_ST_SHORT &&
            xh_ptr(iv.bytes) == KVSPACE_XVALUE_PTR_INLINE &&
            iv.langtype_len == 5 && memcmp(iv.langtype, "int64", 5) == 0)
            idx[i] = (int64_t)xh_rd64(xh_body(iv.bytes));
        else
            idx[i] = kvlangScalarI64(kvlangXvalueScalar(&iv));
    }
}

int kvlangBuiltinXvAt(kvlangFrame_t *f) {
    int nidx = f->instruction->read_count - 1;
    if (nidx < 1)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.at requires array and indices");
    const kvlangXvalue_t *av = &f->instruction->reads[0].value;
    if (xv_value_ready(av)) {
        if (kvlangXvalueIsPtr(av))
            return kvlangBuiltinSetError(
                f, "TypeError: xv.at: pointer cannot be indexed — "
                   "dereference it first (*p)");
        const uint8_t *lt = (const uint8_t *)av->langtype;
        int32_t ltlen = (int32_t)av->langtype_len;
        kvlangLangtype kx;
        kvlangLangtypeParse(lt, ltlen, &kx);
        int tid = (kx.kind && kx.kind_len > 0)
                      ? kvlangLangtypeId(kx.kind, (size_t)kx.kind_len)
                      : KVLANG_LT_UNKNOWN;
        int sz = kvlangLangtypeElemSize(tid);
        if (sz <= 0 || kx.ndim == 0) {
            char ltb[256];
            xh_langtype_copy_of(av, ltb, sizeof ltb);
            return kvlangBuiltinSetError(
                f, "TypeError: xv.at requires a compact array, got %s", ltb);
        }
        if (nidx != kx.ndim) {
            return kvlangBuiltinSetError(
                f, "IndexError: xv.at: %d-dim array needs %d indices, got %d",
                kx.ndim, kx.ndim, nidx);
        }
        int64_t idx[X_MAX_NDIM];
        xv_read_indices_fast(f, 1, nidx, idx);
        int64_t flat = flat_index(&kx, idx, nidx);
        if (flat < 0)
            return kvlangBuiltinSetError(
                f, "IndexError: xv.at: index out of bounds");
        const uint8_t *elem = xh_body(av->bytes) + flat * sz;
        if (kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) {
            const char *kstr = kvlangLangtypeKind(tid);
            if (kvlangBuiltinWriteBody(f, kstr, elem, (uint32_t)sz))
                return 0;
            uint8_t sbuf[64];
            uint32_t slen =
                kvlangScalarToWire(kstr, elem, (uint32_t)sz, sbuf, sizeof sbuf);
            kvlangXvalue_t ev = {sbuf, slen};
            return kvlangBuiltinWriteResult(f, &ev);
        }
        char kb[64];
        int kl = kx.kind_len < 63 ? kx.kind_len : 63;
        memcpy(kb, kx.kind, (size_t)kl);
        kb[kl] = 0;
        kvlangXvalue_t e;
        kvlangXvalueNew(&e, kb, elem, (uint32_t)sz, 1);
        return kvlangBuiltinEmitResult(f, &e);
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    xv_head_t h;
    char *key;
    kvlangXvalue_t arr;
    int hrc = xv_read_head(f, fr, 0, &h, &key, &arr);
    if (hrc != 0) {
        free(fr);
        return kvlangBuiltinSetError(
            f, hrc == -2 ? "TypeError: xv.at: pointer cannot be indexed — "
                           "dereference it first (*p)"
                         : "TypeError: xv.at requires a compact array");
    }
    kvlangLangtype kx;
    kvlangLangtypeParse(h.langtype, h.langtype_len, &kx);
    int sz = xv_elem_size(&kx);
    if (sz <= 0 || kx.ndim == 0) {
        char ltb[sizeof h.head.langtype];
        xv_head_langtype_str(&h, ltb, sizeof ltb);
        free(key);
        free(fr);
        return kvlangBuiltinSetError(
            f, "TypeError: xv.at requires a compact array, got %s", ltb);
    }
    if (nidx != kx.ndim) {
        free(key);
        free(fr);
        return kvlangBuiltinSetError(
            f, "IndexError: xv.at: %d-dim array needs %d indices, got %d",
            kx.ndim, kx.ndim, nidx);
    }
    int64_t idx[X_MAX_NDIM];
    xv_read_indices(f, fr, 1, nidx, idx);
    free(fr);
    int64_t flat = flat_index(&kx, idx, nidx);
    if (flat < 0) {
        free(key);
        return kvlangBuiltinSetError(f, "IndexError: xv.at: index out of bounds");
    }
    char kb[64];
    int kl = kx.kind_len < 63 ? kx.kind_len : 63;
    memcpy(kb, kx.kind, (size_t)kl);
    kb[kl] = 0;
    kvlangXvalue_t e;
    if (key) {
        kvlangXvalue_t part;
        kvlangKvspaceGetPart(f->kvspace, key, (uint32_t)(h.body_offset + flat * sz),
                        (uint32_t)sz, &part);
        kvlangXvalueNew(&e, kb, part.bytes, part.length, 1);
        free(key);
    } else {
        const uint8_t *body = arr.bytes + h.body_offset;
        kvlangXvalueNew(&e, kb, body + flat * sz, (uint32_t)sz, 1);
    }
    int rc = kvlangBuiltinEmitResult(f, &e);
    return rc;
}

int kvlangBuiltinXvSet(kvlangFrame_t *f) {
    int nidx = f->instruction->read_count - 2;
    if (nidx < 1)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.set requires array, indices, value");
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.set requires a write param (-> a)");
    const char *rname = f->instruction->reads[0].name;
    const char *wname = f->instruction->writes[0].name;
    const kvlangXvalue_t *av = &f->instruction->reads[0].value;
    kvspaceKeyOff *wo =
        f->instruction->write_offs ? f->instruction->write_offs[0] : NULL;
    if (rname && rname[0] == '/' && wname && wname[0] == '/' &&
        strcmp(rname, wname) == 0 && xv_value_ready(av)) {
        int64_t idx[X_MAX_NDIM];
        xv_read_indices_fast(f, 1, nidx, idx);
        kvlangXvalue_t vv;
        xv_resolve_param(f, nidx + 1, &vv);
        bool is_ptr = kvlangXvalueIsPtr(av);
        kvlangLangtype kx;
        kvlangLangtypeParse((const uint8_t *)av->langtype,
                            (int32_t)av->langtype_len, &kx);
        int tid = (kx.kind && kx.kind_len > 0)
                      ? kvlangLangtypeId(kx.kind, (size_t)kx.kind_len)
                      : KVLANG_LT_UNKNOWN;
        int sz = kvlangLangtypeElemSize(tid);
        int64_t flat = (sz > 0 && kx.ndim && nidx == kx.ndim)
                           ? flat_index(&kx, idx, nidx)
                           : -1;
        const char *emsg =
            is_ptr ? "TypeError: xv.set: pointer cannot be indexed — "
                     "dereference it first (*p)"
            : sz <= 0 || kx.ndim == 0
                ? "TypeError: xv.set requires a compact array"
            : nidx != kx.ndim ? "IndexError: xv.set: dim/index count mismatch"
            : flat < 0        ? "IndexError: xv.set: index out of bounds"
                              : NULL;
        if (emsg)
            return kvlangBuiltinSetError(f, "%s", emsg);
        const char *ek = kvlangLangtypeKind(tid);
        char ekbuf[65];
        if (!ek || !ek[0]) {
            snprintf(ekbuf, sizeof ekbuf, "%.*s", kx.kind_len, kx.kind);
            ek = ekbuf;
        }
        const uint8_t *cvb = NULL;
        int c = 0;
        uint8_t cbuf[64];
        kvlangXvalue_t cv;
        if (vv.bytes && xh_len(vv.bytes) == 8 &&
            xh_storetype(vv.bytes) == KVSPACE_XVALUE_ST_SHORT &&
            xh_ptr(vv.bytes) == KVSPACE_XVALUE_PTR_INLINE &&
            ((tid == KVLANG_LT_INT64 && vv.langtype_len == 5 &&
              memcmp(vv.langtype, "int64", 5) == 0) ||
             (tid == KVLANG_LT_FLOAT64 && vv.langtype_len == 7 &&
              memcmp(vv.langtype, "float64", 7) == 0))) {
            cvb = xh_body(vv.bytes);
            c = 8;
        } else {
            if (kvlangCoerceAssign(f, ek, &vv, cbuf, sizeof cbuf, &cv) != 0)
                return -1;
            cvb = xh_body_of(&cv);
            int32_t cvblen = xh_content_len_of(&cv);
            c = cvblen < sz ? cvblen : sz;
        }
        uint32_t off = (uint32_t)(KVSPACE_XVALUE_HEAD_LEN + flat * sz);
        char err[256];
        if (wo && wo->resolved &&
            kvspaceSetPartByRef(f->kvspace->handle, &wo->off, wo->key, off, cvb,
                                (uint32_t)c, err, sizeof err) == 0) {
            kvlangBuiltinNextPc(f);
            return 0;
        }
        kvlangKvspaceSetPart(f->kvspace, wname, off, cvb, (uint32_t)c, err,
                             sizeof err);
        if (wo && !wo->resolved &&
            kvspaceResolveRef(f->kvspace->handle, wo->key, &wo->off) == 0)
            wo->resolved = true;
        kvlangBuiltinNextPc(f);
        return 0;
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *rk = kvlangBuiltinResolveReadKey(f->kvspace, fr, f->instruction->reads[0].name,
                                           &f->instruction->reads[0].value);
    char *wk =
        kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
    int64_t idx[X_MAX_NDIM];
    xv_read_indices(f, fr, 1, nidx, idx);
    kvlangXvalue_t vv;
    kvlangBuiltinResolveReadValue(f->kvspace, fr, f->instruction->reads[nidx + 1].name,
                                  &f->instruction->reads[nidx + 1].value, &vv);
    const uint8_t *vb = xh_body_of(&vv);
    int32_t vblen = xh_content_len_of(&vv);

    kvspaceHead_t h;
    if (rk && wk && strcmp(rk, wk) == 0 &&
        kvlangKvspaceGetHead(f->kvspace, wk, &h) == 0) {
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, h.langtype_len, &kx);
        int sz = xv_elem_size(&kx);
        int64_t flat = (sz > 0 && kx.ndim && nidx == kx.ndim)
                           ? flat_index(&kx, idx, nidx)
                           : -1;
        const char *emsg =
            h.ref == KVSPACE_REF_PTR
                ? "TypeError: xv.set: pointer cannot be indexed — "
                  "dereference it first (*p)"
            : sz <= 0 || kx.ndim == 0
                ? "TypeError: xv.set requires a compact array"
            : nidx != kx.ndim ? "IndexError: xv.set: dim/index count mismatch"
            : flat < 0        ? "IndexError: xv.set: index out of bounds"
                              : NULL;
        int rc = 0;
        if (emsg)
            rc = kvlangBuiltinSetError(f, "%s", emsg);
        else {
            char ek[65];
            snprintf(ek, sizeof ek, "%.*s", kx.kind_len, kx.kind);
            uint8_t cbuf[64];
            kvlangXvalue_t cv;
            if (kvlangCoerceAssign(f, ek, &vv, cbuf, sizeof cbuf, &cv) != 0)
                rc = -1;
            else {
                const uint8_t *cvb = xh_body_of(&cv);
                int32_t cvblen = xh_content_len_of(&cv);
                int c = cvblen < sz ? cvblen : sz;
                char err[256];
                kvlangKvspaceSetPart(f->kvspace, wk,
                                     (uint32_t)(h.body_offset + flat * sz), cvb,
                                     (uint32_t)c, err, sizeof err);
                kvlangBuiltinNextPc(f);
            }
        }
        free(rk);
        free(wk);
        free(fr);
        return rc;
    }
    free(rk);
    free(wk);

    kvlangXvalue_t arr;
    kvlangBuiltinResolveReadValue(f->kvspace, fr, f->instruction->reads[0].name,
                                  &f->instruction->reads[0].value, &arr);
    free(fr);
    const char *k = kvlangXvalueKind(&arr);
    int sz = kvlangXvalueElemSize(k);
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype_of(&arr), (int32_t)xh_langtype_len_of(&arr), &kx);
    const char *emsg =
        kvlangXvalueIsPtr(&arr)
            ? "TypeError: xv.set: pointer cannot be indexed — "
              "dereference it first (*p)"
        : sz <= 0 || kx.ndim == 0 ? "TypeError: xv.set requires a compact array"
        : nidx != kx.ndim ? "IndexError: xv.set: dim/index count mismatch"
                          : NULL;
    int64_t flat = emsg ? -1 : flat_index(&kx, idx, nidx);
    if (!emsg && flat < 0)
        emsg = "IndexError: xv.set: index out of bounds";
    if (emsg) {
        return kvlangBuiltinSetError(f, "%s", emsg);
    }
    uint8_t cbuf[64];
    kvlangXvalue_t cv;
    if (kvlangCoerceAssign(f, k, &vv, cbuf, sizeof cbuf, &cv) != 0)
        return -1;
    int32_t ablen = xh_content_len_of(&arr);
    uint8_t *nb = malloc((size_t)ablen);
    memcpy(nb, xh_body_of(&arr), (size_t)ablen);
    int32_t cvblen = xh_content_len_of(&cv);
    int c = cvblen < sz ? cvblen : sz;
    memcpy(nb + flat * sz, xh_body_of(&cv), (size_t)c);
    kvlangXvalue_t nv;
    kvlangXvalueNewDims(&nv, k, nb, (uint32_t)ablen, kx.dims, kx.ndim);
    int rc = kvlangBuiltinEmitResult(f, &nv);
    free(nb);
    return rc;
}

int kvlangBuiltinXvReshape(kvlangFrame_t *f) {
    int ndims = f->instruction->read_count - 1;
    if (ndims < 1)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reshape requires array and >=1 dims");
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reshape requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(k) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reshape requires a compact array, got %s", k);
    }
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype_of(&in[0]), (int32_t)xh_langtype_len_of(&in[0]), &kx);
    if (kx.ndim < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reshape requires a compact array, got scalar %s",
            k);
    }
    if (ndims > X_MAX_NDIM) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "IndexError: xv.reshape: at most %d dims, got %d", X_MAX_NDIM,
            ndims);
    }
    int32_t dims[X_MAX_NDIM];
    int64_t numel = 1;
    for (int i = 0; i < ndims; i++) {
        dims[i] = (int32_t)kvlangScalarI64(kvlangXvalueScalar(&in[i + 1]));
        if (dims[i] < 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetError(
                f, "IndexError: xv.reshape: negative dim %d", dims[i]);
        }
        numel *= dims[i];
    }
    if (numel != kx.array_len) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "IndexError: xv.reshape: cannot reshape %d elements into %lld",
            kx.array_len, (long long)numel);
    }
    const uint8_t *body = xh_body_of(&in[0]);
    kvlangXvalue_t nv;
    kvlangXvalueNewDims(&nv, k, body, (uint32_t)xh_content_len_of(&in[0]), dims,
                           ndims);
    int rc = kvlangBuiltinEmitResult(f, &nv);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·reinterpret(arr, langtype) -> a：body 字节原样，整个 langtype 换成传入的（kind+dims 一起），不做校验。 */
int kvlangBuiltinXvReinterpret(kvlangFrame_t *f) {
    if (f->instruction->read_count < 2)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reinterpret requires array and langtype");
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.reinterpret requires a write param (-> a)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *ke = kvlangXvalueValueString(&in[1]);
    kvlangLangtype nkx;
    kvlangLangtypeParse((const uint8_t *)ke, (int32_t)strlen(ke), &nkx);
    const uint8_t *body = xh_body_of(&in[0]);
    int32_t blen = xh_content_len_of(&in[0]);
    /* 动态 "[]kind"（parse 得 ndim0 但带方括号）：按 body 字节数补出一维长度，与落盘数组表示一致。 */
    int32_t ndim = nkx.ndim;
    if (nkx.ndim == 0 && strchr(ke, '[')) {
        int32_t es = kvlangXvalueElemSize(nkx.kind);
        nkx.dims[0] = es > 0 ? (int32_t)(blen / es) : (int32_t)blen;
        ndim = 1;
    }
    kvlangXvalue_t nv;
    kvlangXvalueNewDims(&nv, nkx.kind, body, (uint32_t)blen, nkx.dims, ndim);
    int rc = kvlangBuiltinEmitResult(f, &nv);
    free(ke);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·langtype(v) -> s：返回 v 的 head langtype 串（含 ref 前缀与 [dims]），作为字符串。 */
int kvlangBuiltinXvLangtype(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.langtype requires a write param (-> s)");
    xv_head_t h;
    char ke[sizeof h.head.langtype] = "";
    if (xv_head1(f, &h) == 0)
        xv_head_langtype_str(&h, ke, sizeof ke);
    kvlangXvalue_t r;
    kvlangXvalueNewCharUtf8(&r, ke);
    int rc = kvlangBuiltinEmitResult(f, &r);
    return rc;
}

/* xv·bodylen(v) -> n：返回 v 的 body 字节数（int64）。 */
int kvlangBuiltinXvBodyLen(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.bodylen requires a write param (-> n)");
    xv_head_t h;
    int64_t bl = 0;
    if (xv_head1(f, &h) == 0)
        bl = h.body_len;
    int rc = kvlangBuiltinEmitInt64(f, bl);
    return rc;
}

/* 写第 i 个写参槽并释放构造值（多输出用；不接受借用视图）。 */
static void xv_write_slot(kvlangFrame_t *f, int i, kvlangXvalue_t *v) {
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) {
        owned = kvlangKeytreeFrameRoot(f->pc);
        fr = owned;
    }
    char *key = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[i].name);
    kvlangKvPair_t pair = {key, *v};
    char err[256];
    kvlangKvspaceSet(f->kvspace, &pair, 1, err, sizeof err);
    kvlangXvalueFree(v);
    free(key);
    free(owned);
}

/* xv·parselangtype(v) -> (kind[, ndim])：解析 v 的 langtype（三轴正交的第三轴）。
 *   kind —— 剥掉前导 [dims] 与 "def " 前缀后的基名：char / char/utf8 / float64 /
 *           rwfunc / rwir / langtype / stringkeymap / index …
 *   ndim —— 维数（可省：只绑一个写参时不出）
 * 用途：按类型给 KV 成员分类（如 def langtype = 形参槽、def rwir = 路由头），
 *       调用方不必对 langtype 串做字符串手术。 */
int kvlangBuiltinXvParseLangtype(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: xv.parselangtype requires a write param (-> kind[, ndim])");
    xv_head_t h;
    const uint8_t *lt = (const uint8_t *)"";
    int32_t ltlen = 0;
    if (xv_head1(f, &h) == 0) {
        lt = h.langtype;
        ltlen = h.langtype_len;
    }
    kvlangLangtype p;
    kvlangLangtypeParse(lt, ltlen, &p);
    const char *k = p.kind ? p.kind : "";
    int32_t kl = p.kind_len;
    if (kl >= 4 && strncmp(k, "def ", 4) == 0) {
        k += 4;
        kl -= 4;
    }
    char buf[256];
    if (kl < 0)
        kl = 0;
    if ((size_t)kl >= sizeof buf)
        kl = (int32_t)sizeof buf - 1;
    memcpy(buf, k, (size_t)kl);
    buf[kl] = 0;
    kvlangXvalue_t vk;
    kvlangXvalueNewCharUtf8(&vk, buf);
    xv_write_slot(f, 0, &vk);
    if (f->instruction->write_count > 1) {
        kvlangXvalue_t vn;
        kvlangXvalueNewInt64(&vn, p.ndim);
        xv_write_slot(f, 1, &vn);
    }
    kvlangBuiltinNextPc(f);
    return 0;
}
