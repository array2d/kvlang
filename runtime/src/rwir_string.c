// rwir_string —— string·* 字符串 rwir

#include "rwir_internal.h"

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

static int write_char32(kvlangFrame_t *f, const uint32_t *r, int n) {
    if (n == 1 && f->kvspace && f->kvspace->shm && f->instruction &&
        f->instruction->write_count == 1 && f->instruction->write_offs) {
        const char *target = f->instruction->writes[0].type;
        if (!target || !target[0]) {
            kvspaceKeyOff *wo = f->instruction->write_offs[0];
            if (wo && wo->resolved) {
                uint8_t *cur = NULL;
                uint32_t cur_len = 0;
                if (kvspaceGetByRef(f->kvspace->handle, &wo->off, wo->key, &cur,
                                    &cur_len) == 0 &&
                    cur && cur_len == 68 && cur[0] == 6 && cur[1] == 1 &&
                    xh_rd64(cur + 2) == 4 && xh_rd64(cur + 10) == 4 &&
                    memcmp(cur + XH_PREFIX, "[1]char/utf32", 14) == 0) {
                    cur[64] = (uint8_t)(r[0] & 0xFF);
                    cur[65] = (uint8_t)((r[0] >> 8) & 0xFF);
                    cur[66] = (uint8_t)((r[0] >> 16) & 0xFF);
                    cur[67] = (uint8_t)((r[0] >> 24) & 0xFF);
                    kvlangBuiltinNextPc(f);
                    return 0;
                }
            }
        }
    }
    if (n >= 0 && n <= 16) {
        uint8_t sbuf[128];
        uint32_t blen = (uint32_t)n * 4;
        memset(sbuf, 0, 64);
        sbuf[0] = 6;
        sbuf[1] = 1;
        for (int i = 0; i < 4; i++) {
            sbuf[2 + i] = (uint8_t)(blen >> (8 * i));
            sbuf[10 + i] = (uint8_t)(blen >> (8 * i));
        }
        snprintf((char *)sbuf + XH_PREFIX, 64 - XH_PREFIX, "[%d]char/utf32", n);
        for (int i = 0; i < n; i++) {
            sbuf[64 + i * 4 + 0] = (uint8_t)(r[i] & 0xFF);
            sbuf[64 + i * 4 + 1] = (uint8_t)((r[i] >> 8) & 0xFF);
            sbuf[64 + i * 4 + 2] = (uint8_t)((r[i] >> 16) & 0xFF);
            sbuf[64 + i * 4 + 3] = (uint8_t)((r[i] >> 24) & 0xFF);
        }
        kvlangXvalue_t e = {sbuf, 64 + blen};
        return kvlangBuiltinWriteResult(f, &e);
    }
    kvlangXvalue_t e;
    if (n > 0) {
        uint8_t *raw = malloc((size_t)n * 4);
        if (!raw)
            kvlangDie("out of memory writing char/utf32");
        for (int i = 0; i < n; i++) {
            raw[i * 4 + 0] = (uint8_t)(r[i] & 0xFF);
            raw[i * 4 + 1] = (uint8_t)((r[i] >> 8) & 0xFF);
            raw[i * 4 + 2] = (uint8_t)((r[i] >> 16) & 0xFF);
            raw[i * 4 + 3] = (uint8_t)((r[i] >> 24) & 0xFF);
        }
        uint8_t *out; uint32_t len; int32_t d = n;
        kvspaceXvalueEncode(KVSPACE_KIND_CHAR, raw, (uint32_t)n * 4, &d, 1, &out, &len);
        free(raw);
        e.bytes = out; e.length = len;
    } else {
        uint8_t *out; uint32_t len; int32_t d = 0;
        kvspaceXvalueEncode(KVSPACE_KIND_CHAR, (const uint8_t *)"", 0, &d, 1, &out, &len);
        e.bytes = out; e.length = len;
    }
    int rc = kvlangBuiltinEmitResult(f, &e);
    return rc;
}

static const char *variable_length_char_error(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 ? "TypeError: char/utf8 is variable-width; index/code-point ops require char/utf32 or char/ascii" : NULL;
}

int kvlangBuiltinStringSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = NULL;
    if (n > 0) kvlangDisplay(&in[0], &s);
    else s = strdup("");
    if (f->instruction->write_count > 0) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *key = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
        kvlangXvalue_t v; kvlangXvalueNewCharUtf32(&v, s);
        kvlangKvPair_t p = { key, v };
        char err[256]; kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
        free(key); free(fr);
    }
    free(s);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

static inline bool fast_utf32_bytes(const kvlangXvalue_t *v) {
    return v->bytes && v->length >= XH_PREFIX &&
           !xh_is_none(v->bytes) &&
           kvlangXvalueKindIs(v, KVSPACE_KIND_CHAR);
}

int kvlangBuiltinStringChar(kvlangFrame_t *f) {
    if (f->instruction && f->instruction->read_count >= 2) {
        const kvlangXvalue_t *in0 = &f->instruction->reads[0].value;
        const kvlangXvalue_t *in1 = &f->instruction->reads[1].value;
        if (fast_utf32_bytes(in0) && in1->bytes && !xh_is_none(in1->bytes)) {
            int idx = (int)kvlangScalarI64(kvlangXvalueScalar(in1));
            int rn = (int)(xh_content_len(in0->bytes) >> 2);
            if (idx < 0 || idx >= rn)
                return kvlangBuiltinSetError(
                    f, "IndexError: at: index %d out of bounds (char count=%d)",
                    idx, rn);
            const uint8_t *p = xh_body(in0->bytes) + (size_t)idx * 4;
            uint32_t one = xh_rd32(p);
            return write_char32(f, &one, 1);
        }
    }
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: string.char requires string and index"); }
    if (!kvlangXvalueNone(&in[0]) && kvlangXvalueKindIs(&in[0], KVSPACE_KIND_CHAR)) {
        int idx = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1]));
        int rn = (int)(xh_content_len_of(&in[0]) >> 2);
        if (idx < 0 || idx >= rn) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetError(f, "IndexError: at: index %d out of bounds (char count=%d)", idx, rn);
        }
        const uint8_t *p = xh_body(in[0].bytes) + (size_t)idx * 4;
        uint32_t one = xh_rd32(p);
        int rc = write_char32(f, &one, 1);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    { const char *cerr = variable_length_char_error(kvlangXvalueKind(&in[0])); if (cerr) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "%s", cerr); } }
    int idx = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1]));
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    int rc;
    if (idx < 0 || idx >= rn) rc = kvlangBuiltinSetError(f, "IndexError: at: index %d out of bounds (char count=%d)", idx, rn);
    else { uint32_t one = r[idx]; rc = write_char32(f, &one, 1); }
    free(r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringOrd(kvlangFrame_t *f) {
    if (f->instruction && f->instruction->read_count >= 1) {
        const kvlangXvalue_t *in0 = &f->instruction->reads[0].value;
        if (fast_utf32_bytes(in0)) {
            int rn = (int)(xh_content_len(in0->bytes) >> 2);
            int64_t cp = rn <= 0 ? -1 : (int64_t)xh_rd32(xh_body(in0->bytes));
            return kvlangBuiltinEmitInt64(f, cp);
        }
    }
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: string.ord requires a string"); }
    if (!kvlangXvalueNone(&in[0]) && kvlangXvalueKindIs(&in[0], KVSPACE_KIND_CHAR)) {
        int rn = (int)(xh_content_len_of(&in[0]) >> 2);
        int64_t cp = rn <= 0 ? -1 : (int64_t)xh_rd32(xh_body(in[0].bytes));
        int rc = kvlangBuiltinEmitInt64(f, cp);
        kvlangBuiltinFreeInputs(in, n);
        return rc;
    }
    { const char *cerr = variable_length_char_error(kvlangXvalueKind(&in[0])); if (cerr) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "%s", cerr); } }
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    int rc = kvlangBuiltinEmitInt64(f, rn == 0 ? -1 : (int64_t)r[0]);free(r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: string.cmp requires two strings"); }
    char *a = kvlangXvalueValueString(&in[0]), *b = kvlangXvalueValueString(&in[1]);
    int64_t r = strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
    int rc = kvlangBuiltinEmitInt64(f, r);free(a); free(b); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringFind(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: string.find requires two strings"); }
    { const char *cerr = variable_length_char_error(kvlangXvalueKind(&in[0])); if (cerr) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "%s", cerr); } }
    int hn; uint32_t *hay = string_runes(&in[0], &hn);
    int nn; uint32_t *needle = string_runes(&in[1], &nn);
    int64_t r = -1;
    if (nn == 0) r = 0;
    else for (int i = 0; i + nn <= hn; i++) { bool m = true; for (int j = 0; j < nn; j++) if (hay[i + j] != needle[j]) { m = false; break; } if (m) { r = i; break; } }
    int rc = kvlangBuiltinEmitInt64(f, r);free(hay); free(needle); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringLen(kvlangFrame_t *f) {
    if (f->instruction && f->instruction->read_count >= 1) {
        const kvlangXvalue_t *in0 = &f->instruction->reads[0].value;
        if (fast_utf32_bytes(in0)) {
            int len = (int)(xh_content_len(in0->bytes) >> 2);
            return kvlangBuiltinEmitInt64(f, len);
        }
    }
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    int len = 0;
    if (n > 0) {
        { const char *cerr = variable_length_char_error(kvlangXvalueKind(&in[0])); if (cerr) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "%s", cerr); } }
        if (kvlangXvalueKindIs(&in[0], KVSPACE_KIND_CHAR)) len = (int)(xh_content_len_of(&in[0]) >> 2);
        else { uint32_t *r = string_runes(&in[0], &len); free(r); }
    }
    int rc = kvlangBuiltinEmitInt64(f, len);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringSlice(kvlangFrame_t *f) {
    kvlangXvalue_t in[3]; int n = kvlangBuiltinReadInputs(f, in, 3);
    if (n < 3) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: string.slice requires string, start, end"); }
    { const char *cerr = variable_length_char_error(kvlangXvalueKind(&in[0])); if (cerr) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "%s", cerr); } }
    int lo = (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])), hi = (int)kvlangScalarI64(kvlangXvalueScalar(&in[2]));
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    if (lo < 0 || hi > rn || lo > hi) { free(r); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, rn); }
    write_char32(f, r + lo, hi - lo);
    free(r); kvlangBuiltinFreeInputs(in, n);
    return 0;
}

bool kvlangBuiltinCharConcat(const kvlangXvalue_t *a, const kvlangXvalue_t *b, kvlangXvalue_t *out) {
    const char *ka = kvlangXvalueKind(a), *kb = kvlangXvalueKind(b);
    /* utf32（4字节/码点布局）只能与 utf32 互拼；ascii ⊆ utf8 字节兼容，可互拼且不转码，
       同种保持、异种取 utf8。utf32 与非 utf32 混拼须显式转码，返 false 让调用方 throw。 */
    bool a32 = strcmp(ka, KVSPACE_KIND_CHAR) == 0, b32 = strcmp(kb, KVSPACE_KIND_CHAR) == 0;
    if (a32 != b32) return false;
    const char *rk = a32 ? KVSPACE_KIND_CHAR : (strcmp(ka, kb) == 0 ? ka : KVSPACE_KIND_CHAR_UTF8);
    char *sa = kvlangXvalueValueString(a), *sb = kvlangXvalueValueString(b);
    size_t la = strlen(sa), lb = strlen(sb);
    char *cat = malloc(la + lb + 1);
    memcpy(cat, sa, la); memcpy(cat + la, sb, lb); cat[la + lb] = 0;
    if (a32) kvlangXvalueNewCharUtf32(out, cat);
    else kvlangXvalueNewCharKind(out, rk, cat);
    free(cat); free(sa); free(sb);
    return true;
}

int kvlangBuiltinStringConcat(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) {
        kvlangXvalue_t e; kvlangXvalueNewCharUtf32(&e, ""); kvlangBuiltinEmitResult(f, &e);
        kvlangBuiltinFreeInputs(in, n); return 0;
    }
    const char *k0 = kvlangXvalueKind(&in[0]), *k1 = kvlangXvalueKind(&in[1]);
    if (!kvlangXvalueIsCharKind(k0) || !kvlangXvalueIsCharKind(k1)) {
        kvlangBuiltinSetError(f, "TypeError: string.concat requires strings, got %s and %s", k0, k1);
        kvlangBuiltinFreeInputs(in, n); return -1;
    }
    kvlangXvalue_t r;
    if (!kvlangBuiltinCharConcat(&in[0], &in[1], &r)) {
        kvlangBuiltinSetError(f, "TypeError: cannot concat %s with %s; convert encoding explicitly (char/utf8|char/utf32|char/ascii)", k0, k1);
        kvlangBuiltinFreeInputs(in, n); return -1;
    }
    int rc = kvlangBuiltinEmitResult(f, &r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int format_base(uint64_t u, int base, int neg, char *buf) {
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
    int64_t v = kvlangScalarI64(kvlangXvalueScalar(&in[0]));
    int base = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 10;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    char buf[66]; int len = format_base(u, base, v < 0, buf);
    int rc = write_ascii(f, buf, len); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringFormatUint(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    uint64_t u = kvlangScalarU64(kvlangXvalueScalar(&in[0]));
    int base = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 10;
    char buf[66]; int len = format_base(u, base, 0, buf);
    int rc = write_ascii(f, buf, len); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringParseInt(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = kvlangXvalueValueString(&in[0]);
    int base = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 10;
    char *end = NULL; long long v = strtoll(s, &end, base);
    int bad = s[0] == '\0' || end == s || *end != '\0';
    free(s);
    if (bad) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "ValueError: string.parseint: invalid syntax"); }
    int rc = kvlangBuiltinEmitInt64(f, (int64_t)v);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinStringParseUint(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *s = kvlangXvalueValueString(&in[0]);
    int base = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 10;
    char *end = NULL; unsigned long long v = strtoull(s, &end, base);
    int bad = s[0] == '\0' || end == s || *end != '\0';
    free(s);
    if (bad) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "ValueError: string.parseuint: invalid syntax"); }
    uint64_t u = (uint64_t)v; uint8_t r[8]; memcpy(r, &u, 8);
    kvlangXvalue_t e; kvlangXvalueNew(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* string·parsefloat(s) -> f：对齐 Go strconv.ParseFloat / Python float() / Rust parse::<f64>()。
 * 与 parseint 同形：整串须消费完（无尾随空白/垃圾），否则 ValueError；无 base（浮点无进制）。 */
int kvlangBuiltinStringParseFloat(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) return kvlangBuiltinSetError(f, "TypeError: string.parsefloat requires a string");
    char *s = kvlangXvalueValueString(&in[0]);
    char *end = NULL; double v = strtod(s, &end);
    int bad = s[0] == '\0' || end == s || *end != '\0';
    free(s);
    if (bad) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "ValueError: string.parsefloat: invalid syntax"); }
    uint8_t r[8]; memcpy(r, &v, 8);
    kvlangXvalue_t e; kvlangXvalueNew(&e, KVSPACE_KIND_FLOAT64, r, 8, 1);
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}
