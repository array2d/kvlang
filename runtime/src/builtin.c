#include "runtime_internal.h"
#include <math.h>
#include <time.h>

typedef int (*kvlangBuiltinFn)(kvlangFrame_t *f);

/* collection 模块 handler（builtin_coll.c） */
int kvlangBuiltinArray(kvlangFrame_t *f), kvlangBuiltinNdarrayNumel(kvlangFrame_t *f), kvlangBuiltinNdarrayDim(kvlangFrame_t *f), kvlangBuiltinNdarrayShape(kvlangFrame_t *f),
    kvlangBuiltinXvAt(kvlangFrame_t *f), kvlangBuiltinXvSet(kvlangFrame_t *f), kvlangBuiltinXvReshape(kvlangFrame_t *f),
    kvlangBuiltinScatter(kvlangFrame_t *f), kvlangBuiltinCompact(kvlangFrame_t *f),
    kvlangBuiltinAppend(kvlangFrame_t *f), kvlangBuiltinSlice(kvlangFrame_t *f), kvlangBuiltinObj(kvlangFrame_t *f), kvlangBuiltinMap(kvlangFrame_t *f), kvlangBuiltinStringSet(kvlangFrame_t *f),
    kvlangBuiltinStringChar(kvlangFrame_t *f), kvlangBuiltinStringOrd(kvlangFrame_t *f), kvlangBuiltinStringCmp(kvlangFrame_t *f),
    kvlangBuiltinStringFind(kvlangFrame_t *f), kvlangBuiltinStringLen(kvlangFrame_t *f), kvlangBuiltinStringSlice(kvlangFrame_t *f),
    kvlangBuiltinStringConcat(kvlangFrame_t *f), kvlangBuiltinTimeNow(kvlangFrame_t *f), kvlangBuiltinTimeSub(kvlangFrame_t *f),
    kvlangBuiltinTimeAdd(kvlangFrame_t *f), kvlangBuiltinDurFrom(kvlangFrame_t *f), kvlangBuiltinDurTo(kvlangFrame_t *f),
    kvlangBuiltinDurArith(kvlangFrame_t *f), kvlangBuiltinDurCmp(kvlangFrame_t *f), kvlangBuiltinTimeCmp(kvlangFrame_t *f),
    kvlangBuiltinRandUint64(kvlangFrame_t *f), kvlangBuiltinRandInt63(kvlangFrame_t *f), kvlangBuiltinRandIntn(kvlangFrame_t *f),
    kvlangBuiltinKvGet(kvlangFrame_t *f), kvlangBuiltinKvSet(kvlangFrame_t *f), kvlangBuiltinKvDel(kvlangFrame_t *f),
    kvlangBuiltinKvDelTree(kvlangFrame_t *f), kvlangBuiltinKvList(kvlangFrame_t *f), kvlangBuiltinKvListLen(kvlangFrame_t *f), kvlangBuiltinKvListN(kvlangFrame_t *f), kvlangBuiltinKvMkindex(kvlangFrame_t *f),
    kvlangBuiltinKvExtIndex(kvlangFrame_t *f), kvlangBuiltinKvRmIndexExt(kvlangFrame_t *f), kvlangBuiltinKvWatch(kvlangFrame_t *f),
    kvlangBuiltinDebugger(kvlangFrame_t *f);

/* ── 类型 helper（对齐 Go isIntKind 含 uint）────────────────────── */

static bool is_int_kind(const char *k) { return kvlangXvalueIsIntKind(k) || kvlangXvalueIsUintKind(k); }
static bool is_float_kind(const char *k) { return kvlangXvalueIsFloatKind(k); }
static bool is_unsigned_kind(const char *k) { return kvlangXvalueIsUintKind(k); }
static bool is_numeric(const kvlangXvalue_t *v) { return kvlangXvalueIsNumKind(kvlangXvalueKind(v)); }

static int int_width(const char *k) {
    if (strcmp(k, KVSPACE_KIND_INT8) == 0 || strcmp(k, KVSPACE_KIND_UINT8) == 0) return 8;
    if (strcmp(k, KVSPACE_KIND_INT16) == 0 || strcmp(k, KVSPACE_KIND_UINT16) == 0) return 16;
    if (strcmp(k, KVSPACE_KIND_INT32) == 0 || strcmp(k, KVSPACE_KIND_UINT32) == 0) return 32;
    if (strcmp(k, KVSPACE_KIND_INT64) == 0 || strcmp(k, KVSPACE_KIND_UINT64) == 0) return 64;
    return 0;
}

static const char *wider_int_kind(const char *a, const char *b) {
    int aw = int_width(a), bw = int_width(b);
    bool au = is_unsigned_kind(a), bu = is_unsigned_kind(b);
    if (au && bu) return aw >= bw ? a : b;
    if (!au && !bu) return aw >= bw ? a : b;
    int w = aw > bw ? aw : bw;
    switch (w) {
    case 8: return KVSPACE_KIND_INT16;
    case 16: return KVSPACE_KIND_INT32;
    case 32: return KVSPACE_KIND_INT64;
    default: return KVSPACE_KIND_INT64;
    }
}

static const char *wider_float_kind(const char *a, const char *b) {
    if (strcmp(a, KVSPACE_KIND_FLOAT64) == 0 || strcmp(b, KVSPACE_KIND_FLOAT64) == 0) return KVSPACE_KIND_FLOAT64;
    if (strcmp(a, KVSPACE_KIND_FLOAT32) == 0 || strcmp(b, KVSPACE_KIND_FLOAT32) == 0) return KVSPACE_KIND_FLOAT32;
    return KVSPACE_KIND_FLOAT64;
}

static void narrow_int(const char *a, const char *b, int64_t v, kvlangXvalue_t *out) {
    const char *k = wider_int_kind(a, b);
    if (strcmp(k, KVSPACE_KIND_INT8) == 0) { int8_t x = (int8_t)v; kvlangXvalueNewTlv(out, KVSPACE_KIND_INT8, (uint8_t *)&x, 1, 1); return; }
    if (strcmp(k, KVSPACE_KIND_INT16) == 0) { int16_t x = (int16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; kvlangXvalueNewTlv(out, KVSPACE_KIND_INT16, r, 2, 1); return; }
    if (strcmp(k, KVSPACE_KIND_INT32) == 0) { int32_t x = (int32_t)v; uint8_t r[4]; memcpy(r, &x, 4); kvlangXvalueNewTlv(out, KVSPACE_KIND_INT32, r, 4, 1); return; }
    if (strcmp(k, KVSPACE_KIND_UINT8) == 0) { uint8_t x = (uint8_t)v; kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT8, &x, 1, 1); return; }
    if (strcmp(k, KVSPACE_KIND_UINT16) == 0) { uint16_t x = (uint16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT16, r, 2, 1); return; }
    if (strcmp(k, KVSPACE_KIND_UINT32) == 0) { uint32_t x = (uint32_t)v; uint8_t r[4]; memcpy(r, &x, 4); kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT32, r, 4, 1); return; }
    if (strcmp(k, KVSPACE_KIND_UINT64) == 0) { uint64_t x = (uint64_t)v; uint8_t r[8]; memcpy(r, &x, 8); kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT64, r, 8, 1); return; }
    kvlangXvalueNewInt64(out, v);
}

static void narrow_float(const char *a, const char *b, double v, kvlangXvalue_t *out) {
    if (strcmp(wider_float_kind(a, b), KVSPACE_KIND_FLOAT32) == 0) {
        float f = (float)v; uint8_t r[4]; memcpy(r, &f, 4);
        kvlangXvalueNewTlv(out, KVSPACE_KIND_FLOAT32, r, 4, 1);
    } else kvlangXvalueNewFloat64(out, v);
}

static int cmp_int(const kvlangXvalue_t *a, const kvlangXvalue_t *b) {
    bool au = is_unsigned_kind(kvlangXvalueKind(a)), bu = is_unsigned_kind(kvlangXvalueKind(b));
    if (!au && !bu) { int64_t ai = kvlangXvalueAsInt64(a), bi = kvlangXvalueAsInt64(b); return ai < bi ? -1 : ai > bi ? 1 : 0; }
    if (au && bu) { uint64_t x = kvlangXvalueAsUint64(a), y = kvlangXvalueAsUint64(b); return x < y ? -1 : x > y ? 1 : 0; }
    if (au && !bu) { int64_t bi = kvlangXvalueAsInt64(b); if (bi < 0) return 1; uint64_t x = kvlangXvalueAsUint64(a); return x < (uint64_t)bi ? -1 : x > (uint64_t)bi ? 1 : 0; }
    int64_t ai = kvlangXvalueAsInt64(a); if (ai < 0) return -1; uint64_t y = kvlangXvalueAsUint64(b);
    return (uint64_t)ai < y ? -1 : (uint64_t)ai > y ? 1 : 0;
}

/* ── resolve ───────────────────────────────────────────────────────── */

char *kvlangBuiltinFuncFrameRoot(kvlangKv_t *kv, const char *frame_root) {
    char *cur = strdup(frame_root);
    for (;;) {
        kvlangStrbuf_t k; kvlangStrbufInit(&k);
        char *stk = kvlangKeytreeStack(cur);
        kvlangStrbufPuts(&k, stk); free(stk);
        kvlangStrbufPuts(&k, SEG_LIB);
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, k.p, &v);
        bool found = !kvlangXvalueNone(&v);
        kvlangXvalueFree(&v); kvlangStrbufFree(&k);
        if (found) return cur;
        char *parent = kvlangKeytreeParentFrame(cur);
        if (parent[0] == 0) { free(parent); return cur; }
        free(cur); cur = parent;
    }
}

void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_path, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC)) {
        out->data = malloc(val->len);
        memcpy(out->data, val->data, val->len);
        out->len = val->len;
        return;
    }
    if (!name || !name[0]) return;
    if (name[0] == '/') { kvlangKvGetOne(kv, name, out); return; }
    char *rw = kvlangBuiltinFuncFrameRoot(kv, frame_path);
    kvlangStrbuf_t key; kvlangStrbufInit(&key);
    char *stk = kvlangKeytreeStack(rw);
    kvlangStrbufPuts(&key, stk); free(stk); kvlangStrbufPuts(&key, name);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetOne(kv, key.p, &pv);
    if (kvlangXvalueIsPtr(&pv)) {
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangStrbuf_t ak; kvlangStrbufInit(&ak);
        char *stk2 = kvlangKeytreeStack(rw);
        kvlangStrbufPuts(&ak, stk2); free(stk2); kvlangStrbufPuts(&ak, target);
        free(target);
        kvlangXvalue_t av; kvlangXvalueZero(&av);
        kvlangKvGetOne(kv, ak.p, &av);
        if (!kvlangXvalueNone(&av)) {
            char *path = kvlangXvalueValueString(&av);
            kvlangKvGetOne(kv, path, out);
            free(path);
        }
        kvlangXvalueFree(&av); kvlangStrbufFree(&ak);
    } else if (!kvlangXvalueNone(&pv)) {
        *out = pv; pv.data = NULL; pv.len = 0;
    }
    kvlangXvalueFree(&pv); kvlangStrbufFree(&key); free(rw);
}

char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_path, const char *name) {
    if (name[0] == '/') return strdup(name);
    char *rw = kvlangBuiltinFuncFrameRoot(kv, frame_path);
    kvlangStrbuf_t key; kvlangStrbufInit(&key);
    char *stk = kvlangKeytreeStack(rw);
    kvlangStrbufPuts(&key, stk); free(stk); kvlangStrbufPuts(&key, name);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetOne(kv, key.p, &pv);
    char *result = NULL;
    if (kvlangXvalueIsPtr(&pv)) {
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangStrbuf_t ak; kvlangStrbufInit(&ak);
        char *stk2 = kvlangKeytreeStack(rw);
        kvlangStrbufPuts(&ak, stk2); free(stk2); kvlangStrbufPuts(&ak, target);
        free(target);
        kvlangXvalue_t av; kvlangXvalueZero(&av);
        kvlangKvGetOne(kv, ak.p, &av);
        if (!kvlangXvalueNone(&av)) {
            kvlangXvalue_t v = av; av.data = NULL; av.len = 0;
            for (;;) {
                if (!kvlangXvalueIsCharKind(kvlangXvalueKind(&v))) break;
                char *p = kvlangXvalueValueString(&v);
                kvlangXvalue_t next; kvlangXvalueZero(&next);
                kvlangKvGetOne(kv, p, &next);
                if (kvlangXvalueNone(&next) || !kvlangXvalueIsCharKind(kvlangXvalueKind(&next))) { result = p; break; }
                free(p);
                kvlangXvalueFree(&v); v = next;
            }
            kvlangXvalueFree(&v);
        }
        kvlangXvalueFree(&av); kvlangStrbufFree(&ak);
    }
    kvlangXvalueFree(&pv); kvlangStrbufFree(&key); free(rw);
    if (result) return result;
    /* fallback: Stack(rw) + name */
    char *rw2 = kvlangBuiltinFuncFrameRoot(kv, frame_path);
    kvlangStrbuf_t o; kvlangStrbufInit(&o);
    char *stk3 = kvlangKeytreeStack(rw2);
    kvlangStrbufPuts(&o, stk3); free(stk3); kvlangStrbufPuts(&o, name);
    free(rw2);
    return kvlangStrbufDetach(&o);
}

/* ── coerce / kvlangDisplay ─────────────────────────────────────────────── */

static bool try_parse_int(const char *s, int64_t *out) {
    if (!s || !s[0]) return false;
    char *end; long long v = strtoll(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = v; return true;
}

bool kvlangBuiltinTryParseNumber(const char *s, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!s || !s[0]) return false;
    char c0 = s[0];
    bool num = (c0 >= '0' && c0 <= '9') || (c0 == '-' && s[1] >= '0' && s[1] <= '9');
    if (!num) return false;
    int64_t iv;
    if (try_parse_int(s, &iv)) { kvlangXvalueNewInt64(out, iv); return true; }
    if (c0 != '-' && !strpbrk(s, ".eE")) {
        char *end; unsigned long long uv = strtoull(s, &end, 10);
        if (end != s && *end == 0) {
            uint8_t r[8]; memcpy(r, &uv, 8);
            kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT64, r, 8, 1); return true;
        }
    }
    char *end; double f = strtod(s, &end);
    if (end != s && *end == 0) { kvlangXvalueNewFloat64(out, f); return true; }
    return false;
}

static void xvalue_at(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    int n = kvlangXvalueArrayLen(v);
    if (i < 0 || i >= n) return;
    const char *k = kvlangXvalueKind(v);
    int sz = kvlangXvalueElemSize(k);
    if (sz <= 0) return;
    kvspaceHead_t h; kvspaceDecodeHead(v->data, v->len, &h);
    const uint8_t *body = v->data + h.body_offset;
    kvlangXvalueNewTlv(out, k, body + i * sz, (uint32_t)sz, 1);
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out);

static void format_array(const kvlangXvalue_t *v, char **out) {
    int n = kvlangXvalueArrayLen(v);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPutc(&b, '[');
    for (int i = 0; i < n; i++) {
        if (i) kvlangStrbufPuts(&b, ", ");
        kvlangXvalue_t e; xvalue_at(v, i, &e);
        char *s; kvlangDisplay(&e, &s);
        kvlangStrbufPuts(&b, s); free(s); kvlangXvalueFree(&e);
    }
    kvlangStrbufPutc(&b, ']');
    *out = kvlangStrbufDetach(&b);
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v))) { *out = kvlangXvalueValueString(v); return; }
    if (kvlangXvalueArrayLen(v) > 1) { format_array(v, out); return; }
    *out = kvlangXvalueValueString(v);
}

/* ── frame helper ─────────────────────────────────────────────────── */

static int read_inputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *ff = kvlangBuiltinFuncFrameRoot(f->kv, fr);
    free(fr);
    int n = 0;
    for (int i = 0; i < f->inst->nr && n < cap; i++) {
        kvlangBuiltinResolveReadValue(f->kv, ff, f->inst->reads[i].name, &f->inst->reads[i].val, &out[n]);
        n++;
    }
    free(ff);
    return n;
}

static void free_inputs(kvlangXvalue_t *in, int n) { for (int i = 0; i < n; i++) kvlangXvalueFree(&in[i]); }

static void next_pc(kvlangFrame_t *f) {
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc);
    kvlangRwirNextPc(f->pc, &npc);
    kvlangVthreadSet(f->kv, f->vtid, npc.p, "running");
    kvlangStrbufFree(&npc);
}

static int write_result(kvlangFrame_t *f, const kvlangXvalue_t *result) {
    if (f->inst->nw > 0) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
        free(fr);
        kvlangKvPair_t pair = { key, *result };
        char err[256];
        kvlangKvSet(f->kv, &pair, 1, err, sizeof err);
        free(key);
    }
    next_pc(f);
    return 0;
}

static int set_err(kvlangFrame_t *f, const char *fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
    return -1;
}

/* ── 数值算子 ─────────────────────────────────────────────────────── */

static int kvlangBuiltinAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n == 2 && kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0])) && kvlangXvalueIsCharKind(kvlangXvalueKind(&in[1]))) {
        char *a = kvlangXvalueValueString(&in[0]), *b = kvlangXvalueValueString(&in[1]);
        kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPuts(&s, a); kvlangStrbufPuts(&s, b);
        kvlangXvalue_t r; kvlangXvalueNewCharUtf32(&r, s.p);
        rc = write_result(f, &r);
        kvlangXvalueFree(&r); free(a); free(b); kvlangStrbufFree(&s);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        if (is_int_kind(kvlangXvalueKind(&in[0])) && is_int_kind(kvlangXvalueKind(&in[1]))) {
            kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsInt64(&in[0]) + kvlangXvalueAsInt64(&in[1]), &r);
            rc = write_result(f, &r); kvlangXvalueFree(&r);
        } else {
            kvlangXvalue_t r; narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsFloat64(&in[0]) + kvlangXvalueAsFloat64(&in[1]), &r);
            rc = write_result(f, &r); kvlangXvalueFree(&r);
        }
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n == 1) {
        kvlangXvalue_t r; kvlangXvalueNewInt64(&r, -kvlangXvalueAsInt64(&in[0]));
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && is_int_kind(kvlangXvalueKind(&in[0])) && is_int_kind(kvlangXvalueKind(&in[1]))) {
        kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsInt64(&in[0]) - kvlangXvalueAsInt64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        kvlangXvalue_t r; narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsFloat64(&in[0]) - kvlangXvalueAsFloat64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinMul(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n >= 2 && is_int_kind(kvlangXvalueKind(&in[0])) && is_int_kind(kvlangXvalueKind(&in[1]))) {
        kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsInt64(&in[0]) * kvlangXvalueAsInt64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && is_numeric(&in[0]) && is_numeric(&in[1])) {
        kvlangXvalue_t r; narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsFloat64(&in[0]) * kvlangXvalueAsFloat64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else rc = set_err(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinDiv(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n < 2) { rc = set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return rc; }
    if (kvlangXvalueAsFloat64(&in[1]) == 0) { rc = set_err(f, "ZeroDivisionError: division by zero"); free_inputs(in, n); return rc; }
    if (is_int_kind(kvlangXvalueKind(&in[0])) && is_int_kind(kvlangXvalueKind(&in[1]))) {
        kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsInt64(&in[0]) / kvlangXvalueAsInt64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    } else {
        kvlangXvalue_t r; narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsFloat64(&in[0]) / kvlangXvalueAsFloat64(&in[1]), &r);
        rc = write_result(f, &r); kvlangXvalueFree(&r);
    }
    free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinMod(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int rc;
    if (n < 2 || !is_int_kind(kvlangXvalueKind(&in[0])) || !is_int_kind(kvlangXvalueKind(&in[1]))) {
        rc = set_err(f, "TypeError: expected integer, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
        free_inputs(in, n); return rc;
    }
    int64_t b = kvlangXvalueAsInt64(&in[1]);
    if (b == 0) { rc = set_err(f, "ZeroDivisionError: modulo by zero"); free_inputs(in, n); return rc; }
    kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), kvlangXvalueAsInt64(&in[0]) % b, &r);
    rc = write_result(f, &r); kvlangXvalueFree(&r);
    free_inputs(in, n);
    return rc;
}

typedef enum { CMP_EQ, CMP_NEQ, CMP_LT, CMP_GT, CMP_LE, CMP_GE } cmp_op;

static int kvlangBuiltinCmp(kvlangFrame_t *f, cmp_op op) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return -1; }
    bool allow_null = (op == CMP_EQ || op == CMP_NEQ);
    if (kvlangXvalueNone(&in[0]) || kvlangXvalueNone(&in[1])) {
        if (!allow_null) { set_err(f, "TypeError: None in comparison"); free_inputs(in, n); return -1; }
        bool eq = kvlangXvalueNone(&in[0]) == kvlangXvalueNone(&in[1]);
        bool r = (op == CMP_EQ) ? eq : (op == CMP_NEQ) ? !eq : false;
        kvlangXvalue_t rv; kvlangXvalueNewBool(&rv, r);
        int rc = write_result(f, &rv); kvlangXvalueFree(&rv); free_inputs(in, n); return rc;
    }
    bool r;
    const char *ka = kvlangXvalueKind(&in[0]), *kb = kvlangXvalueKind(&in[1]);
    if (is_int_kind(ka) && is_int_kind(kb)) {
        int c = cmp_int(&in[0], &in[1]);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
    } else if (is_numeric(&in[0]) && is_numeric(&in[1])) {
        double a = kvlangXvalueAsFloat64(&in[0]), b = kvlangXvalueAsFloat64(&in[1]);
        r = op == CMP_EQ ? a == b : op == CMP_NEQ ? a != b : op == CMP_LT ? a < b : op == CMP_GT ? a > b : op == CMP_LE ? a <= b : a >= b;
    } else if (kvlangXvalueIsCharKind(ka) && kvlangXvalueIsCharKind(kb)) {
        char *a = kvlangXvalueValueString(&in[0]), *b = kvlangXvalueValueString(&in[1]);
        int c = strcmp(a, b);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
        free(a); free(b);
    } else if (strcmp(ka, KVSPACE_KIND_BOOL) == 0 && strcmp(kb, KVSPACE_KIND_BOOL) == 0) {
        bool a = kvlangXvalueAsBool(&in[0]), b = kvlangXvalueAsBool(&in[1]);
        r = op == CMP_EQ ? a == b : op == CMP_NEQ ? a != b : op == CMP_LT ? a < b : op == CMP_GT ? a > b : op == CMP_LE ? a <= b : a >= b;
    } else {
        set_err(f, "TypeError: cannot compare %s with %s", ka, kb); free_inputs(in, n); return -1;
    }
    kvlangXvalue_t rv; kvlangXvalueNewBool(&rv, r);
    int rc = write_result(f, &rv); kvlangXvalueFree(&rv); free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinEq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_EQ); }
static int kvlangBuiltinNeq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_NEQ); }
static int kvlangBuiltinLt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LT); }
static int kvlangBuiltinGt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GT); }
static int kvlangBuiltinLe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LE); }
static int kvlangBuiltinGe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GE); }

static int kvlangBuiltinAnd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, n >= 2 && kvlangXvalueAsBool(&in[0]) && kvlangXvalueAsBool(&in[1]));
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinOr(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, n >= 2 && (kvlangXvalueAsBool(&in[0]) || kvlangXvalueAsBool(&in[1])));
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinNot(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, !(n >= 1 && kvlangXvalueAsBool(&in[0])));
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinBit(kvlangFrame_t *f, int op) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !is_int_kind(kvlangXvalueKind(&in[0])) || !is_int_kind(kvlangXvalueKind(&in[1]))) {
        set_err(f, "TypeError: expected integer, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
        free_inputs(in, n); return -1;
    }
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    int64_t v = op == 0 ? a & b : op == 1 ? a | b : op == 2 ? a ^ b : op == 3 ? (a << (uint64_t)b) : (a >> (uint64_t)b);
    kvlangXvalue_t r; narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), v, &r);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinBitand(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 0); }
static int kvlangBuiltinBitor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 1); }
static int kvlangBuiltinBitxor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 2); }
static int kvlangBuiltinShl(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 3); }
static int kvlangBuiltinShr(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 4); }

/* math */
static int kvlangBuiltinMathUnary(kvlangFrame_t *f, int op) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || !is_numeric(&in[0])) { set_err(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none"); free_inputs(in, n); return -1; }
    kvlangXvalue_t r;
    double x = kvlangXvalueAsFloat64(&in[0]);
    switch (op) {
    case 0: kvlangXvalueNewFloat64(&r, sqrt(x)); break;
    case 1: kvlangXvalueNewFloat64(&r, exp(x)); break;
    case 2: kvlangXvalueNewFloat64(&r, log(x)); break;
    case 3: /* neg */ if (is_float_kind(kvlangXvalueKind(&in[0]))) { narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[0]), -x, &r); } else { narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[0]), -kvlangXvalueAsInt64(&in[0]), &r); } break;
    case 4: /* abs */ if (is_float_kind(kvlangXvalueKind(&in[0]))) { narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[0]), fabs(x), &r); } else { narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[0]), kvlangXvalueAsInt64(&in[0]) < 0 ? -kvlangXvalueAsInt64(&in[0]) : kvlangXvalueAsInt64(&in[0]), &r); } break;
    case 5: kvlangXvalueNewInt64(&r, x < 0 ? -1 : x > 0 ? 1 : 0); break;
    }
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinSqrt(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 0); }
static int kvlangBuiltinExp(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 1); }
static int kvlangBuiltinLog(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 2); }
static int kvlangBuiltinNeg(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 3); }
static int kvlangBuiltinAbs(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 4); }
static int kvlangBuiltinSign(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 5); }

static int kvlangBuiltinPow(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !is_numeric(&in[0]) || !is_numeric(&in[1])) { set_err(f, "TypeError: expected numeric"); free_inputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewFloat64(&r, pow(kvlangXvalueAsFloat64(&in[0]), kvlangXvalueAsFloat64(&in[1])));
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

static int kvlangBuiltinMaxmin(kvlangFrame_t *f, bool is_max) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { set_err(f, "TypeError: binary op requires 2 inputs, got %d", n); free_inputs(in, n); return -1; }
    kvlangXvalue_t r;
    if (is_int_kind(kvlangXvalueKind(&in[0])) && is_int_kind(kvlangXvalueKind(&in[1]))) {
        int c = cmp_int(&in[0], &in[1]);
        bool take_a = (is_max && c >= 0) || (!is_max && c <= 0);
        narrow_int(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), take_a ? kvlangXvalueAsInt64(&in[0]) : kvlangXvalueAsInt64(&in[1]), &r);
    } else if (is_numeric(&in[0]) && is_numeric(&in[1])) {
        double a = kvlangXvalueAsFloat64(&in[0]), b = kvlangXvalueAsFloat64(&in[1]);
        bool take_a = (is_max && a >= b) || (!is_max && a <= b);
        narrow_float(kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]), take_a ? a : b, &r);
    } else { set_err(f, "TypeError: max/min requires numeric, got %s and %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1])); free_inputs(in, n); return -1; }
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinMax(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, true); }
static int kvlangBuiltinMin(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, false); }

/* cast */
static int kvlangBuiltinCastNum(kvlangFrame_t *f, const char *kind) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { set_err(f, "TypeError: cannot cast None"); free_inputs(in, n); return -1; }
    kvlangXvalue_t r;
    if (strcmp(kind, KVSPACE_KIND_BOOL) == 0) kvlangXvalueNewBool(&r, kvlangXvalueAsBool(&in[0]));
    else if (strcmp(kind, KVSPACE_KIND_FLOAT32) == 0) { float fv = (float)kvlangXvalueAsFloat64(&in[0]); uint8_t b[4]; memcpy(b, &fv, 4); kvlangXvalueNewTlv(&r, KVSPACE_KIND_FLOAT32, b, 4, 1); }
    else if (strcmp(kind, KVSPACE_KIND_FLOAT64) == 0) kvlangXvalueNewFloat64(&r, kvlangXvalueAsFloat64(&in[0]));
    else { int64_t v = kvlangXvalueAsInt64(&in[0]); narrow_int(kind, kind, v, &r); }
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinCastBool(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_BOOL); }
static int kvlangBuiltinCastInt8(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT8); }
static int kvlangBuiltinCastInt16(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT16); }
static int kvlangBuiltinCastInt32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT32); }
static int kvlangBuiltinCastInt64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT64); }
static int kvlangBuiltinCastUint8(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT8); }
static int kvlangBuiltinCastUint16(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT16); }
static int kvlangBuiltinCastUint32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT32); }
static int kvlangBuiltinCastUint64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT64); }
static int kvlangBuiltinCastF32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT32); }
static int kvlangBuiltinCastF64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT64); }

static int kvlangBuiltinCastChar(kvlangFrame_t *f, const char *kind) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { set_err(f, "TypeError: char conversion requires a value"); free_inputs(in, n); return -1; }
    char *s = kvlangXvalueValueString(&in[0]);
    kvlangXvalue_t r;
    if (strcmp(kind, KVSPACE_KIND_CHAR) == 0) kvlangXvalueNewCharUtf32(&r, s);
    else kvlangXvalueNewCharKind(&r, kind, s);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free(s); free_inputs(in, n);
    return rc;
}
static int kvlangBuiltinCastChar32(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR); }
static int kvlangBuiltinCastChar8(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_UTF8); }
static int kvlangBuiltinCastCharAscii(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_ASCII); }

/* ── 注册表 ───────────────────────────────────────────────────────── */

/* 数字多类型运算融合为单条：派发前 strip_num_kind 剥掉 <numkind>. 前缀，
 * int64.add / float32.add … 全部归到同一条 add（union 语义），kvlangBuiltin* 按操作数 kind 归约。 */
static const struct { const char *op; kvlangBuiltinFn fn; } builtins[] = {
    {"add", kvlangBuiltinAdd}, {"+", kvlangBuiltinAdd},
    {"sub", kvlangBuiltinSub}, {"-", kvlangBuiltinSub},
    {"mul", kvlangBuiltinMul}, {"×", kvlangBuiltinMul},
    {"div", kvlangBuiltinDiv}, {"÷", kvlangBuiltinDiv},
    {"mod", kvlangBuiltinMod}, {"%", kvlangBuiltinMod},
    {"eq", kvlangBuiltinEq}, {"==", kvlangBuiltinEq},
    {"neq", kvlangBuiltinNeq}, {"!=", kvlangBuiltinNeq}, {"≠", kvlangBuiltinNeq},
    {"lt", kvlangBuiltinLt}, {"<", kvlangBuiltinLt},
    {"gt", kvlangBuiltinGt}, {">", kvlangBuiltinGt},
    {"le", kvlangBuiltinLe}, {"<=", kvlangBuiltinLe}, {"≤", kvlangBuiltinLe},
    {"ge", kvlangBuiltinGe}, {">=", kvlangBuiltinGe}, {"≥", kvlangBuiltinGe},
    {"and", kvlangBuiltinAnd}, {"&&", kvlangBuiltinAnd},
    {"or", kvlangBuiltinOr}, {"||", kvlangBuiltinOr},
    {"not", kvlangBuiltinNot}, {"!", kvlangBuiltinNot},
    {"bitand", kvlangBuiltinBitand}, {"&", kvlangBuiltinBitand},
    {"bitor", kvlangBuiltinBitor}, {"|", kvlangBuiltinBitor},
    {"bitxor", kvlangBuiltinBitxor}, {"^", kvlangBuiltinBitxor},
    {"shl", kvlangBuiltinShl}, {"<<", kvlangBuiltinShl},
    {"shr", kvlangBuiltinShr}, {">>", kvlangBuiltinShr},
    {"pow", kvlangBuiltinPow},
    {"sqrt", kvlangBuiltinSqrt}, {"√", kvlangBuiltinSqrt},
    {"exp", kvlangBuiltinExp},
    {"log", kvlangBuiltinLog},
    {"neg", kvlangBuiltinNeg},
    {"abs", kvlangBuiltinAbs},
    {"sign", kvlangBuiltinSign},
    {"max", kvlangBuiltinMax}, {"min", kvlangBuiltinMin},
    /* cast */
    {"bool", kvlangBuiltinCastBool}, {"int8", kvlangBuiltinCastInt8}, {"int16", kvlangBuiltinCastInt16},
    {"int32", kvlangBuiltinCastInt32}, {"int64", kvlangBuiltinCastInt64}, {"uint8", kvlangBuiltinCastUint8},
    {"uint16", kvlangBuiltinCastUint16}, {"uint32", kvlangBuiltinCastUint32}, {"uint64", kvlangBuiltinCastUint64},
    {"float32", kvlangBuiltinCastF32}, {"float64", kvlangBuiltinCastF64},
    {"char/utf32", kvlangBuiltinCastChar32}, {"char/utf8", kvlangBuiltinCastChar8}, {"char/ascii", kvlangBuiltinCastCharAscii},
    /* collection */
    {"array", kvlangBuiltinArray},
    {"array.scatter", kvlangBuiltinScatter}, {"array.compact", kvlangBuiltinCompact},
    {"array.append", kvlangBuiltinAppend}, {"array.slice", kvlangBuiltinSlice},
    {"obj", kvlangBuiltinObj}, {"map", kvlangBuiltinMap},
    {"ndarray.numel", kvlangBuiltinNdarrayNumel}, {"ndarray.dim", kvlangBuiltinNdarrayDim}, {"ndarray.shape", kvlangBuiltinNdarrayShape},
    {"xv.at", kvlangBuiltinXvAt}, {"xv.set", kvlangBuiltinXvSet}, {"xv.reshape", kvlangBuiltinXvReshape},
    {"string.set", kvlangBuiltinStringSet}, {"string.char", kvlangBuiltinStringChar}, {"string.ord", kvlangBuiltinStringOrd},
    {"string.cmp", kvlangBuiltinStringCmp}, {"string.find", kvlangBuiltinStringFind}, {"string.len", kvlangBuiltinStringLen},
    {"string.slice", kvlangBuiltinStringSlice}, {"string.concat", kvlangBuiltinStringConcat},
    {"time.now", kvlangBuiltinTimeNow}, {"time.sub", kvlangBuiltinTimeSub}, {"time.add", kvlangBuiltinTimeAdd},
    {"time/duration.nanos", kvlangBuiltinDurFrom}, {"time/duration.millis", kvlangBuiltinDurFrom},
    {"time/duration.seconds", kvlangBuiltinDurFrom}, {"time/duration.minutes", kvlangBuiltinDurFrom},
    {"time/duration.hours", kvlangBuiltinDurFrom},
    {"time/duration.as_nanos", kvlangBuiltinDurTo}, {"time/duration.as_millis", kvlangBuiltinDurTo},
    {"time/duration.as_seconds", kvlangBuiltinDurTo}, {"time/duration.as_minutes", kvlangBuiltinDurTo},
    {"time/duration.as_hours", kvlangBuiltinDurTo},
    {"time/duration.add", kvlangBuiltinDurArith}, {"time/duration.sub", kvlangBuiltinDurArith},
    {"time/duration.before", kvlangBuiltinDurCmp}, {"time/duration.after", kvlangBuiltinDurCmp},
    {"time.before", kvlangBuiltinTimeCmp}, {"time.after", kvlangBuiltinTimeCmp},
    {"random.uint64", kvlangBuiltinRandUint64}, {"random.int63", kvlangBuiltinRandInt63}, {"random.intn", kvlangBuiltinRandIntn},
    {"kv.get", kvlangBuiltinKvGet}, {"kv.set", kvlangBuiltinKvSet}, {"kv.del", kvlangBuiltinKvDel},
    {"kv.deltree", kvlangBuiltinKvDelTree}, {"kv.list", kvlangBuiltinKvList}, {"kv.listlen", kvlangBuiltinKvListLen}, {"kv.listn", kvlangBuiltinKvListN}, {"kv.mkindex", kvlangBuiltinKvMkindex},
    {"kv.extindex", kvlangBuiltinKvExtIndex}, {"kv.rmindexext", kvlangBuiltinKvRmIndexExt}, {"kv.watch", kvlangBuiltinKvWatch},
    {"debugger", kvlangBuiltinDebugger},
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

bool kvlangBuiltinIsNative(const char *opcode) {
    const char *op = strip_num_kind(opcode);
    for (size_t i = 0; i < builtins_n; i++) if (strcmp(builtins[i].op, op) == 0) return true;
    return false;
}

bool kvlangBuiltinNumOp(const char *opcode) {
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

int kvlangBuiltinNative(kvlangFrame_t *f) {
    const char *op = strip_num_kind(f->inst->opcode);
    for (size_t i = 0; i < builtins_n; i++) {
        if (strcmp(builtins[i].op, op) == 0) return builtins[i].fn(f);
    }
    return set_err(f, "unknown builtin op: %s", f->inst->opcode);
}

/* ── copy ─────────────────────────────────────────────────────────── */

int kvlangBuiltinExecuteCopy(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    char *fr = kvlangKeytreeFrameRoot(pc);
    kvlangFrame_t f = { kv, vtid, pc, inst };
    if (inst->nr == 0) { free(fr); next_pc(&f); return 0; }
    char *ff = kvlangBuiltinFuncFrameRoot(kv, fr);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(kv, ff, inst->reads[0].name, &inst->reads[0].val, &v);
    free(ff);
    for (int i = 0; i < inst->nw; i++) {
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, inst->writes[i].name);
        kvlangKvPair_t pair = { key, v };
        char err[256];
        kvlangKvSet(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangXvalueFree(&v);
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
    int rc = write_result(f, &e);
    kvlangXvalueFree(&e);
    return rc;
}

static char *kvlangKvKey(const kvlangXvalue_t *v) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v))) return kvlangXvalueValueString(v);
    if (is_int_kind(kvlangXvalueKind(v))) { char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)kvlangXvalueAsInt64(v)); return strdup(buf); }
    return strdup("");
}

static const char *var_len_char_err(const char *kind) {
    return strcmp(kind, KVSPACE_KIND_CHAR_UTF8) == 0 ? "TypeError: char/utf8 is variable-width; index/code-point ops require char/utf32 or char/ascii" : NULL;
}

static void pack_typed_array(const char *kind, const kvlangXvalue_t *elems, int n, kvlangXvalue_t *out) {
    int sz = kvlangXvalueElemSize(kind);
    uint8_t *raw = malloc((size_t)sz * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        const uint8_t *b; int32_t blen;
        kvspaceHead_t h; kvspaceDecodeHead(elems[i].data, elems[i].len, &h);
        b = elems[i].data + h.body_offset; blen = h.body_len;
        int c = blen < sz ? blen : sz;
        memcpy(raw + i * sz, b, (size_t)c);
        for (int j = c; j < sz; j++) raw[i * sz + j] = 0;
    }
    kvlangXvalueNewTlv(out, kind, raw, (uint32_t)(sz * n), n);
    free(raw);
}

static char *separated_base(kvlangKv_t *kv, const char *fp, const char *name) {
    char *base = kvlangBuiltinResolveWriteSlot(kv, fp, name);
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangStrbufPuts(&k, base); kvlangStrbufPuts(&k, ".[0]");
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, k.p, &v);
    bool exists = !kvlangXvalueNone(&v);
    kvlangXvalueFree(&v); kvlangStrbufFree(&k);
    if (!exists) { free(base); return NULL; }
    return base;
}

static int separated_len(kvlangKv_t *kv, const char *base) {
    for (int i = 0; ; i++) {
        kvlangStrbuf_t k; kvlangStrbufInit(&k);
        kvlangStrbufPrintf(&k, "%s.[%d]", base, i);
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, k.p, &v);
        bool none = kvlangXvalueNone(&v);
        kvlangXvalueFree(&v); kvlangStrbufFree(&k);
        if (none) return i;
    }
}

/* 坐标段 key：base.[s0,s1,...]。1 维即 base.[s0]。 */
static char *scatter_key(const char *base, const int64_t *coords, int ncoord) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, base); kvlangStrbufPutc(&b, '.'); kvlangStrbufPutc(&b, '[');
    for (int i = 0; i < ncoord; i++) {
        if (i) kvlangStrbufPutc(&b, ',');
        kvlangStrbufPrintf(&b, "%lld", (long long)coords[i]);
    }
    kvlangStrbufPutc(&b, ']');
    return kvlangStrbufDetach(&b);
}

/* map 目录标记：kind=strkeymapindex，body=[4B count LE][name\n...]，dims 落 head。 */
static void map_marker(kvlangXvalue_t *out, const char *const *names, int n, const int32_t *dims, int ndim) {
    kvlangStrbuf_t body; kvlangStrbufInit(&body);
    char count[4] = { (char)(n & 0xFF), (char)((n >> 8) & 0xFF), (char)((n >> 16) & 0xFF), (char)((n >> 24) & 0xFF) };
    kvlangStrbufPutn(&body, count, 4);
    for (int i = 0; i < n; i++) {
        if (i) kvlangStrbufPutc(&body, '\n');
        kvlangStrbufPuts(&body, names[i]);
    }
    kvlangXvalueNewTlvDims(out, KVSPACE_KIND_MAP, (const uint8_t *)body.p, (uint32_t)body.len, dims, ndim);
    kvlangStrbufFree(&body);
}

/* ── array 系列 ───────────────────────────────────────────────────── */

static void ensure_scattered(kvlangFrame_t *f, const char *base) {
    kvlangXvalue_t arr; kvlangXvalueZero(&arr);
    kvlangKvGetOne(f->kv, base, &arr);
    if (kvlangXvalueNone(&arr) || kvlangXvalueElemSize(kvlangXvalueKind(&arr)) <= 0) { kvlangXvalueFree(&arr); return; }
    int n = kvlangXvalueArrayLen(&arr);
    for (int i = 0; i < n; i++) {
        int64_t c[1] = { i };
        char *k = scatter_key(base, c, 1);
        kvlangXvalue_t e; xvalue_at(&arr, i, &e);
        kvlangKvPair_t p = { k, e };
        char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k);
    }
    kvlangXvalueFree(&arr);
    char err[256]; kvlangKvDel(f->kv, base, err, sizeof err);
}

int kvlangBuiltinArray(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = read_inputs(f, in, 64);
    if (f->inst->nw == 0 || n == 0) { next_pc(f); free_inputs(in, n); return 0; }
    const char *kind = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(kind) <= 0) { free_inputs(in, n); return set_err(f, "array: unsupported element kind %s", kind); }
    for (int i = 1; i < n; i++) if (strcmp(kvlangXvalueKind(&in[i]), kind) != 0) { free_inputs(in, n); return set_err(f, "array: mixed kinds %s and %s", kind, kvlangXvalueKind(&in[i])); }
    kvlangXvalue_t arr; pack_typed_array(kind, in, n, &arr);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    kvlangKvPair_t p = { key, arr };
    char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key); kvlangXvalueFree(&arr);
    next_pc(f);
    free_inputs(in, n);
    return 0;
}

int kvlangBuiltinNdarrayNumel(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    int64_t n_el = 0;
    if (n > 0) n_el = kvlangXvalueArrayLen(&in[0]);
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, n_el);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinNdarrayDim(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    int64_t ndim = 0;
    if (n > 0) {
        kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        ndim = kx.ndim;
    }
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, ndim);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinNdarrayShape(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    int32_t dims[8]; int32_t ndim = 0;
    if (n > 0 && !kvlangXvalueNone(&in[0])) {
        kvspaceHead_t h; kvlangXvalueHead(&in[0], &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        ndim = kx.ndim;
        for (int i = 0; i < ndim && i < 8; i++) dims[i] = kx.dims[i];
    }
    uint8_t raw[64]; uint32_t raw_len = 0;
    for (int i = 0; i < ndim; i++) {
        int64_t d = dims[i];
        for (int j = 0; j < 8; j++) raw[i * 8 + j] = (d >> (j * 8)) & 0xFF;
        raw_len += 8;
    }
    int32_t sd[1] = { ndim };
    kvlangXvalue_t r; kvlangXvalueNewTlvDims(&r, KVSPACE_KIND_INT64, raw, raw_len, sd, 1);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

/* 计算多维下标 (i0..i_{n-1}) 的 row-major 扁平索引，越界返回 -1。 */
static int64_t flat_index(const kvlang_kindexpr_t *kx, const kvlangXvalue_t *in, int nidx) {
    int64_t flat = 0;
    for (int i = 0; i < nidx; i++) {
        int64_t idx = kvlangXvalueAsInt64(&in[i + 1]);
        if (idx < 0 || idx >= kx->dims[i]) return -1;
        flat = flat * kx->dims[i] + idx;
    }
    return flat;
}

int kvlangBuiltinXvAt(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 1;
    if (nidx < 1) return set_err(f, "TypeError: xv.at requires array and indices");
    kvlangXvalue_t in[MAX_PARAMS]; int n = read_inputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    int sz = kvlangXvalueElemSize(k);
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (sz <= 0 || kx.ndim == 0) { free_inputs(in, n); return set_err(f, "TypeError: xv.at requires a compact array, got %s", k); }
    if (nidx != kx.ndim) { free_inputs(in, n); return set_err(f, "IndexError: xv.at: %d-dim array needs %d indices, got %d", kx.ndim, kx.ndim, nidx); }
    int64_t flat = flat_index(&kx, in, nidx);
    if (flat < 0) { free_inputs(in, n); return set_err(f, "IndexError: xv.at: index out of bounds"); }
    const uint8_t *body = in[0].data + h.body_offset;
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, k, body + flat * sz, (uint32_t)sz, 1);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinXvSet(kvlangFrame_t *f) {
    int nidx = f->inst->nr - 2;
    if (nidx < 1) return set_err(f, "TypeError: xv.set requires array, indices, value");
    if (f->inst->nw == 0) return set_err(f, "TypeError: xv.set requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS]; int n = read_inputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    int sz = kvlangXvalueElemSize(k);
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (sz <= 0 || kx.ndim == 0) { free_inputs(in, n); return set_err(f, "TypeError: xv.set requires a compact array, got %s", k); }
    if (nidx != kx.ndim) { free_inputs(in, n); return set_err(f, "IndexError: xv.set: %d-dim array needs %d indices, got %d", kx.ndim, kx.ndim, nidx); }
    int64_t flat = flat_index(&kx, in, nidx);
    if (flat < 0) { free_inputs(in, n); return set_err(f, "IndexError: xv.set: index out of bounds"); }
    const uint8_t *body = in[0].data + h.body_offset;
    uint8_t *nb = malloc((size_t)h.body_len);
    memcpy(nb, body, (size_t)h.body_len);
    kvspaceHead_t vh; kvspaceDecodeHead(in[nidx + 1].data, in[nidx + 1].len, &vh);
    const uint8_t *vb = in[nidx + 1].data + vh.body_offset;
    int c = vh.body_len < sz ? vh.body_len : sz;
    memcpy(nb + flat * sz, vb, (size_t)c);
    kvlangXvalue_t nv; kvlangXvalueNewTlvDims(&nv, k, nb, (uint32_t)h.body_len, kx.dims, kx.ndim);
    int rc = write_result(f, &nv);
    kvlangXvalueFree(&nv); free(nb); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinXvReshape(kvlangFrame_t *f) {
    int ndims = f->inst->nr - 1;
    if (ndims < 1) return set_err(f, "TypeError: xv.reshape requires array and >=1 dims");
    if (f->inst->nw == 0) return set_err(f, "TypeError: xv.reshape requires a write param (-> a)");
    kvlangXvalue_t in[MAX_PARAMS]; int n = read_inputs(f, in, MAX_PARAMS);
    const char *k = kvlangXvalueKind(&in[0]);
    if (kvlangXvalueElemSize(k) <= 0) { free_inputs(in, n); return set_err(f, "TypeError: xv.reshape requires a compact array, got %s", k); }
    kvspaceHead_t h; kvspaceDecodeHead(in[0].data, in[0].len, &h);
    kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
    if (kx.ndim < 1) { free_inputs(in, n); return set_err(f, "TypeError: xv.reshape requires a compact array, got scalar %s", k); }
    if (ndims > X_MAX_NDIM) { free_inputs(in, n); return set_err(f, "IndexError: xv.reshape: at most %d dims, got %d", X_MAX_NDIM, ndims); }
    int32_t dims[X_MAX_NDIM]; int64_t numel = 1;
    for (int i = 0; i < ndims; i++) {
        dims[i] = (int32_t)kvlangXvalueAsInt64(&in[i + 1]);
        if (dims[i] < 0) { free_inputs(in, n); return set_err(f, "IndexError: xv.reshape: negative dim %d", dims[i]); }
        numel *= dims[i];
    }
    if (numel != kx.array_len) { free_inputs(in, n); return set_err(f, "IndexError: xv.reshape: cannot reshape %d elements into %lld", kx.array_len, (long long)numel); }
    const uint8_t *body = in[0].data + h.body_offset;
    kvlangXvalue_t nv; kvlangXvalueNewTlvDims(&nv, k, body, (uint32_t)h.body_len, dims, ndims);
    int rc = write_result(f, &nv);
    kvlangXvalueFree(&nv);
    free_inputs(in, n);
    return rc;
}

int kvlangBuiltinScatter(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return set_err(f, "TypeError: array.scatter requires a write param");
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n == 0 || kvlangXvalueNone(&in[0])) { next_pc(f); free_inputs(in, n); return 0; }
    if (kvlangXvalueElemSize(kvlangXvalueKind(&in[0])) <= 0) { free_inputs(in, n); return set_err(f, "TypeError: array.scatter requires a compact array ([]T), got %s", kvlangXvalueKind(&in[0])); }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    int al = kvlangXvalueArrayLen(&in[0]);
    if (al > 0) {
        char **names = malloc(sizeof(char *) * (size_t)al);
        for (int i = 0; i < al; i++) {
            kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPrintf(&s, "[%d]", i);
            names[i] = kvlangStrbufDetach(&s);
        }
        int32_t dims[1] = { al };
        kvlangXvalue_t mark; map_marker(&mark, (const char *const *)names, al, dims, 1);
        kvlangKvPair_t p0 = { dst, mark }; char err[256]; kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        for (int i = 0; i < al; i++) free(names[i]);
        free(names);
    }
    for (int i = 0; i < al; i++) {
        int64_t c[1] = { i };
        char *k = scatter_key(dst, c, 1);
        kvlangXvalue_t e; xvalue_at(&in[0], i, &e);
        kvlangKvPair_t p = { k, e }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k);
    }
    free(dst); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

int kvlangBuiltinCompact(kvlangFrame_t *f) {
    if (f->inst->nr == 0 || f->inst->nw == 0) return set_err(f, "TypeError: array.compact requires read and write params");
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *src = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[0].name);
    kvlangXvalue_t elems[1024]; int n = 0;
    for (int i = 0; ; i++) {
        int64_t c[1] = { i };
        char *k = scatter_key(src, c, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v); free(k);
        if (none) break;
        elems[n++] = v;
    }
    if (n == 0) { free(src); free(fr); next_pc(f); return 0; }
    kvlangXvalue_t arr; pack_typed_array(kvlangXvalueKind(&elems[0]), elems, n, &arr);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    kvlangKvPair_t p = { dst, arr }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    for (int i = 0; i < n; i++) kvlangXvalueFree(&elems[i]);
    kvlangXvalueFree(&arr);
    free(dst); free(src); free(fr);
    next_pc(f); return 0;
}

int kvlangBuiltinAppend(kvlangFrame_t *f) {
    if (f->inst->nr < 2) return set_err(f, "TypeError: array.append requires array and element");
    if (f->inst->nw == 0) return set_err(f, "TypeError: array.append requires a write param (-> arr)");
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int len = separated_len(f->kv, base);
    int64_t c[1] = { len };
    char *k = scatter_key(base, c, 1);
    kvlangKvPair_t p = { k, n >= 2 ? in[1] : in[0] };
    char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(k); free(base); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

int kvlangBuiltinSlice(kvlangFrame_t *f) {
    if (f->inst->nr < 3) return set_err(f, "TypeError: array.slice requires array, start, end");
    if (f->inst->nw == 0) return set_err(f, "TypeError: array.slice requires a write param (-> arr)");
    kvlangXvalue_t in[3]; int n = read_inputs(f, in, 3);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    ensure_scattered(f, base);
    int al = separated_len(f->kv, base);
    int lo = (int)kvlangXvalueAsInt64(&in[1]), hi = (int)kvlangXvalueAsInt64(&in[2]);
    if (lo < 0 || hi < lo || hi > al) { free(base); free(fr); free_inputs(in, n); return set_err(f, "IndexError: array.slice: bounds [%d:%d] out of range (len=%d)", lo, hi, al); }
    for (int i = lo; i < hi; i++) {
        int64_t sc[1] = { i }, dc[1] = { i - lo };
        char *sk = scatter_key(base, sc, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, sk, &v);
        char *dk = scatter_key(base, dc, 1);
        kvlangKvPair_t p = { dk, v }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v); free(sk); free(dk);
    }
    for (int i = hi - lo; i < al; i++) {
        int64_t dc[1] = { i };
        char *dk = scatter_key(base, dc, 1);
        char err[256]; kvlangKvDel(f->kv, dk, err, sizeof err); free(dk);
    }
    free(base); free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

/* ── dict ─────────────────────────────────────────────────────────── */

int kvlangBuiltinObj(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = read_inputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        kvlangXvalue_t mark; kvlangXvalueNewTlv(&mark, KVSPACE_KIND_OBJ, (const uint8_t *)"", 0, 1);
        kvlangKvPair_t p0 = { ok, mark };
        char err[256]; kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        for (int i = 0; i + 1 < n; i += 2) {
            if (kvlangXvalueNone(&in[i + 1])) continue;
            char *k = kvlangXvalueValueString(&in[i]);
            char *mk = kvlangKeytreeMember(ok, k);
            kvlangKvPair_t p = { mk, in[i + 1] };
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(k); free(mk);
        }
        free(ok);
    }
    free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

int kvlangBuiltinMap(kvlangFrame_t *f) {
    kvlangXvalue_t in[64]; int n = read_inputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        /* 散 key 数组：元素在 ok.[i]，目录标记（kind=strkeymapindex，dims=[n]）在 ok。 */
        int old = separated_len(f->kv, ok);
        for (int i = 0; i < old; i++) {
            int64_t c[1] = { i };
            char *k = scatter_key(ok, c, 1);
            char err[256]; kvlangKvDel(f->kv, k, err, sizeof err); free(k);
        }
        char err[256]; kvlangKvDel(f->kv, ok, err, sizeof err);

        char **names = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
        int64_t *cs = malloc(sizeof(int64_t) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPrintf(&s, "[%d]", i);
            names[i] = kvlangStrbufDetach(&s); cs[i] = i;
        }
        int32_t dims[1] = { n };
        if (n > 0) {
            kvlangXvalue_t mark; map_marker(&mark, (const char *const *)names, n, dims, 1);
            kvlangKvPair_t p0 = { ok, mark };
            kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
            kvlangXvalueFree(&mark);
        }
        for (int i = 0; i < n; i++) {
            char *k = scatter_key(ok, &cs[i], 1);
            kvlangKvPair_t p = { k, in[i] };
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(k); free(names[i]);
        }
        free(names); free(cs); free(ok);
    }
    free(fr);
    next_pc(f); free_inputs(in, n); return 0;
}

/* ── string 系列 ──────────────────────────────────────────────────── */

int kvlangBuiltinStringSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
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
    next_pc(f); free_inputs(in, n); return 0;
}

int kvlangBuiltinStringChar(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.char requires string and index"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int idx = (int)kvlangXvalueAsInt64(&in[1]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    int rc;
    if (idx < 0 || idx >= rn) rc = set_err(f, "IndexError: at: index %d out of bounds (char count=%d)", idx, rn);
    else { uint32_t one = r[idx]; rc = write_char32(f, &one, 1); }
    free(r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinStringOrd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: string.ord requires a string"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, rn == 0 ? -1 : (int64_t)r[0]);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free(r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinStringCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.cmp requires two strings"); }
    char *a = kvlangXvalueValueString(&in[0]), *b = kvlangXvalueValueString(&in[1]);
    int64_t r = strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, r);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free(a); free(b); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinStringFind(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: string.find requires two strings"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int hn; uint32_t *hay = string_runes(&in[0], &hn);
    int nn; uint32_t *needle = string_runes(&in[1], &nn);
    int64_t r = -1;
    if (nn == 0) r = 0;
    else for (int i = 0; i + nn <= hn; i++) { bool m = true; for (int j = 0; j < nn; j++) if (hay[i + j] != needle[j]) { m = false; break; } if (m) { r = i; break; } }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, r);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free(hay); free(needle); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinStringLen(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    int len = 0;
    if (n > 0) {
        if (var_len_char_err(kvlangXvalueKind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
        if (kvlangXvalueKindIs(&in[0], KVSPACE_KIND_CHAR)) len = kvlangXvalueArrayLen(&in[0]);
        else { uint32_t *r = string_runes(&in[0], &len); free(r); }
    }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, len);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinStringSlice(kvlangFrame_t *f) {
    kvlangXvalue_t in[3]; int n = read_inputs(f, in, 3);
    if (n < 3) { free_inputs(in, n); return set_err(f, "TypeError: string.slice requires string, start, end"); }
    if (var_len_char_err(kvlangXvalueKind(&in[0]))) { free_inputs(in, n); return set_err(f, "%s", var_len_char_err(kvlangXvalueKind(&in[0]))); }
    int lo = (int)kvlangXvalueAsInt64(&in[1]), hi = (int)kvlangXvalueAsInt64(&in[2]);
    int rn; uint32_t *r = string_runes(&in[0], &rn);
    if (lo < 0 || hi > rn || lo > hi) { free(r); free_inputs(in, n); return set_err(f, "IndexError: at: slice index out of bounds (lo=%d hi=%d char count=%d)", lo, hi, rn); }
    write_char32(f, r + lo, hi - lo);
    free(r); free_inputs(in, n);
    return 0;
}

int kvlangBuiltinStringConcat(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0])) || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[1]))) {
        if (n >= 2) set_err(f, "TypeError: string.concat requires strings, got %s and %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]));
        else { kvlangXvalue_t e; kvlangXvalueNewCharUtf32(&e, ""); write_result(f, &e); kvlangXvalueFree(&e); }
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

static void new_time(kvlangXvalue_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_TIME, r, 8, 1);
}
static void new_duration(kvlangXvalue_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_DURATION, r, 8, 1);
}

int kvlangBuiltinTimeNow(kvlangFrame_t *f) {
    kvlangXvalue_t e; new_time(&e, now_nanos());
    int rc = write_result(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinTimeSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time.sub requires 2 time args"); }
    kvlangXvalue_t e; new_duration(&e, kvlangXvalueAsInt64(&in[0]) - kvlangXvalueAsInt64(&in[1]));
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinTimeAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time.add requires time and duration"); }
    kvlangXvalue_t e; new_time(&e, kvlangXvalueAsInt64(&in[0]) + kvlangXvalueAsInt64(&in[1]));
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
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

int kvlangBuiltinDurFrom(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: time/duration.from requires 1 int64 arg"); }
    kvlangXvalue_t e; new_duration(&e, kvlangXvalueAsInt64(&in[0]) * dur_scale(f->inst->opcode));
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinDurTo(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1) { free_inputs(in, n); return set_err(f, "TypeError: time/duration.as requires 1 duration arg"); }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, kvlangXvalueAsInt64(&in[0]) / dur_scale(f->inst->opcode));
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinDurArith(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time/duration arith requires 2 duration args"); }
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    bool sub = strstr(f->inst->opcode, ".sub") != NULL;
    kvlangXvalue_t e; new_duration(&e, sub ? a - b : a + b);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinDurCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time/duration cmp requires 2 duration args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    kvlangXvalue_t e; kvlangXvalueNewBool(&e, before ? a < b : a > b);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinTimeCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 2) { free_inputs(in, n); return set_err(f, "TypeError: time cmp requires 2 args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    kvlangXvalue_t e; kvlangXvalueNewBool(&e, before ? a < b : a > b);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

/* ── random ───────────────────────────────────────────────────────── */

static uint64_t crypto_rand_u64(void) {
    uint64_t v = 0;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) { fread(&v, 8, 1, fp); fclose(fp); }
    return v;
}

int kvlangBuiltinRandUint64(kvlangFrame_t *f) {
    uint64_t v = crypto_rand_u64();
    uint8_t r[8]; memcpy(r, &v, 8);
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = write_result(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinRandInt63(kvlangFrame_t *f) {
    int64_t v = (int64_t)(crypto_rand_u64() >> 1);
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, v);
    int rc = write_result(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinRandIntn(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { free_inputs(in, n); return set_err(f, "TypeError: random.intn requires 1 int64 arg"); }
    uint64_t m = (uint64_t)kvlangXvalueAsInt64(&in[0]);
    uint64_t v = m == 0 ? 0 : crypto_rand_u64() % m;
    uint8_t r[8]; memcpy(r, &v, 8);
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = write_result(f, &e); kvlangXvalueFree(&e); free_inputs(in, n);
    return rc;
}

/* ── kv.* ─────────────────────────────────────────────────────────── */

static char *path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    const char *name = f->inst->reads[idx].name;
    if (name[0] == '/') return strdup(name);
    if (!kvlangXvalueNone(&in[idx])) {
        char *s = kvlangXvalueValueString(&in[idx]);
        if (s[0] == '/') return s;
        free(s);
    }
    return NULL;
}

static char *member_path(kvlangFrame_t *f, const kvlangXvalue_t *in) {
    const kvlangXvalue_t *base = &in[0];
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *ff = kvlangBuiltinFuncFrameRoot(f->kv, fr);
    char *bp = kvlangXvalueNone(base) || (strcmp(kvlangXvalueKind(base), KVSPACE_KIND_OBJ) == 0 || strcmp(kvlangXvalueKind(base), KVSPACE_KIND_MAP) == 0 || strcmp(kvlangXvalueKind(base), KVSPACE_KIND_INDEX) == 0 || strcmp(kvlangXvalueKind(base), KVSPACE_KIND_EXT_INDEX) == 0) ? kvlangBuiltinResolveWriteSlot(f->kv, ff, f->inst->reads[0].name) : kvlangXvalueValueString(base);
    free(ff); free(fr);
    char *kk = kvlangKvKey(&in[1]);
    char *path = kvlangKeytreeMember(bp, kk);
    free(kk); free(bp);
    return path;
}

int kvlangBuiltinKvGet(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    char *key = f->inst->nr >= 2 ? member_path(f, in) : (n >= 1 ? path_arg(f, 0, in) : NULL);
    if (!key) { free_inputs(in, n); return set_err(f, "TypeError: kv.get requires a path"); }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(f->kv, key, &v);
    int rc = write_result(f, &v); kvlangXvalueFree(&v);
    free(key); free_inputs(in, n); return rc;
}

int kvlangBuiltinKvSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[3]; int n = read_inputs(f, in, 3);
    char *key; kvlangXvalue_t *val;
    if (f->inst->nr >= 3) { key = member_path(f, in); val = &in[2]; }
    else { key = n >= 1 ? path_arg(f, 0, in) : NULL; val = &in[1]; }
    if (!key || (f->inst->nr < 3 && n < 2)) { free(key); free_inputs(in, n); return set_err(f, "TypeError: kv.set requires path and value"); }
    kvlangKvPair_t p = { key, *val };
    char err[256]; int rc = kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key); free_inputs(in, n);
    if (rc != 0) return set_err(f, "%s", err);
    next_pc(f); return 0;
}

static int kv_path_void(kvlangFrame_t *f, const char *name,
                        int (*op)(kvlangKv_t *, const char *, char *, uint32_t)) {
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) { free_inputs(in, n); return set_err(f, "TypeError: %s requires 1 path arg", name); }
    char err[256]; int rc = op(f->kv, key, err, sizeof err);
    free(key); free_inputs(in, n);
    if (rc != 0) return set_err(f, "%s", err);
    next_pc(f); return 0;
}

int kvlangBuiltinKvDel(kvlangFrame_t *f) { return kv_path_void(f, "kv.del", kvlangKvDel); }

int kvlangBuiltinKvDelTree(kvlangFrame_t *f) { return kv_path_void(f, "kv.deltree", kvlangKvDelTree); }

int kvlangBuiltinKvList(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return set_err(f, "TypeError: kv.list requires a write param");
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->inst->nr >= 1) {
        /* 裸变量（obj/map 成员目录）：解析为 <frame>/<name>. 目录。 */
        const char *name = f->inst->reads[0].name;
        if (name[0] != '/') {
            char *fr = kvlangKeytreeFrameRoot(f->pc);
            char *ff = kvlangBuiltinFuncFrameRoot(f->kv, fr);
            char *base = kvlangBuiltinResolveWriteSlot(f->kv, ff, name);
            free(ff); free(fr);
            key = kvlangKeytreeMember(base, "");
            free(base);
        }
    }
    if (!key) { free_inputs(in, n); return set_err(f, "TypeError: kv.list requires 1 path arg"); }
    char **names = NULL; int count = 0;
    kvlangKvList(f->kv, key, false, false, &names, &count);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    /* 结果是一个 strkeymapindex：目录标记在 dst（dims=[count]），成员名是坐标段 [i]，
     * 值是对应的成员名字符串。 */
    if (count > 0) {
        char **coords = malloc(sizeof(char *) * (size_t)count);
        for (int i = 0; i < count; i++) {
            kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPrintf(&s, "[%d]", i);
            coords[i] = kvlangStrbufDetach(&s);
        }
        int32_t dims[1] = { count };
        kvlangXvalue_t mark; map_marker(&mark, (const char *const *)coords, count, dims, 1);
        kvlangKvPair_t p0 = { dst, mark }; char err[256]; kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        for (int i = 0; i < count; i++) free(coords[i]);
        free(coords);
    }
    for (int i = 0; i < count; i++) {
        int64_t c[1] = { i };
        char *k = scatter_key(dst, c, 1);
        kvlangXvalue_t e; kvlangXvalueNewCharUtf8(&e, names[i]);
        kvlangKvPair_t p = { k, e }; char err[256]; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k); free(names[i]);
    }
    for (int i = count; ; i++) {
        int64_t c[1] = { i };
        char *k = scatter_key(dst, c, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v); kvlangXvalueFree(&v);
        if (none) { free(k); break; }
        char err[256]; kvlangKvDel(f->kv, k, err, sizeof err); free(k);
    }
    free(names); free(dst); free(key);
    next_pc(f); free_inputs(in, n); return 0;
}

/* 解析 obj/map 成员目录 key（<frame>/<name>.），供 kv.list/listlen/listn 共用。 */
static char *kv_list_dir(kvlangFrame_t *f, kvlangXvalue_t *in, int n) {
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->inst->nr >= 1) {
        const char *name = f->inst->reads[0].name;
        if (name[0] != '/') {
            char *fr = kvlangKeytreeFrameRoot(f->pc);
            char *ff = kvlangBuiltinFuncFrameRoot(f->kv, fr);
            char *base = kvlangBuiltinResolveWriteSlot(f->kv, ff, name);
            free(ff); free(fr);
            key = kvlangKeytreeMember(base, "");
            free(base);
        }
    }
    return key;
}

int kvlangBuiltinKvListLen(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = read_inputs(f, in, 1);
    char *key = kv_list_dir(f, in, n);
    int64_t count = 0;
    if (key) {
        char **names = NULL; int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
        for (int i = 0; i < cnt; i++) free(names[i]);
        free(names);
        count = cnt;
        free(key);
    }
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, count);
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinKvListN(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    char *key = kv_list_dir(f, in, n);
    int idx = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : -1;
    kvlangXvalue_t r; kvlangXvalueZero(&r);
    if (key && idx >= 0) {
        char **names = NULL; int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
        if (idx < cnt) {
            kvlangXvalueNewCharUtf8(&r, names[idx]);
        }
        for (int i = 0; i < cnt; i++) free(names[i]);
        free(names);
        free(key);
    }
    int rc = write_result(f, &r); kvlangXvalueFree(&r); free_inputs(in, n);
    return rc;
}

int kvlangBuiltinKvMkindex(kvlangFrame_t *f) { return kv_path_void(f, "kv.mkindex", kvlangKvMkindex); }

int kvlangBuiltinKvExtIndex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    char *ext = n >= 2 ? kvlangXvalueValueString(&in[1]) : NULL;
    if (!key || !ext) { free(key); free(ext); free_inputs(in, n); return set_err(f, "TypeError: kv.extindex requires path and ext path"); }
    char err[256]; int rc = kvlangKvExtIndex(f->kv, key, ext, err, sizeof err);
    free(key); free(ext); free_inputs(in, n);
    if (rc != 0) return set_err(f, "%s", err);
    next_pc(f); return 0;
}

int kvlangBuiltinKvRmIndexExt(kvlangFrame_t *f) { return kv_path_void(f, "kv.rmindexext", kvlangKvDelExtIndex); }

int kvlangBuiltinKvWatch(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = read_inputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key || n < 2) { free(key); free_inputs(in, n); return set_err(f, "TypeError: kv.watch requires key and target"); }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvWatch(f->kv, key, &in[1], 1000000, &v);
    int rc = write_result(f, &v); kvlangXvalueFree(&v);
    free(key); free_inputs(in, n); return rc;
}

/* ── debugger ─────────────────────────────────────────────────────── */

int kvlangBuiltinDebugger(kvlangFrame_t *f) {
    kvlangStrbuf_t dk; kvlangStrbufInit(&dk);
    kvlangKeytreeVthreadDebugger(f->vtid, &dk);
    kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, dk.p, &v);
    bool dbg = !kvlangXvalueNone(&v);
    kvlangXvalueFree(&v);
    if (!dbg) { kvlangStrbufFree(&dk); next_pc(f); return 0; }
    kvlangStrbufFree(&dk);
    next_pc(f);
    return 0;
}
