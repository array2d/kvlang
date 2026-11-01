#include "runtime_internal.h"
#include "rwir_internal.h"
#include "intops.h"
#include <math.h>

#ifdef KV_COUNT
unsigned long kvCNarrow, kvCCompare;
#define CNT_NARROW() kvCNarrow++
#define CNT_CMP() kvCCompare++
#else
#define CNT_NARROW() do {} while (0)
#define CNT_CMP() do {} while (0)
#endif

/* ── 类型 helper ─────────────────────────────────────────────── */

/* ── 类型 helper：全经 langtype id + 标量 0copy 视图，热路径零 strcmp、每值只解 head 一次 ── */

static const kvlangScalar_t SCALAR_NONE = { KVLANG_LT_NONE, NULL, 0 };

/* 两操作数结果整型 id（对齐旧 wider_int_kind：同号取宽者，异号升一档有符号）。 */
static int wider_int_id(int a, int b) {
    int aw = kvlangLangtypeIntWidth(a), bw = kvlangLangtypeIntWidth(b);
    bool au = kvlangLangtypeIsUint(a), bu = kvlangLangtypeIsUint(b);
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

static void narrow_int(int a, int b, int64_t v, uint8_t *buf, size_t cap,
                       kvlangXvalue_t *out) {
    CNT_NARROW();
    int k = wider_int_id(a, b);
    uint8_t raw[8];
    uint32_t len;
    switch (k) {
    case KVLANG_LT_INT8: raw[0] = (uint8_t)(int8_t)v; len = 1; break;
    case KVLANG_LT_INT16: { int16_t x = (int16_t)v; memcpy(raw, &x, 2); len = 2; break; }
    case KVLANG_LT_INT32: { int32_t x = (int32_t)v; memcpy(raw, &x, 4); len = 4; break; }
    case KVLANG_LT_UINT8: raw[0] = (uint8_t)v; len = 1; break;
    case KVLANG_LT_UINT16: { uint16_t x = (uint16_t)v; memcpy(raw, &x, 2); len = 2; break; }
    case KVLANG_LT_UINT32: { uint32_t x = (uint32_t)v; memcpy(raw, &x, 4); len = 4; break; }
    case KVLANG_LT_UINT64: { uint64_t x = (uint64_t)v; memcpy(raw, &x, 8); len = 8; break; }
    default: { uint64_t x = (uint64_t)v; memcpy(raw, &x, 8); len = 8; break; }
    }
    const char *kind = kvlangLangtypeKind(k);
    out->length = kvlangScalarToWire(kind, raw, len, buf, cap);
    out->bytes = buf;
    /* Keep `langtype` cache synchronized with wire attr (`out` may be uninitialized). */
    kvlangXvalueSetLangtype(out, kind, (uint32_t)strlen(kind));
}

static void narrow_float(int a, int b, double v, uint8_t *buf, size_t cap,
                         kvlangXvalue_t *out) {
    CNT_NARROW();
    uint8_t raw[8];
    uint32_t len;
    const char *kind;
    if (wider_float_id(a, b) == KVLANG_LT_FLOAT32) {
        float f = (float)v;
        memcpy(raw, &f, 4);
        len = 4;
        kind = KVSPACE_KIND_FLOAT32;
    } else {
        memcpy(raw, &v, 8);
        len = 8;
        kind = KVSPACE_KIND_FLOAT64;
    }
    out->length = kvlangScalarToWire(kind, raw, len, buf, cap);
    out->bytes = buf;
    kvlangXvalueSetLangtype(out, kind, (uint32_t)strlen(kind));
}

/* Result is unsigned iff both operands are unsigned (see `wider_int_id`). */
static bool int_result_unsigned(int a, int b) {
    return kvlangLangtypeIsUint(a) && kvlangLangtypeIsUint(b);
}

static const char *xv_kind_or_none(const kvlangXvalue_t *v) {
    return (!v || kvlangXvalueNone(v)) ? "None" : kvlangXvalueKind(v);
}

/* Unary negation (`-x` / `neg`): wrap signed ints, negate floats, and reject unsigned/other kinds. */
static int emit_neg(kvlangFrame_t *f, const kvlangXvalue_t *in,
                    kvlangScalar_t sa) {
    if (!kvlangLangtypeIsNum(sa.kind_id))
        return kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s",
                                     xv_kind_or_none(in));
    if (kvlangLangtypeIsUint(sa.kind_id))
        return kvlangBuiltinSetError(f, "TypeError: cannot negate unsigned %s",
                                     xv_kind_or_none(in));
    uint8_t sbuf[64];
    kvlangXvalue_t r;
    if (kvlangLangtypeIsFloat(sa.kind_id))
        narrow_float(sa.kind_id, sa.kind_id, -kvlangScalarF64(sa), sbuf, sizeof sbuf, &r);
    else
        narrow_int(sa.kind_id, sa.kind_id, kvi_neg(kvlangScalarI64(sa)), sbuf, sizeof sbuf, &r);
    return kvlangBuiltinWriteResult(f, &r);
}

/* Compare in the promoted integer kind (matching arithmetic promotion): unsigned iff both are
 * unsigned, else signed (`uint64 >= 2^63` wraps as `int64`, keeping `==`, `min`, and `max` consistent). */
int compare_int(kvlangScalar_t a, kvlangScalar_t b) {
    CNT_CMP();
    if (int_result_unsigned(a.kind_id, b.kind_id)) {
        uint64_t x = kvlangScalarU64(a), y = kvlangScalarU64(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    int64_t ai = kvlangScalarI64(a), bi = kvlangScalarI64(b);
    return ai < bi ? -1 : ai > bi ? 1 : 0;
}

/* ── 算术算子 ─────────────────────────────────────────────── */

/* ── 数值算子 ─────────────────────────────────────────────────────── */

int kvlangBuiltinAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    uint8_t sbuf[64];
    int rc;
    if (n == 2 && kvlangLangtypeIsChar(a.kind_id) && kvlangLangtypeIsChar(b.kind_id)) {
        kvlangXvalue_t r;
        if (kvlangBuiltinCharConcat(&in[0], &in[1], &r))
            rc = kvlangBuiltinEmitResult(f, &r);
        else rc = kvlangBuiltinSetError(f, "TypeError: cannot concat %s with %s; convert encoding explicitly (char/utf8|char/utf32|char/ascii)", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1]));
    } else if (n >= 2 && kvlangLangtypeIsNum(a.kind_id) && kvlangLangtypeIsNum(b.kind_id)) {
        kvlangXvalue_t r;
        if (kvlangLangtypeIsInt(a.kind_id) && kvlangLangtypeIsInt(b.kind_id))
            narrow_int(a.kind_id, b.kind_id, kvi_add(kvlangScalarI64(a), kvlangScalarI64(b)), sbuf, sizeof sbuf, &r);
        else
            narrow_float(a.kind_id, b.kind_id, kvlangScalarF64(a) + kvlangScalarF64(b), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r); /* 栈上 wire，不 free */
    } else rc = kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s",
                                      n == 0 ? "None" : (!kvlangLangtypeIsNum(a.kind_id) ? xv_kind_or_none(&in[0]) : (n >= 2 ? xv_kind_or_none(&in[1]) : "None")));
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    uint8_t sbuf[64];
    int rc;
    if (n == 1) {
        rc = emit_neg(f, &in[0], a); /* unary minus per operand kind */
    } else if (n >= 2 && kvlangLangtypeIsInt(a.kind_id) && kvlangLangtypeIsInt(b.kind_id)) {
        kvlangXvalue_t r;
        narrow_int(a.kind_id, b.kind_id, kvi_sub(kvlangScalarI64(a), kvlangScalarI64(b)), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r);
    } else if (n >= 2 && kvlangLangtypeIsNum(a.kind_id) && kvlangLangtypeIsNum(b.kind_id)) {
        kvlangXvalue_t r;
        narrow_float(a.kind_id, b.kind_id, kvlangScalarF64(a) - kvlangScalarF64(b), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r);
    } else rc = kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s",
                                      n == 0 ? "None" : (!kvlangLangtypeIsNum(a.kind_id) ? xv_kind_or_none(&in[0]) : (n >= 2 ? xv_kind_or_none(&in[1]) : "None")));
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinMul(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    uint8_t sbuf[64];
    int rc;
    if (n >= 2 && kvlangLangtypeIsInt(a.kind_id) && kvlangLangtypeIsInt(b.kind_id)) {
        kvlangXvalue_t r;
        narrow_int(a.kind_id, b.kind_id, kvi_mul(kvlangScalarI64(a), kvlangScalarI64(b)), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r);
    } else if (n >= 2 && kvlangLangtypeIsNum(a.kind_id) && kvlangLangtypeIsNum(b.kind_id)) {
        kvlangXvalue_t r;
        narrow_float(a.kind_id, b.kind_id, kvlangScalarF64(a) * kvlangScalarF64(b), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r);
    } else rc = kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s",
                                      n == 0 ? "None" : (!kvlangLangtypeIsNum(a.kind_id) ? xv_kind_or_none(&in[0]) : (n >= 2 ? xv_kind_or_none(&in[1]) : "None")));
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDiv(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    uint8_t sbuf[64];
    int rc;
    if (n < 2) { rc = kvlangBuiltinSetError(f, "TypeError: binary op requires 2 inputs, got %d", n); kvlangBuiltinFreeInputs(in, n); return rc; }
    if (!kvlangLangtypeIsNum(a.kind_id) || !kvlangLangtypeIsNum(b.kind_id)) {
        rc = kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s",
                                   !kvlangLangtypeIsNum(a.kind_id) ? xv_kind_or_none(&in[0]) : xv_kind_or_none(&in[1]));
        kvlangBuiltinFreeInputs(in, n); return rc;
    }
    if (kvlangScalarF64(b) == 0) { rc = kvlangBuiltinSetError(f, "ZeroDivisionError: division by zero"); kvlangBuiltinFreeInputs(in, n); return rc; }
    kvlangXvalue_t r;
    if (kvlangLangtypeIsInt(a.kind_id) && kvlangLangtypeIsInt(b.kind_id))
        narrow_int(a.kind_id, b.kind_id,
                   kvi_div(kvlangScalarI64(a), kvlangScalarI64(b),
                           int_result_unsigned(a.kind_id, b.kind_id)),
                   sbuf, sizeof sbuf, &r);
    else
        narrow_float(a.kind_id, b.kind_id, kvlangScalarF64(a) / kvlangScalarF64(b), sbuf, sizeof sbuf, &r);
    rc = kvlangBuiltinWriteResult(f, &r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinMod(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t a = kvlangXvalueScalar(&in[0]), b = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    uint8_t sbuf[64];
    int rc;
    if (n < 2 || !kvlangLangtypeIsInt(a.kind_id) || !kvlangLangtypeIsInt(b.kind_id)) {
        rc = kvlangBuiltinSetError(f, "TypeError: expected integer, got %s",
                                   n == 0 ? "None" : (!kvlangLangtypeIsInt(a.kind_id) ? xv_kind_or_none(&in[0]) : (n >= 2 ? xv_kind_or_none(&in[1]) : "None")));
        kvlangBuiltinFreeInputs(in, n); return rc;
    }
    int64_t bv = kvlangScalarI64(b);
    if (bv == 0) { rc = kvlangBuiltinSetError(f, "ZeroDivisionError: modulo by zero"); kvlangBuiltinFreeInputs(in, n); return rc; }
    kvlangXvalue_t r;
    narrow_int(a.kind_id, b.kind_id,
               kvi_mod(kvlangScalarI64(a), bv,
                       int_result_unsigned(a.kind_id, b.kind_id)),
               sbuf, sizeof sbuf, &r);
    rc = kvlangBuiltinWriteResult(f, &r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* ── 位运算 ───────────────────────────────────────────────── */

static int kvlangBuiltinBit(kvlangFrame_t *f, int op) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]), sb = n >= 2 ? kvlangXvalueScalar(&in[1]) : SCALAR_NONE;
    if (n < 2 || !kvlangLangtypeIsInt(sa.kind_id) || !kvlangLangtypeIsInt(sb.kind_id)) {
        kvlangBuiltinSetError(f, "TypeError: expected integer, got %s",
                              n == 0 ? "None" : (!kvlangLangtypeIsInt(sa.kind_id) ? xv_kind_or_none(&in[0]) : (n >= 2 ? xv_kind_or_none(&in[1]) : "None")));
        kvlangBuiltinFreeInputs(in, n); return -1;
    }
    int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);
    /* Right-shift signedness follows the left operand; shift count masks with `& 63`. */
    int64_t v = op == 0 ? a & b : op == 1 ? a | b : op == 2 ? a ^ b
              : op == 3 ? kvi_shl(a, b)
                        : kvi_shr(a, b, kvlangLangtypeIsUint(sa.kind_id));
    uint8_t sbuf[64];
    kvlangXvalue_t r;
    narrow_int(sa.kind_id, sb.kind_id, v, sbuf, sizeof sbuf, &r);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangBuiltinFreeInputs(in, n);
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
    if (n < 1 || !kvlangLangtypeIsNum(sa.kind_id)) { kvlangBuiltinSetError(f, "TypeError: expected numeric, got %s", n ? xv_kind_or_none(&in[0]) : "None"); kvlangBuiltinFreeInputs(in, n); return -1; }
    uint8_t sbuf[64];
    double x = kvlangScalarF64(sa);
    int rc;
    switch (op) {
    case 0: rc = kvlangBuiltinEmitFloat64(f, sqrt(x)); break;
    case 1: rc = kvlangBuiltinEmitFloat64(f, exp(x)); break;
    case 2:
        if (x <= 0.0) rc = kvlangBuiltinSetError(f, "ValueError: log of non-positive number");
        else rc = kvlangBuiltinEmitFloat64(f, log(x));
        break;
    case 3: rc = emit_neg(f, &in[0], sa); break;
    case 4: { kvlangXvalue_t r; /* `abs` is identity on unsigned kinds */
        if (kvlangLangtypeIsFloat(sa.kind_id)) narrow_float(sa.kind_id, sa.kind_id, fabs(x), sbuf, sizeof sbuf, &r);
        else if (kvlangLangtypeIsUint(sa.kind_id)) narrow_int(sa.kind_id, sa.kind_id, (int64_t)kvlangScalarU64(sa), sbuf, sizeof sbuf, &r);
        else narrow_int(sa.kind_id, sa.kind_id, kvi_abs(kvlangScalarI64(sa)), sbuf, sizeof sbuf, &r);
        rc = kvlangBuiltinWriteResult(f, &r); break; }
    case 5: rc = kvlangBuiltinEmitInt64(f, x < 0 ? -1 : x > 0 ? 1 : 0); break;
    default: rc = -1; break;
    }
    kvlangBuiltinFreeInputs(in, n);
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
    if (n < 2 || !kvlangLangtypeIsNum(sa.kind_id) || !kvlangLangtypeIsNum(sb.kind_id)) { kvlangBuiltinSetError(f, "TypeError: expected numeric"); kvlangBuiltinFreeInputs(in, n); return -1; }
    int rc = kvlangBuiltinEmitFloat64(f, pow(kvlangScalarF64(sa), kvlangScalarF64(sb)));
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int kvlangBuiltinMaxMin(kvlangFrame_t *f, bool is_max) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinSetError(f, "TypeError: binary op requires 2 inputs, got %d", n); kvlangBuiltinFreeInputs(in, n); return -1; }
    uint8_t sbuf[64];
    kvlangXvalue_t r;
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]), sb = kvlangXvalueScalar(&in[1]);
    if (kvlangLangtypeIsInt(sa.kind_id) && kvlangLangtypeIsInt(sb.kind_id)) {
        int c = compare_int(sa, sb);
        bool take_a = (is_max && c >= 0) || (!is_max && c <= 0);
        narrow_int(sa.kind_id, sb.kind_id, take_a ? kvlangScalarI64(sa) : kvlangScalarI64(sb), sbuf, sizeof sbuf, &r);
    } else if (kvlangLangtypeIsNum(sa.kind_id) && kvlangLangtypeIsNum(sb.kind_id)) {
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);
        bool take_a = (is_max && a >= b) || (!is_max && a <= b);
        narrow_float(sa.kind_id, sb.kind_id, take_a ? a : b, sbuf, sizeof sbuf, &r);
    } else { kvlangBuiltinSetError(f, "TypeError: max/min requires numeric, got %s and %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1])); kvlangBuiltinFreeInputs(in, n); return -1; }
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
int kvlangBuiltinMax(kvlangFrame_t *f) { return kvlangBuiltinMaxMin(f, true); }
int kvlangBuiltinMin(kvlangFrame_t *f) { return kvlangBuiltinMaxMin(f, false); }

/* ── cast ─────────────────────────────────────────────────── */

/* cast */
__attribute__((visibility("hidden"))) int
kvlangScalarCast(int target_id, kvlangScalar_t s, uint8_t *buf, size_t cap,
                 kvlangXvalue_t *out) {
    if (!kvlangLangtypeIsNum(s.kind_id))
        return -1;
    if (kvlangLangtypeIsInt(target_id)) {
        narrow_int(target_id, target_id, kvlangScalarI64(s), buf, cap, out);
        return 0;
    }
    if (kvlangLangtypeIsFloat(target_id)) {
        narrow_float(target_id, target_id, kvlangScalarF64(s), buf, cap, out);
        return 0;
    }
    return -1;
}

static int kvlangBuiltinCastNum(kvlangFrame_t *f, const char *kind) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { kvlangBuiltinSetError(f, "TypeError: cannot cast None"); kvlangBuiltinFreeInputs(in, n); return -1; }
    uint8_t sbuf[64];
    kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);
    int tid = kvlangLangtypeId(kind, strlen(kind));
    int rc;
    if (tid == KVLANG_LT_BOOL) {
        if (!kvlangXvalueKindIs(&in[0], KVSPACE_KIND_BOOL)) { kvlangBuiltinSetError(f, "TypeError: cannot cast %s to bool — use != 0", kvlangXvalueKind(&in[0])); kvlangBuiltinFreeInputs(in, n); return -1; }
        rc = kvlangBuiltinEmitBool(f, kvlangScalarI64(sa) != 0);
    } else {
        kvlangXvalue_t r;
        if (kvlangScalarCast(tid, sa, sbuf, sizeof sbuf, &r) != 0) {
            kvlangBuiltinSetError(f, "TypeError: cannot cast %s to %s",
                                  kvlangXvalueKind(&in[0]), kind);
            kvlangBuiltinFreeInputs(in, n);
            return -1;
        }
        rc = kvlangBuiltinWriteResult(f, &r);
    }
    kvlangBuiltinFreeInputs(in, n);
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
int kvlangBuiltinCastFloat32(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT32); }
int kvlangBuiltinCastFloat64(kvlangFrame_t *f) { return kvlangBuiltinCastNum(f, KVSPACE_KIND_FLOAT64); }

static int kvlangBuiltinCastChar(kvlangFrame_t *f, const char *kind) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { kvlangBuiltinSetError(f, "TypeError: char conversion requires a value"); kvlangBuiltinFreeInputs(in, n); return -1; }
    char *s = kvlangXvalueValueString(&in[0]);
    kvlangXvalue_t r;
    if (strcmp(kind, KVSPACE_KIND_CHAR) == 0) kvlangXvalueNewCharUtf32(&r, s);
    else kvlangXvalueNewCharKind(&r, kind, s);
    int rc = kvlangBuiltinEmitResult(f, &r);free(s); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
int kvlangBuiltinCastCharUtf32(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR); }
int kvlangBuiltinCastCharUtf8(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_UTF8); }
int kvlangBuiltinCastCharAscii(kvlangFrame_t *f) { return kvlangBuiltinCastChar(f, KVSPACE_KIND_CHAR_ASCII); }
