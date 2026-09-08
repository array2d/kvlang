#include "runtime_internal.h"
#include "builtin_internal.h"
#include <math.h>
#include <time.h>

typedef int (*kvlangBuiltinFn)(kvlangFrame_t *f);


/* ── 类型 helper：全经 langtype id + 标量 0copy 视图，热路径零 strcmp、每值只 decode 一次 ── */

static const kvlangScalar_t SCALAR_NONE = { KVLANG_LT_NONE, NULL, 0 };

/* 两操作数结果整型 id（对齐旧 wider_int_kind：同号取宽者，异号升一档有符号）。 */
static int wider_int_id(int a, int b) {
    int aw = kvlangLtIntWidth(a), bw = kvlangLtIntWidth(b);
    bool au = kvlangLtIsUint(a), bu = kvlangLtIsUint(b);
    if (au == bu) {
        int w = aw >= bw ? aw : bw;
        int off = w == 8 ? 0 : w == 16 ? 1 : w == 32 ? 2 : 3;
        return (au ? KVLANG_LT_UINT8 : KVLANG_LT_INT8) + off;
    }
    int w = aw > bw ? aw : bw;
    return w <= 8 ? KVLANG_LT_INT16 : w == 16 ? KVLANG_LT_INT32 : KVLANG_LT_INT64;
}

static int wider_float_id(int a, int b) {
    if (a == KVLANG_LT_FLOAT64 || b == KVLANG_LT_FLOAT64) return KVLANG_LT_FLOAT64;
    if (a == KVLANG_LT_FLOAT32 || b == KVLANG_LT_FLOAT32) return KVLANG_LT_FLOAT32;
    return KVLANG_LT_FLOAT64;
}

static void narrow_int(int a, int b, int64_t v, kvlangXvalue_t *out) {
    int k = wider_int_id(a, b);
    const char *kind = kvlangLangTypeKind(k);
    switch (k) {
    case KVLANG_LT_INT8: { int8_t x = (int8_t)v; kvlangXvalueNewTlv(out, kind, (uint8_t *)&x, 1, 1); return; }
    case KVLANG_LT_INT16: { int16_t x = (int16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; kvlangXvalueNewTlv(out, kind, r, 2, 1); return; }
    case KVLANG_LT_INT32: { int32_t x = (int32_t)v; uint8_t r[4]; memcpy(r, &x, 4); kvlangXvalueNewTlv(out, kind, r, 4, 1); return; }
    case KVLANG_LT_UINT8: { uint8_t x = (uint8_t)v; kvlangXvalueNewTlv(out, kind, &x, 1, 1); return; }
    case KVLANG_LT_UINT16: { uint16_t x = (uint16_t)v; uint8_t r[2] = { x & 0xFF, (x >> 8) & 0xFF }; kvlangXvalueNewTlv(out, kind, r, 2, 1); return; }
    case KVLANG_LT_UINT32: { uint32_t x = (uint32_t)v; uint8_t r[4]; memcpy(r, &x, 4); kvlangXvalueNewTlv(out, kind, r, 4, 1); return; }
    case KVLANG_LT_UINT64: { uint64_t x = (uint64_t)v; uint8_t r[8]; memcpy(r, &x, 8); kvlangXvalueNewTlv(out, kind, r, 8, 1); return; }
    default: kvlangXvalueNewInt64(out, v); return;   /* INT64 */
    }
}

static void narrow_float(int a, int b, double v, kvlangXvalue_t *out) {
    if (wider_float_id(a, b) == KVLANG_LT_FLOAT32) {
        float f = (float)v; uint8_t r[4]; memcpy(r, &f, 4);
        kvlangXvalueNewTlv(out, KVSPACE_KIND_FLOAT32, r, 4, 1);
    } else kvlangXvalueNewFloat64(out, v);
}

static int cmp_int(kvlangScalar_t a, kvlangScalar_t b) {
    bool au = kvlangLtIsUint(a.id), bu = kvlangLtIsUint(b.id);
    if (!au && !bu) { int64_t ai = kvlangScalarI64(a), bi = kvlangScalarI64(b); return ai < bi ? -1 : ai > bi ? 1 : 0; }
    if (au && bu) { uint64_t x = kvlangScalarU64(a), y = kvlangScalarU64(b); return x < y ? -1 : x > y ? 1 : 0; }
    if (au && !bu) { int64_t bi = kvlangScalarI64(b); if (bi < 0) return 1; uint64_t x = kvlangScalarU64(a); return x < (uint64_t)bi ? -1 : x > (uint64_t)bi ? 1 : 0; }
    int64_t ai = kvlangScalarI64(a); if (ai < 0) return -1; uint64_t y = kvlangScalarU64(b);
    return (uint64_t)ai < y ? -1 : (uint64_t)ai > y ? 1 : 0;
}

/* ── resolve ───────────────────────────────────────────────────────── */
/* #116 后 if/while 不再建 scope 帧，当前帧 [d] 即 rwfunc 帧，frame_root 直接可用。 */

void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_root, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC)) {
        out->data = val->data; /* 借指令内冻结字面量（生命周期同 decode 缓存，永不 flush/free） */
        out->len = val->len;
        out->borrowed = 1;
        return;
    }
    if (!name || !name[0]) return;
    if (name[0] == '/') { kvlangKvGetOne(kv, name, out); return; }
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetMember(kv, stk, name, &pv);
    if (kvlangXvalueIsPtr(&pv)) {
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangXvalue_t av; kvlangXvalueZero(&av);
        kvlangKvGetMember(kv, stk, target, &av);
        free(target);
        if (!kvlangXvalueNone(&av)) {
            char *path = kvlangXvalueValueString(&av);
            kvlangKvGetOne(kv, path, out);
            free(path);
        }
        kvlangXvalueFree(&av);
    } else if (!kvlangXvalueNone(&pv)) {
        *out = pv; pv.data = NULL; pv.len = 0;
    }
    kvlangXvalueFree(&pv); free(stk);
}

/* 读参 → 其最终存储键（malloc）：字面量（指令内冻结的具体值，无后端槽）返 NULL；
 * 变量则复用 ResolveWriteSlot——读侧最终槽键与写侧同一路径（普通成员=stk+name，
 * ptr 追链到最终目标），供 xv 系列对 key 直发 GetHead/GetPart/SetPart 做分片读写。 */
char *kvlangBuiltinResolveReadKey(kvlangKv_t *kv, const char *frame_root, const char *name,
                          const kvlangXvalue_t *val) {
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC))
        return NULL;
    if (!name || !name[0]) return NULL;
    return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
}

char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_root, const char *name) {
    if (name[0] == '/') return strdup(name);
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetMember(kv, stk, name, &pv);
    char *result = NULL;
    if (kvlangXvalueIsPtr(&pv)) {
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangXvalue_t av; kvlangXvalueZero(&av);
        kvlangKvGetMember(kv, stk, target, &av);
        free(target);
        if (!kvlangXvalueNone(&av)) {
            /* 只追显式指针（ptr, ref==1）链；av 是 handle_call 已 resolve 好的
             * 最终写目标路径（普通 char），直接取字符串，勿再按值读下一跳——
             * 否则会把已写数据（char）误当路径再追，导致二次调用写回旧值。 */
            kvlangXvalue_t v = av; av.data = NULL; av.len = 0;
            while (kvlangXvalueIsPtr(&v)) {
                char *np = kvlangXvaluePtrTarget(&v);
                kvlangXvalue_t nxt; kvlangXvalueZero(&nxt);
                kvlangKvGetOne(kv, np, &nxt);
                free(np);
                kvlangXvalueFree(&v);
                v = nxt;
            }
            if (!kvlangXvalueNone(&v)) result = kvlangXvalueValueString(&v);
            kvlangXvalueFree(&v);
        }
        kvlangXvalueFree(&av);
    }
    kvlangXvalueFree(&pv);
    if (result) { free(stk); return result; }
    kvlangStrbuf_t o; kvlangStrbufInit(&o);
    kvlangStrbufPuts(&o, stk); kvlangStrbufPuts(&o, name);
    free(stk);
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

void kvlangBuiltinXvalueAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out) {
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
        kvlangXvalue_t e; kvlangBuiltinXvalueAt(v, i, &e);
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

int kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    int n = 0;
    for (int i = 0; i < f->inst->nr && n < cap; i++) {
        kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[i].name, &f->inst->reads[i].val, &out[n]);
        n++;
    }
    free(fr);
    return n;
}

void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n) { for (int i = 0; i < n; i++) kvlangXvalueFree(&in[i]); }

void kvlangBuiltinNextPc(kvlangFrame_t *f) {
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc);
    kvlangRwirNextPc(f->pc, &npc);
    kvlangVthreadSet(f->kv, f->vtid, npc.p, "running");
    kvlangStrbufFree(&npc);
}

int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result) {
    if (f->inst->nw > 0) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
        free(fr);
        kvlangKvPair_t pair = { key, *result };
        char err[256];
        kvlangKvSet(f->kv, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinSetErr(kvlangFrame_t *f, const char *fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
    return -1;
}

/* ── 数值算子 ─────────────────────────────────────────────────────── */

static int kvlangBuiltinAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    int rc;
    if (n == 2 && kvlangLtIsChar(a.id) && kvlangLtIsChar(b.id)) {
        kvlangXvalue_t r;
        if (kvlangBuiltinCharConcat(&in[0], &in[1], &r)) {
            rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
        } else rc = kvlangBuiltinSetErr(f, "TypeError: cannot concat %s with %s; convert encoding explicitly (char/utf8|char/utf32|char/ascii)", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]));
    } else if (n >= 2 && kvlangLtIsNum(a.id) && kvlangLtIsNum(b.id)) {
        kvlangXvalue_t r;
        if (kvlangLtIsInt(a.id) && kvlangLtIsInt(b.id)) narrow_int(a.id, b.id, kvlangScalarI64(a) + kvlangScalarI64(b), &r);
        else narrow_float(a.id, b.id, kvlangScalarF64(a) + kvlangScalarF64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else rc = kvlangBuiltinSetErr(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    int rc;
    if (n == 1) {
        kvlangXvalue_t r; kvlangXvalueNewInt64(&r, -kvlangScalarI64(a));
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && kvlangLtIsInt(a.id) && kvlangLtIsInt(b.id)) {
        kvlangXvalue_t r; narrow_int(a.id, b.id, kvlangScalarI64(a) - kvlangScalarI64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && kvlangLtIsNum(a.id) && kvlangLtIsNum(b.id)) {
        kvlangXvalue_t r; narrow_float(a.id, b.id, kvlangScalarF64(a) - kvlangScalarF64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else rc = kvlangBuiltinSetErr(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinMul(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    int rc;
    if (n >= 2 && kvlangLtIsInt(a.id) && kvlangLtIsInt(b.id)) {
        kvlangXvalue_t r; narrow_int(a.id, b.id, kvlangScalarI64(a) * kvlangScalarI64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else if (n >= 2 && kvlangLtIsNum(a.id) && kvlangLtIsNum(b.id)) {
        kvlangXvalue_t r; narrow_float(a.id, b.id, kvlangScalarF64(a) * kvlangScalarF64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else rc = kvlangBuiltinSetErr(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinDiv(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    int rc;
    if (n < 2) { rc = kvlangBuiltinSetErr(f, "TypeError: binary op requires 2 inputs, got %d", n); kvlangBuiltinFreeInputs(in, n); return rc; }
    if (kvlangScalarF64(b) == 0) { rc = kvlangBuiltinSetErr(f, "ZeroDivisionError: division by zero"); kvlangBuiltinFreeInputs(in, n); return rc; }
    if (kvlangLtIsInt(a.id) && kvlangLtIsInt(b.id)) {
        kvlangXvalue_t r; narrow_int(a.id, b.id, kvlangScalarI64(a) / kvlangScalarI64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    } else {
        kvlangXvalue_t r; narrow_float(a.id, b.id, kvlangScalarF64(a) / kvlangScalarF64(b), &r);
        rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    }
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinMod(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    int rc;
    if (n < 2 || !kvlangLtIsInt(a.id) || !kvlangLtIsInt(b.id)) {
        rc = kvlangBuiltinSetErr(f, "TypeError: expected integer, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
        kvlangBuiltinFreeInputs(in, n); return rc;
    }
    int64_t bv = kvlangScalarI64(b);
    if (bv == 0) { rc = kvlangBuiltinSetErr(f, "ZeroDivisionError: modulo by zero"); kvlangBuiltinFreeInputs(in, n); return rc; }
    kvlangXvalue_t r; narrow_int(a.id, b.id, kvlangScalarI64(a) % bv, &r);
    rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

typedef enum { CMP_EQ, CMP_NEQ, CMP_LT, CMP_GT, CMP_LE, CMP_GE } cmp_op;

static int kvlangBuiltinCmp(kvlangFrame_t *f, cmp_op op) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinSetErr(f, "TypeError: binary op requires 2 inputs, got %d", n); kvlangBuiltinFreeInputs(in, n); return -1; }
    bool allow_null = (op == CMP_EQ || op == CMP_NEQ);
    if (kvlangXvalueNone(&in[0]) || kvlangXvalueNone(&in[1])) {
        if (!allow_null) { kvlangBuiltinSetErr(f, "TypeError: None in comparison"); kvlangBuiltinFreeInputs(in, n); return -1; }
        bool eq = kvlangXvalueNone(&in[0]) == kvlangXvalueNone(&in[1]);
        bool r = (op == CMP_EQ) ? eq : (op == CMP_NEQ) ? !eq : false;
        kvlangXvalue_t rv; kvlangXvalueNewBool(&rv, r);
        int rc = kvlangBuiltinWriteResult(f, &rv); kvlangXvalueFree(&rv); kvlangBuiltinFreeInputs(in, n); return rc;
    }
    bool r;
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = kvlangXvalueScalar(&in[1]);
    if (kvlangLtIsInt(a.id) && kvlangLtIsInt(b.id)) {
        int c = cmp_int(a, b);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
    } else if (kvlangLtIsNum(a.id) && kvlangLtIsNum(b.id)) {
        double av = kvlangScalarF64(a), bv = kvlangScalarF64(b);
        r = op == CMP_EQ ? av == bv : op == CMP_NEQ ? av != bv : op == CMP_LT ? av < bv : op == CMP_GT ? av > bv : op == CMP_LE ? av <= bv : av >= bv;
    } else if (kvlangLtIsChar(a.id) && kvlangLtIsChar(b.id)) {
        char *as = kvlangXvalueValueString(&in[0]), *bs = kvlangXvalueValueString(&in[1]);
        int c = strcmp(as, bs);
        r = op == CMP_EQ ? c == 0 : op == CMP_NEQ ? c != 0 : op == CMP_LT ? c < 0 : op == CMP_GT ? c > 0 : op == CMP_LE ? c <= 0 : c >= 0;
        free(as); free(bs);
    } else if (a.id == KVLANG_LT_BOOL && b.id == KVLANG_LT_BOOL) {
        bool av = kvlangScalarI64(a) != 0, bv = kvlangScalarI64(b) != 0;
        r = op == CMP_EQ ? av == bv : op == CMP_NEQ ? av != bv : op == CMP_LT ? av < bv : op == CMP_GT ? av > bv : op == CMP_LE ? av <= bv : av >= bv;
    } else {
        kvlangBuiltinSetErr(f, "TypeError: cannot compare %s with %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1])); kvlangBuiltinFreeInputs(in, n); return -1;
    }
    kvlangXvalue_t rv; kvlangXvalueNewBool(&rv, r);
    int rc = kvlangBuiltinWriteResult(f, &rv); kvlangXvalueFree(&rv); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinEq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_EQ); }
static int kvlangBuiltinNeq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_NEQ); }
static int kvlangBuiltinLt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LT); }
static int kvlangBuiltinGt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GT); }
static int kvlangBuiltinLe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LE); }
static int kvlangBuiltinGe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GE); }

static bool require_bool(kvlangFrame_t *f, const char *op, kvlangXvalue_t *in, int n, int want) {
    if (n < want) return false;
    for (int i = 0; i < want; i++) {
        if (!kvlangXvalueKindIs(&in[i], KVSPACE_KIND_BOOL)) {
            kvlangBuiltinSetErr(f, "TypeError: %s requires bool, got %s", op, kvlangXvalueKind(&in[i]));
            return false;
        }
    }
    return true;
}
static int kvlangBuiltinAnd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "&&", in, n, 2)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) != 0 && kvlangScalarI64(kvlangXvalueScalar(&in[1])) != 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinOr(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "||", in, n, 2)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) != 0 || kvlangScalarI64(kvlangXvalueScalar(&in[1])) != 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinNot(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "!", in, n, 1)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) == 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinBit(kvlangFrame_t *f, int op) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]), sb = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    if (n < 2 || !kvlangLtIsInt(sa.id) || !kvlangLtIsInt(sb.id)) {
        kvlangBuiltinSetErr(f, "TypeError: expected integer, got %s", n ? kvlangXvalueKind(&in[0]) : "none");
        kvlangBuiltinFreeInputs(in, n); return -1;
    }
    int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);
    int64_t v = op == 0 ? a & b : op == 1 ? a | b : op == 2 ? a ^ b : op == 3 ? (a << (uint64_t)b) : (a >> (uint64_t)b);
    kvlangXvalue_t r; narrow_int(sa.id, sb.id, v, &r);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinBitand(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 0); }
static int kvlangBuiltinBitor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 1); }
static int kvlangBuiltinBitxor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 2); }
static int kvlangBuiltinShl(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 3); }
static int kvlangBuiltinShr(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 4); }

/* math */
static int kvlangBuiltinMathUnary(kvlangFrame_t *f, int op) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);
    if (n < 1 || !kvlangLtIsNum(sa.id)) { kvlangBuiltinSetErr(f, "TypeError: expected numeric, got %s", n ? kvlangXvalueKind(&in[0]) : "none"); kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r;
    double x = kvlangScalarF64(sa);
    switch (op) {
    case 0: kvlangXvalueNewFloat64(&r, sqrt(x)); break;
    case 1: kvlangXvalueNewFloat64(&r, exp(x)); break;
    case 2: kvlangXvalueNewFloat64(&r, log(x)); break;
    case 3: /* neg */ if (kvlangLtIsFloat(sa.id)) { narrow_float(sa.id, sa.id, -x, &r); } else { narrow_int(sa.id, sa.id, -kvlangScalarI64(sa), &r); } break;
    case 4: /* abs */ if (kvlangLtIsFloat(sa.id)) { narrow_float(sa.id, sa.id, fabs(x), &r); } else { int64_t iv = kvlangScalarI64(sa); narrow_int(sa.id, sa.id, iv < 0 ? -iv : iv, &r); } break;
    case 5: kvlangXvalueNewInt64(&r, x < 0 ? -1 : x > 0 ? 1 : 0); break;
    }
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinSqrt(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 0); }
static int kvlangBuiltinExp(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 1); }
static int kvlangBuiltinLog(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 2); }
static int kvlangBuiltinNeg(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 3); }
static int kvlangBuiltinAbs(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 4); }
static int kvlangBuiltinSign(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 5); }

static int kvlangBuiltinPow(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]), sb = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    if (n < 2 || !kvlangLtIsNum(sa.id) || !kvlangLtIsNum(sb.id)) { kvlangBuiltinSetErr(f, "TypeError: expected numeric"); kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewFloat64(&r, pow(kvlangScalarF64(sa), kvlangScalarF64(sb)));
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinMaxmin(kvlangFrame_t *f, bool is_max) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinSetErr(f, "TypeError: binary op requires 2 inputs, got %d", n); kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r;
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]), sb = kvlangXvalueScalar(&in[1]);
    if (kvlangLtIsInt(sa.id) && kvlangLtIsInt(sb.id)) {
        int c = cmp_int(sa, sb);
        bool take_a = (is_max && c >= 0) || (!is_max && c <= 0);
        narrow_int(sa.id, sb.id, take_a ? kvlangScalarI64(sa) : kvlangScalarI64(sb), &r);
    } else if (kvlangLtIsNum(sa.id) && kvlangLtIsNum(sb.id)) {
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);
        bool take_a = (is_max && a >= b) || (!is_max && a <= b);
        narrow_float(sa.id, sb.id, take_a ? a : b, &r);
    } else { kvlangBuiltinSetErr(f, "TypeError: max/min requires numeric, got %s and %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1])); kvlangBuiltinFreeInputs(in, n); return -1; }
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinMax(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, true); }
static int kvlangBuiltinMin(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, false); }

/* cast */
static int kvlangBuiltinCastNum(kvlangFrame_t *f, const char *kind) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { kvlangBuiltinSetErr(f, "TypeError: cannot cast None"); kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r;
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);
    int tid = kvlangLangTypeId(kind, strlen(kind));
    if (tid == KVLANG_LT_BOOL) {
        if (!kvlangXvalueKindIs(&in[0], KVSPACE_KIND_BOOL)) { kvlangBuiltinSetErr(f, "TypeError: cannot cast %s to bool — use != 0", kvlangXvalueKind(&in[0])); kvlangBuiltinFreeInputs(in, n); return -1; }
        kvlangXvalueNewBool(&r, kvlangScalarI64(sa) != 0);
    }
    else if (tid == KVLANG_LT_FLOAT32) { float fv = (float)kvlangScalarF64(sa); uint8_t b[4]; memcpy(b, &fv, 4); kvlangXvalueNewTlv(&r, KVSPACE_KIND_FLOAT32, b, 4, 1); }
    else if (tid == KVLANG_LT_FLOAT64) kvlangXvalueNewFloat64(&r, kvlangScalarF64(sa));
    else { int64_t v = kvlangScalarI64(sa); narrow_int(tid, tid, v, &r); }
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
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
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { kvlangBuiltinSetErr(f, "TypeError: char conversion requires a value"); kvlangBuiltinFreeInputs(in, n); return -1; }
    char *s = kvlangXvalueValueString(&in[0]);
    kvlangXvalue_t r;
    if (strcmp(kind, KVSPACE_KIND_CHAR) == 0) kvlangXvalueNewCharUtf32(&r, s);
    else kvlangXvalueNewCharKind(&r, kind, s);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); free(s); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
static int kvlangBuiltinCastChar32(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR); }
static int kvlangBuiltinCastChar8(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_UTF8); }
static int kvlangBuiltinCastCharAscii(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_ASCII); }

/* ── 注册表 ───────────────────────────────────────────────────────── */

/* 精度前缀（int64·add / float32·add …）保留：CapIndex 两级查表——先按完整 opcode 命中特化，
 * 未命中且前缀是 C native 数字 kind 时才剥前缀归到裸 op（如 add），kvlangBuiltin* 按操作数 kind 归约。 */
static int kvlangCtlCopy(kvlangFrame_t *f) { return kvlangBuiltinExecuteCopy(f->kv, f->vtid, f->pc, f->inst); }

static const struct { const char *op; kvlangBuiltinFn fn; } myrwircaps[] = {
    /* control / copy：与 native 算子同表，op_id 单跳派发 */
    {OP_CALL, kvlangCtlCall}, {OP_RETURN, kvlangCtlReturn}, {OP_GOTO, kvlangCtlGoto},
    {OP_BR, kvlangCtlBr}, {OP_COPY, kvlangCtlCopy},
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
    {"array·scatter", kvlangBuiltinScatter}, {"array·compact", kvlangBuiltinCompact},
    {"array·append", kvlangBuiltinAppend}, {"array·slice", kvlangBuiltinSlice},
    {"obj", kvlangBuiltinObj}, {"map", kvlangBuiltinMap}, {"struct·new", kvlangBuiltinStructNew},
    {"ndarray·numel", kvlangBuiltinNdarrayNumel}, {"ndarray·dim", kvlangBuiltinNdarrayDim}, {"ndarray·shape", kvlangBuiltinNdarrayShape},
    {"xv·at", kvlangBuiltinXvAt}, {"xv·set", kvlangBuiltinXvSet}, {"xv·reshape", kvlangBuiltinXvReshape},
    {"xv·reinterpret", kvlangBuiltinXvReinterpret},
    {"xv·langtype", kvlangBuiltinXvLangtype}, {"xv·bodylen", kvlangBuiltinXvBodylen},
    {"string·set", kvlangBuiltinStringSet}, {"string·char", kvlangBuiltinStringChar}, {"string·ord", kvlangBuiltinStringOrd},
    {"string·cmp", kvlangBuiltinStringCmp}, {"string·find", kvlangBuiltinStringFind}, {"string·len", kvlangBuiltinStringLen},
    {"string·slice", kvlangBuiltinStringSlice}, {"string·concat", kvlangBuiltinStringConcat},
    {"string·formatint", kvlangBuiltinStringFormatInt}, {"string·formatuint", kvlangBuiltinStringFormatUint},
    {"string·parseint", kvlangBuiltinStringParseInt}, {"string·parseuint", kvlangBuiltinStringParseUint},
    {"time·now", kvlangBuiltinTimeNow}, {"time·sub", kvlangBuiltinTimeSub}, {"time·add", kvlangBuiltinTimeAdd},
    {"time/duration·nanos", kvlangBuiltinDurFrom}, {"time/duration·millis", kvlangBuiltinDurFrom},
    {"time/duration·seconds", kvlangBuiltinDurFrom}, {"time/duration·minutes", kvlangBuiltinDurFrom},
    {"time/duration·hours", kvlangBuiltinDurFrom},
    {"time/duration·as_nanos", kvlangBuiltinDurTo}, {"time/duration·as_millis", kvlangBuiltinDurTo},
    {"time/duration·as_seconds", kvlangBuiltinDurTo}, {"time/duration·as_minutes", kvlangBuiltinDurTo},
    {"time/duration·as_hours", kvlangBuiltinDurTo},
    {"time/duration·add", kvlangBuiltinDurArith}, {"time/duration·sub", kvlangBuiltinDurArith},
    {"time/duration·before", kvlangBuiltinDurCmp}, {"time/duration·after", kvlangBuiltinDurCmp},
    {"time·before", kvlangBuiltinTimeCmp}, {"time·after", kvlangBuiltinTimeCmp},
    {"random·uint64", kvlangBuiltinRandUint64}, {"random·int63", kvlangBuiltinRandInt63}, {"random·intn", kvlangBuiltinRandIntn},
    {"kv·get", kvlangBuiltinKvGet}, {"kv·set", kvlangBuiltinKvSet}, {"kv·del", kvlangBuiltinKvDel},
    {"kv·deltree", kvlangBuiltinKvDelTree}, {"kv·cp", kvlangBuiltinKvCp}, {"kv·cpdir", kvlangBuiltinKvCpTree}, {"kv·cplist", kvlangBuiltinKvCpList}, {"kv·list", kvlangBuiltinKvList}, {"kv·listlen", kvlangBuiltinKvListLen}, {"kv·listn", kvlangBuiltinKvListN}, {"kv·mkindex", kvlangBuiltinKvMkindex},
    {"kv·extindex", kvlangBuiltinKvExtIndex}, {"kv·rmindexext", kvlangBuiltinKvRmIndexExt}, {"kv·watch", kvlangBuiltinKvWatch}, {"kv·abs", kvlangBuiltinKvAbs},
    {"vthread·create", kvlangBuiltinVthreadCreate},
    {"vthread·run", kvlangBuiltinVthreadRun},
    {"vthread·call", kvlangBuiltinVthreadCall},
    {"vthread·sleep", kvlangBuiltinVthreadSleep},
    {"vthread·setstatus", kvlangBuiltinVthreadSetstatus},
    {"debugger", kvlangBuiltinDebugger},
};

static const size_t myrwircaps_n = sizeof(myrwircaps) / sizeof(myrwircaps[0]);

/* C 原生数值 langtype 集：仅这些精度的算术走 kvlangBuiltinAdd 等通用实现。
 * int4/fp8/bf16 等 C 表达不了的量化精度不在此列，其 <langtype>·op 走专精 fn 或 handoff。 */
static const char *NUM_KINDS[] = {"int8", "int16", "int32", "int64", "uint8",
                                  "uint16", "uint32", "uint64", "float32", "float64"};
static bool is_num_kind_prefix(const char *op, size_t n) {
    for (size_t i = 0; i < sizeof(NUM_KINDS) / sizeof(NUM_KINDS[0]); i++)
        if (strlen(NUM_KINDS[i]) == n && strncmp(op, NUM_KINDS[i], n) == 0) return true;
    return false;
}

/* rwirtable 两级查找：先查完整 opcode（命中=专精实现，如 fp8·add，或 time·add 等成员算子），
 * 未命中且末段前缀是 C 原生数值 langtype 才拆前缀查裸算子（int64·add → add，通用实现）。
 * 非数值前缀（time/duration·max 等用户 rwfunc）不剥离，两级皆 miss → 非本 runtime native。 */
int kvlangBuiltinCapIndex(const char *opcode) {
    for (size_t i = 0; i < myrwircaps_n; i++)
        if (strcmp(myrwircaps[i].op, opcode) == 0) return (int)i;
    const char *dot = strstr(opcode, MEMBER_SEP);
    if (dot && is_num_kind_prefix(opcode, (size_t)(dot - opcode))) {
        const char *bare = dot + MEMBER_SEP_LEN;
        for (size_t i = 0; i < myrwircaps_n; i++)
            if (strcmp(myrwircaps[i].op, bare) == 0) return (int)i;
    }
    return -1;
}

/* notinmycaps：查 myrwircaps table。opcode 不在本 runtime 能力表内 → true
 * （即须经 /lib/<op> 的 def rwir 路由给能兑现它的其它 runtime，或为用户 rwfunc）。 */
bool notinmycaps(const char *opcode) {
    return kvlangBuiltinCapIndex(opcode) < 0;
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
    int i = f->inst->op_id >= 0 ? f->inst->op_id : kvlangBuiltinCapIndex(f->inst->opcode);
    if (i >= 0) return myrwircaps[i].fn(f);
    return kvlangBuiltinSetErr(f, "unknown builtin op: %s", f->inst->opcode);
}

/* ── copy ─────────────────────────────────────────────────────────── */

int kvlangBuiltinExecuteCopy(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    char *fr = kvlangKeytreeFrameRoot(pc);
    kvlangFrame_t f = { kv, vtid, pc, inst };
    if (inst->nr == 0) { free(fr); kvlangBuiltinNextPc(&f); return 0; }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(kv, fr, inst->reads[0].name, &inst->reads[0].val, &v);
    for (int i = 0; i < inst->nw; i++) {
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, inst->writes[i].name);
        kvlangKvPair_t pair = { key, v };
        char err[256];
        kvlangKvSet(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangXvalueFree(&v);
    free(fr);
    kvlangBuiltinNextPc(&f);
    return 0;
}

/* ── collection helper ────────────────────────────────────────────── */












/* ── array 系列 ───────────────────────────────────────────────────── */

















/* ── dict ─────────────────────────────────────────────────────────── */



/* ── string 系列 ──────────────────────────────────────────────────── */









/* ── strconv（对齐 Go strconv 的 Format/Parse，base 2..36）─────────── */







/* ── time / duration ──────────────────────────────────────────────── */












/* ── random ───────────────────────────────────────────────────────── */





/* ── kv.* ─────────────────────────────────────────────────────────── */
















/* ── vthread ─────────────────────────────────────────────────────── */





/* ── vthread 控制 / debugger ─────────────────────────────────────── */

