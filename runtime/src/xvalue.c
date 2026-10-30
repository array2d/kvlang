#include "runtime_internal.h"

/* XValue helpers use the shared KVSpace wire codec. */

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint64_t read_u64(const uint8_t *p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

/* 释放构造出来的值（kvlangXvalueNew* / Copy 的产物）。从 kvspace 读到的视图不得调用。 */
void kvlangXvalueFree(kvlangXvalue_t *v) {
    free(v->bytes);
    v->bytes = NULL;
    v->length = 0;
}

/* 显式复制：把后端借用的视图拷成独立堆副本，调用方负责 kvlangXvalueFree。 */
void kvlangXvalueCopy(kvlangXvalue_t *v) {
    if (!v->bytes || !v->length)
        return;
    uint8_t *p = malloc(v->length);
    if (!p)
        kvlangDie("out of memory copying xvalue");
    memcpy(p, v->bytes, v->length);
    v->bytes = p;
    /* langtype 缓存随行内副本走（视图 → 堆副本）。 */
    v->langtype[255] = 0;
    v->langtype_len = (uint32_t)strlen(v->langtype);
}

/* 标量 inline 写 wire：为省掉「每个标量结果一次 malloc」而写的第二份 head 写入。
 * 固定 8 字节头 + body + 值内 attr 区（langtype=kind，#571 单 key 写）。 */
uint32_t kvlangScalarToWire(const char *kind, const uint8_t *raw, uint32_t len,
                            uint8_t *buf, uint32_t cap) {
    uint32_t ltl = kind ? (uint32_t)strlen(kind) : 0;
    uint32_t alen = 2 + (ltl ? 3 + ltl : 0);
    if (len > UINT48_MAX - KVSPACE_XVALUE_HEAD_LEN ||
        KVSPACE_XVALUE_HEAD_LEN + len + alen > cap)
        kvlangDie("scalar to-wire overflow: kind=%s len=%u", kind, len);
    memset(buf, 0, KVSPACE_XVALUE_HEAD_LEN);
    kvspaceXvalueHead h = {
        .storetype = KVSPACE_XVALUE_ST_SHORT,
        .ptr = KVSPACE_XVALUE_PTR_INLINE,
    };
    kvspaceXvalueHeadSetLen(&h, len);
    memcpy(buf, &h, sizeof h);
    if (len)
        memcpy(buf + KVSPACE_XVALUE_HEAD_LEN, raw, len);
    xh_attr_write(buf + KVSPACE_XVALUE_HEAD_LEN + len, kind);
    return KVSPACE_XVALUE_HEAD_LEN + len + alen;
}

/* 变长 char/utf8 的 SLACK 写 wire：固定 8 字节头 + body + 空 attr
 * （`‥` 系统键无类型，#571）。返回值 = 写出总长。 */
__attribute__((visibility("hidden"))) uint32_t
kvlangSlackCharToWire(const uint8_t *raw, uint32_t len, uint32_t cap,
                      uint8_t *buf, uint32_t bufsize) {
    if (len > cap || len > UINT48_MAX - KVSPACE_XVALUE_HEAD_LEN ||
        KVSPACE_XVALUE_HEAD_LEN + len + 2 > bufsize)
        kvlangDie("slack to-wire overflow: len=%u cap=%u", len, cap);
    memset(buf, 0, KVSPACE_XVALUE_HEAD_LEN + len + 2);
    kvspaceXvalueHead h = {
        .storetype = KVSPACE_XVALUE_ST_SLACK,
        .ptr = KVSPACE_XVALUE_PTR_INLINE,
    };
    kvspaceXvalueHeadSetLen(&h, len);
    memcpy(buf, &h, sizeof h);
    if (len)
        memcpy(buf + KVSPACE_XVALUE_HEAD_LEN, raw, len);
    /* 空 attr = [attr_len:2]，位置恒 @8+len。 */
    buf[KVSPACE_XVALUE_HEAD_LEN + len] = 2;
    buf[KVSPACE_XVALUE_HEAD_LEN + len + 1] = 0;
    return KVSPACE_XVALUE_HEAD_LEN + len + 2;
}

void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data, uint32_t len) {
    v->bytes = data;
    v->length = len;
}

/* 有界查找 MEMBER_SEP（多字节）；无则 -1。 */
static int32_t find_member_separator(const uint8_t *p, int32_t n) {
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
    if (find_member_separator(kx, klen) >= 0) {
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
static int32_t array_len_to_dims(const char *kind, int32_t array_len, int32_t *dims) {
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

/* 接管 wire codec 返回的堆缓冲：同进程同一 libc，free 兼容；省一次 malloc+memcpy。 */
static uint8_t *kvlangXvalueOwn(uint8_t *tmp, uint32_t tl, uint32_t *out_len) {
    if (!tmp) {
        *out_len = 0;
        return NULL;
    }
    *out_len = tl;
    return tmp;
}

static uint8_t *kvlangXvalueToWire(const char *kind, const uint8_t *raw,
                                      uint32_t raw_len, int32_t array_len,
                                      const char *langtype,
                                      uint32_t *out_len) {
    int32_t dims[1];
    int32_t ndim = array_len_to_dims(kind, array_len, dims);
    uint8_t *tmp = NULL;
    uint64_t tl = 0;
    int rc;
    if (!kind || kind[0] == '\0') {
        rc = kvspaceXvalueHeadNewNone(&tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 ||
               strcmp(kind, KVSPACE_KIND_CHAR_ASCII) == 0 ||
               strcmp(kind, KVSPACE_KIND_CHAR) == 0) {
        rc = kvspaceXvalueHeadNewSlack(raw, raw_len, raw_len, langtype,
                                        &tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_RWIR) == 0 ||
               strcmp(kind, KVSPACE_KIND_DEF_RWIR) == 0 ||
               strcmp(kind, KVSPACE_KIND_DEF_LANGTYPE) == 0 ||
               (strcmp(kind, KVSPACE_KIND_RWFUNC) == 0 && raw_len > 0)) {
        rc = kvspaceXvalueHeadNewCode(raw, raw_len, raw_len, langtype,
                                      &tmp, &tl);
    } else if (ndim > 0) {
        uint64_t dims_u64[X_MAX_NDIM];
        for (int i = 0; i < ndim && i < X_MAX_NDIM; i++)
            dims_u64[i] = (uint64_t)dims[i];
        rc = kvspaceXvalueHeadNewTensor(dims_u64, (uint32_t)ndim, raw,
                                        raw_len, langtype, &tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_NONE) == 0 && raw_len == 0) {
        rc = kvspaceXvalueHeadNewNone(&tmp, &tl);
    } else {
        rc = kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, raw, raw_len,
                                        langtype, &tmp, &tl);
    }
    if (rc != 0 || tl > UINT32_MAX) {
        *out_len = 0;
        return NULL;
    }
    return kvlangXvalueOwn(tmp, (uint32_t)tl, out_len);
}

/* head 视图：把 wire 原位解进调用方给的 kvspaceHead_t（不校验）。
 * 这是**测试/外部**用的完整 head 入口（runtime/tests/ 两处调用，Linux CI 编译）；
 * 热路径走 xh_* 访问器，不建 328B head。langtype 取 v 的缓存（GetHead 读值 attr 区）。 */
static void xh_fill_head(const uint8_t *d, kvspaceHead_t *h,
                         const char *lt, uint32_t ltlen) {
    memset(h, 0, sizeof(*h));
    h->headlen = KVSPACE_XVALUE_HEAD_LEN;
    h->ref = xh_is_ptr(d)    ? KVSPACE_REF_PTR
             : xh_is_ext(d)  ? KVSPACE_REF_EXT
                             : KVSPACE_REF_INLINE;
    h->storetype = xh_storetype(d);
    h->body_len = xh_content_len(d);
    h->body_cap = (uint64_t)h->body_len;
    h->body_offset = (int32_t)KVSPACE_XVALUE_HEAD_LEN;
    if (ltlen > sizeof(h->langtype) - 1)
        ltlen = (uint32_t)sizeof(h->langtype) - 1;
    if (ltlen > 0)
        memcpy(h->langtype, lt, (size_t)ltlen);
    h->langtype[ltlen] = 0;
    h->langtype_len = (int32_t)ltlen;
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)lt, (int32_t)ltlen, &kx);
    h->ndim = kx.ndim;
    for (int i = 0; i < kx.ndim && i < X_MAX_NDIM; i++)
        h->dims[i] = kx.dims[i];
}

int kvlangXvalueHead(const kvlangXvalue_t *v, kvspaceHead_t *h) {
    if (kvlangXvalueNone(v))
        return -1;
    xh_fill_head(v->bytes, h, v->langtype, v->langtype_len);
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
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
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
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
    return (size_t)kx.kind_len == strlen(kind) &&
           memcmp(kx.kind, kind, (size_t)kx.kind_len) == 0;
}

/* 完整 langtype（去 dims 前缀）拷入 buf：写槽带 map langtype 标注时据此取容器类型。 */
int kvlangXvalueLangtype(const kvlangXvalue_t *v, char *buf, size_t cap) {
    buf[0] = 0;
    if (cap == 0 || kvlangXvalueNone(v))
        return -1;
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
    size_t n = (size_t)kx.kind_len;
    if (n >= cap)
        n = cap - 1;
    memcpy(buf, kx.kind, n);
    buf[n] = 0;
    return (int)n;
}

static char *str_dup_n(const uint8_t *p, int32_t n);

/* 有界拷出完整 langtype（malloc，调用方 free）。None → ""。 */
char *kvlangXvalueLangtypeDup(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return strdup("");
    return str_dup_n((const uint8_t *)v->langtype, (int32_t)v->langtype_len);
}

/* Code slots store a five-byte prefix before the target name. */
char *kvlangXvalueSlotName(const kvlangXvalue_t *v) {
    const char *k = kvlangXvalueKind(v);
    if (strcmp(k, KVSPACE_KIND_RWIR) == 0 || strcmp(k, KVSPACE_KIND_RWFUNC) == 0) {
        const uint8_t *body = xh_body(v->bytes);
        int32_t blen = xh_content_len(v->bytes);
        return str_dup_n(body + (blen >= 5 ? 5 : 0), blen >= 5 ? blen - 5 : 0);
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
    return xh_is_ptr(v->bytes);
}

int32_t kvlangXvalueArrayLen(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return 0;
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
    return kx.array_len;
}

const uint8_t *kvlangXvalueBody(const kvlangXvalue_t *v, const kvspaceHead_t *h,
                                int32_t *out_len) {
    int32_t off = h->body_offset, len = h->body_len;
    if (off < 0 || len < 0 || off + len > (int32_t)v->length) {
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    if (out_len)
        *out_len = len;
    return v->bytes + off;
}

/* kind 分类：经 langtypetable intern 成 id 后整数区间判定（替代旧 strcmp 链）。 */
bool kvlangXvalueIsCharKind(const char *kind) {
    return kvlangLangtypeIsChar(kvlangLangtypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsIntKind(const char *kind) {
    return kvlangLangtypeIsSint(kvlangLangtypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsUintKind(const char *kind) {
    return kvlangLangtypeIsUint(kvlangLangtypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsFloatKind(const char *kind) {
    return kvlangLangtypeIsFloat(kvlangLangtypeId(kind, strlen(kind)));
}
bool kvlangXvalueIsNumKind(const char *kind) {
    return kvlangLangtypeIsNum(kvlangLangtypeId(kind, strlen(kind)));
}

int32_t kvlangXvalueElemSize(const char *kind) {
    return kvlangLangtypeElemSize(kvlangLangtypeId(kind, strlen(kind)));
}

/* 0copy 标量视图：原位取 langtype id + 指向 body 首字节的借用指针。 */
kvlangScalar_t kvlangXvalueScalar(const kvlangXvalue_t *v) {
    kvlangScalar_t s = {KVLANG_LT_NONE, NULL, 0};
    if (kvlangXvalueNone(v))
        return s;
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
    s.kind_id = kvlangLangtypeId(kx.kind, (size_t)kx.kind_len);
    s.bytes = xh_body(v->bytes);
    s.length = xh_content_len(v->bytes);
    return s;
}

/* 按 langtype id 直接从 body 指针读标量（0copy）。body==NULL → 0。 */
int64_t kvlangScalarReadInt64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    switch (id) {
    case KVLANG_LT_BOOL:
        return b[0] != 0;
    case KVLANG_LT_INT8:
        return (int8_t)b[0];
    case KVLANG_LT_INT16:
        return (int16_t)read_u16(b);
    case KVLANG_LT_INT32:
        return (int32_t)read_u32(b);
    case KVLANG_LT_INT64:
        return (int64_t)read_u64(b);
    case KVLANG_LT_UINT8:
        return b[0];
    case KVLANG_LT_UINT16:
        return read_u16(b);
    case KVLANG_LT_UINT32:
        return read_u32(b);
    case KVLANG_LT_UINT64:
        return (int64_t)read_u64(b);
    case KVLANG_LT_FLOAT32: {
        float f;
        uint32_t u = read_u32(b);
        memcpy(&f, &u, 4);
        return (int64_t)f;
    }
    case KVLANG_LT_FLOAT64: {
        double d;
        uint64_t u = read_u64(b);
        memcpy(&d, &u, 8);
        return (int64_t)d;
    }
    case KVLANG_LT_TIME:
    case KVLANG_LT_DURATION:
        return (int64_t)read_u64(b);
    default:
        return 0;
    }
}

double kvlangScalarReadFloat64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    if (id == KVLANG_LT_FLOAT32) {
        float f;
        uint32_t u = read_u32(b);
        memcpy(&f, &u, 4);
        return f;
    }
    if (id == KVLANG_LT_FLOAT64) {
        double d;
        uint64_t u = read_u64(b);
        memcpy(&d, &u, 8);
        return d;
    }
    return (double)kvlangScalarReadInt64(id, b);
}

uint64_t kvlangScalarReadUint64(int id, const uint8_t *b) {
    if (!b)
        return 0;
    switch (id) {
    case KVLANG_LT_UINT8:
        return b[0];
    case KVLANG_LT_UINT16:
        return read_u16(b);
    case KVLANG_LT_UINT32:
        return read_u32(b);
    case KVLANG_LT_UINT64:
        return read_u64(b);
    default:
        return (uint64_t)kvlangScalarReadInt64(id, b);
    }
}

uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx) {
    if (kvlangXvalueNone(v))
        return 0;
    kvlangLangtype kx;
    kvlangLangtypeParse((const uint8_t *)v->langtype,
                        (int32_t)v->langtype_len, &kx);
    if (idx < 0 || idx >= kx.array_len)
        return 0;
    return read_u32(xh_body(v->bytes) + idx * 4);
}

/* ── UTF-8 ↔ UTF-32 ────────────────────────────────────────────────── */

static size_t utf8_putc(char *out, uint32_t cp) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
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
    size_t n = (size_t)(blen / 4);
    char *s = malloc(n * 4 + 1);
    if (!s)
        kvlangDie("out of memory formatting char/utf32");
    size_t off = 0;
    for (int32_t i = 0; i + 4 <= blen; i += 4)
        off += utf8_putc(s + off, read_u32(body + i));
    s[off] = 0;
    return s;
}

static char *str_dup_n(const uint8_t *p, int32_t n) {
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
    return str_dup_n(xh_body(v->bytes), xh_content_len(v->bytes));
}

/* ── value_string（对齐 Go ValueString）────────────────────────────── */

char *kvlangXvalueValueString(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return strdup(KVSPACE_KIND_NONE);
    const uint8_t *body = xh_body(v->bytes);
    int32_t blen = xh_content_len(v->bytes);
    const char *k = kvlangXvalueKind(v);

    if (xh_is_ptr(v->bytes)) {
        char *s = malloc((size_t)blen + 4);
        if (!s)
            kvlangDie("out of memory formatting pointer");
        memcpy(s, "\xE2\x86\x92", 3);
        if (blen)
            memcpy(s + 3, body, (size_t)blen);
        s[blen + 3] = 0;
        return s;
    }
    if (strcmp(k, KVSPACE_KIND_BOOL) == 0)
        return strdup(body[0] ? "true" : "false");
    if (strcmp(k, KVSPACE_KIND_INT8) == 0)
        return kvlangStringFormat("%d", (int)(int8_t)body[0]);
    if (strcmp(k, KVSPACE_KIND_INT16) == 0)
        return kvlangStringFormat("%d", (int)(int16_t)read_u16(body));
    if (strcmp(k, KVSPACE_KIND_INT32) == 0)
        return kvlangStringFormat("%d", (int32_t)read_u32(body));
    if (strcmp(k, KVSPACE_KIND_INT64) == 0)
        return kvlangStringFormat("%lld", (long long)(int64_t)read_u64(body));
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0)
        return kvlangStringFormat("%u", (unsigned)body[0]);
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0)
        return kvlangStringFormat("%u", (unsigned)read_u16(body));
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0)
        return kvlangStringFormat("%u", read_u32(body));
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0)
        return kvlangStringFormat("%llu", (unsigned long long)read_u64(body));
    if (strcmp(k, KVSPACE_KIND_FLOAT32) == 0 ||
        strcmp(k, KVSPACE_KIND_FLOAT64) == 0) {
        char tmp[64];
        kvlangFormatFloat(tmp, sizeof tmp,
                          kvlangScalarF64(kvlangXvalueScalar(v)));
        return strdup(tmp);
    }
    if (strcmp(k, KVSPACE_KIND_CHAR_UTF8) == 0 ||
        strcmp(k, KVSPACE_KIND_CHAR_ASCII) == 0)
        return str_dup_n(body, blen);
    if (strcmp(k, KVSPACE_KIND_CHAR) == 0)
        return utf32_to_utf8(body, blen);
    if (strcmp(k, KVSPACE_KIND_RWIR) == 0)
        return str_dup_n(body + (blen >= 5 ? 5 : 0), blen >= 5 ? blen - 5 : 0);
    if (strcmp(k, KVSPACE_KIND_RWFUNC) == 0) {
        if (blen > 5)
            return str_dup_n(body + 5, blen - 5);
        return kvlangStringFormat("r%d/w%d", (blen >= 2 ? read_u16(body) : 0),
                          (blen >= 4 ? read_u16(body + 2) : 0));
    }
    if (kvlangKindIsMap(k))
        return strdup("map");
    return str_dup_n(body, blen);
}

/* ── 构造 ──────────────────────────────────────────────────────────── */

/* 完整 langtype 串（标量裸 kind；char/数组加 `[N]` 维度前缀）写入 buf、
 * 返回长度。kind 空 → 0（None）。与写值 wire 的 attr 区同源（#571）。 */
static int32_t lt_fmt(char *buf, size_t cap, const char *kind, int32_t al,
                      const int32_t *dims, int32_t ndim) {
    if (!kind || kind[0] == '\0')
        return 0;
    int32_t dd[1];
    if (ndim <= 0 || !dims) {
        ndim = array_len_to_dims(kind, al, dd);
        dims = dd;
    }
    if (ndim > 0) {
        int n = snprintf(buf, cap, "[%d]", dims[0]);
        for (int i = 1; i < ndim; i++)
            n += snprintf(buf + n, cap - (size_t)n, ",%d", dims[i]);
        n += snprintf(buf + n, cap - (size_t)n, "%s", kind);
        return n;
    }
    return (int32_t)snprintf(buf, cap, "%s", kind);
}

void kvlangXvalueNew(kvlangXvalue_t *v, const char *kind, const uint8_t *raw,
                        uint32_t raw_len, int32_t al) {
    char lt[256];
    int32_t ltl = lt_fmt(lt, sizeof lt, kind, al, NULL, 0);
    uint32_t len;
    v->bytes = kvlangXvalueToWire(kind, raw, raw_len, al, lt, &len);
    v->length = len;
    kvlangXvalueSetLangtype(v, lt, (uint32_t)ltl);
}

/* 显式 ndim/dims 构造（保留多维 shape，供 xv.shape/xv.set 用）。 */
void kvlangXvalueNewDims(kvlangXvalue_t *v, const char *kind,
                            const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim) {
    char lt[256];
    int32_t ltl = lt_fmt(lt, sizeof lt, kind, 1, dims, ndim);
    uint8_t *tmp = NULL;
    uint64_t tl = 0;
    int rc;
    if (!kind || kind[0] == '\0') {
        rc = kvspaceXvalueHeadNewNone(&tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 ||
               strcmp(kind, KVSPACE_KIND_CHAR_ASCII) == 0 ||
               strcmp(kind, KVSPACE_KIND_CHAR) == 0) {
        rc = kvspaceXvalueHeadNewSlack(raw, raw_len, raw_len, lt, &tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_RWIR) == 0 ||
               strcmp(kind, KVSPACE_KIND_DEF_RWIR) == 0 ||
               strcmp(kind, KVSPACE_KIND_DEF_LANGTYPE) == 0 ||
               (strcmp(kind, KVSPACE_KIND_RWFUNC) == 0 && raw_len > 0)) {
        rc = kvspaceXvalueHeadNewCode(raw, raw_len, raw_len, lt, &tmp, &tl);
    } else if (ndim > 0 && dims) {
        uint64_t dims_u64[X_MAX_NDIM];
        for (int i = 0; i < ndim && i < X_MAX_NDIM; i++)
            dims_u64[i] = (uint64_t)dims[i];
        rc = kvspaceXvalueHeadNewTensor(dims_u64, (uint32_t)ndim, raw,
                                        raw_len, lt, &tmp, &tl);
    } else if (strcmp(kind, KVSPACE_KIND_NONE) == 0 && raw_len == 0) {
        rc = kvspaceXvalueHeadNewNone(&tmp, &tl);
    } else {
        rc = kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, raw, raw_len,
                                        lt, &tmp, &tl);
    }
    if (rc != 0 || tl > UINT32_MAX || !tmp) {
        kvlangXvalueZero(v);
        return;
    }
    v->bytes = tmp;
    v->length = (uint32_t)tl;
    kvlangXvalueSetLangtype(v, lt, (uint32_t)ltl);
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
    kvlangXvalueNew(v, KVSPACE_KIND_INT64, r, 8, 1);
}
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f) {
    uint64_t u;
    memcpy(&u, &f, 8);
    uint8_t r[8];
    for (int i = 0; i < 8; i++)
        r[i] = (u >> (i * 8)) & 0xFF;
    kvlangXvalueNew(v, KVSPACE_KIND_FLOAT64, r, 8, 1);
}
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b) {
    uint8_t r = b ? 1 : 0;
    kvlangXvalueNew(v, KVSPACE_KIND_BOOL, &r, 1, 1);
}
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNew(v, KVSPACE_KIND_CHAR_UTF8, (const uint8_t *)s, sl,
                       (int32_t)sl);
}
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind,
                             const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    kvlangXvalueNew(v, kind, (const uint8_t *)s, sl, (int32_t)sl);
}
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v, const char *s) {
    size_t len = strlen(s);
    uint8_t *raw = malloc((len ? len : 1) * 4);
    if (!raw)
        kvlangDie("out of memory encoding char/utf32");
    size_t i = 0, off = 0;
    while (i < len) {
        uint32_t cp = utf8_next(s, &i, len);
        raw[off++] = (uint8_t)(cp & 0xFF);
        raw[off++] = (uint8_t)((cp >> 8) & 0xFF);
        raw[off++] = (uint8_t)((cp >> 16) & 0xFF);
        raw[off++] = (uint8_t)((cp >> 24) & 0xFF);
    }
    kvlangXvalueNew(v, KVSPACE_KIND_CHAR, raw, (uint32_t)off,
                       (int32_t)(off / 4));
    free(raw);
}
/* 指针：值 attr 区 langtype = target_langtype（目标完整 kindexpr，codec 拼入），
 * body = 目标 key。 */
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_langtype,
                        const char *target) {
    uint8_t *tmp = NULL;
    uint32_t tl = 0, len = 0;
    if (kvspaceNewPtr(target_langtype, target, &tmp, &tl) != 0) {
        kvlangXvalueZero(v);
        return;
    }
    v->bytes = kvlangXvalueOwn(tmp, tl, &len);
    v->length = len;
    kvlangXvalueSetLangtype(v, target_langtype,
                            (uint32_t)strlen(target_langtype));
}
/* def rwir 路由头：body 仅计数头 [nr:u16 LE][nw:u16 LE][dynamic:u8]，无参数载荷。
 * 各参数类型由 kvlangDefRwir 落 /lib/<op>/[0,x] 签名行槽（def langtype）。 */
void kvlangXvalueNewDefRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw,
                            int dynamic) {
    uint8_t raw[5] = {nr & 0xFF, (nr >> 8) & 0xFF, nw & 0xFF, (nw >> 8) & 0xFF,
                      dynamic ? 1 : 0};
    kvlangXvalueNew(v, KVSPACE_KIND_DEF_RWIR, raw, 5, 1);
}

/* 签名行 [0,x] 槽：一个参数的类型定义，body=该参数完整 langtype 串。 */
void kvlangXvalueNewDefLangtype(kvlangXvalue_t *v, const char *langtype) {
    kvlangXvalueNew(v, KVSPACE_KIND_DEF_LANGTYPE, (const uint8_t *)langtype,
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
