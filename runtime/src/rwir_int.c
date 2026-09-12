#include "runtime_internal.h"
#include "rwir_internal.h"
#include <math.h>

/* ── 类型 helper ─────────────────────────────────────────────── */

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

int cmp_int(kvlangScalar_t a, kvlangScalar_t b) {
    bool au = kvlangLtIsUint(a.id), bu = kvlangLtIsUint(b.id);
    if (!au && !bu) { int64_t ai = kvlangScalarI64(a), bi = kvlangScalarI64(b); return ai < bi ? -1 : ai > bi ? 1 : 0; }
    if (au && bu) { uint64_t x = kvlangScalarU64(a), y = kvlangScalarU64(b); return x < y ? -1 : x > y ? 1 : 0; }
    if (au && !bu) { int64_t bi = kvlangScalarI64(b); if (bi < 0) return 1; uint64_t x = kvlangScalarU64(a); return x < (uint64_t)bi ? -1 : x > (uint64_t)bi ? 1 : 0; }
    int64_t ai = kvlangScalarI64(a); if (ai < 0) return -1; uint64_t y = kvlangScalarU64(b);
    return (uint64_t)ai < y ? -1 : (uint64_t)ai > y ? 1 : 0;
}

/* ── 算术算子 ─────────────────────────────────────────────── */

/* ── 数值算子 ─────────────────────────────────────────────────────── */

int kvlangBuiltinAdd(kvlangFrame_t *f) {
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

int kvlangBuiltinSub(kvlangFrame_t *f) {
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

int kvlangBuiltinMul(kvlangFrame_t *f) {
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

int kvlangBuiltinDiv(kvlangFrame_t *f) {
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

int kvlangBuiltinMod(kvlangFrame_t *f) {
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

/* ── 位运算 ───────────────────────────────────────────────── */

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
int kvlangBuiltinBitand(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 0); }
int kvlangBuiltinBitor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 1); }
int kvlangBuiltinBitxor(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 2); }
int kvlangBuiltinShl(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 3); }
int kvlangBuiltinShr(kvlangFrame_t *f) { return kvlangBuiltinBit(f, 4); }

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
int kvlangBuiltinSqrt(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 0); }
int kvlangBuiltinExp(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 1); }
int kvlangBuiltinLog(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 2); }
int kvlangBuiltinNeg(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 3); }
int kvlangBuiltinAbs(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 4); }
int kvlangBuiltinSign(kvlangFrame_t *f) { return kvlangBuiltinMathUnary(f, 5); }

int kvlangBuiltinPow(kvlangFrame_t *f) {
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
int kvlangBuiltinMax(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, true); }
int kvlangBuiltinMin(kvlangFrame_t *f) { return kvlangBuiltinMaxmin(f, false); }

/* ── cast ─────────────────────────────────────────────────── */


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
int kvlangBuiltinCastBool(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_BOOL); }
int kvlangBuiltinCastInt8(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT8); }
int kvlangBuiltinCastInt16(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT16); }
int kvlangBuiltinCastInt32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT32); }
int kvlangBuiltinCastInt64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_INT64); }
int kvlangBuiltinCastUint8(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT8); }
int kvlangBuiltinCastUint16(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT16); }
int kvlangBuiltinCastUint32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT32); }
int kvlangBuiltinCastUint64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_UINT64); }
int kvlangBuiltinCastF32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT32); }
int kvlangBuiltinCastF64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT64); }

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
int kvlangBuiltinCastChar32(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR); }
int kvlangBuiltinCastChar8(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_UTF8); }
int kvlangBuiltinCastCharAscii(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_ASCII); }
