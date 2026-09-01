#include "runtime_internal.h"

/* 后端无关的 XValue TLV 编解码（对齐 kvspace-durable/kvspace-c 的 kindexp TLV）。
 * xval.data 一律 malloc（free 释放），后端在 kv.c 层负责拷贝。 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

void kvlangXvalueFree(kvlangXvalue_t *v) {
    free(v->data);
    v->data = NULL; v->len = 0;
}

void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data, uint32_t len) {
    v->data = data; v->len = len;
}

/* 解析 kindexpr 内容 → (ref, dims, base kind)。kindexpr 为 NUL 终止串。 */
void kvlang_kindexpr_parse(const uint8_t *kx, kvlang_kindexpr_t *out) {
    memset(out, 0, sizeof(*out));
    if (!kx) return;
    int32_t i = 0;
    if (kx[0] == '*') { out->ref = 1; i = 1; }
    else if (kx[0] == '@') { out->ref = 2; i = 1; }
    if (kx[i] == '[') {
        i++;
        while (kx[i] != ']' && kx[i] != 0 && out->ndim < X_MAX_NDIM) {
            int32_t d = 0;
            while (kx[i] >= '0' && kx[i] <= '9') { d = d * 10 + (kx[i] - '0'); i++; }
            out->dims[out->ndim++] = d;
            if (kx[i] == ',') i++;
        }
        if (kx[i] == ']') i++;
    }
    out->kind = (const char *)(kx + i);
    out->kind_len = (int32_t)strlen((const char *)(kx + i));
    out->array_len = 1;
    for (int d = 0; d < out->ndim; d++) out->array_len *= out->dims[d];
}

/* head 编解码统一委托给链接的 kvspace .so（kvspace-c / kvspace-durable 同一 ABI），
 * runtime 不再私持 TLV head 布局，杜绝多份手写偏移不一致。 */
static int kvlangXvalueDecodeHeadRaw(const uint8_t *d, uint32_t len, kvspaceHead_t *h) {
    memset(h, 0, sizeof(*h));
    if (!d || len == 0) return -1;
    kvspaceDecodeHead(d, len, h);
    return h->kindexpr[0] ? 0 : -1;
}

/* array_len → dims：char/* 恒一维（含空串/单字符）；其余标量(≤1)=0 维、多元素=1 维。 */
static int32_t al_to_dims(const char *kind, int32_t array_len, int32_t *dims) {
    if (strncmp(kind, "char/", 5) == 0) { dims[0] = array_len < 0 ? 0 : array_len; return 1; }
    if (array_len > 1) { dims[0] = array_len; return 1; }
    return 0;
}

/* .so 分配的 TLV → 转交 runtime 所有权（统一 free 释放）。 */
static uint8_t *kvlangXvalueOwn(uint8_t *tmp, uint32_t tl, uint32_t *out_len) {
    if (!tmp) { *out_len = 0; return NULL; }
    uint8_t *buf = malloc(tl);
    memcpy(buf, tmp, tl);
    kvspaceBytesFree(tmp, tl);
    *out_len = tl;
    return buf;
}

static uint8_t *kvlangXvalueEncodeTlv(const char *kind, const uint8_t *raw,
                              uint32_t raw_len, int32_t array_len, uint32_t *out_len) {
    int32_t dims[1]; int32_t ndim = al_to_dims(kind, array_len, dims);
    uint8_t *tmp = NULL; uint32_t tl = 0;
    if (kvspaceTlvEncode(kind, raw, raw_len, dims, ndim, &tmp, &tl) != 0) { *out_len = 0; return NULL; }
    return kvlangXvalueOwn(tmp, tl, out_len);
}

int kvlangXvalueHead(const kvlangXvalue_t *v, kvspaceHead_t *h) {
    memset(h, 0, sizeof(*h));
    if (kvlangXvalueNone(v)) return -1;
    return kvlangXvalueDecodeHeadRaw(v->data, v->len, h);
}

const char *kvlangXvalueKind(const kvlangXvalue_t *v) {
    static __thread char buf[16][33];
    static __thread int idx = 0;
    char *b = buf[idx];
    idx = (idx + 1) & 15;
    if (kvlangXvalueNone(v)) { b[0] = 0; return b; }
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) < 0) { b[0] = 0; return b; }
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    int32_t kl = kx.kind_len;
    if (kl > 32) kl = 32;
    memcpy(b, kx.kind, (size_t)kl);
    b[kl] = 0;
    return b;
}

bool kvlangXvalueKindIs(const kvlangXvalue_t *v, const char *kind) {
    if (kvlangXvalueNone(v)) return kind[0] == 0;
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) < 0) return false;
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    return (size_t)kx.kind_len == strlen(kind) && memcmp(kx.kind, kind, (size_t)kx.kind_len) == 0;
}

bool kvlangXvalueIsPtr(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return false;
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) < 0) return false;
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    return kx.ref == 1;
}

int32_t kvlangXvalueArrayLen(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return 0;
    kvspaceHead_t h; kvlangXvalueDecodeHeadRaw(v->data, v->len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    return kx.array_len;
}

const uint8_t *kvlangXvalueBody(const kvlangXvalue_t *v, const kvspaceHead_t *h, int32_t *out_len) {
    int32_t off = h->body_offset, len = h->body_len;
    if (off < 0 || len < 0 || off + len > (int32_t)v->len) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = len;
    return v->data + off;
}

bool kvlangXvalueIsCharKind(const char *kind) { return strncmp(kind, "char/", 5) == 0; }
bool kvlangXvalueIsIntKind(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_INT8) == 0 || strcmp(kind, KVSPACE_KIND_INT16) == 0 ||
           strcmp(kind, KVSPACE_KIND_INT32) == 0 || strcmp(kind, KVSPACE_KIND_INT64) == 0;
}
bool kvlangXvalueIsUintKind(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_UINT8) == 0 || strcmp(kind, KVSPACE_KIND_UINT16) == 0 ||
           strcmp(kind, KVSPACE_KIND_UINT32) == 0 || strcmp(kind, KVSPACE_KIND_UINT64) == 0;
}
bool kvlangXvalueIsFloatKind(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_FLOAT32) == 0 || strcmp(kind, KVSPACE_KIND_FLOAT64) == 0;
}
bool kvlangXvalueIsNumKind(const char *kind) {
    return kvlangXvalueIsIntKind(kind) || kvlangXvalueIsUintKind(kind) || kvlangXvalueIsFloatKind(kind);
}

int32_t kvlangXvalueElemSize(const char *kind) {
    if (strcmp(kind, KVSPACE_KIND_INT8) == 0 || strcmp(kind, KVSPACE_KIND_UINT8) == 0 ||
        strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 || strcmp(kind, KVSPACE_KIND_CHAR_ASCII) == 0 ||
        strcmp(kind, KVSPACE_KIND_BOOL) == 0) return 1;
    if (strcmp(kind, KVSPACE_KIND_INT16) == 0 || strcmp(kind, KVSPACE_KIND_UINT16) == 0) return 2;
    if (strcmp(kind, KVSPACE_KIND_INT32) == 0 || strcmp(kind, KVSPACE_KIND_UINT32) == 0 ||
        strcmp(kind, KVSPACE_KIND_FLOAT32) == 0 || strcmp(kind, KVSPACE_KIND_CHAR) == 0) return 4;
    if (strcmp(kind, KVSPACE_KIND_INT64) == 0 || strcmp(kind, KVSPACE_KIND_UINT64) == 0 ||
        strcmp(kind, KVSPACE_KIND_FLOAT64) == 0 || strcmp(kind, KVSPACE_KIND_TIME) == 0 ||
        strcmp(kind, KVSPACE_KIND_DURATION) == 0) return 8;
    return 0;
}

static const uint8_t *v_body(const kvlangXvalue_t *v, kvspaceHead_t *h) {
    if (kvlangXvalueDecodeHeadRaw(v->data, v->len, h) < 0) return NULL;
    return v->data + h->body_offset;
}

int64_t kvlangXvalueAsInt64(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return 0;
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = kvlangXvalueKind(v);
    if (strcmp(k, KVSPACE_KIND_BOOL) == 0) return b[0] != 0;
    if (strcmp(k, KVSPACE_KIND_INT8) == 0) return (int8_t)b[0];
    if (strcmp(k, KVSPACE_KIND_INT16) == 0) return (int16_t)rd16(b);
    if (strcmp(k, KVSPACE_KIND_INT32) == 0) return (int32_t)rd32(b);
    if (strcmp(k, KVSPACE_KIND_INT64) == 0) return (int64_t)rd64(b);
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0) return b[0];
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0) return rd16(b);
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0) return rd32(b);
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0) return (int64_t)rd64(b);
    if (strcmp(k, KVSPACE_KIND_FLOAT32) == 0) { float f; uint32_t u = rd32(b); memcpy(&f, &u, 4); return (int64_t)f; }
    if (strcmp(k, KVSPACE_KIND_FLOAT64) == 0) { double d; uint64_t u = rd64(b); memcpy(&d, &u, 8); return (int64_t)d; }
    if (strcmp(k, KVSPACE_KIND_TIME) == 0 || strcmp(k, KVSPACE_KIND_DURATION) == 0) return (int64_t)rd64(b);
    return 0;
}

double kvlangXvalueAsFloat64(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return 0;
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = kvlangXvalueKind(v);
    if (strcmp(k, KVSPACE_KIND_FLOAT32) == 0) { float f; uint32_t u = rd32(b); memcpy(&f, &u, 4); return f; }
    if (strcmp(k, KVSPACE_KIND_FLOAT64) == 0) { double d; uint64_t u = rd64(b); memcpy(&d, &u, 8); return d; }
    return (double)kvlangXvalueAsInt64(v);
}

uint64_t kvlangXvalueAsUint64(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return 0;
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = kvlangXvalueKind(v);
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0) return b[0];
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0) return rd16(b);
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0) return rd32(b);
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0) return rd64(b);
    return (uint64_t)kvlangXvalueAsInt64(v);
}

bool kvlangXvalueAsBool(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return false;
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    return b && h.body_len > 0 && b[0] != 0;
}

uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx) {
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (idx < 0 || idx >= kx.array_len) return 0;
    return rd32(b + idx * 4);
}

/* ── UTF-8 ↔ UTF-32 ────────────────────────────────────────────────── */

static void utf8_putc(kvlangStrbuf_t *b, uint32_t cp) {
    if (cp < 0x80) kvlangStrbufPutc(b, (char)cp);
    else if (cp < 0x800) { kvlangStrbufPutc(b, (char)(0xC0 | (cp >> 6))); kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        kvlangStrbufPutc(b, (char)(0xE0 | (cp >> 12))); kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        kvlangStrbufPutc(b, (char)(0xF0 | (cp >> 18))); kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 6) & 0x3F))); kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

static uint32_t utf8_next(const char *s, size_t *i, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = p[*i];
    if (cp < 0x80) { (*i)++; return cp; }
    int n = 0;
    if ((cp & 0xE0) == 0xC0) { n = 1; cp &= 0x1F; }
    else if ((cp & 0xF0) == 0xE0) { n = 2; cp &= 0x0F; }
    else if ((cp & 0xF8) == 0xF0) { n = 3; cp &= 0x07; }
    else { (*i)++; return 0xFFFD; }
    (*i)++;
    for (int j = 0; j < n && *i < len; j++, (*i)++) cp = (cp << 6) | (p[*i] & 0x3F);
    return cp;
}

static char *utf32_to_utf8(const uint8_t *body, int32_t blen) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    for (int32_t i = 0; i + 4 <= blen; i += 4) utf8_putc(&b, rd32(body + i));
    return kvlangStrbufDetach(&b);
}

static char *strndup2(const uint8_t *p, int32_t n) {
    char *s = malloc((size_t)n + 1);
    if (s) { memcpy(s, p, (size_t)n); s[n] = 0; }
    return s;
}

char *kvlangXvaluePtrTarget(const kvlangXvalue_t *v) {
    kvspaceHead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return strdup("");
    return strndup2(b, h.body_len);
}

/* ── value_string（对齐 Go ValueString）────────────────────────────── */

static void append_num_int(kvlangStrbuf_t *b, int64_t n) { kvlangStrbufPrintf(b, "%lld", (long long)n); }
static void append_num_uint(kvlangStrbuf_t *b, uint64_t n) { kvlangStrbufPrintf(b, "%llu", (unsigned long long)n); }

char *kvlangXvalueValueString(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v)) return strdup(KVSPACE_KIND_NONE);
    kvspaceHead_t h; const uint8_t *body = v_body(v, &h);
    if (!body) return strdup(KVSPACE_KIND_NONE);
    int32_t blen = h.body_len;
    const char *k = kvlangXvalueKind(v);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);

    if (kx.ref == 1) {
        kvlangStrbuf_t b; kvlangStrbufInit(&b);
        kvlangStrbufPutn(&b, "\xE2\x86\x92", 3);
        kvlangStrbufPutn(&b, (const char *)body, (size_t)blen);
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_BOOL) == 0) return strdup(body[0] ? "true" : "false");
    if (strcmp(k, KVSPACE_KIND_INT8) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_int(&b, (int8_t)body[0]); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_INT16) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_int(&b, (int16_t)rd16(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_INT32) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_int(&b, (int32_t)rd32(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_INT64) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_int(&b, (int64_t)rd64(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_uint(&b, body[0]); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_uint(&b, rd16(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_uint(&b, rd32(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0) { kvlangStrbuf_t b; kvlangStrbufInit(&b); append_num_uint(&b, rd64(body)); return kvlangStrbufDetach(&b); }
    if (strcmp(k, KVSPACE_KIND_FLOAT32) == 0 || strcmp(k, KVSPACE_KIND_FLOAT64) == 0) {
        char tmp[64]; kvlangFormatFloat(tmp, sizeof tmp, kvlangXvalueAsFloat64(v)); return strdup(tmp);
    }
    if (strcmp(k, KVSPACE_KIND_CHAR_UTF8) == 0 || strcmp(k, KVSPACE_KIND_CHAR_ASCII) == 0) return strndup2(body, blen);
    if (strcmp(k, KVSPACE_KIND_CHAR) == 0) return utf32_to_utf8(body, blen);
    if (strcmp(k, KVSPACE_KIND_RWIR) == 0 || strcmp(k, KVSPACE_KIND_RWIR_OR_RWFUNC) == 0) return strndup2(body + (blen >= 4 ? 4 : 0), blen >= 4 ? blen - 4 : 0);
    if (strcmp(k, KVSPACE_KIND_RWFUNC) == 0) {
        kvlangStrbuf_t b; kvlangStrbufInit(&b);
        kvlangStrbufPrintf(&b, "r%d/w%d", (blen >= 2 ? rd16(body) : 0), (blen >= 4 ? rd16(body + 2) : 0));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_INDEX) == 0) {
        int n = blen >= 4 ? (int)rd32(body) : 0;
        kvlangStrbuf_t b; kvlangStrbufInit(&b); kvlangStrbufPrintf(&b, "(%d)", n); return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_OBJ) == 0) return strdup(KVSPACE_KIND_OBJ);
    if (strcmp(k, KVSPACE_KIND_MAP) == 0) {
        kvlangStrbuf_t b; kvlangStrbufInit(&b);
        kvlangStrbufPuts(&b, "map[");
        for (int d = 0; d < kx.ndim; d++) {
            if (d) kvlangStrbufPutc(&b, ',');
            kvlangStrbufPrintf(&b, "%d", kx.dims[d]);
        }
        kvlangStrbufPutc(&b, ']');
        return kvlangStrbufDetach(&b);
    }
    return strndup2(body, blen);
}

/* ── 构造 ──────────────────────────────────────────────────────────── */

void kvlangXvalueNewTlv(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len, int32_t al) {
    uint32_t len;
    v->data = kvlangXvalueEncodeTlv(kind, raw, raw_len, al, &len);
    v->len = len;
}

/* 显式 ndim/dims 构造（保留多维 shape，供 xv.shape/xv.set 用）。 */
void kvlangXvalueNewTlvDims(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim) {
    uint8_t *tmp = NULL; uint32_t tl = 0;
    if (kvspaceTlvEncode(kind, raw, raw_len, dims, ndim, &tmp, &tl) != 0 || !tmp) {
        v->data = NULL; v->len = 0; return;
    }
    uint8_t *buf = malloc(tl);
    memcpy(buf, tmp, tl);
    kvspaceBytesFree(tmp, tl);
    v->data = buf; v->len = tl;
}

void kvlangXvalueNewInt64(kvlangXvalue_t *v, int64_t n) {
    uint8_t r[8];
    r[0] = n & 0xFF; r[1] = (n >> 8) & 0xFF; r[2] = (n >> 16) & 0xFF; r[3] = (n >> 24) & 0xFF;
    r[4] = (n >> 32) & 0xFF; r[5] = (n >> 40) & 0xFF; r[6] = (n >> 48) & 0xFF; r[7] = (n >> 56) & 0xFF;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_INT64, r, 8, 1);
}
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f) {
    uint64_t u; memcpy(&u, &f, 8);
    uint8_t r[8];
    for (int i = 0; i < 8; i++) r[i] = (u >> (i * 8)) & 0xFF;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_FLOAT64, r, 8, 1);
}
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b) {
    uint8_t r = b ? 1 : 0;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_BOOL, &r, 1, 1);
}
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_CHAR_UTF8, (const uint8_t *)s, sl, (int32_t)sl);
}
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNewTlv(v, kind, (const uint8_t *)s, sl, (int32_t)sl);
}
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v, const char *s) {
    size_t len = strlen(s);
    kvlangStrbuf_t raw; kvlangStrbufInit(&raw);
    size_t i = 0;
    while (i < len) {
        uint32_t cp = utf8_next(s, &i, len);
        uint8_t le[4] = { cp & 0xFF, (cp >> 8) & 0xFF, (cp >> 16) & 0xFF, (cp >> 24) & 0xFF };
        kvlangStrbufPutn(&raw, (const char *)le, 4);
    }
    kvlangXvalueNewTlv(v, KVSPACE_KIND_CHAR, (const uint8_t *)raw.p, (uint32_t)raw.len, (int32_t)(raw.len / 4));
    kvlangStrbufFree(&raw);
}
/* 指针：head kindexpr = "*" + target_kindexpr（目标完整 kindexpr），body = 目标 key。 */
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_kindexpr, const char *target) {
    uint8_t *tmp = NULL; uint32_t tl = 0, len = 0;
    if (kvspaceNewPtr(target_kindexpr, target, &tmp, &tl) != 0) { v->data = NULL; v->len = 0; return; }
    v->data = kvlangXvalueOwn(tmp, tl, &len);
    v->len = len;
}
void kvlangXvalueNewRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw, const char *sig) {
    size_t sl = strlen(sig);
    uint8_t *raw = malloc(4 + sl);
    raw[0] = nr & 0xFF; raw[1] = (nr >> 8) & 0xFF;
    raw[2] = nw & 0xFF; raw[3] = (nw >> 8) & 0xFF;
    memcpy(raw + 4, sig, sl);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_DEF_RWIR, raw, (uint32_t)(4 + sl), 1);
    free(raw);
}

void kvlangFormatFloat(char *out, size_t cap, double v) {
    char s[64];
    snprintf(s, sizeof s, "%.16g", v);
    if (strtod(s, NULL) != v) snprintf(s, sizeof s, "%.17g", v);
    snprintf(out, cap, "%s", s);
    if (strchr(out, 'e')) return;
    char *dot = strchr(out, '.');
    if (!dot) {
        size_t l = strlen(out);
        if (l + 2 < cap) { out[l] = '.'; out[l + 1] = '0'; out[l + 2] = 0; }
        return;
    }
    char *p = out + strlen(out) - 1;
    while (p > dot && *p == '0') *p-- = 0;
    if (p == dot) { p[1] = '0'; p[2] = 0; }
}
