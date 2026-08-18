#include "runtime_internal.h"
#include <math.h>
#include <time.h>

typedef int (*bi_fn)(frame_t *f);

/* collection 模块 handler（builtin_coll.c） */
int bi_array(frame_t *f), bi_len(frame_t *f), bi_at(frame_t *f), bi_set(frame_t *f),
    bi_has(frame_t *f), bi_sort(frame_t *f), bi_scatter(frame_t *f), bi_compact(frame_t *f),
    bi_append(frame_t *f), bi_slice(frame_t *f), bi_dict(frame_t *f), bi_string_set(frame_t *f),
    bi_string_char(frame_t *f), bi_string_ord(frame_t *f), bi_string_cmp(frame_t *f),
    bi_string_find(frame_t *f), bi_string_len(frame_t *f), bi_string_slice(frame_t *f),
    bi_string_concat(frame_t *f), bi_time_now(frame_t *f), bi_time_sub(frame_t *f),
    bi_time_add(frame_t *f), bi_dur_from(frame_t *f), bi_dur_to(frame_t *f),
    bi_dur_arith(frame_t *f), bi_dur_cmp(frame_t *f), bi_time_cmp(frame_t *f),
    bi_rand_uint64(frame_t *f), bi_rand_int63(frame_t *f), bi_rand_intn(frame_t *f),
    bi_kvhas(frame_t *f), bi_kvat(frame_t *f), bi_debugger(frame_t *f);

/* ── 类型 helper（对齐 Go isIntKind 含 uint）────────────────────── */

static bool is_int_kind(const char *k) { return xv_is_int_kind(k) || xv_is_uint_kind(k); }
static bool is_float_kind(const char *k) { return xv_is_float_kind(k); }
static bool is_unsigned_kind(const char *k) { return xv_is_uint_kind(k); }
static bool is_numeric(const xval_t *v) { return xv_is_num_kind(xv_kind(v)); }

static int int_width(const char *k) {
    if (strcmp(k, K_INT8) == 0 || strcmp(k, K_UINT8) == 0) return 8;
    if (strcmp(k, K_INT16) == 0 || strcmp(k, K_UINT16) == 0) return 16;
    if (strcmp(k, K_INT32) == 0 || strcmp(k, K_UINT32) == 0) return 32;
    if (strcmp(k, K_INT64) == 0 || strcmp(k, K_UINT64) == 0) return 64;
    return 0;
}

static const char *wider_int_kind(const char *a, const char *b) {
    int aw = int_width(a), bw = int_width(b);
    bool au = is_unsigned_kind(a), bu = is_unsigned_kind(b);
    if (au && bu) return aw >= bw ? a : b;
    if (!au && !bu) return aw >= bw ? a : b;
    int w = aw > bw ? aw : bw;
    switch (w) {
    case 8: return K_INT16;
    case 16: return K_INT32;
    case 32: return K_INT64;
    default: return K_INT64;
    }
}

static const char *wider_float_kind(const char *a, const char *b) {
    if (strcmp(a, K_FLOAT64) == 0 || strcmp(b, K_FLOAT64) == 0) return K_FLOAT64;
    if (strcmp(a, K_FLOAT32) == 0 || strcmp(b, K_FLOAT32) == 0) return K_FLOAT32;
    return K_FLOAT64;
}

static void narrow_int(const char *a, const char *b, int64_t v, xval_t *out) {
    const char *k = wider_int_kind(a, b);
    if (strcmp(k, K_INT8) == 0) { int8_t x = (int8_t)v; xv_new_tlv(out, K_INT8, (uint8_t *)&x, 1, 1); return; }
    if (strcmp(k, K_INT16) == 0) { int16_t x = (int16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; xv_new_tlv(out, K_INT16, r, 2, 1); return; }
    if (strcmp(k, K_INT32) == 0) { int32_t x = (int32_t)v; uint8_t r[4]; memcpy(r, &x, 4); xv_new_tlv(out, K_INT32, r, 4, 1); return; }
    if (strcmp(k, K_UINT8) == 0) { uint8_t x = (uint8_t)v; xv_new_tlv(out, K_UINT8, &x, 1, 1); return; }
    if (strcmp(k, K_UINT16) == 0) { uint16_t x = (uint16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; xv_new_tlv(out, K_UINT16, r, 2, 1); return; }
    if (strcmp(k, K_UINT32) == 0) { uint32_t x = (uint32_t)v; uint8_t r[4]; memcpy(r, &x, 4); xv_new_tlv(out, K_UINT32, r, 4, 1); return; }
    if (strcmp(k, K_UINT64) == 0) { uint64_t x = (uint64_t)v; uint8_t r[8]; memcpy(r, &x, 8); xv_new_tlv(out, K_UINT64, r, 8, 1); return; }
    xv_new_int64(out, v);
}

static void narrow_float(const char *a, const char *b, double v, xval_t *out) {
    if (strcmp(wider_float_kind(a, b), K_FLOAT32) == 0) {
        float f = (float)v; uint8_t r[4]; memcpy(r, &f, 4);
        xv_new_tlv(out, K_FLOAT32, r, 4, 1);
    } else xv_new_float64(out, v);
}

static int cmp_int(const xval_t *a, const xval_t *b) {
    bool au = is_unsigned_kind(xv_kind(a)), bu = is_unsigned_kind(xv_kind(b));
    if (!au && !bu) { int64_t ai = xv_as_int64(a), bi = xv_as_int64(b); return ai < bi ? -1 : ai > bi ? 1 : 0; }
    if (au && bu) { uint64_t x = xv_as_uint64(a), y = xv_as_uint64(b); return x < y ? -1 : x > y ? 1 : 0; }
    if (au && !bu) { int64_t bi = xv_as_int64(b); if (bi < 0) return 1; uint64_t x = xv_as_uint64(a); return x < (uint64_t)bi ? -1 : x > (uint64_t)bi ? 1 : 0; }
    int64_t ai = xv_as_int64(a); if (ai < 0) return -1; uint64_t y = xv_as_uint64(b);
    return (uint64_t)ai < y ? -1 : (uint64_t)ai > y ? 1 : 0;
}

/* ── resolve ───────────────────────────────────────────────────────── */

char *bi_func_frame_root(kv_t *kv, const char *frame_root) {
    char *cur = strdup(frame_root);
    for (;;) {
        sbuf_t k; sb_init(&k);
        char *stk = kt_stack(cur);
        sb_puts(&k, stk); free(stk);
        sb_puts(&k, SEG_LIB);
        xval_t v; xv_zero(&v);
        kv_get_one(kv, k.p, &v);
        bool found = !xv_none(&v);
        xv_free(&v); sb_free(&k);
        if (found) return cur;
        char *parent = kt_parent_frame(cur);
        if (parent[0] == 0) { free(parent); return cur; }
        free(cur); cur = parent;
    }
}

void bi_resolve_read_value(kv_t *kv, const char *frame_path, const char *name,
                           const xval_t *val, xval_t *out) {
    xv_zero(out);
    if (val && !xv_none(val) && !xv_kind_is(val, K_RWIR) && !xv_kind_is(val, K_RWFUNC)) {
        kvhead_t h; xv_head(val, &h);
        int32_t blen; const uint8_t *body = xv_body(val, &h, &blen);
        xv_new_tlv(out, xv_kind(val), body, (uint32_t)blen, h.array_len);
        return;
    }
    if (!name || !name[0]) return;
    if (name[0] == '/') { kv_get_one(kv, name, out); return; }
    char *rw = bi_func_frame_root(kv, frame_path);
    sbuf_t key; sb_init(&key);
    char *stk = kt_stack(rw);
    sb_puts(&key, stk); free(stk); sb_puts(&key, name);
    xval_t pv; xv_zero(&pv);
    kv_get_one(kv, key.p, &pv);
    if (xv_is_ptr(&pv)) {
        char *target = xv_ptr_target(&pv);
        sbuf_t ak; sb_init(&ak);
        char *stk2 = kt_stack(rw);
        sb_puts(&ak, stk2); free(stk2); sb_puts(&ak, target);
        free(target);
        xval_t av; xv_zero(&av);
        kv_get_one(kv, ak.p, &av);
        if (!xv_none(&av)) {
            char *path = xv_value_string(&av);
            kv_get_one(kv, path, out);
            free(path);
        }
        xv_free(&av); sb_free(&ak);
    } else if (!xv_none(&pv)) {
        *out = pv; pv.data = NULL; pv.len = 0;
    }
    xv_free(&pv); sb_free(&key); free(rw);
}

char *bi_resolve_write_slot(kv_t *kv, const char *frame_path, const char *name) {
    if (name[0] == '/') return strdup(name);
    char *rw = bi_func_frame_root(kv, frame_path);
    sbuf_t key; sb_init(&key);
    char *stk = kt_stack(rw);
    sb_puts(&key, stk); free(stk); sb_puts(&key, name);
    xval_t pv; xv_zero(&pv);
    kv_get_one(kv, key.p, &pv);
    char *result = NULL;
    if (xv_is_ptr(&pv)) {
        char *target = xv_ptr_target(&pv);
        sbuf_t ak; sb_init(&ak);
        char *stk2 = kt_stack(rw);
        sb_puts(&ak, stk2); free(stk2); sb_puts(&ak, target);
        free(target);
        xval_t av; xv_zero(&av);
        kv_get_one(kv, ak.p, &av);
        if (!xv_none(&av)) {
            xval_t v = av; av.data = NULL; av.len = 0;
            for (;;) {
                if (!xv_is_char_kind(xv_kind(&v))) break;
                char *p = xv_value_string(&v);
                xval_t next; xv_zero(&next);
                kv_get_one(kv, p, &next);
                if (xv_none(&next) || !xv_is_char_kind(xv_kind(&next))) { result = p; break; }
                free(p);
                xv_free(&v); v = next;
            }
            xv_free(&v);
        }
        xv_free(&av); sb_free(&ak);
    }
    xv_free(&pv); sb_free(&key); free(rw);
    if (result) return result;
    /* fallback: Stack(rw) + name */
    char *rw2 = bi_func_frame_root(kv, frame_path);
    sbuf_t o; sb_init(&o);
    char *stk3 = kt_stack(rw2);
    sb_puts(&o, stk3); free(stk3); sb_puts(&o, name);
    free(rw2);
    return sb_detach(&o);
}

/* ── coerce / display ─────────────────────────────────────────────── */

static bool try_parse_int(const char *s, int64_t *out) {
    if (!s || !s[0]) return false;
    char *end; long long v = strtoll(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = v; return true;
}

bool bi_try_parse_number(const char *s, xval_t *out) {
    xv_zero(out);
    if (!s || !s[0]) return false;
    char c0 = s[0];
    bool num = (c0 >= '0' && c0 <= '9') || (c0 == '-' && s[1] >= '0' && s[1] <= '9');
    if (!num) return false;
    int64_t iv;
    if (try_parse_int(s, &iv)) { xv_new_int64(out, iv); return true; }
    if (c0 != '-' && !strpbrk(s, ".eE")) {
        char *end; unsigned long long uv = strtoull(s, &end, 10);
        if (end != s && *end == 0) {
            uint8_t r[8]; memcpy(r, &uv, 8);
            xv_new_tlv(out, K_UINT64, r, 8, 1); return true;
        }
    }
    char *end; double f = strtod(s, &end);
    if (end != s && *end == 0) { xv_new_float64(out, f); return true; }
    return false;
}

static void xvalue_at(const xval_t *v, int i, xval_t *out) {
    xv_zero(out);
    int n = xv_array_len(v);
    if (i < 0 || i >= n) return;
    const char *k = xv_kind(v);
    int sz = xv_elem_size(k);
    if (sz <= 0) return;
    kvhead_t h; kvspace_decode_head(v->data, v->len, &h);
    const uint8_t *body = v->data + h.body_offset;
    xv_new_tlv(out, k, body + i * sz, (uint32_t)sz, 1);
}

void display(const xval_t *v, char **out);

static void format_array(const xval_t *v, char **out) {
    int n = xv_array_len(v);
    sbuf_t b; sb_init(&b);
    sb_putc(&b, '[');
    for (int i = 0; i < n; i++) {
        if (i) sb_puts(&b, ", ");
        xval_t e; xvalue_at(v, i, &e);
        char *s; display(&e, &s);
        sb_puts(&b, s); free(s); xv_free(&e);
    }
    sb_putc(&b, ']');
    *out = sb_detach(&b);
}

void display(const xval_t *v, char **out) {
    if (xv_is_char_kind(xv_kind(v))) { *out = xv_value_string(v); return; }
    if (xv_array_len(v) > 1) { format_array(v, out); return; }
    *out = xv_value_string(v);
}

/* ── frame helper ─────────────────────────────────────────────────── */

static int read_inputs(frame_t *f, xval_t *out, int cap) {
    char *fr = kt_frame_root(f->pc);
    char *ff = bi_func_frame_root(f->kv, fr);
    free(fr);
    int n = 0;
    for (int i = 0; i < f->inst->nr && n < cap; i++) {
        bi_resolve_read_value(f->kv, ff, f->inst->reads[i].name, &f->inst->reads[i].val, &out[n]);
        n++;
    }
    free(ff);
    return n;
}

static void free_inputs(xval_t *in, int n) { for (int i = 0; i < n; i++) xv_free(&in[i]); }

static void next_pc(frame_t *f) {
    sbuf_t npc; sb_init(&npc);
    rwir_next_pc(f->pc, &npc);
    vt_set(f->kv, f->vtid, npc.p, "running");
    sb_free(&npc);
}

static int write_result(frame_t *f, const xval_t *result) {
    if (f->inst->nw > 0) {
        char *fr = kt_frame_root(f->pc);
        char *key = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
        free(fr);
        kv_pair_t pair = { key, *result };
        char err[256];
        kv_set(f->kv, &pair, 1, err, sizeof err);
        free(key);
    }
    next_pc(f);
    return 0;
}

static int set_err(frame_t *f, const char *fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    vt_set_error(f->kv, f->vtid, f->pc, msg);
    return -1;
}

/* ── 数值算子 ─────────────────────────────────────────────────────── */

static int bi_add(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n == 2 && xv_is_char_kind(xv_kind(&in[0])) && xv_is_char_kind(xv_kind(&in[1]))) {
        char *a = xv_value_string(&in[0]), *b = xv_value_string(&in[1]);
        sbuf_t s; sb_init(&s); sb_puts(&s, a); sb_puts(&s, b);
        xval_t r; xv_new_char_utf32(&r, s.p);
        rc = write_result(f, &r);
        xv_free(&r); free(a); free(b); sb_free(&s);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        if (is_int_kind(xv_kind(&in[0])) && is_int_kind(xv_kind(&in[1]))) {
            xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_int64(&in[0]) + xv_as_int64(&in[1]), &r);
            rc = write_result(f, &r); xv_free(&r);
        } else {
            xval_t r; narrow_float(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_float64(&in[0]) + xv_as_float64(&in[1]), &r);
            rc = write_result(f, &r); xv_free(&r);
        }
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? xv_kind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int bi_sub(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n == 1) {
        xval_t r; xv_new_int64(&r, -xv_as_int64(&in[0]));
        rc = write_result(f, &r); xv_free(&r);
    } else if (n >= 2 && is_int_kind(xv_kind(&in[0])) && is_int_kind(xv_kind(&in[1]))) {
        xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_int64(&in[0]) - xv_as_int64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        xval_t r; narrow_float(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_float64(&in[0]) - xv_as_float64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? xv_kind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int bi_mul(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n >= 2 && is_int_kind(xv_kind(&in[0])) && is_int_kind(xv_kind(&in[1]))) {
        xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_int64(&in[0]) * xv_as_int64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        xval_t r; narrow_float(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_float64(&in[0]) * xv_as_float64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? xv_kind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int bi_div(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n < 2) { rc = set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return rc; }
    if (xv_as_float64(&in[1]) == 0) { rc = set_err(f, "ZeroDivisionError: division by zero"); free_inputs(in, n); return rc; }
    if (is_int_kind(xv_kind(&in[0])) && is_int_kind(xv_kind(&in[1]))) {
        xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_int64(&in[0]) / xv_as_int64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    } else {
        xval_t r; narrow_float(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_float64(&in[0]) / xv_as_float64(&in[1]), &r);
        rc = write_result(f, &r); xv_free(&r);
    }
    free_inputs(in, n);
    return rc;
}

static int bi_mod(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n < 2 || !is_int_kind(xv_kind(&in[0])) || !is_int_kind(xv_kind(&in[1]))) {
        rc = set_err(f, "TypeError: expected integer, got %s", n ? xv_kind(&in[0]) : "none");
        free_inputs(in, n); return rc;
    }
    int64_t b = xv_as_int64(&in[1]);
    if (b == 0) { rc = set_err(f, "ZeroDivisionError: modulo by zero"); free_inputs(in, n); return rc; }
    xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), xv_as_int64(&in[0]) % b, &r);
    rc = write_result(f, &r); xv_free(&r);
    free_inputs(in, n);
    return rc;
}

typedef enum { CMP_EQ, CMP_NEQ, CMP_LT, CMP_GT, CMP_LE, CMP_GE } cmp_op;

static int bi_cmp(frame_t *f, cmp_op op) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return -1; }
    bool allow_null = (op == CMP_EQ || op == CMP_NEQ);
    if (xv_none(&in[0]) || xv_none(&in[1])) {
        if (!allow_null) { set_err(f, "TypeError: None in comparison"); free_inputs(in, n); return -1; }
        bool eq = xv_none(&in[0]) == xv_none(&in[1]);
        bool r = (op == CMP_EQ) ? eq : (op == CMP_NEQ) ? !eq : false;
        xval_t rv; xv_new_bool(&rv, r);
        int rc = write_result(f, &rv); xv_free(&rv); free_inputs(in, n); return rc;
    }
    bool r;
    const char *ka = xv_kind(&in[0]), *kb = xv_kind(&in[1]);
    if (is_int_kind(ka) && is_int_kind(kb)) {
        int c = cmp_int(&in[0], &in[1]);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
    } else if (is_numeric(&in[0]) && is_numeric(&in[1])) {
        double a = xv_as_float64(&in[0]), b = xv_as_float64(&in[1]);
        r = op == CMP_EQ ? a == b : op == CMP_NEQ ? a != b : op == CMP_LT ? a < b : op == CMP_GT ? a > b : op == CMP_LE ? a <= b : a >= b;
    } else if (xv_is_char_kind(ka) && xv_is_char_kind(kb)) {
        char *a = xv_value_string(&in[0]), *b = xv_value_string(&in[1]);
        int c = strcmp(a, b);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
        free(a); free(b);
    } else if (strcmp(ka, K_BOOL) == 0 && strcmp(kb, K_BOOL) == 0) {
        bool a = xv_as_bool(&in[0]), b = xv_as_bool(&in[1]);
        r = op == CMP_EQ ? a == b : op == CMP_NEQ ? a != b : op == CMP_LT ? a < b : op == CMP_GT ? a > b : op == CMP_LE ? a <= b : a >= b;
    } else {
        set_err(f, "TypeError: cannot compare %s with %s", ka, kb); free_inputs(in, n); return -1;
    }
    xval_t rv; xv_new_bool(&rv, r);
    int rc = write_result(f, &rv); xv_free(&rv); free_inputs(in, n);
    return rc;
}

static int bi_eq(frame_t *f) { return bi_cmp(f, CMP_EQ); }
static int bi_neq(frame_t *f) { return bi_cmp(f, CMP_NEQ); }
static int bi_lt(frame_t *f) { return bi_cmp(f, CMP_LT); }
static int bi_gt(frame_t *f) { return bi_cmp(f, CMP_GT); }
static int bi_le(frame_t *f) { return bi_cmp(f, CMP_LE); }
static int bi_ge(frame_t *f) { return bi_cmp(f, CMP_GE); }

static int bi_and(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    xval_t r; xv_new_bool(&r, n >= 2 && xv_as_bool(&in[0]) && xv_as_bool(&in[1]));
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_or(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    xval_t r; xv_new_bool(&r, n >= 2 && (xv_as_bool(&in[0]) || xv_as_bool(&in[1])));
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_not(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    xval_t r; xv_new_bool(&r, !(n >= 1 && xv_as_bool(&in[0])));
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}

static int bi_bit(frame_t *f, int op) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !is_int_kind(xv_kind(&in[0])) || !is_int_kind(xv_kind(&in[1]))) {
        set_err(f, "TypeError: expected integer, got %s", n ? xv_kind(&in[0]) : "none");
        free_inputs(in, n); return -1;
    }
    int64_t a = xv_as_int64(&in[0]), b = xv_as_int64(&in[1]);
    int64_t v = op == 0 ? a & b : op == 1 ? a | b : op == 2 ? a ^ b : op == 3 ? (a << (uint64_t)b) : (a >> (uint64_t)b);
    xval_t r; narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), v, &r);
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_bitand(frame_t *f) { return bi_bit(f, 0); }
static int bi_bitor(frame_t *f) { return bi_bit(f, 1); }
static int bi_bitxor(frame_t *f) { return bi_bit(f, 2); }
static int bi_shl(frame_t *f) { return bi_bit(f, 3); }
static int bi_shr(frame_t *f) { return bi_bit(f, 4); }

/* math */
static int bi_math_unary(frame_t *f, int op) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || !is_numeric(&in[0])) { set_err(f, "TypeError: expected numeric, got %s", n ? xv_kind(&in[0]) : "none"); free_inputs(in, n); return -1; }
    xval_t r;
    double x = xv_as_float64(&in[0]);
    switch (op) {
    case 0: xv_new_float64(&r, sqrt(x)); break;
    case 1: xv_new_float64(&r, exp(x)); break;
    case 2: xv_new_float64(&r, log(x)); break;
    case 3: /* neg */ if (is_float_kind(xv_kind(&in[0]))) { narrow_float(xv_kind(&in[0]), xv_kind(&in[0]), -x, &r); } else { narrow_int(xv_kind(&in[0]), xv_kind(&in[0]), -xv_as_int64(&in[0]), &r); } break;
    case 4: /* abs */ if (is_float_kind(xv_kind(&in[0]))) { narrow_float(xv_kind(&in[0]), xv_kind(&in[0]), fabs(x), &r); } else { narrow_int(xv_kind(&in[0]), xv_kind(&in[0]), xv_as_int64(&in[0]) < 0 ? -xv_as_int64(&in[0]) : xv_as_int64(&in[0]), &r); } break;
    case 5: xv_new_int64(&r, x < 0 ? -1 : x > 0 ? 1 : 0); break;
    }
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_sqrt(frame_t *f) { return bi_math_unary(f, 0); }
static int bi_exp(frame_t *f) { return bi_math_unary(f, 1); }
static int bi_log(frame_t *f) { return bi_math_unary(f, 2); }
static int bi_neg(frame_t *f) { return bi_math_unary(f, 3); }
static int bi_abs(frame_t *f) { return bi_math_unary(f, 4); }
static int bi_sign(frame_t *f) { return bi_math_unary(f, 5); }

static int bi_pow(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !is_numeric(&in[0]) || !is_numeric(&in[1])) { set_err(f, "TypeError: expected numeric"); free_inputs(in, n); return -1; }
    xval_t r; xv_new_float64(&r, pow(xv_as_float64(&in[0]), xv_as_float64(&in[1])));
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}

static int bi_maxmin(frame_t *f, bool is_max) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return -1; }
    xval_t r;
    if (is_int_kind(xv_kind(&in[0])) && is_int_kind(xv_kind(&in[1]))) {
        int c = cmp_int(&in[0], &in[1]);
        bool take_a = (is_max && c >= 0) || (!is_max && c <= 0);
        narrow_int(xv_kind(&in[0]), xv_kind(&in[1]), take_a ? xv_as_int64(&in[0]) : xv_as_int64(&in[1]), &r);
    } else if (is_numeric(&in[0]) && is_numeric(&in[1])) {
        double a = xv_as_float64(&in[0]), b = xv_as_float64(&in[1]);
        bool take_a = (is_max && a >= b) || (!is_max && a <= b);
        narrow_float(xv_kind(&in[0]), xv_kind(&in[1]), take_a ? a : b, &r);
    } else { set_err(f, "TypeError: max/min requires numeric, got %s and %s", xv_kind(&in[0]), xv_kind(&in[1])); free_inputs(in, n); return -1; }
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_max(frame_t *f) { return bi_maxmin(f, true); }
static int bi_min(frame_t *f) { return bi_maxmin(f, false); }

/* cast */
static int bi_cast_num(frame_t *f, const char *kind) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || xv_none(&in[0])) { set_err(f, "TypeError: cannot cast None"); free_inputs(in, n); return -1; }
    xval_t r;
    if (strcmp(kind, K_BOOL) == 0) xv_new_bool(&r, xv_as_bool(&in[0]));
    else if (strcmp(kind, K_FLOAT32) == 0) { float fv = (float)xv_as_float64(&in[0]); uint8_t b[4]; memcpy(b, &fv, 4); xv_new_tlv(&r, K_FLOAT32, b, 4, 1); }
    else if (strcmp(kind, K_FLOAT64) == 0) xv_new_float64(&r, xv_as_float64(&in[0]));
    else { int64_t v = xv_as_int64(&in[0]); narrow_int(kind, kind, v, &r); }
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}
static int bi_cast_bool(frame_t *f) { return bi_cast_num(f, K_BOOL); }
static int bi_cast_int8(frame_t *f) { return bi_cast_num(f, K_INT8); }
static int bi_cast_int16(frame_t *f) { return bi_cast_num(f, K_INT16); }
static int bi_cast_int32(frame_t *f) { return bi_cast_num(f, K_INT32); }
static int bi_cast_int64(frame_t *f) { return bi_cast_num(f, K_INT64); }
static int bi_cast_uint8(frame_t *f) { return bi_cast_num(f, K_UINT8); }
static int bi_cast_uint16(frame_t *f) { return bi_cast_num(f, K_UINT16); }
static int bi_cast_uint32(frame_t *f) { return bi_cast_num(f, K_UINT32); }
static int bi_cast_uint64(frame_t *f) { return bi_cast_num(f, K_UINT64); }
static int bi_cast_f32(frame_t *f) { return bi_cast_num(f, K_FLOAT32); }
static int bi_cast_f64(frame_t *f) { return bi_cast_num(f, K_FLOAT64); }

static int bi_cast_char(frame_t *f, const char *kind) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || xv_none(&in[0])) { set_err(f, "TypeError: char conversion requires a value"); free_inputs(in, n); return -1; }
    char *s = xv_value_string(&in[0]);
    xval_t r;
    if (strcmp(kind, K_CHAR) == 0) xv_new_char_utf32(&r, s);
    else xv_new_char_kind(&r, kind, s);
    int rc = write_result(f, &r); xv_free(&r); free(s); free_inputs(in, n);
    return rc;
}
static int bi_cast_char32(frame_t *f) { return bi_cast_char(f, K_CHAR); }
static int bi_cast_char8(frame_t *f) { return bi_cast_char(f, K_CHAR_UTF8); }
static int bi_cast_char_ascii(frame_t *f) { return bi_cast_char(f, K_CHAR_ASCII); }

/* ── 注册表 ───────────────────────────────────────────────────────── */

/* 数字多类型运算融合为单条：派发前 strip_num_kind 剥掉 <numkind>. 前缀，
 * int64.add / float32.add … 全部归到同一条 add（union 语义），bi_* 按操作数 kind 归约。 */
static const struct { const char *op; bi_fn fn; } builtins[] = {
    {"add", bi_add}, {"+", bi_add},
    {"sub", bi_sub}, {"-", bi_sub},
    {"mul", bi_mul}, {"×", bi_mul},
    {"div", bi_div}, {"÷", bi_div},
    {"mod", bi_mod}, {"%", bi_mod},
    {"eq", bi_eq}, {"==", bi_eq},
    {"neq", bi_neq}, {"!=", bi_neq}, {"≠", bi_neq},
    {"lt", bi_lt}, {"<", bi_lt},
    {"gt", bi_gt}, {">", bi_gt},
    {"le", bi_le}, {"<=", bi_le}, {"≤", bi_le},
    {"ge", bi_ge}, {">=", bi_ge}, {"≥", bi_ge},
    {"and", bi_and}, {"&&", bi_and},
    {"or", bi_or}, {"||", bi_or},
    {"not", bi_not}, {"!", bi_not},
    {"bitand", bi_bitand}, {"&", bi_bitand},
    {"bitor", bi_bitor}, {"|", bi_bitor},
    {"bitxor", bi_bitxor}, {"^", bi_bitxor},
    {"shl", bi_shl}, {"<<", bi_shl},
    {"shr", bi_shr}, {">>", bi_shr},
    {"pow", bi_pow},
    {"sqrt", bi_sqrt}, {"√", bi_sqrt},
    {"exp", bi_exp},
    {"log", bi_log},
    {"neg", bi_neg},
    {"abs", bi_abs},
    {"sign", bi_sign},
    {"max", bi_max}, {"min", bi_min},
    /* cast */
    {"bool", bi_cast_bool}, {"int8", bi_cast_int8}, {"int16", bi_cast_int16},
    {"int32", bi_cast_int32}, {"int64", bi_cast_int64}, {"uint8", bi_cast_uint8},
    {"uint16", bi_cast_uint16}, {"uint32", bi_cast_uint32}, {"uint64", bi_cast_uint64},
    {"float32", bi_cast_f32}, {"float64", bi_cast_f64},
    {"char/utf32", bi_cast_char32}, {"char/utf8", bi_cast_char8}, {"char/ascii", bi_cast_char_ascii},
    /* collection */
    {"array", bi_array}, {"len", bi_len}, {"at", bi_at}, {"set", bi_set}, {"has", bi_has},
    {"array.sort", bi_sort}, {"array.scatter", bi_scatter}, {"array.compact", bi_compact},
    {"array.append", bi_append}, {"array.slice", bi_slice},
    {"dict", bi_dict},
    {"string.set", bi_string_set}, {"string.char", bi_string_char}, {"string.ord", bi_string_ord},
    {"string.cmp", bi_string_cmp}, {"string.find", bi_string_find}, {"string.len", bi_string_len},
    {"string.slice", bi_string_slice}, {"string.concat", bi_string_concat},
    {"time.now", bi_time_now}, {"time.sub", bi_time_sub}, {"time.add", bi_time_add},
    {"time/duration.nanos", bi_dur_from}, {"time/duration.millis", bi_dur_from},
    {"time/duration.seconds", bi_dur_from}, {"time/duration.minutes", bi_dur_from},
    {"time/duration.hours", bi_dur_from},
    {"time/duration.as_nanos", bi_dur_to}, {"time/duration.as_millis", bi_dur_to},
    {"time/duration.as_seconds", bi_dur_to}, {"time/duration.as_minutes", bi_dur_to},
    {"time/duration.as_hours", bi_dur_to},
    {"time/duration.add", bi_dur_arith}, {"time/duration.sub", bi_dur_arith},
    {"time/duration.before", bi_dur_cmp}, {"time/duration.after", bi_dur_cmp},
    {"time.before", bi_time_cmp}, {"time.after", bi_time_cmp},
    {"random.uint64", bi_rand_uint64}, {"random.int63", bi_rand_int63}, {"random.intn", bi_rand_intn},
    {"kvhas", bi_kvhas}, {"kvat", bi_kvat},
    {"debugger", bi_debugger},
};

static const size_t builtins_n = sizeof(builtins) / sizeof(builtins[0]);

/* 剥离 <numkind>. 前缀（int64.add → add），使融合后的单条 add 覆盖全部数字类型。
 * 非数字前缀（array./string./time/duration. 等）与裸类型 cast（int64）原样保留。 */
static const char *NUM_KINDS[] = {"int8", "int16", "int32", "int64", "uint8",
                                  "uint16", "uint32", "uint64", "float32", "float64"};
static const char *strip_num_kind(const char *op) {
    const char *dot = strchr(op, '.');
    if (!dot) return op;
    size_t n = (size_t)(dot - op);
    for (size_t i = 0; i < sizeof(NUM_KINDS) / sizeof(NUM_KINDS[0]); i++)
        if (strlen(NUM_KINDS[i]) == n && strncmp(op, NUM_KINDS[i], n) == 0) return dot + 1;
    return op;
}

bool bi_is_native(const char *opcode) {
    const char *op = strip_num_kind(opcode);
    for (size_t i = 0; i < builtins_n; i++) if (strcmp(builtins[i].op, op) == 0) return true;
    return false;
}

bool bi_num_op(const char *opcode) {
    switch (opcode[0]) {
    case 'a': return strcmp(opcode, "add") == 0 || strcmp(opcode, "abs") == 0;
    case 'b': return strcmp(opcode, "bitand") == 0 || strcmp(opcode, "bitor") == 0 || strcmp(opcode, "bitxor") == 0;
    case 'd': return strcmp(opcode, "div") == 0;
    case 'e': return strcmp(opcode, "eq") == 0 || strcmp(opcode, "exp") == 0;
    case 'g': return strcmp(opcode, "gt") == 0 || strcmp(opcode, "ge") == 0;
    case 'l': return strcmp(opcode, "lt") == 0 || strcmp(opcode, "le") == 0 || strcmp(opcode, "log") == 0;
    case 'm': return strcmp(opcode, "mod") == 0 || strcmp(opcode, "mul") == 0 || strcmp(opcode, "max") == 0 || strcmp(opcode, "min") == 0;
    case 'n': return strcmp(opcode, "neq") == 0 || strcmp(opcode, "neg") == 0;
    case 'p': return strcmp(opcode, "pow") == 0;
    case 's': return strcmp(opcode, "sub") == 0 || strcmp(opcode, "sqrt") == 0 || strcmp(opcode, "shl") == 0 || strcmp(opcode, "shr") == 0 || strcmp(opcode, "sign") == 0;
    }
    return false;
}

int bi_native(frame_t *f) {
    const char *op = strip_num_kind(f->inst->opcode);
    for (size_t i = 0; i < builtins_n; i++) {
        if (strcmp(builtins[i].op, op) == 0) return builtins[i].fn(f);
    }
    return set_err(f, "unknown builtin op: %s", f->inst->opcode);
}

/* ── copy ─────────────────────────────────────────────────────────── */

int bi_execute_copy(kv_t *kv, const char *vtid, const char *pc, rwir_inst_t *inst) {
    char *fr = kt_frame_root(pc);
    frame_t f = { kv, vtid, pc, inst };
    if (inst->nr == 0) { free(fr); next_pc(&f); return 0; }
    char *ff = bi_func_frame_root(kv, fr);
    xval_t v; xv_zero(&v);
    bi_resolve_read_value(kv, ff, inst->reads[0].name, &inst->reads[0].val, &v);
    free(ff);
    for (int i = 0; i < inst->nw; i++) {
        char *key = bi_resolve_write_slot(kv, fr, inst->writes[i].name);
        kv_pair_t pair = { key, v };
        char err[256];
        kv_set(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    xv_free(&v);
    free(fr);
    next_pc(&f);
    return 0;
}

/* ── collection helper ────────────────────────────────────────────── */

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

static uint32_t *string_runes(const xval_t *v, int *out_n) {
    if (xv_kind_is(v, K_CHAR)) {
        int n = xv_array_len(v);
        uint32_t *r = malloc(sizeof(uint32_t) * (n > 0 ? n : 1));
        for (int i = 0; i < n; i++) r[i] = xv_char32_at(v, i);
        *out_n = n;
        return r;
    }
    char *s = xv_value_string(v);
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

static void new_char32_cp(xval_t *out, uint32_t cp) {
    uint8_t le[4] = { cp & 0xFF, (cp >> 8) & 0xFF, (cp >> 16) & 0xFF, (cp >> 24) & 0xFF };
    uint8_t *o; uint32_t l; int32_t d = 1;
    kvspace_tlv_encode(K_CHAR, le, 4, &d, 1, &o, &l);
    out->data = o; out->len = l;
}

static int write_char32(frame_t *f, const uint32_t *r, int n) {
    xval_t e;
    if (n > 0) {
        sbuf_t raw; sb_init(&raw);
        for (int i = 0; i < n; i++) {
            uint8_t le[4] = { r[i] & 0xFF, (r[i] >> 8) & 0xFF, (r[i] >> 16) & 0xFF, (r[i] >> 24) & 0xFF };
            sb_putn(&raw, (const char *)le, 4);
        }
        uint8_t *out; uint32_t len; int32_t d = n;
        kvspace_tlv_encode(K_CHAR, (const uint8_t *)raw.p, (uint32_t)raw.len, &d, 1, &out, &len);
        sb_free(&raw);
        e.data = out; e.len = len;
    } else {
        uint8_t *out; uint32_t len; int32_t d = 0;
        kvspace_tlv_encode(K_CHAR, (const uint8_t *)"", 0, &d, 1, &out, &len);
        e.data = out; e.len = len;
    }
    int rc = write_result(f, &e);
    xv_free(&e);
    return rc;
}

static char *kv_key(const xval_t *v) {
    if (xv_is_char_kind(xv_kind(v))) return xv_value_string(v);
    if (is_int_kind(xv_kind(v))) { char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)xv_as_int64(v)); return strdup(buf); }
    return strdup("");
}

static const char *var_len_char_err(const char *kind) {
    return strcmp(kind, K_CHAR_UTF8) == 0 ? "TypeError: char/utf8 is variable-width; index/code-point ops require char/utf32 or char/ascii" : NULL;
}

static void pack_typed_array(const char *kind, const xval_t *elems, int n, xval_t *out) {
    int sz = xv_elem_size(kind);
    uint8_t *raw = malloc((size_t)sz * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        const uint8_t *b; int32_t blen;
        kvhead_t h; kvspace_decode_head(elems[i].data, elems[i].len, &h);
        b = elems[i].data + h.body_offset; blen = h.body_len;
        int c = blen < sz ? blen : sz;
        memcpy(raw + i * sz, b, (size_t)c);
        for (int j = c; j < sz; j++) raw[i * sz + j] = 0;
    }
    xv_new_tlv(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static char *separated_base(kv_t *kv, const char *fp, const char *name) {
    char *base = bi_resolve_write_slot(kv, fp, name);
    sbuf_t k; sb_init(&k);
    sb_puts(&k, base); sb_puts(&k, "[0]");
    xval_t v; xv_zero(&v);
    kv_get_one(kv, k.p, &v);
    bool exists = !xv_none(&v);
    xv_free(&v); sb_free(&k);
    if (!exists) { free(base); return NULL; }
    return base;
}

static int separated_len(kv_t *kv, const char *base) {
    for (int i = 0; ; i++) {
        sbuf_t k; sb_init(&k);
        sb_printf(&k, "%s[%d]", base, i);
        xval_t v; xv_zero(&v);
        kv_get_one(kv, k.p, &v);
        bool none = xv_none(&v);
        xv_free(&v); sb_free(&k);
        if (none) return i;
    }
}

/* ── array 系列 ───────────────────────────────────────────────────── */

static void ensure_scattered(frame_t *f, const char *base) {
    xval_t arr; xv_zero(&arr);
    kv_get_one(f->kv, base, &arr);
    if (xv_none(&arr) || xv_elem_size(xv_kind(&arr)) <= 0) { xv_free(&arr); return; }
    int n = xv_array_len(&arr);
    for (int i = 0; i < n; i++) {
        sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", base, i);
        xval_t e; xvalue_at(&arr, i, &e);
        kv_pair_t p = { k.p, e };
        char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
        xv_free(&e); sb_free(&k);
    }
    xv_free(&arr);
    char err[256]; kv_del(f->kv, base, err, sizeof err);
}

int bi_array(frame_t *f) {
    xval_t in[64]; int n = read_inputs(f, in, 64);
    if (f->inst->nw == 0 || n == 0) { next_pc(f); free_inputs(in, n); return 0; }
    const char *kind = xv_kind(&in[0]);
    if (xv_elem_size(kind) <= 0) { free_inputs(in, n); return set_err(f, "array: unsupported element kind %s", kind); }
    for (int i = 1; i < n; i++) if (strcmp(xv_kind(&in[i]), kind) != 0) { free_inputs(in, n); return set_err(f, "array: mixed kinds %s and %s", kind, xv_kind(&in[i])); }
    xval_t arr; pack_typed_array(kind, in, n, &arr);
    char *fr = kt_frame_root(f->pc);
    char *key = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    kv_pair_t p = { key, arr };
    char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
    free(key); xv_free(&arr);
    next_pc(f);
    free_inputs(in, n);
    return 0;
}

int bi_len(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int len = 0;
    if (n > 0 && xv_none(&in[0]) && f->inst->nr > 0) {
        char *fr = kt_frame_root(f->pc);
        char *base = separated_base(f->kv, fr, f->inst->reads[0].name);
        if (base) { len = separated_len(f->kv, base); free(base); }
        free(fr);
    } else if (n > 0) len = xv_array_len(&in[0]);
    xval_t r; xv_new_int64(&r, len);
    int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n);
    return rc;
}

int bi_at(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: at requires array and index"); }
    const char *k0 = xv_kind(&in[0]), *k1 = xv_kind(&in[1]);
    /* 字符串非路径 → 码点索引 */
    if (xv_is_char_kind(k0)) {
        char *s = xv_value_string(&in[0]);
        bool is_path = s[0] == '/';
        free(s);
        if (!is_path) {
            if (var_len_char_err(k0)) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(k0)); }
            if (!is_int_kind(k1)) { xval_t e; xv_new_char_utf32(&e, ""); int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n); return rc; }
            int idx = (int)xv_as_int64(&in[1]);
            int rn; uint32_t *r = string_runes(&in[0], &rn);
            if (idx < 0 || idx >= rn) { free(r); xval_t e; xv_new_char_utf32(&e, ""); int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n); return rc; }
            xval_t e; new_char32_cp(&e, r[idx]);
            int rc = write_result(f, &e); xv_free(&e); free(r); free_inputs(in, n); return rc;
        }
    }
    /* 路径访问 */
    bool path_access = strcmp(k0, K_DICT) == 0 || xv_is_char_kind(k0) || xv_is_char_kind(k1) ||
        (f->inst->nr > 0 && (f->inst->reads[0].name[0] == '/' || (f->inst->reads[0].name[0] == '"' && f->inst->reads[0].name[1] == '/')));
    if (path_access) {
        char *fr = kt_frame_root(f->pc);
        char *ff = bi_func_frame_root(f->kv, fr);
        xval_t base; xv_zero(&base);
        bi_resolve_read_value(f->kv, ff, f->inst->reads[0].name, &f->inst->reads[0].val, &base);
        char *bp = xv_none(&base) || (strcmp(xv_kind(&base), K_DICT) == 0 || strcmp(xv_kind(&base), K_INDEX) == 0 || strcmp(xv_kind(&base), K_EXT_INDEX) == 0) ? bi_resolve_write_slot(f->kv, ff, f->inst->reads[0].name) : xv_value_string(&base);
        char *kk = kv_key(&in[1]);
        char *path = kt_member(bp, kk);
        xval_t v; xv_zero(&v);
        kv_get_one(f->kv, path, &v);
        int rc = write_result(f, &v); xv_free(&v);
        free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr);
        free_inputs(in, n); return rc;
    }
    if (xv_elem_size(k0) > 0 && xv_is_char_kind(k1)) { free_inputs(in, n); return set_err(f, "IndexError: at: index must be integer for typed array"); }
    if (xv_none(&in[0]) && f->inst->nr > 0) {
        char *fr = kt_frame_root(f->pc);
        char *base = separated_base(f->kv, fr, f->inst->reads[0].name);
        if (base) {
            int idx = (int)xv_as_int64(&in[1]);
            sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", base, idx);
            xval_t v; xv_zero(&v); kv_get_one(f->kv, k.p, &v);
            int rc = write_result(f, &v); xv_free(&v); sb_free(&k);
            free(base); free(fr); free_inputs(in, n); return rc;
        }
        free(fr);
    }
    if (xv_none(&in[0])) { free_inputs(in, n); return set_err(f, "IndexError: at: base is None; help: declare a key-family first or pass a path string"); }
    int idx = (int)xv_as_int64(&in[1]);
    xval_t e; xvalue_at(&in[0], idx, &e);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_has(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { xval_t r; xv_new_bool(&r, false); int rc = write_result(f, &r); xv_free(&r); free_inputs(in, n); return rc; }
    char *fr = kt_frame_root(f->pc);
    char *ff = bi_func_frame_root(f->kv, fr);
    xval_t base; xv_zero(&base);
    bi_resolve_read_value(f->kv, ff, f->inst->reads[0].name, &f->inst->reads[0].val, &base);
    char *bp = xv_none(&base) || (strcmp(xv_kind(&base), K_DICT) == 0 || strcmp(xv_kind(&base), K_INDEX) == 0) ? bi_resolve_write_slot(f->kv, ff, f->inst->reads[0].name) : xv_value_string(&base);
    char *kk = kv_key(&in[1]);
    char *path = kt_member(bp, kk);
    xval_t v; xv_zero(&v); kv_get_one(f->kv, path, &v);
    bool exists = !xv_none(&v);
    xval_t r; xv_new_bool(&r, exists);
    int rc = write_result(f, &r); xv_free(&r); xv_free(&v);
    free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr);
    free_inputs(in, n); return rc;
}

int bi_set(frame_t *f) {
    xval_t in[3]; int n = read_inputs(f, in, 3);
    if (n < 3) { free_inputs(in, n); return set_err(f, "TypeError: set requires array, index, value"); }
    const char *k0 = xv_kind(&in[0]);
    /* 字符串非路径 → 替换码点 */
    if (xv_is_char_kind(k0)) {
        char *s = xv_value_string(&in[0]); bool is_path = s[0] == '/'; free(s);
        if (!is_path) {
            if (var_len_char_err(k0)) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(k0)); }
            int idx = (int)xv_as_int64(&in[1]);
            int rn; uint32_t *r = string_runes(&in[0], &rn);
            if (idx < 0 || idx >= rn) { free(r); free_inputs(in, n); return set_err(f, "IndexError: set: string index %d out of bounds (len=%d)", idx, rn); }
            int rn2; uint32_t *repl = string_runes(&in[2], &rn2);
            if (rn2 == 0) { free(repl); free(r); free_inputs(in, n); return set_err(f, "TypeError: set: replacement char is empty"); }
            r[idx] = repl[0];
            int rc = write_char32(f, r, rn);
            free(repl); free(r); free_inputs(in, n);
            return rc;
        }
    }
    /* 路径写入 */
    bool path_access = strcmp(k0, K_DICT) == 0 || xv_is_char_kind(k0) || xv_is_char_kind(xv_kind(&in[1])) ||
        (f->inst->nr > 0 && (f->inst->reads[0].name[0] == '/' || (f->inst->reads[0].name[0] == '"' && f->inst->reads[0].name[1] == '/')));
    if (path_access) {
        char *fr = kt_frame_root(f->pc);
        char *ff = bi_func_frame_root(f->kv, fr);
        xval_t base; xv_zero(&base);
        bi_resolve_read_value(f->kv, ff, f->inst->reads[0].name, &f->inst->reads[0].val, &base);
        char *bp = xv_none(&base) || (strcmp(xv_kind(&base), K_DICT) == 0 || strcmp(xv_kind(&base), K_INDEX) == 0) ? bi_resolve_write_slot(f->kv, ff, f->inst->reads[0].name) : xv_value_string(&base);
        char *kk = kv_key(&in[1]);
        char *path = kt_member(bp, kk);
        kv_pair_t p = { path, in[2] };
        char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
        if (f->inst->nw > 0 && !xv_none(&in[0])) {
            char *ok = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
            kv_pair_t p2 = { ok, in[0] }; kv_set(f->kv, &p2, 1, err, sizeof err);
            free(ok);
        }
        free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr);
        next_pc(f); free_inputs(in, n); return 0;
    }
    if (xv_elem_size(k0) > 0 && xv_is_char_kind(xv_kind(&in[1]))) { free_inputs(in, n); return set_err(f, "IndexError: set: index must be integer for typed array"); }
    if (xv_none(&in[0]) && f->inst->nr > 0) {
        char *fr = kt_frame_root(f->pc);
        char *base = separated_base(f->kv, fr, f->inst->reads[0].name);
        if (base) {
            int idx = (int)xv_as_int64(&in[1]);
            sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", base, idx);
            kv_pair_t p = { k.p, in[2] };
            char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
            sb_free(&k); free(base); free(fr);
            next_pc(f); free_inputs(in, n); return 0;
        }
        free(fr);
    }
    if (xv_none(&in[0])) { free_inputs(in, n); return set_err(f, "IndexError: set: base is None"); }
    if (xv_elem_size(k0) > 0) {
        /* 整存整取改元素 */
        char *fr = kt_frame_root(f->pc);
        char *key = bi_resolve_write_slot(f->kv, fr, f->inst->reads[0].name);
        xval_t arr; xv_zero(&arr); kv_get_one(f->kv, key, &arr);
        if (!xv_none(&arr)) {
            int idx = (int)xv_as_int64(&in[1]);
            int sz = xv_elem_size(k0);
            kvhead_t h; kvspace_decode_head(arr.data, arr.len, &h);
            const uint8_t *body = arr.data + h.body_offset;
            int al = xv_array_len(&arr);
            if (idx >= 0 && idx < al) {
                uint8_t *nb = malloc((size_t)h.body_len);
                memcpy(nb, body, (size_t)h.body_len);
                const uint8_t *vb; int32_t vbl; kvhead_t vh; kvspace_decode_head(in[2].data, in[2].len, &vh); vb = in[2].data + vh.body_offset; vbl = vh.body_len;
                int c = vbl < sz ? vbl : sz;
                memcpy(nb + idx * sz, vb, (size_t)c);
                xval_t nv; xv_new_tlv(&nv, k0, nb, (uint32_t)h.body_len, al);
                kv_pair_t p = { key, nv };
                char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
                xv_free(&nv); free(nb);
            }
        }
        xv_free(&arr); free(key); free(fr);
        next_pc(f); free_inputs(in, n); return 0;
    }
    free_inputs(in, n); return set_err(f, "IndexError: set: unsupported array kind %s", k0);
}

int bi_sort(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { xval_t r; xv_zero(&r); int rc = write_result(f, &r); free_inputs(in, n); return rc; }
    int al = xv_array_len(&in[0]);
    if (al <= 1) { int rc = write_result(f, &in[0]); free_inputs(in, n); return rc; }
    xval_t *elems = malloc(sizeof(xval_t) * al);
    for (int i = 0; i < al; i++) { xvalue_at(&in[0], i, &elems[i]); }
    for (int i = 0; i < al - 1; i++)
        for (int j = 0; j < al - i - 1; j++)
            if (xv_as_float64(&elems[j]) > xv_as_float64(&elems[j + 1])) { xval_t t = elems[j]; elems[j] = elems[j + 1]; elems[j + 1] = t; }
    xval_t r; pack_typed_array(xv_kind(&in[0]), elems, al, &r);
    int rc = write_result(f, &r); xv_free(&r);
    for (int i = 0; i < al; i++) xv_free(&elems[i]);
    free(elems); free_inputs(in, n); return rc;
}

int bi_scatter(frame_t *f) {
    if (f->inst->nw == 0) return set_err(f, "TypeError: array.scatter requires a write param");
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n == 0 || xv_none(&in[0])) { next_pc(f); free_inputs(in, n); return 0; }
    if (xv_elem_size(xv_kind(&in[0])) <= 0) { free_inputs(in, n); return set_err(f, "TypeError: array.scatter requires a compact array ([]T), got %s", xv_kind(&in[0])); }
    char *fr = kt_frame_root(f->pc);
    char *dst = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
    int al = xv_array_len(&in[0]);
    for (int i = 0; i < al; i++) {
        sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", dst, i);
        xval_t e; xvalue_at(&in[0], i, &e);
        kv_pair_t p = { k.p, e }; char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
        xv_free(&e); sb_free(&k);
    }
    free(dst); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

int bi_compact(frame_t *f) {
    if (f->inst->nr == 0 || f->inst->nw == 0) return set_err(f, "TypeError: array.compact requires read and write params");
    char *fr = kt_frame_root(f->pc);
    char *src = bi_resolve_write_slot(f->kv, fr, f->inst->reads[0].name);
    xval_t elems[1024]; int n = 0;
    for (int i = 0; ; i++) {
        sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", src, i);
        xval_t v; xv_zero(&v); kv_get_one(f->kv, k.p, &v);
        bool none = xv_none(&v); sb_free(&k);
        if (none) break;
        elems[n++] = v;
    }
    if (n == 0) { free(src); free(fr); next_pc(f); return 0; }
    xval_t arr; pack_typed_array(xv_kind(&elems[0]), elems, n, &arr);
    char *dst = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
    kv_pair_t p = { dst, arr }; char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
    for (int i = 0; i < n; i++) xv_free(&elems[i]);
    xv_free(&arr);
    free(dst); free(src); free(fr);
    next_pc(f); return 0;
}

int bi_append(frame_t *f) {
    if (f->inst->nr < 2) return set_err(f, "TypeError: array.append requires array and element");
    xval_t in[2]; int n = read_inputs(f, in, 2);
    char *fr = kt_frame_root(f->pc);
    char *base = bi_resolve_write_slot(f->kv, fr, f->inst->reads[0].name);
    ensure_scattered(f, base);
    int len = separated_len(f->kv, base);
    sbuf_t k; sb_init(&k); sb_printf(&k, "%s[%d]", base, len);
    kv_pair_t p = { k.p, n >= 2 ? in[1] : in[0] };
    char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
    sb_free(&k); free(base); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

int bi_slice(frame_t *f) {
    if (f->inst->nr < 3) return set_err(f, "TypeError: array.slice requires array, start, end");
    xval_t in[3]; int n = read_inputs(f, in, 3);
    char *fr = kt_frame_root(f->pc);
    char *base = bi_resolve_write_slot(f->kv, fr, f->inst->reads[0].name);
    ensure_scattered(f, base);
    int al = separated_len(f->kv, base);
    int lo = (int)xv_as_int64(&in[1]), hi = (int)xv_as_int64(&in[2]);
    if (lo < 0 || hi < lo || hi > al) { free(base); free(fr); free_inputs(in, n); return set_err(f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)", lo, hi, al); }
    for (int i = lo; i < hi; i++) {
        sbuf_t sk; sb_init(&sk); sb_printf(&sk, "%s[%d]", base, i);
        xval_t v; xv_zero(&v); kv_get_one(f->kv, sk.p, &v);
        sbuf_t dk; sb_init(&dk); sb_printf(&dk, "%s[%d]", base, i - lo);
        kv_pair_t p = { dk.p, v }; char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
        xv_free(&v); sb_free(&sk); sb_free(&dk);
    }
    for (int i = hi - lo; i < al; i++) {
        sbuf_t dk; sb_init(&dk); sb_printf(&dk, "%s[%d]", base, i);
        char err[256]; kv_del(f->kv, dk.p, err, sizeof err); sb_free(&dk);
    }
    free(base); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

/* ── dict ─────────────────────────────────────────────────────────── */

int bi_dict(frame_t *f) {
    xval_t in[64]; int n = read_inputs(f, in, 64);
    char *fr = kt_frame_root(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok = bi_resolve_write_slot(f->kv, fr, f->inst->writes[w].name);
        xval_t mark; xv_new_tlv(&mark, K_DICT, (const uint8_t *)"", 0, 1);
        kv_pair_t p0 = { ok, mark };
        char err[256]; kv_set(f->kv, &p0, 1, err, sizeof err);
        xv_free(&mark);
        for (int i = 0; i + 1 < n; i += 2) {
            if (xv_none(&in[i + 1])) continue;
            char *k = xv_value_string(&in[i]);
            char *mk = kt_member(ok, k);
            kv_pair_t p = { mk, in[i + 1] };
            kv_set(f->kv, &p, 1, err, sizeof err);
            free(k); free(mk);
        }
        free(ok);
    }
    free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

/* ── string 系列 ──────────────────────────────────────────────────── */

int bi_string_set(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    char *s = NULL;
    if (n > 0) display(&in[0], &s);
    else s = strdup("");
    if (f->inst->nw > 0) {
        char *fr = kt_frame_root(f->pc);
        char *key = bi_resolve_write_slot(f->kv, fr, f->inst->writes[0].name);
        xval_t v; xv_new_char_utf32(&v, s);
        kv_pair_t p = { key, v };
        char err[256]; kv_set(f->kv, &p, 1, err, sizeof err);
        xv_free(&v); free(key); free(fr);
    }
    free(s);
    next_pc(f); free_inputs(in, n); return 0;
}

int bi_string_char(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.char requires string and index"); }
    if (var_len_char_err(xv_kind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(xv_kind(&in[0]))); }
    int idx = (int)xv_as_int64(&in[1]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    int rc;
    if (idx < 0 || idx >= rn) rc = set_err(f, "IndexError: at: index %d out of bounds (char count=%d)", idx, rn);
    else { uint32_t one = r[idx]; rc = write_char32(f, &one, 1); }
    free(r); free_inputs(in, n);
    return rc;
}

int bi_string_ord(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: string.ord requires a string"); }
    if (var_len_char_err(xv_kind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(xv_kind(&in[0]))); }
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    xval_t e; xv_new_int64(&e, rn == 0 ? -1 : (int64_t)r[0]);
    int rc = write_result(f, &e); xv_free(&e); free(r); free_inputs(in, n);
    return rc;
}

int bi_string_cmp(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.cmp requires two strings"); }
    char *a = xv_value_string(&in[0]), *b = xv_value_string(&in[1]);
    int64_t r = strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
    xval_t e; xv_new_int64(&e, r);
    int rc = write_result(f, &e); xv_free(&e); free(a); free(b); free_inputs(in, n);
    return rc;
}

int bi_string_find(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.find requires two strings"); }
    if (var_len_char_err(xv_kind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(xv_kind(&in[0]))); }
    int hn; uint32_t *hay = string_runes(&in[0], &hn);
    int nn; uint32_t *needle = string_runes(&in[1], &nn);
    int64_t r = -1;
    if (nn == 0) r = 0;
    else for (int i = 0; i + nn <= hn; i++) { bool m = true; for (int j = 0; j < nn; j++) if (hay[i + j] != needle[j]) { m = false; break; } if (m) { r = i; break; } }
    xval_t e; xv_new_int64(&e, r);
    int rc = write_result(f, &e); xv_free(&e); free(hay); free(needle); free_inputs(in, n);
    return rc;
}

int bi_string_len(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    int len = 0;
    if (n > 0) {
        if (var_len_char_err(xv_kind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(xv_kind(&in[0]))); }
        if (xv_kind_is(&in[0], K_CHAR)) len = xv_array_len(&in[0]);
        else { uint32_t *r = string_runes(&in[0], &len); free(r); }
    }
    xval_t e; xv_new_int64(&e, len);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_string_slice(frame_t *f) {
    xval_t in[3]; int n = read_inputs(f, in, 3);
    if (n < 3) { free_inputs(in, n); return set_err(f, "TypeError: string.slice requires string, start, end"); }
    if (var_len_char_err(xv_kind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(xv_kind(&in[0]))); }
    int lo = (int)xv_as_int64(&in[1]), hi = (int)xv_as_int64(&in[2]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    if (lo < 0 || hi > rn || lo > hi) { free(r); free_inputs(in, n); return set_err(f, "IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, rn); }
    write_char32(f, r + lo, hi - lo);
    free(r); free_inputs(in, n);
    return 0;
}

int bi_string_concat(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !xv_is_char_kind(xv_kind(&in[0])) || !xv_is_char_kind(xv_kind(&in[1]))) {
        if (n >= 2) set_err(f, "TypeError: string.concat requires strings, got %s and %s", xv_kind(&in[0]), xv_kind(&in[1]));
        else { xval_t e; xv_new_char_utf32(&e, ""); write_result(f, &e); xv_free(&e); }
        free_inputs(in, n); return n < 2 ? 0 : -1;
    }
    int a, b; uint32_t *ra = string_runes(&in[0], &a); uint32_t *rb = string_runes(&in[1], &b);
    uint32_t *r = malloc(sizeof(uint32_t) * (a + b));
    memcpy(r, ra, sizeof(uint32_t) * a); memcpy(r + a, rb, sizeof(uint32_t) * b);
    write_char32(f, r, a + b);
    free(r); free(ra); free(rb); free_inputs(in, n);
    return 0;
}

/* ── time / duration ──────────────────────────────────────────────── */

static int64_t now_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void new_time(xval_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    xv_new_tlv(v, K_TIME, r, 8, 1);
}
static void new_duration(xval_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    xv_new_tlv(v, K_DURATION, r, 8, 1);
}

int bi_time_now(frame_t *f) {
    xval_t e; new_time(&e, now_nanos());
    int rc = write_result(f, &e); xv_free(&e);
    return rc;
}

int bi_time_sub(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time.sub requires 2 time args"); }
    xval_t e; new_duration(&e, xv_as_int64(&in[0]) - xv_as_int64(&in[1]));
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_time_add(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time.add requires time and duration"); }
    xval_t e; new_time(&e, xv_as_int64(&in[0]) + xv_as_int64(&in[1]));
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

static int64_t dur_scale(const char *op) {
    if (strstr(op, "nanos")) return 1;
    if (strstr(op, "millis")) return 1000000;
    if (strstr(op, "seconds")) return 1000000000;
    if (strstr(op, "minutes")) return 60000000000LL;
    if (strstr(op, "hours")) return 3600000000000LL;
    return 1;
}

int bi_dur_from(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: time/duration.from requires 1 int64 arg"); }
    xval_t e; new_duration(&e, xv_as_int64(&in[0]) * dur_scale(f->inst->opcode));
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_dur_to(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: time/duration.as requires 1 duration arg"); }
    xval_t e; xv_new_int64(&e, xv_as_int64(&in[0]) / dur_scale(f->inst->opcode));
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_dur_arith(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time/duration arith requires 2 duration args"); }
    int64_t a = xv_as_int64(&in[0]), b = xv_as_int64(&in[1]);
    bool sub = strstr(f->inst->opcode, ".sub") != NULL;
    xval_t e; new_duration(&e, sub ? a - b : a + b);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_dur_cmp(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time/duration cmp requires 2 duration args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = xv_as_int64(&in[0]), b = xv_as_int64(&in[1]);
    xval_t e; xv_new_bool(&e, before ? a < b : a > b);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

int bi_time_cmp(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time cmp requires 2 args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = xv_as_int64(&in[0]), b = xv_as_int64(&in[1]);
    xval_t e; xv_new_bool(&e, before ? a < b : a > b);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

/* ── random ───────────────────────────────────────────────────────── */

static uint64_t crypto_rand_u64(void) {
    uint64_t v = 0;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) { fread(&v, 8, 1, fp); fclose(fp); }
    return v;
}

int bi_rand_uint64(frame_t *f) {
    uint64_t v = crypto_rand_u64();
    uint8_t r[8]; memcpy(r, &v, 8);
    xval_t e; xv_new_tlv(&e, K_UINT64, r, 8, 1);
    int rc = write_result(f, &e); xv_free(&e);
    return rc;
}

int bi_rand_int63(frame_t *f) {
    int64_t v = (int64_t)(crypto_rand_u64() >> 1);
    xval_t e; xv_new_int64(&e, v);
    int rc = write_result(f, &e); xv_free(&e);
    return rc;
}

int bi_rand_intn(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || xv_none(&in[0])) { free_inputs(in, n); return set_err(f, "TypeError: random.intn requires 1 int64 arg"); }
    uint64_t m = (uint64_t)xv_as_int64(&in[0]);
    uint64_t v = m == 0 ? 0 : crypto_rand_u64() % m;
    uint8_t r[8]; memcpy(r, &v, 8);
    xval_t e; xv_new_tlv(&e, K_UINT64, r, 8, 1);
    int rc = write_result(f, &e); xv_free(&e); free_inputs(in, n);
    return rc;
}

/* ── kvop ─────────────────────────────────────────────────────────── */

int bi_kvhas(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: kvhas requires 2 args"); }
    char *fr = kt_frame_root(f->pc);
    char *ff = bi_func_frame_root(f->kv, fr);
    xval_t base; xv_zero(&base);
    bi_resolve_read_value(f->kv, ff, f->inst->reads[0].name, &f->inst->reads[0].val, &base);
    char *bp = xv_none(&base) || (strcmp(xv_kind(&base), K_DICT) == 0 || strcmp(xv_kind(&base), K_INDEX) == 0 || strcmp(xv_kind(&base), K_EXT_INDEX) == 0) ? bi_resolve_write_slot(f->kv, ff, f->inst->reads[0].name) : xv_value_string(&base);
    char *kk = kv_key(&in[1]);
    char *path = kt_member(bp, kk);
    xval_t v; xv_zero(&v); kv_get_one(f->kv, path, &v);
    xval_t e; xv_new_bool(&e, !xv_none(&v));
    int rc = write_result(f, &e); xv_free(&e); xv_free(&v);
    free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr);
    free_inputs(in, n); return rc;
}

int bi_kvat(frame_t *f) {
    xval_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: kvat requires 2 args"); }
    char *fr = kt_frame_root(f->pc);
    char *ff = bi_func_frame_root(f->kv, fr);
    xval_t base; xv_zero(&base);
    bi_resolve_read_value(f->kv, ff, f->inst->reads[0].name, &f->inst->reads[0].val, &base);
    char *bp = xv_none(&base) || (strcmp(xv_kind(&base), K_DICT) == 0 || strcmp(xv_kind(&base), K_INDEX) == 0 || strcmp(xv_kind(&base), K_EXT_INDEX) == 0) ? bi_resolve_write_slot(f->kv, ff, f->inst->reads[0].name) : xv_value_string(&base);
    char *kk = kv_key(&in[1]);
    char *path = kt_member(bp, kk);
    xval_t v; xv_zero(&v); kv_get_one(f->kv, path, &v);
    if (xv_none(&v)) { free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr); free_inputs(in, n); return set_err(f, "KeyError: kvat: key not found: %s", path); }
    int rc = write_result(f, &v); xv_free(&v);
    free(path); free(kk); free(bp); xv_free(&base); free(ff); free(fr);
    free_inputs(in, n); return rc;
}

/* ── debugger ─────────────────────────────────────────────────────── */

int bi_debugger(frame_t *f) {
    sbuf_t dk; sb_init(&dk);
    kt_vthread_debugger(f->vtid, &dk);
    xval_t v; xv_zero(&v); kv_get_one(f->kv, dk.p, &v);
    bool dbg = !xv_none(&v);
    xv_free(&v);
    if (!dbg) { sb_free(&dk); next_pc(f); return 0; }
    sb_free(&dk);
    next_pc(f);
    return 0;
}
