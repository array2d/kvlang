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

/* 读参 ri 的 head：变量走 GetHead 只读前缀（不借 body，*key=malloc'd 键），字面量数组借整块
 * 解 head（*key=NULL，*borrow 持整块，调用方 kvlangXvalueFree）。返回 0 成功、非 0 空/不存在。 */
static int xv_read_head(kvlangFrame_t *f, const char *fr, int ri,
                        kvspaceHead_t *h, char **key, kvlangXvalue_t *borrow) {
    kvlangXvalueZero(borrow);
    *key = NULL;
    char *k = kvlangBuiltinResolveReadKey(f->kv, fr, f->inst->reads[ri].name,
                                          &f->inst->reads[ri].val);
    if (k) {
        if (kvlangKvGetHead(f->kv, k, h) != 0) {
            free(k);
            return -1;
        }
        *key = k;
        return 0;
    }
    kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[ri].name,
                                  &f->inst->reads[ri].val, borrow);
    if (kvlangXvalueNone(borrow))
        return -1;
    return kvspaceDecodeHead(borrow->data, borrow->len, h) == 0 ? 0 : -1;
}

/* 单读参 head：GetHead-only（变量）或借块解码（字面量），完毕即释放借块与键。
 * 返回 0 并填 *h；空/不存在返回 -1（调用方给默认值）。 */
int xv_head1(kvlangFrame_t *f, kvspaceHead_t *h) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key;
    kvlangXvalue_t borrow;
    int rc = xv_read_head(f, fr, 0, h, &key, &borrow);
    free(key);
    kvlangXvalueFree(&borrow);
    free(fr);
    return rc;
}

/* 读 first..first+nidx-1 的标量下标到 idx[]。 */
static void xv_read_indices(kvlangFrame_t *f, const char *fr, int first,
                            int nidx, int64_t *idx) {
    for (int i = 0; i < nidx && i < X_MAX_NDIM; i++) {
        kvlangXvalue_t iv;
        kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[first + i].name,
                                      &f->inst->reads[first + i].val, &iv);
        idx[i] = kvlangScalarI64(kvlangXvalueScalar(&iv));
        kvlangXvalueFree(&iv);
    }
}

/* 分片读单元素：GetHead 定位 + GetPart 只借该元素的 [off, off+sz) 字节，不借整块。 */
int kvlangBuiltinXvAt(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 1;
    if (nidx < 1)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.at requires array and indices");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    kvspaceHead_t h;
    char *key;
    kvlangXvalue_t arr;
    if (xv_read_head(f, fr, 0, &h, &key, &arr) != 0) {
        free(fr);
        return kvlangBuiltinSetErr(f,
                                   "TypeError: xv.at requires a compact array");
    }
    kvlangLangtype kx;
    kvlangLangtypeParse(h.langtype, &kx);
    int sz = xv_elem_size(&kx);
    if (sz <= 0 || kx.ndim == 0) {
        free(key);
        kvlangXvalueFree(&arr);
        free(fr);
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.at requires a compact array, got %s", h.langtype);
    }
    if (nidx != kx.ndim) {
        free(key);
        kvlangXvalueFree(&arr);
        free(fr);
        return kvlangBuiltinSetErr(
            f, "IndexError: xv.at: %d-dim array needs %d indices, got %d",
            kx.ndim, kx.ndim, nidx);
    }
    int64_t idx[X_MAX_NDIM];
    xv_read_indices(f, fr, 1, nidx, idx);
    free(fr);
    int64_t flat = flat_index(&kx, idx, nidx);
    if (flat < 0) {
        free(key);
        kvlangXvalueFree(&arr);
        return kvlangBuiltinSetErr(f, "IndexError: xv.at: index out of bounds");
    }
    char kb[64];
    int kl = kx.kind_len < 63 ? kx.kind_len : 63;
    memcpy(kb, kx.kind, (size_t)kl);
    kb[kl] = 0;
    kvlangXvalue_t e;
    if (key) {
        kvlangXvalue_t part;
        kvlangKvGetPart(f->kv, key, (uint32_t)(h.body_offset + flat * sz),
                        (uint32_t)sz, &part);
        kvlangXvalueNewTlv(&e, kb, part.data, part.len, 1);
        kvlangXvalueFree(&part);
        free(key);
    } else {
        const uint8_t *body = arr.data + h.body_offset;
        kvlangXvalueNewTlv(&e, kb, body + flat * sz, (uint32_t)sz, 1);
    }
    kvlangXvalueFree(&arr);
    int rc = kvlangBuiltinWriteResult(f, &e);
    kvlangXvalueFree(&e);
    return rc;
}

/* 分片写单元素：写目标==源变量且已存在 → GetHead 定位 + SetPart 就地写该元素字节（O(1)，
 * 不重建整块）；源≠目标 / 字面量 / 缺失 → 回退借整块、拷贝、改元素、整体重建。 */
int kvlangBuiltinXvSet(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 2;
    if (nidx < 1)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.set requires array, indices, value");
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.set requires a write param (-> a)");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *rk = kvlangBuiltinResolveReadKey(f->kv, fr, f->inst->reads[0].name,
                                           &f->inst->reads[0].val);
    char *wk =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    int64_t idx[X_MAX_NDIM];
    xv_read_indices(f, fr, 1, nidx, idx);
    kvlangXvalue_t vv;
    kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[nidx + 1].name,
                                  &f->inst->reads[nidx + 1].val, &vv);
    kvspaceHead_t vh;
    kvspaceDecodeHead(vv.data, vv.len, &vh);
    const uint8_t *vb = vv.data + vh.body_offset;

    kvspaceHead_t h;
    if (rk && wk && strcmp(rk, wk) == 0 &&
        kvlangKvGetHead(f->kv, wk, &h) == 0) {
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, &kx);
        int sz = xv_elem_size(&kx);
        int64_t flat = (sz > 0 && kx.ndim && nidx == kx.ndim)
                           ? flat_index(&kx, idx, nidx)
                           : -1;
        const char *emsg =
            sz <= 0 || kx.ndim == 0
                ? "TypeError: xv.set requires a compact array"
            : nidx != kx.ndim ? "IndexError: xv.set: dim/index count mismatch"
            : flat < 0        ? "IndexError: xv.set: index out of bounds"
                              : NULL;
        int rc = 0;
        if (emsg)
            rc = kvlangBuiltinSetErr(f, "%s", emsg);
        else {
            int c = vh.body_len < sz ? vh.body_len : sz;
            char err[256];
            kvlangKvSetPart(f->kv, wk, (uint32_t)(h.body_offset + flat * sz),
                            vb, (uint32_t)c, err, sizeof err);
            kvlangBuiltinNextPc(f);
        }
        free(rk);
        free(wk);
        free(fr);
        kvlangXvalueFree(&vv);
        return rc;
    }
    free(rk);
    free(wk);

    kvlangXvalue_t arr;
    kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[0].name,
                                  &f->inst->reads[0].val, &arr);
    free(fr);
    const char *k = kvlangXvalueKind(&arr);
    int sz = kvlangXvalueElemSize(k);
    kvspaceHead_t ah;
    kvspaceDecodeHead(arr.data, arr.len, &ah);
    kvlangLangtype kx;
    kvlangLangtypeParse(ah.langtype, &kx);
    const char *emsg =
        sz <= 0 || kx.ndim == 0 ? "TypeError: xv.set requires a compact array"
        : nidx != kx.ndim       ? "IndexError: xv.set: dim/index count mismatch"
                                : NULL;
    int64_t flat = emsg ? -1 : flat_index(&kx, idx, nidx);
    if (!emsg && flat < 0)
        emsg = "IndexError: xv.set: index out of bounds";
    if (emsg) {
        kvlangXvalueFree(&arr);
        kvlangXvalueFree(&vv);
        return kvlangBuiltinSetErr(f, "%s", emsg);
    }
    uint8_t *nb = malloc((size_t)ah.body_len);
    memcpy(nb, arr.data + ah.body_offset, (size_t)ah.body_len);
    int c = vh.body_len < sz ? vh.body_len : sz;
    memcpy(nb + flat * sz, vb, (size_t)c);
    kvlangXvalue_t nv;
    kvlangXvalueNewTlvDims(&nv, k, nb, (uint32_t)ah.body_len, kx.dims, kx.ndim);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv);
    free(nb);
    kvlangXvalueFree(&arr);
    kvlangXvalueFree(&vv);
    return rc;
}

int kvlangBuiltinXvReshape(kvlangFrame_t *f) {
    int ndims = f->inst->nr - 1;
    if (ndims < 1)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reshape requires array and >=1 dims");
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reshape requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(k) <= 0) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reshape requires a compact array, got %s", k);
    }
    kvspaceHead_t h;
    kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlangLangtype kx;
    kvlangLangtypeParse(h.langtype, &kx);
    if (kx.ndim < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reshape requires a compact array, got scalar %s",
            k);
    }
    if (ndims > X_MAX_NDIM) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "IndexError: xv.reshape: at most %d dims, got %d", X_MAX_NDIM,
            ndims);
    }
    int32_t dims[X_MAX_NDIM];
    int64_t numel = 1;
    for (int i = 0; i < ndims; i++) {
        dims[i] = (int32_t)kvlangScalarI64(kvlangXvalueScalar(&in[i + 1]));
        if (dims[i] < 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetErr(
                f, "IndexError: xv.reshape: negative dim %d", dims[i]);
        }
        numel *= dims[i];
    }
    if (numel != kx.array_len) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "IndexError: xv.reshape: cannot reshape %d elements into %lld",
            kx.array_len, (long long)numel);
    }
    const uint8_t *body = in[0].data + h.body_offset;
    kvlangXvalue_t nv;
    kvlangXvalueNewTlvDims(&nv, k, body, (uint32_t)h.body_len, dims, ndims);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·reinterpret(arr, langtype) -> a：body 字节原样，整个 langtype 换成传入的（kind+dims 一起），不做校验。 */
int kvlangBuiltinXvReinterpret(kvlangFrame_t *f) {
    if (f->inst->nr < 2)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reinterpret requires array and langtype");
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.reinterpret requires a write param (-> a)");
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *ke = kvlangXvalueValueString(&in[1]);
    kvlangLangtype nkx;
    kvlangLangtypeParse((const uint8_t *)ke, &nkx);
    kvspaceHead_t h;
    kvspaceDecodeHead(in[0].data, in[0].len, &h);
    const uint8_t *body = in[0].data + h.body_offset;
    /* 动态 "[]kind"（parse 得 ndim0 但带方括号）：按 body 字节数补出一维长度，与落盘数组表示一致。 */
    int32_t ndim = nkx.ndim;
    if (nkx.ndim == 0 && strchr(ke, '[')) {
        int32_t es = kvlangXvalueElemSize(nkx.kind);
        nkx.dims[0] = es > 0 ? (int32_t)(h.body_len / es) : (int32_t)h.body_len;
        ndim = 1;
    }
    kvlangXvalue_t nv;
    kvlangXvalueNewTlvDims(&nv, nkx.kind, body, (uint32_t)h.body_len, nkx.dims,
                           ndim);
    int rc = kvlangBuiltinWriteResult(f, &nv);
    kvlangXvalueFree(&nv);
    free(ke);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* xv·langtype(v) -> s：返回 v 的 head langtype 串（含 ref 前缀与 [dims]），作为字符串。 */
int kvlangBuiltinXvLangtype(kvlangFrame_t *f) {
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.langtype requires a write param (-> s)");
    kvspaceHead_t h;
    const char *ke = "";
    if (xv_head1(f, &h) == 0)
        ke = (const char *)h.langtype;
    kvlangXvalue_t r;
    kvlangXvalueNewCharUtf8(&r, ke);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}

/* xv·bodylen(v) -> n：返回 v 的 body 字节数（int64）。 */
int kvlangBuiltinXvBodylen(kvlangFrame_t *f) {
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(
            f, "TypeError: xv.bodylen requires a write param (-> n)");
    kvspaceHead_t h;
    int64_t bl = 0;
    if (xv_head1(f, &h) == 0)
        bl = h.body_len;
    kvlangXvalue_t r;
    kvlangXvalueNewInt64(&r, bl);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}

