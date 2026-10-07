#include "runtime_internal.h"

/* XValue helpers use the shared KVSpace codec. */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

void kvlangXvalueFree(kvlangXvalue_t *v) {
    if (!v->borrowed)
        free(v->data);
    v->data = NULL;
    v->len = 0;
    v->borrowed = 0;
}

void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data, uint32_t len) {
    v->data = data;
    v->len = len;
    v->borrowed = 0;
}

/* Own borrowed values across reads within an instruction. */
void kvlangXvalueMaterialize(kvlangXvalue_t *v) {
    if (v->borrowed && v->data && v->len > 0) {
        uint8_t *o = malloc(v->len);
        memcpy(o, v->data, v->len);
        v->data = o;
    }
    v->borrowed = 0;
}

/* 有界查找 MEMBER_SEP（多字节）；无则 -1。 */
static int32_t find_member_sep(const uint8_t *p, int32_t n) {
    for (int32_t i = 0; i + MEMBER_SEP_LEN <= n; i++)
        if (memcmp(p + i, MEMBER_SEP, MEMBER_SEP_LEN) == 0)
            return i;
    return -1;
}

/* 解析 langtype 内容 → (dims, base kind)。langtype 为「指针 + 长度」、无前缀（ref 归 head.ref）；
 * 区内无 NUL 保证，禁止按 C 串读。**map langtype 无形状段**：`{keylt}·{valt}` 里 `·` 之前的
 * 方括号是键类型（`[int64]`、`[float64,float64]`），不是维度——与 `[2]float64`（数组形状）
 * 截然不同，故整串即 base kind。 */
void kvlangLangtypeParse(const uint8_t *kx, int32_t klen, kvlangLangtype *out) {
    memset(out, 0, sizeof(*out));
    if (!kx)
        return;
    if (klen < 0)
        klen = 0;
    int32_t i = 0;
    if (find_member_sep(kx, klen) >= 0) {
        out->kind = (const char *)kx;
        out->kind_len = klen;
        out->array_len = 1;
        return;
    }
    if (klen > 0 && kx[0] == '[') {
        i = 1;
        while (i < klen && kx[i] != ']' && out->ndim < X_MAX_NDIM) {
            int32_t d = 0;
            while (i < klen && kx[i] >= '0' && kx[i] <= '9') {
                d = d * 10 + (kx[i] - '0');
                i++;
            }
            out->dims[out->ndim++] = d;
            if (i < klen && kx[i] == ',')
                i++;
        }
        if (i < klen && kx[i] == ']')
            i++;
    }
    out->kind = (const char *)(kx + i);
    out->kind_len = klen - i;
    out->array_len = 1;
    for (int d = 0; d < out->ndim; d++)
        out->array_len *= out->dims[d];
}

/* array_len → dims：char/* 恒一维（含空串/单字符）；其余标量(≤1)=0 维、多元素=1 维。 */
static int32_t al_to_dims(const char *kind, int32_t array_len, int32_t *dims) {
    if (strncmp(kind, "char/", 5) == 0) {
        dims[0] = array_len < 0 ? 0 : array_len;
        return 1;
    }
    if (array_len > 1) {
        dims[0] = array_len;
        return 1;
    }
    return 0;
}

static uint8_t *kvlangXvalueEncodeTlv(const char *kind, const uint8_t *raw,
                                      uint32_t raw_len, int32_t array_len,
                                      uint32_t *out_len) {
    int32_t dims[1];
    int32_t ndim = al_to_dims(kind, array_len, dims);
    uint8_t *tmp = NULL;
    uint32_t tl = 0;
    if (kvspaceTlvEncode(kind, raw, raw_len, dims, ndim, &tmp, &tl) != 0) {
        *out_len = 0;
        return NULL;
    }
    *out_len = tl;
    return tmp;
}

/* head 视图：原位取偏移与 langtype（不建 328B head、不校验）。
 * 仅供按 head 取值的使用方（测试/外部）；热路径直接走 xh_* 访问器。 */
static void xh_fill_head(const uint8_t *d, kvspaceHead_t *h) {
    memset(h, 0, sizeof(*h));
    uint32_t headlen = xh_headlen(d);
    h->headlen = (uint16_t)headlen;
    h->ref = xh_is_ptr(d)                   ? KVSPACE_REF_PTR
             : (d[1] & 3u) == 3u            ? KVSPACE_REF_EXT
                                            : KVSPACE_REF_INLINE;
    h->storetype = xh_class(d);
    h->body_len = xh_content_len(d);
    h->body_cap = (uint64_t)h->body_len;
    h->body_offset = (int32_t)headlen;
    const uint8_t *lt = xh_langtype(d);
    uint32_t ltlen = xh_langtype_len(d);
    if (ltlen > sizeof(h->langtype) - 1)
        ltlen = (uint32_t)sizeof(h->langtype) - 1;
    memcpy(h->langtype, lt, (size_t)ltlen);
    h->langtype[ltlen] = 0;
    h->langtype_len = (int32_t)ltlen;
    kvlangLangtype kx;
    kvlangLangtypeParse(lt, (int32_t)xh_langtype_len(d), &kx);
    h->ndim = kx.ndim;
    for (int i = 0; i < kx.ndim && i < X_MAX_NDIM; i++)
        h->dims[i] = kx.dims[i];
}

int kvlangXvalueHead(const kvlangXvalue_t *v, kvspaceHead_t *h) {
    if (kvlangXvalueNone(v))
        return -1;
    xh_fill_head(v->data, h);
    return 0;
}

const char *kvlangXvalueKind(const kvlangXvalue_t *v) {
    static __thread char buf[16][65];
    static __thread int idx = 0;
    char *b = buf[idx];
    idx = (idx + 1) & 15;
    if (kvlangXvalueNone(v)) {
        b[0] = 0;
        return b;
    }
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    int32_t kl = kx.kind_len;
    if (kl > 64)
        kl = 64;
    memcpy(b, kx.kind, (size_t)kl);
    b[kl] = 0;
    return b;
}

bool kvlangXvalueKindIs(const kvlangXvalue_t *v, const char *kind) {
    if (kvlangXvalueNone(v))
        return kind[0] == 0;
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    return (size_t)kx.kind_len == strlen(kind) &&
           memcmp(kx.kind, kind, (size_t)kx.kind_len) == 0;
}

/* 完整 langtype（去 dims 前缀）拷入 buf：写槽带 map langtype 标注时据此取容器类型。 */
int kvlangXvalueLangtype(const kvlangXvalue_t *v, char *buf, size_t cap) {
    buf[0] = 0;
    if (cap == 0 || kvlangXvalueNone(v))
        return -1;
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    size_t n = (size_t)kx.kind_len;
    if (n >= cap)
        n = cap - 1;
    memcpy(buf, kx.kind, n);
    buf[n] = 0;
    return (int)n;
}

static char *strndup2(const uint8_t *p, int32_t n);

/* 有界拷出完整 langtype（malloc，调用方 free）。None → ""。 */
char *kvlangXvalueLangtypeDup(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return strdup("");
    return strndup2(xh_langtype(v->data), (int32_t)xh_langtype_len(v->data));
}

/* Code slots store a five-byte prefix before the target name. */
char *kvlangXvalueSlotName(const kvlangXvalue_t *v) {
    const char *k = kvlangXvalueKind(v);
    if (strcmp(k, KVSPACE_KIND_RWIR) == 0 || strcmp(k, KVSPACE_KIND_RWFUNC) == 0) {
        const uint8_t *body = xh_body(v->data);
        int32_t blen = xh_content_len(v->data);
        return strndup2(body + (blen >= 5 ? 5 : 0), blen >= 5 ? blen - 5 : 0);
    }
    return kvlangXvalueValueString(v);
}

/* 值容器判定：裸种类名 `stringkeymap`，或完整 map langtype `{keylt}·{valt}`（见 [[map容器]]）。
 * 后者 `·` 之前是键类型（可能是 `[int64]`/`[float64,float64]`），故不能与 KIND_MAP 比串。 */
bool kvlangKindIsMap(const char *kind) {
    return kind && strstr(kind, MEMBER_SEP) != NULL;
}

bool kvlangXvalueIsPtr(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return false;
    return xh_is_ptr(v->data);
}

int32_t kvlangXvalueArrayLen(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return 0;
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    return kx.array_len;
}

const uint8_t *kvlangXvalueBody(const kvlangXvalue_t *v, const kvspaceHead_t *h,
                                int32_t *out_len) {
    int32_t off = h->body_offset, len = h->body_len;
    if (off < 0 || len < 0 || off + len > (int32_t)v->len) {
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    if (out_len)
        *out_len = len;
    return v->data + off;
}

/* kind 分类：经 langtypetable intern 成 id 后整数区间判定（替代旧 strcmp 链）。 */
bool kvlangXvalueIsCharKind(const char *kind) {
    return kvlangLtIsChar(kvlangLangTypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsIntKind(const char *kind) {
    return kvlangLtIsSint(kvlangLangTypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsUintKind(const char *kind) {
    return kvlangLtIsUint(kvlangLangTypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsFloatKind(const char *kind) {
    return kvlangLtIsFloat(kvlangLangTypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsNumKind(const char *kind) {
    return kvlangLtIsNum(kvlangLangTypeId(kind, strlen(kind)));
}

int32_t kvlangXvalueElemSize(const char *kind) {
    return kvlangLtElemSize(kvlangLangTypeId(kind, strlen(kind)));
}

/* 0copy 标量视图：原位取 langtype id + 指向 body 首字节的借用指针。 */
kvlangScalar_t kvlangXvalueScalar(const kvlangXvalue_t *v) {
    kvlangScalar_t s = {KVLANG_LT_NONE, NULL, 0};
    if (kvlangXvalueNone(v))
        return s;
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    s.id = kvlangLangTypeId(kx.kind, (size_t)kx.kind_len);
    s.body = xh_body(v->data);
    s.len = xh_content_len(v->data);
    return s;
}

/* 按 langtype id 直接从 body 指针读标量（0copy）。body==NULL → 0。 */
int64_t kvlangScalarReadI64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    switch (id) {
    case KVLANG_LT_BOOL:
        return b[0] != 0;
    case KVLANG_LT_INT8:
        return (int8_t)b[0];
    case KVLANG_LT_INT16:
        return (int16_t)rd16(b);
    case KVLANG_LT_INT32:
        return (int32_t)rd32(b);
    case KVLANG_LT_INT64:
        return (int64_t)rd64(b);
    case KVLANG_LT_UINT8:
        return b[0];
    case KVLANG_LT_UINT16:
        return rd16(b);
    case KVLANG_LT_UINT32:
        return rd32(b);
    case KVLANG_LT_UINT64:
        return (int64_t)rd64(b);
    case KVLANG_LT_FLOAT32: {
        float f;
        uint32_t u = rd32(b);
        memcpy(&f, &u, 4);
        return (int64_t)f;
    }
    case KVLANG_LT_FLOAT64: {
        double d;
        uint64_t u = rd64(b);
        memcpy(&d, &u, 8);
        return (int64_t)d;
    }
    case KVLANG_LT_TIME:
    case KVLANG_LT_DURATION:
        return (int64_t)rd64(b);
    default:
        return 0;
    }
}

double kvlangScalarReadF64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    if (id == KVLANG_LT_FLOAT32) {
        float f;
        uint32_t u = rd32(b);
        memcpy(&f, &u, 4);
        return f;
    }
    if (id == KVLANG_LT_FLOAT64) {
        double d;
        uint64_t u = rd64(b);
        memcpy(&d, &u, 8);
        return d;
    }
    return (double)kvlangScalarReadI64(id, b);
}

uint64_t kvlangScalarReadU64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    switch (id) {
    case KVLANG_LT_UINT8:
        return b[0];
    case KVLANG_LT_UINT16:
        return rd16(b);
    case KVLANG_LT_UINT32:
        return rd32(b);
    case KVLANG_LT_UINT64:
        return rd64(b);
    default:
        return (uint64_t)kvlangScalarReadI64(id, b);
    }
}

uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx) {
    if (kvlangXvalueNone(v))
        return 0;
    kvlangLangtype kx;
    kvlangLangtypeParse(xh_langtype(v->data),
                        (int32_t)xh_langtype_len(v->data), &kx);
    if (idx < 0 || idx >= kx.array_len)
        return 0;
    return rd32(xh_body(v->data) + idx * 4);
}

/* ── UTF-8 ↔ UTF-32 ────────────────────────────────────────────────── */

static void utf8_putc(kvlangStrbuf_t *b, uint32_t cp) {
    if (cp < 0x80)
        kvlangStrbufPutc(b, (char)cp);
    else if (cp < 0x800) {
        kvlangStrbufPutc(b, (char)(0xC0 | (cp >> 6)));
        kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        kvlangStrbufPutc(b, (char)(0xE0 | (cp >> 12)));
        kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        kvlangStrbufPutc(b, (char)(0xF0 | (cp >> 18)));
        kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        kvlangStrbufPutc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        kvlangStrbufPutc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

static uint32_t utf8_next(const char *s, size_t *i, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = p[*i];
    if (cp < 0x80) {
        (*i)++;
        return cp;
    }
    int n = 0;
    if ((cp & 0xE0) == 0xC0) {
        n = 1;
        cp &= 0x1F;
    } else if ((cp & 0xF0) == 0xE0) {
        n = 2;
        cp &= 0x0F;
    } else if ((cp & 0xF8) == 0xF0) {
        n = 3;
        cp &= 0x07;
    } else {
        (*i)++;
        return 0xFFFD;
    }
    (*i)++;
    for (int j = 0; j < n && *i < len; j++, (*i)++)
        cp = (cp << 6) | (p[*i] & 0x3F);
    return cp;
}

static char *utf32_to_utf8(const uint8_t *body, int32_t blen) {
    kvlangStrbuf_t b;
    kvlangStrbufInit(&b);
    for (int32_t i = 0; i + 4 <= blen; i += 4)
        utf8_putc(&b, rd32(body + i));
    return kvlangStrbufDetach(&b);
}

static char *strndup2(const uint8_t *p, int32_t n) {
    char *s = malloc((size_t)n + 1);
    if (s) {
        memcpy(s, p, (size_t)n);
        s[n] = 0;
    }
    return s;
}

char *kvlangXvaluePtrTarget(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return strdup("");
    return strndup2(xh_body(v->data), xh_content_len(v->data));
}

/* ── value_string（对齐 Go ValueString）────────────────────────────── */

static void append_num_int(kvlangStrbuf_t *b, int64_t n) {
    kvlangStrbufPrintf(b, "%lld", (long long)n);
}
static void append_num_uint(kvlangStrbuf_t *b, uint64_t n) {
    kvlangStrbufPrintf(b, "%llu", (unsigned long long)n);
}

char *kvlangXvalueValueString(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return strdup(KVSPACE_KIND_NONE);
    const uint8_t *body = xh_body(v->data);
    int32_t blen = xh_content_len(v->data);
    const char *k = kvlangXvalueKind(v);

    if (xh_is_ptr(v->data)) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        kvlangStrbufPutn(&b, "\xE2\x86\x92", 3);
        kvlangStrbufPutn(&b, (const char *)body, (size_t)blen);
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_BOOL) == 0)
        return strdup(body[0] ? "true" : "false");
    if (strcmp(k, KVSPACE_KIND_INT8) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_int(&b, (int8_t)body[0]);
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_INT16) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_int(&b, (int16_t)rd16(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_INT32) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_int(&b, (int32_t)rd32(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_INT64) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_int(&b, (int64_t)rd64(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_uint(&b, body[0]);
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_uint(&b, rd16(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_uint(&b, rd32(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0) {
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        append_num_uint(&b, rd64(body));
        return kvlangStrbufDetach(&b);
    }
    if (strcmp(k, KVSPACE_KIND_FLOAT32) == 0 ||
        strcmp(k, KVSPACE_KIND_FLOAT64) == 0) {
        char tmp[64];
        kvlangFormatFloat(tmp, sizeof tmp,
                          kvlangScalarF64(kvlangXvalueScalar(v)));
        return strdup(tmp);
    }
    if (strcmp(k, KVSPACE_KIND_CHAR_UTF8) == 0 ||
        strcmp(k, KVSPACE_KIND_CHAR_ASCII) == 0)
        return strndup2(body, blen);
    if (strcmp(k, KVSPACE_KIND_CHAR) == 0)
        return utf32_to_utf8(body, blen);
    if (strcmp(k, KVSPACE_KIND_RWIR) == 0)
        return strndup2(body + (blen >= 5 ? 5 : 0), blen >= 5 ? blen - 5 : 0);
    if (strcmp(k, KVSPACE_KIND_RWFUNC) == 0) {
        if (blen > 5)
            return strndup2(body + 5, blen - 5);
        kvlangStrbuf_t b;
        kvlangStrbufInit(&b);
        kvlangStrbufPrintf(&b, "r%d/w%d", (blen >= 2 ? rd16(body) : 0),
                           (blen >= 4 ? rd16(body + 2) : 0));
        return kvlangStrbufDetach(&b);
    }
    if (kvlangKindIsMap(k))
        return strdup("map");
    return strndup2(body, blen);
}

/* ── 构造 ──────────────────────────────────────────────────────────── */

void kvlangXvalueNewTlv(kvlangXvalue_t *v, const char *kind, const uint8_t *raw,
                        uint32_t raw_len, int32_t al) {
    uint32_t len;
    v->data = kvlangXvalueEncodeTlv(kind, raw, raw_len, al, &len);
    v->len = len;
    v->borrowed = 0;
}

/* 显式 ndim/dims 构造（保留多维 shape，供 xv.shape/xv.set 用）。 */
void kvlangXvalueNewTlvDims(kvlangXvalue_t *v, const char *kind,
                            const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim) {
    uint8_t *tmp = NULL;
    uint32_t tl = 0;
    if (kvspaceTlvEncode(kind, raw, raw_len, dims, ndim, &tmp, &tl) != 0 ||
        !tmp) {
        kvlangXvalueZero(v);
        return;
    }
    v->data = tmp;
    v->len = tl;
    v->borrowed = 0;
}

void kvlangXvalueNewInt64(kvlangXvalue_t *v, int64_t n) {
    uint8_t r[8];
    r[0] = n & 0xFF;
    r[1] = (n >> 8) & 0xFF;
    r[2] = (n >> 16) & 0xFF;
    r[3] = (n >> 24) & 0xFF;
    r[4] = (n >> 32) & 0xFF;
    r[5] = (n >> 40) & 0xFF;
    r[6] = (n >> 48) & 0xFF;
    r[7] = (n >> 56) & 0xFF;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_INT64, r, 8, 1);
}
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f) {
    uint64_t u;
    memcpy(&u, &f, 8);
    uint8_t r[8];
    for (int i = 0; i < 8; i++)
        r[i] = (u >> (i * 8)) & 0xFF;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_FLOAT64, r, 8, 1);
}
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b) {
    uint8_t r = b ? 1 : 0;
    kvlangXvalueNewTlv(v, KVSPACE_KIND_BOOL, &r, 1, 1);
}
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_CHAR_UTF8, (const uint8_t *)s, sl,
                       (int32_t)sl);
}
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind,
                             const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNewTlv(v, kind, (const uint8_t *)s, sl, (int32_t)sl);
}
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v, const char *s) {
    size_t len = strlen(s);
    kvlangStrbuf_t raw;
    kvlangStrbufInit(&raw);
    size_t i = 0;
    while (i < len) {
        uint32_t cp = utf8_next(s, &i, len);
        uint8_t le[4] = {cp & 0xFF, (cp >> 8) & 0xFF, (cp >> 16) & 0xFF,
                         (cp >> 24) & 0xFF};
        kvlangStrbufPutn(&raw, (const char *)le, 4);
    }
    kvlangXvalueNewTlv(v, KVSPACE_KIND_CHAR, (const uint8_t *)raw.p,
                       (uint32_t)raw.len, (int32_t)(raw.len / 4));
    kvlangStrbufFree(&raw);
}
/* 指针：head langtype = "*" + target_langtype（目标完整 langtype），body = 目标 key。 */
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_langtype,
                        const char *target) {
    uint8_t *tmp = NULL;
    uint32_t tl = 0;
    if (kvspaceNewPtr(target_langtype, target, &tmp, &tl) != 0) {
        kvlangXvalueZero(v);
        return;
    }
    v->data = tmp;
    v->len = tl;
    v->borrowed = 0;
}
/* def rwir 路由头：body 仅计数头 [nr:u16 LE][nw:u16 LE][dynamic:u8]，无参数载荷。
 * 各参数类型由 kvlangDefRwir 落 /lib/<op>/[0,x] 签名行槽（def langtype）。 */
void kvlangXvalueNewDefRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw,
                            int dynamic) {
    uint8_t raw[5] = {nr & 0xFF, (nr >> 8) & 0xFF, nw & 0xFF, (nw >> 8) & 0xFF,
                      dynamic ? 1 : 0};
    kvlangXvalueNewTlv(v, KVSPACE_KIND_DEF_RWIR, raw, 5, 1);
}

/* 签名行 [0,x] 槽：一个参数的类型定义，body=该参数完整 langtype 串。 */
void kvlangXvalueNewDefLangtype(kvlangXvalue_t *v, const char *langtype) {
    kvlangXvalueNewTlv(v, KVSPACE_KIND_DEF_LANGTYPE, (const uint8_t *)langtype,
                       (uint32_t)strlen(langtype), 1);
}

void kvlangFormatFloat(char *out, size_t cap, double v) {
    char s[64];
    snprintf(s, sizeof s, "%.16g", v);
    if (strtod(s, NULL) != v)
        snprintf(s, sizeof s, "%.17g", v);
    snprintf(out, cap, "%s", s);
    if (strchr(out, 'e'))
        return;
    char *dot = strchr(out, '.');
    if (!dot) {
        size_t l = strlen(out);
        if (l + 2 < cap) {
            out[l] = '.';
            out[l + 1] = '0';
            out[l + 2] = 0;
        }
        return;
    }
    char *p = out + strlen(out) - 1;
    while (p > dot && *p == '0')
        *p-- = 0;
    if (p == dot) {
        p[1] = '0';
        p[2] = 0;
    }
}
