// builtin_string —— string·* 字符串 rwir

#include "builtin_internal.h"

static uint32_t utf8_decode_next(const char *s, size_t *i, size_t len) {
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

static uint32_t *string_runes(const kvlangXvalue_t *v, int *out_n) {
    if (kvlangXvalueKindIs(v, KVSPACE_KIND_CHAR)) {
        int n = kvlangXvalueArrayLen(v);
        uint32_t *r = malloc(sizeof(uint32_t) * (n > 0 ? n : 1));
        for (int i = 0; i < n; i++) r[i] = kvlangXvalueChar32At(v, i);
        *out_n = n;
        return r;
    }
    char *s = kvlangXvalueValueString(v);
    size_t len = strlen(s);
    int cap = 16, n = 0;
    uint32_t *r = malloc(sizeof(uint32_t) * cap);
    size_t i = 0;
    while (i < len) {
        if (n == cap) { cap *= 2; r = realloc(r, sizeof(uint32_t) * cap); }
        r[n++] = utf8_decode_next(s, &i, len);
    }
    free(s);
    *out_n = n;
    return r;
}

static void new_char32_cp(kvlangXvalue_t *out, uint32_t cp) {
    uint8_t le[4] = { cp & 0xFF, (cp >> 8) & 0xFF, (cp >> 16) & 0xFF, (cp >> 24) & 0xFF };
    uint8_t *o; uint32_t l; int32_t d = 1;
    kvspaceTlvEncode(KVSPACE_KIND_CHAR, le, 4, &d, 1, &o, &l);
    out->data = o; out->len = l;
}

static int write_char32(kvlangFrame_t *f, const uint32_t *r, int n) {
    kvlangXvalue_t e;
    if (n > 0) {
        kvlangStrbuf_t raw; kvlangStrbufInit(&raw);
        for (int i = 0; i < n; i++) {
            uint8_t le[4] = { r[i] & 0xFF, (r[i] >> 8) & 0xFF, (r[i] >> 16) & 0xFF, (r[i] >> 24) & 0xFF };
            kvlangStrbufPutn(&raw, (const char *)le, 4);
        }
        uint8_t *out; uint32_t len; int32_t d = n;
        kvspaceTlvEncode(KVSPACE_KIND_CHAR, (const uint8_t *)raw.p, (uint32_t)raw.len, &d, 1, &out, &len);
        kvlangStrbufFree(&raw);
        e.data = out; e.len = len;
    } else {
        uint8_t *out; uint32_t len; int32_t d = 0;
        kvspaceTlvEncode(KVSPACE_KIND_CHAR, (const uint8_t *)"", 0, &d, 1, &out, &len);
        e.data = out; e.len = len;
    }
    int rc = kvlangBuiltinWriteResult(f, &e);
    kvlangXvalueFree(&e);
    return rc;
}

static const char *var_len_char_err(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 ? "TypeError: char/utf8 is variable-width; index/code-point ops require char/utf32 or char/ascii" : NULL;
}

int kvlangBuiltinStringSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = NULL;
    if (n > 0) kvlangDisplay(&in[0], &s);
    else s = strdup("");
    if (f->inst->nw > 0) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
        kvlangXvalue_t v; kvlangXvalueNewCharUtf32(&v, s);
        kvlangKvPair_t p = { key, v };
        char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v); free(key); free(fr);
    }
    free(s);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

int kvlangBuiltinStringChar(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: string.char requires string and index"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int idx = (int)kvlangXvalueAsInt64(&in[1]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    int rc;
    if (idx < 0 || idx >= rn) rc = kvlangBuiltinSetErr(f, "IndexError: at: index %d out of bounds (char count=%d)", idx, rn);
    else { uint32_t one = r[idx]; rc = write_char32(f, &one, 1); }
    free(r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringOrd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: string.ord requires a string"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, rn == 0 ? -1 : (int64_t)r[0]);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); free(r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: string.cmp requires two strings"); }
    char *a = kvlangXvalueValueString(&in[0]), *b = kvlangXvalueValueString(&in[1]);
    int64_t r = strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, r);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); free(a); free(b); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringFind(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: string.find requires two strings"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int hn; uint32_t *hay = string_runes(&in[0], &hn);
    int nn; uint32_t *needle = string_runes(&in[1], &nn);
    int64_t r = -1;
    if (nn == 0) r = 0;
    else for (int i = 0; i + nn <= hn; i++) { bool m = true; for (int j = 0; j < nn; j++) if (hay[i + j] != needle[j]) { m = false; break; } if (m) { r = i; break; } }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, r);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); free(hay); free(needle); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringLen(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    int len = 0;
    if (n > 0) {
        if (var_len_char_err(kvlangXvalueKind(&in[0]))) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
        if (kvlangXvalueKindIs(&in[0], KVSPACE_KIND_CHAR)) len = kvlangXvalueArrayLen(&in[0]);
        else { uint32_t *r = string_runes(&in[0], &len); free(r); }
    }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, len);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringSlice(kvlangFrame_t *f) {
    kvlangXvalue_t in[3]; int n = kvlangBuiltinReadInputs(f, in, 3);
    if (n < 3) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: string.slice requires string, start, end"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int lo = (int)kvlangXvalueAsInt64(&in[1]), hi = (int)kvlangXvalueAsInt64(&in[2]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    if (lo < 0 || hi > rn || lo > hi) { free(r); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, rn); }
    write_char32(f, r + lo, hi - lo);
    free(r); kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinStringConcat(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0])) || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[1]))) {
        if (n >= 2) kvlangBuiltinSetErr(f, "TypeError: string.concat requires strings, got %s and %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]));
        else { kvlangXvalue_t e; kvlangXvalueNewCharUtf32(&e, ""); kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); }
        kvlangBuiltinFreeInputs(in, n); return n < 2 ? 0 : -1;
    }
    int a, b; uint32_t *ra = string_runes(&in[0], &a); uint32_t *rb = string_runes(&in[1], &b);
    uint32_t *r = malloc(sizeof(uint32_t) * (a + b));
    memcpy(r, ra, sizeof(uint32_t) * a); memcpy(r + a, rb, sizeof(uint32_t) * b);
    write_char32(f, r, a + b);
    free(r); free(ra); free(rb); kvlangBuiltinFreeInputs(in, n);
    return 0;
}

static int fmt_base(uint64_t u, int base, int neg, char *buf) {
    if (base < 2 || base > 36) base = 10;
    char tmp[65]; int n = 0;
    if (u == 0) tmp[n++] = '0';
    while (u) { int d = (int)(u % (unsigned)base); tmp[n++] = d < 10 ? '0' + d : 'a' + d - 10; u /= (unsigned)base; }
    int len = 0;
    if (neg) buf[len++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[len++] = tmp[i];
    return len;
}

static int write_ascii(kvlangFrame_t *f, const char *buf, int len) {
    uint32_t r[66];
    for (int i = 0; i < len; i++) r[i] = (uint32_t)(unsigned char)buf[i];
    return write_char32(f, r, len);
}

int kvlangBuiltinStringFormatInt(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    int64_t v = kvlangXvalueAsInt64(&in[0]);
    int base = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : 10;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    char buf[66]; int len = fmt_base(u, base, v < 0, buf);
    int rc = write_ascii(f, buf, len); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringFormatUint(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    uint64_t u = kvlangXvalueAsUint64(&in[0]);
    int base = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : 10;
    char buf[66]; int len = fmt_base(u, base, 0, buf);
    int rc = write_ascii(f, buf, len); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringParseInt(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = kvlangXvalueValueString(&in[0]);
    int base = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : 10;
    char *end = NULL; long long v = strtoll(s, &end, base);
    int bad = s[0] == '\0' || end == s || *end != '\0';
    free(s);
    if (bad) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "ValueError: string.parseint: invalid syntax"); }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, (int64_t)v);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringParseUint(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = kvlangXvalueValueString(&in[0]);
    int base = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : 10;
    char *end = NULL; unsigned long long v = strtoull(s, &end, base);
    int bad = s[0] == '\0' || end == s || *end != '\0';
    free(s);
    if (bad) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "ValueError: string.parseuint: invalid syntax"); }
    uint64_t u = (uint64_t)v; uint8_t r[8]; memcpy(r, &u, 8);
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
