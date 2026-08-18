#include "runtime_internal.h"

/* 后端无关的 XValue TLV 编解码（对齐 kvspace-durable/kvspace-c 的 kindexp TLV）。
 * xval.data 一律 malloc（free 释放），后端在 kv.c 层负责拷贝。 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

void xv_free(xval_t *v) {
    free(v->data);
    v->data = NULL; v->len = 0;
}

void xv_set_bytes(xval_t *v, uint8_t *data, uint32_t len) {
    v->data = data; v->len = len;
}

/* head 编解码统一委托给链接的 kvspace .so（kvspace-c / kvspace-durable 同一 ABI），
 * runtime 不再私持 TLV head 布局，杜绝多份手写偏移不一致。 */
static int xv_decode_head_raw(const uint8_t *d, uint32_t len, kvhead_t *h) {
    memset(h, 0, sizeof(*h));
    if (!d || len == 0) return -1;
    kvspace_decode_head(d, len, h);
    return h->kind[0] ? 0 : -1;
}

/* array_len → dims：char/* 恒一维（含空串/单字符）；其余标量(≤1)=0 维、多元素=1 维。 */
static int32_t al_to_dims(const char *kind, int32_t array_len, int32_t *dims) {
    if (strncmp(kind, "char/", 5) == 0) { dims[0] = array_len < 0 ? 0 : array_len; return 1; }
    if (array_len > 1) { dims[0] = array_len; return 1; }
    return 0;
}

static uint8_t *xv_encode_tlv(const char *kind, int32_t ref, const uint8_t *raw,
                              uint32_t raw_len, int32_t array_len, uint32_t *out_len) {
    int32_t dims[1]; int32_t ndim = al_to_dims(kind, array_len, dims);
    uint8_t *tmp = NULL; uint32_t tl = 0;
    int rc = ref == 1 ? kvspace_tlv_encode_ptr(kind, raw, raw_len, dims, ndim, &tmp, &tl)
                      : kvspace_tlv_encode(kind, raw, raw_len, dims, ndim, &tmp, &tl);
    if (rc != 0 || !tmp) { *out_len = 0; return NULL; }
    uint8_t *buf = malloc(tl);          /* 转交 runtime 所有权（统一 free 释放） */
    memcpy(buf, tmp, tl);
    kvspace_bytes_free(tmp, tl);        /* .so 分配器配对释放 */
    *out_len = tl;
    return buf;
}

int xv_head(const xval_t *v, kvhead_t *h) {
    memset(h, 0, sizeof(*h));
    if (xv_none(v)) return -1;
    return xv_decode_head_raw(v->data, v->len, h);
}

const char *xv_kind(const xval_t *v) {
    static __thread char buf[16][33];
    static __thread int idx = 0;
    char *b = buf[idx];
    idx = (idx + 1) & 15;
    if (xv_none(v)) { b[0] = 0; return b; }
    uint8_t kl = v->data[0];
    if (kl > 32) kl = 32;
    memcpy(b, v->data + 1, kl);
    b[kl] = 0;
    return b;
}

bool xv_kind_is(const xval_t *v, const char *kind) {
    if (xv_none(v)) return kind[0] == 0;
    uint8_t kl = v->data[0];
    return (size_t)kl == strlen(kind) && memcmp(v->data + 1, kind, kl) == 0;
}

bool xv_is_ptr(const xval_t *v) {
    if (xv_none(v)) return false;
    return v->len > 2 + v->data[0] && v->data[1 + v->data[0]] == 1;
}

int32_t xv_array_len(const xval_t *v) {
    if (xv_none(v)) return 0;
    kvhead_t h; xv_decode_head_raw(v->data, v->len, &h);
    return h.array_len;
}

const uint8_t *xv_body(const xval_t *v, const kvhead_t *h, int32_t *out_len) {
    int32_t off = h->body_offset, len = h->body_len;
    if (off < 0 || len < 0 || off + len > (int32_t)v->len) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = len;
    return v->data + off;
}

bool xv_is_char_kind(const char *kind) { return strncmp(kind, "char/", 5) == 0; }
bool xv_is_int_kind(const char *kind) {
    return strcmp(kind, K_INT8) == 0 || strcmp(kind, K_INT16) == 0 ||
           strcmp(kind, K_INT32) == 0 || strcmp(kind, K_INT64) == 0;
}
bool xv_is_uint_kind(const char *kind) {
    return strcmp(kind, K_UINT8) == 0 || strcmp(kind, K_UINT16) == 0 ||
           strcmp(kind, K_UINT32) == 0 || strcmp(kind, K_UINT64) == 0;
}
bool xv_is_float_kind(const char *kind) {
    return strcmp(kind, K_FLOAT32) == 0 || strcmp(kind, K_FLOAT64) == 0;
}
bool xv_is_num_kind(const char *kind) {
    return xv_is_int_kind(kind) || xv_is_uint_kind(kind) || xv_is_float_kind(kind);
}

int32_t xv_elem_size(const char *kind) {
    if (strcmp(kind, K_INT8) == 0 || strcmp(kind, K_UINT8) == 0 ||
        strcmp(kind, K_CHAR_UTF8) == 0 || strcmp(kind, K_CHAR_ASCII) == 0 ||
        strcmp(kind, K_BOOL) == 0) return 1;
    if (strcmp(kind, K_INT16) == 0 || strcmp(kind, K_UINT16) == 0) return 2;
    if (strcmp(kind, K_INT32) == 0 || strcmp(kind, K_UINT32) == 0 ||
        strcmp(kind, K_FLOAT32) == 0 || strcmp(kind, K_CHAR) == 0) return 4;
    if (strcmp(kind, K_INT64) == 0 || strcmp(kind, K_UINT64) == 0 ||
        strcmp(kind, K_FLOAT64) == 0 || strcmp(kind, K_TIME) == 0 ||
        strcmp(kind, K_DURATION) == 0) return 8;
    return 0;
}

static const uint8_t *v_body(const xval_t *v, kvhead_t *h) {
    if (xv_decode_head_raw(v->data, v->len, h) < 0) return NULL;
    return v->data + h->body_offset;
}

int64_t xv_as_int64(const xval_t *v) {
    if (xv_none(v)) return 0;
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = xv_kind(v);
    if (strcmp(k, K_BOOL) == 0) return b[0] != 0;
    if (strcmp(k, K_INT8) == 0) return (int8_t)b[0];
    if (strcmp(k, K_INT16) == 0) return (int16_t)rd16(b);
    if (strcmp(k, K_INT32) == 0) return (int32_t)rd32(b);
    if (strcmp(k, K_INT64) == 0) return (int64_t)rd64(b);
    if (strcmp(k, K_UINT8) == 0) return b[0];
    if (strcmp(k, K_UINT16) == 0) return rd16(b);
    if (strcmp(k, K_UINT32) == 0) return rd32(b);
    if (strcmp(k, K_UINT64) == 0) return (int64_t)rd64(b);
    if (strcmp(k, K_FLOAT32) == 0) { float f; uint32_t u = rd32(b); memcpy(&f, &u, 4); return (int64_t)f; }
    if (strcmp(k, K_FLOAT64) == 0) { double d; uint64_t u = rd64(b); memcpy(&d, &u, 8); return (int64_t)d; }
    if (strcmp(k, K_TIME) == 0 || strcmp(k, K_DURATION) == 0) return (int64_t)rd64(b);
    return 0;
}

double xv_as_float64(const xval_t *v) {
    if (xv_none(v)) return 0;
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = xv_kind(v);
    if (strcmp(k, K_FLOAT32) == 0) { float f; uint32_t u = rd32(b); memcpy(&f, &u, 4); return f; }
    if (strcmp(k, K_FLOAT64) == 0) { double d; uint64_t u = rd64(b); memcpy(&d, &u, 8); return d; }
    return (double)xv_as_int64(v);
}

uint64_t xv_as_uint64(const xval_t *v) {
    if (xv_none(v)) return 0;
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return 0;
    const char *k = xv_kind(v);
    if (strcmp(k, K_UINT8) == 0) return b[0];
    if (strcmp(k, K_UINT16) == 0) return rd16(b);
    if (strcmp(k, K_UINT32) == 0) return rd32(b);
    if (strcmp(k, K_UINT64) == 0) return rd64(b);
    return (uint64_t)xv_as_int64(v);
}

bool xv_as_bool(const xval_t *v) {
    if (xv_none(v)) return false;
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    return b && h.body_len > 0 && b[0] != 0;
}

uint32_t xv_char32_at(const xval_t *v, int32_t idx) {
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    if (!b || idx < 0 || idx >= h.array_len) return 0;
    return rd32(b + idx * 4);
}

/* ── UTF-8 ↔ UTF-32 ────────────────────────────────────────────────── */

static void utf8_putc(sbuf_t *b, uint32_t cp) {
    if (cp < 0x80) sb_putc(b, (char)cp);
    else if (cp < 0x800) { sb_putc(b, (char)(0xC0 | (cp >> 6))); sb_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        sb_putc(b, (char)(0xE0 | (cp >> 12))); sb_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(b, (char)(0xF0 | (cp >> 18))); sb_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_putc(b, (char)(0x80 | (cp & 0x3F)));
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
    sbuf_t b; sb_init(&b);
    for (int32_t i = 0; i + 4 <= blen; i += 4) utf8_putc(&b, rd32(body + i));
    return sb_detach(&b);
}

static char *strndup2(const uint8_t *p, int32_t n) {
    char *s = malloc((size_t)n + 1);
    if (s) { memcpy(s, p, (size_t)n); s[n] = 0; }
    return s;
}

char *xv_ptr_target(const xval_t *v) {
    kvhead_t h; const uint8_t *b = v_body(v, &h);
    if (!b) return strdup("");
    return strndup2(b, h.body_len);
}

/* ── value_string（对齐 Go ValueString）────────────────────────────── */

static void append_num_int(sbuf_t *b, int64_t n) { sb_printf(b, "%lld", (long long)n); }
static void append_num_uint(sbuf_t *b, uint64_t n) { sb_printf(b, "%llu", (unsigned long long)n); }

char *xv_value_string(const xval_t *v) {
    if (xv_none(v)) return strdup(K_NONE);
    kvhead_t h; const uint8_t *body = v_body(v, &h);
    if (!body) return strdup(K_NONE);
    int32_t blen = h.body_len;
    const char *k = xv_kind(v);

    if (h.is_ptr) {
        sbuf_t b; sb_init(&b);
        sb_putn(&b, "\xE2\x86\x92", 3);
        sb_putn(&b, (const char *)body, (size_t)blen);
        return sb_detach(&b);
    }
    if (strcmp(k, K_BOOL) == 0) return strdup(body[0] ? "true" : "false");
    if (strcmp(k, K_INT8) == 0) { sbuf_t b; sb_init(&b); append_num_int(&b, (int8_t)body[0]); return sb_detach(&b); }
    if (strcmp(k, K_INT16) == 0) { sbuf_t b; sb_init(&b); append_num_int(&b, (int16_t)rd16(body)); return sb_detach(&b); }
    if (strcmp(k, K_INT32) == 0) { sbuf_t b; sb_init(&b); append_num_int(&b, (int32_t)rd32(body)); return sb_detach(&b); }
    if (strcmp(k, K_INT64) == 0) { sbuf_t b; sb_init(&b); append_num_int(&b, (int64_t)rd64(body)); return sb_detach(&b); }
    if (strcmp(k, K_UINT8) == 0) { sbuf_t b; sb_init(&b); append_num_uint(&b, body[0]); return sb_detach(&b); }
    if (strcmp(k, K_UINT16) == 0) { sbuf_t b; sb_init(&b); append_num_uint(&b, rd16(body)); return sb_detach(&b); }
    if (strcmp(k, K_UINT32) == 0) { sbuf_t b; sb_init(&b); append_num_uint(&b, rd32(body)); return sb_detach(&b); }
    if (strcmp(k, K_UINT64) == 0) { sbuf_t b; sb_init(&b); append_num_uint(&b, rd64(body)); return sb_detach(&b); }
    if (strcmp(k, K_FLOAT32) == 0 || strcmp(k, K_FLOAT64) == 0) {
        char tmp[64]; fmt_float(tmp, sizeof tmp, xv_as_float64(v)); return strdup(tmp);
    }
    if (strcmp(k, K_CHAR_UTF8) == 0 || strcmp(k, K_CHAR_ASCII) == 0) return strndup2(body, blen);
    if (strcmp(k, K_CHAR) == 0) return utf32_to_utf8(body, blen);
    if (strcmp(k, K_RWIR) == 0) return strndup2(body + (blen >= 4 ? 4 : 0), blen >= 4 ? blen - 4 : 0);
    if (strcmp(k, K_RWFUNC) == 0) {
        sbuf_t b; sb_init(&b);
        sb_printf(&b, "r%d/w%d", (blen >= 2 ? rd16(body) : 0), (blen >= 4 ? rd16(body + 2) : 0));
        return sb_detach(&b);
    }
    if (strcmp(k, K_INDEX) == 0) {
        int n = 0;
        for (int32_t i = 0; i < blen; i++) if (body[i] == '\n') n++;
        if (blen > 0) n++;
        sbuf_t b; sb_init(&b); sb_printf(&b, "(%d)", n); return sb_detach(&b);
    }
    if (strcmp(k, K_DICT) == 0) {
        sbuf_t b; sb_init(&b);
        if (blen == 0) sb_puts(&b, "dict");
        else { int n = 1; for (int32_t i = 0; i < blen; i++) if (body[i] == '\n') n++; sb_printf(&b, "{%d}", n); }
        return sb_detach(&b);
    }
    return strndup2(body, blen);
}

/* ── 构造 ──────────────────────────────────────────────────────────── */

void xv_new_tlv(xval_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len, int32_t al) {
    uint32_t len;
    v->data = xv_encode_tlv(kind, 0, raw, raw_len, al, &len);
    v->len = len;
}

void xv_new_int64(xval_t *v, int64_t n) {
    uint8_t r[8];
    r[0] = n & 0xFF; r[1] = (n >> 8) & 0xFF; r[2] = (n >> 16) & 0xFF; r[3] = (n >> 24) & 0xFF;
    r[4] = (n >> 32) & 0xFF; r[5] = (n >> 40) & 0xFF; r[6] = (n >> 48) & 0xFF; r[7] = (n >> 56) & 0xFF;
    xv_new_tlv(v, K_INT64, r, 8, 1);
}
void xv_new_float64(xval_t *v, double f) {
    uint64_t u; memcpy(&u, &f, 8);
    uint8_t r[8];
    for (int i = 0; i < 8; i++) r[i] = (u >> (i * 8)) & 0xFF;
    xv_new_tlv(v, K_FLOAT64, r, 8, 1);
}
void xv_new_bool(xval_t *v, bool b) {
    uint8_t r = b ? 1 : 0;
    xv_new_tlv(v, K_BOOL, &r, 1, 1);
}
void xv_new_char_utf8(xval_t *v, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    xv_new_tlv(v, K_CHAR_UTF8, (const uint8_t *)s, sl, (int32_t)sl);
}
void xv_new_char_kind(xval_t *v, const char *kind, const char *s) {
    uint32_t sl = (uint32_t)strlen(s);
    xv_new_tlv(v, kind, (const uint8_t *)s, sl, (int32_t)sl);
}
void xv_new_char_utf32(xval_t *v, const char *s) {
    size_t len = strlen(s);
    sbuf_t raw; sb_init(&raw);
    size_t i = 0;
    while (i < len) {
        uint32_t cp = utf8_next(s, &i, len);
        uint8_t le[4] = { cp & 0xFF, (cp >> 8) & 0xFF, (cp >> 16) & 0xFF, (cp >> 24) & 0xFF };
        sb_putn(&raw, (const char *)le, 4);
    }
    xv_new_tlv(v, K_CHAR, (const uint8_t *)raw.p, (uint32_t)raw.len, (int32_t)(raw.len / 4));
    sb_free(&raw);
}
void xv_new_ptr(xval_t *v, const char *kind, const char *target, int32_t al) {
    uint32_t len;
    v->data = xv_encode_tlv(kind, 1, (const uint8_t *)target, (uint32_t)strlen(target), al, &len);
    v->len = len;
}
void xv_new_rwir(xval_t *v, int32_t nr, int32_t nw, const char *sig) {
    size_t sl = strlen(sig);
    uint8_t *raw = malloc(4 + sl);
    raw[0] = nr & 0xFF; raw[1] = (nr >> 8) & 0xFF;
    raw[2] = nw & 0xFF; raw[3] = (nw >> 8) & 0xFF;
    memcpy(raw + 4, sig, sl);
    xv_new_tlv(v, K_RWIR, raw, (uint32_t)(4 + sl), 1);
    free(raw);
}

void fmt_float(char *out, size_t cap, double v) {
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
