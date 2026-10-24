// rwir_special —— layout 特化 opcode（`<kind>·<op>`）的原生 handler 原型。
//
// layout 按操作数静态 kind 把算术/比较改写成 `int64·add` / `float64·lt` 等；
// 这里为 int64 / float64 两族提供直接实现：跳过每步 char/num/int 判别与结果宽度选择，
// 只做值相关检查（除零、None）与读写。其它宽度（int8…uint32）继续走通用实现。
// 所有函数对 .so 内部可见、不新增导出符号（避免布局效应）。

#include "runtime_internal.h"
#include "rwir_internal.h"
#include <math.h>

typedef int64_t I64_t;
typedef double F64_t;

#define HIDDEN __attribute__((visibility("hidden")))

/* P1b3：typed_core 行不保留“kind 不等 → 通用回退”分支（layout 已证明）；
 * 仅 KV_DEBUG_TYPES 构建保留为断言 + 兜底。 */
#ifdef KV_DEBUG_TYPES
#define SPEC_FALLBACK2(f, in, n, ka, kb, kid, gfn)                             \
    do {                                                                       \
        if ((ka) != (kid) || (kb) != (kid)) {                                  \
            kvlangSpecCheckTyped(f, 0, ka, kid);                               \
            kvlangSpecCheckTyped(f, 1, kb, kid);                               \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return gfn(f);                                                     \
        }                                                                      \
    } while (0)
#define SPEC_FALLBACK1(f, in, n, ka, kid, gfn)                                 \
    do {                                                                       \
        if ((ka) != (kid)) {                                                   \
            kvlangSpecCheckTyped(f, 0, ka, kid);                               \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return gfn(f);                                                     \
        }                                                                      \
    } while (0)
#else
#define SPEC_FALLBACK2(f, in, n, ka, kb, kid, gfn) ((void)0)
#define SPEC_FALLBACK1(f, in, n, ka, kid, gfn) ((void)0)
#endif


static int64_t arg_I64(kvlangFrame_t *f, kvlangXvalue_t *in, int i) {
    kvlangScalar_t s = kvlangXvalueScalar(&in[i]);
    kvlangSpecCheckDeclared(f, i, s.kind_id, KVLANG_LT_INT64);
    kvlangSpecCheckTyped(f, i, s.kind_id, KVLANG_LT_INT64);
    return kvlangScalarI64(s);
}

static double arg_F64(kvlangFrame_t *f, kvlangXvalue_t *in, int i) {
    kvlangScalar_t s = kvlangXvalueScalar(&in[i]);
    kvlangSpecCheckDeclared(f, i, s.kind_id, KVLANG_LT_FLOAT64);
    kvlangSpecCheckTyped(f, i, s.kind_id, KVLANG_LT_FLOAT64);
    return kvlangScalarF64(s);
}

#define DEF_I64_BIN(name, expr)                                                \
    HIDDEN int kvlangBuiltinI64##name(kvlangFrame_t *f) {                      \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 2);                             \
        if (n < 2) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");\
        }                                                                      \
        int64_t a = arg_I64(f, in, 0), b = arg_I64(f, in, 1);                  \
        int64_t r = (expr);                                                    \
        int rc = kvlangBuiltinEmitInt64(f, r);                                 \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_I64_UN(name, expr)                                                 \
    HIDDEN int kvlangBuiltinI64##name(kvlangFrame_t *f) {                      \
        kvlangXvalue_t in[1];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 1);                             \
        if (n < 1) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: unary op requires 1"); \
        }                                                                      \
        int64_t a = arg_I64(f, in, 0);                                         \
        int64_t r = (expr);                                                    \
        int rc = kvlangBuiltinEmitInt64(f, r);                                 \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F64_BIN(name, expr)                                                \
    HIDDEN int kvlangBuiltinF64##name(kvlangFrame_t *f) {                      \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 2);                             \
        if (n < 2) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");\
        }                                                                      \
        double a = arg_F64(f, in, 0), b = arg_F64(f, in, 1);                   \
        double r = (expr);                                                     \
        int rc = kvlangBuiltinEmitFloat64(f, r);                               \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F64_UN(name, expr)                                                 \
    HIDDEN int kvlangBuiltinF64##name(kvlangFrame_t *f) {                      \
        kvlangXvalue_t in[1];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 1);                             \
        if (n < 1) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: unary op requires 1"); \
        }                                                                      \
        double a = arg_F64(f, in, 0);                                          \
        double r = (expr);                                                     \
        int rc = kvlangBuiltinEmitFloat64(f, r);                               \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

DEF_I64_BIN(Add, a + b)
HIDDEN int kvlangBuiltinI64Sub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: sub requires 1 or 2 inputs");
    }
    int64_t a = arg_I64(f, in, 0);
    int64_t r = n == 1 ? -a : a - arg_I64(f, in, 1);
    int rc = kvlangBuiltinEmitInt64(f, r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
DEF_I64_BIN(Mul, a * b)
DEF_I64_BIN(Bitand, a & b)
DEF_I64_BIN(Bitor, a | b)
DEF_I64_BIN(Bitxor, a ^ b)
DEF_I64_BIN(Shl, a << (uint64_t)b)
DEF_I64_BIN(Shr, a >> (uint64_t)b)
DEF_I64_BIN(Max, a >= b ? a : b)
DEF_I64_BIN(Min, a <= b ? a : b)
DEF_I64_UN(Neg, -a)
DEF_I64_UN(Abs, a < 0 ? -a : a)
DEF_I64_UN(Sign, a < 0 ? -1 : a > 0 ? 1 : 0)

HIDDEN int kvlangBuiltinI64Div(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");
    }
    int64_t a = arg_I64(f, in, 0), b = arg_I64(f, in, 1);
    int rc;
    if (b == 0) {
        rc = kvlangBuiltinSetError(f, "ZeroDivisionError: division by zero");
    } else {
        rc = kvlangBuiltinEmitInt64(f, a / b);
    }
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

HIDDEN int kvlangBuiltinI64Mod(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");
    }
    int64_t a = arg_I64(f, in, 0), b = arg_I64(f, in, 1);
    int rc;
    if (b == 0) {
        rc = kvlangBuiltinSetError(f, "ZeroDivisionError: modulo by zero");
    } else {
        rc = kvlangBuiltinEmitInt64(f, a % b);
    }
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

DEF_F64_BIN(Add, a + b)
HIDDEN int kvlangBuiltinF64Sub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: sub requires 1 or 2 inputs");
    }
    double a = arg_F64(f, in, 0);
    double r = n == 1 ? -a : a - arg_F64(f, in, 1);
    int rc = kvlangBuiltinEmitFloat64(f, r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
DEF_F64_BIN(Mul, a * b)
DEF_F64_BIN(Max, a >= b ? a : b)
DEF_F64_BIN(Min, a <= b ? a : b)
DEF_F64_UN(Neg, -a)
DEF_F64_UN(Abs, a < 0 ? -a : a)
DEF_F64_UN(Sqrt, sqrt(a))
DEF_F64_UN(Exp, exp(a))
DEF_F64_UN(Log, log(a))
DEF_F64_BIN(Pow, pow(a, b))

HIDDEN int kvlangBuiltinF64Div(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");
    }
    double a = arg_F64(f, in, 0), b = arg_F64(f, in, 1);
    int rc;
    if (b == 0) {
        rc = kvlangBuiltinSetError(f, "ZeroDivisionError: division by zero");
    } else {
        rc = kvlangBuiltinEmitFloat64(f, a / b);
    }
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

HIDDEN int kvlangBuiltinF64Sign(kvlangFrame_t *f) {
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: unary op requires 1");
    }
    double a = arg_F64(f, in, 0);
    int rc = kvlangBuiltinEmitInt64(f, a < 0 ? -1 : a > 0 ? 1 : 0);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* 比较：None 语义保留（eq/neq 允许 None，其它报错）。 */
#define DEF_CMP_BODY(KT, NAME, EXPR, ...)                                      \
    HIDDEN int kvlangBuiltin##KT##NAME(kvlangFrame_t *f) {                     \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 2);                             \
        if (n < 2) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");\
        }                                                                      \
        bool r;                                                                \
        if (kvlangXvalueNone(&in[0]) || kvlangXvalueNone(&in[1])) {            \
            __VA_ARGS__;                                                       \
        } else {                                                               \
            KT##_t a = arg_##KT(f, in, 0), b = arg_##KT(f, in, 1);             \
            r = (EXPR);                                                        \
        }                                                                      \
        int rc = kvlangBuiltinEmitBool(f, r);                                  \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

DEF_CMP_BODY(I64, Eq, a == b,
             r = kvlangXvalueNone(&in[0]) && kvlangXvalueNone(&in[1]))
DEF_CMP_BODY(I64, Neq, a != b,
             r = !(kvlangXvalueNone(&in[0]) && kvlangXvalueNone(&in[1])))
DEF_CMP_BODY(I64, Lt, a < b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(I64, Le, a <= b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(I64, Gt, a > b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(I64, Ge, a >= b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))

DEF_CMP_BODY(F64, Eq, a == b,
             r = kvlangXvalueNone(&in[0]) && kvlangXvalueNone(&in[1]))
DEF_CMP_BODY(F64, Neq, a != b,
             r = !(kvlangXvalueNone(&in[0]) && kvlangXvalueNone(&in[1])))
DEF_CMP_BODY(F64, Lt, a < b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(F64, Le, a <= b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(F64, Gt, a > b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))
DEF_CMP_BODY(F64, Ge, a >= b,
             kvlangBuiltinFreeInputs(in, n);
             return kvlangBuiltinSetError(f, "TypeError: None in comparison"))

/* ── Phase 2：整数族同型特化（其余 7 种 kind）────────────────────────
 * 语义逐字节复刻通用实现：一律在 int64 位型上算，再按结果 kind 截断。
 * typed 操作数已被读侧 coercion 变成声明 kind，故实际 kind 必等于 kid；
 * 不等只可能来自动态（无声明）操作数 → 回退通用实现（debug 下声明不符直接 die）。 */

static int emit_int_kind(kvlangFrame_t *f, const char *kind, uint32_t width,
                         int64_t v) {
    uint8_t raw[8];
    switch (width) {
    case 1:
        raw[0] = (uint8_t)v;
        break;
    case 2: {
        uint16_t x = (uint16_t)v;
        memcpy(raw, &x, 2);
        break;
    }
    case 4: {
        uint32_t x = (uint32_t)v;
        memcpy(raw, &x, 4);
        break;
    }
    default: {
        uint64_t x = (uint64_t)v;
        memcpy(raw, &x, 8);
        break;
    }
    }
    if (kvlangBuiltinWriteBody(f, kind, raw, width))
        return 0;
    uint8_t buf[64];
    kvlangXvalue_t r = {buf, kvlangScalarEncode(kind, raw, width, buf,
                                                sizeof buf)};
    if (f->instruction && f->instruction->typed_core)
        return kvlangBuiltinWriteResultTyped(f, &r, kind);
    return kvlangBuiltinWriteResult(f, &r);
}

#define INT_READ2(f, in, n)                                                    \
    kvlangXvalue_t in[2];                                                      \
    int n = kvlangBuiltinReadInputs(f, in, 2);                                 \
    if (n < 2) {                                                               \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");    \
    }

#define DEF_INT_BIN(ktok, kstr, w, kid, name, gfn, expr)                       \
    HIDDEN int kvlangBuiltin##ktok##name(kvlangFrame_t *f) {                   \
        INT_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id, kid, gfn); \
        int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);              \
        int rc = emit_int_kind(f, kstr, w, (expr));                            \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_DIVMOD(ktok, kstr, w, kid, name, gfn, op, msg)                 \
    HIDDEN int kvlangBuiltin##ktok##name(kvlangFrame_t *f) {                   \
        INT_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id, kid, gfn); \
        int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);              \
        int rc;                                                                \
        if (b == 0)                                                            \
            rc = kvlangBuiltinSetError(f, "ZeroDivisionError: " msg);          \
        else                                                                   \
            rc = emit_int_kind(f, kstr, w, a op b);                            \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_MAXMIN(ktok, kstr, w, kid, name, gfn, U, op)                   \
    HIDDEN int kvlangBuiltin##ktok##name(kvlangFrame_t *f) {                   \
        INT_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        bool u = U;                                                            \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id, kid, gfn); \
        int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);              \
        bool take = u ? ((uint64_t)a op(uint64_t)b) : (a op b);                \
        int rc = emit_int_kind(f, kstr, w, take ? a : b);                      \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_EQ(ktok, kstr, w, kid, name, gfn, want)                        \
    HIDDEN int kvlangBuiltin##ktok##name(kvlangFrame_t *f) {                   \
        INT_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id, kid, gfn); \
        bool eq = kvlangScalarI64(sa) == kvlangScalarI64(sb);                  \
        int rc = kvlangBuiltinEmitBool(f, want ? eq : !eq);                    \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_ORD(ktok, kstr, w, kid, name, gfn, U, op)                      \
    HIDDEN int kvlangBuiltin##ktok##name(kvlangFrame_t *f) {                   \
        INT_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        bool u = U;                                                            \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id, kid, gfn); \
        int64_t a = kvlangScalarI64(sa), b = kvlangScalarI64(sb);              \
        bool r = u ? ((uint64_t)a op(uint64_t)b) : (a op b);                   \
        int rc = kvlangBuiltinEmitBool(f, r);                                  \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_NEG(ktok, kstr, w, kid)                                        \
    HIDDEN int kvlangBuiltin##ktok##Neg(kvlangFrame_t *f) {                    \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 1);                             \
        if (n < 1) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: unary op requires 1"); \
        }                                                                      \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        SPEC_FALLBACK1(f, in, n, sa.kind_id, kid, kvlangBuiltinNeg); \
        int rc = emit_int_kind(f, kstr, w, -kvlangScalarI64(sa));              \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_ABS(ktok, kstr, w, kid)                                        \
    HIDDEN int kvlangBuiltin##ktok##Abs(kvlangFrame_t *f) {                    \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 1);                             \
        if (n < 1) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: unary op requires 1"); \
        }                                                                      \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        SPEC_FALLBACK1(f, in, n, sa.kind_id, kid, kvlangBuiltinAbs); \
        int64_t a = kvlangScalarI64(sa);                                       \
        int rc = emit_int_kind(f, kstr, w, a < 0 ? -a : a);                    \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_INT_BASE_FUNCS(ktok, kstr, w, U, kid)                              \
    DEF_INT_BIN(ktok, kstr, w, kid, Add, kvlangBuiltinAdd, a + b)              \
    DEF_INT_BIN(ktok, kstr, w, kid, Sub, kvlangBuiltinSub, a - b)              \
    DEF_INT_BIN(ktok, kstr, w, kid, Mul, kvlangBuiltinMul, a * b)              \
    DEF_INT_DIVMOD(ktok, kstr, w, kid, Div, kvlangBuiltinDiv, /,               \
                   "division by zero")                                         \
    DEF_INT_DIVMOD(ktok, kstr, w, kid, Mod, kvlangBuiltinMod, %,               \
                   "modulo by zero")                                           \
    DEF_INT_MAXMIN(ktok, kstr, w, kid, Max, kvlangBuiltinMax, U, >=)           \
    DEF_INT_MAXMIN(ktok, kstr, w, kid, Min, kvlangBuiltinMin, U, <=)           \
    DEF_INT_EQ(ktok, kstr, w, kid, Eq, kvlangBuiltinEq, 1)                     \
    DEF_INT_EQ(ktok, kstr, w, kid, Neq, kvlangBuiltinNeq, 0)                   \
    DEF_INT_ORD(ktok, kstr, w, kid, Lt, kvlangBuiltinLt, U, <)                 \
    DEF_INT_ORD(ktok, kstr, w, kid, Le, kvlangBuiltinLe, U, <=)                \
    DEF_INT_ORD(ktok, kstr, w, kid, Gt, kvlangBuiltinGt, U, >)                 \
    DEF_INT_ORD(ktok, kstr, w, kid, Ge, kvlangBuiltinGe, U, >=)

#define DEF_INT_SIGNED_FUNCS(ktok, kstr, w, U, kid)                            \
    DEF_INT_NEG(ktok, kstr, w, kid)                                            \
    DEF_INT_ABS(ktok, kstr, w, kid)

#define DEF_INT_U64_FUNCS(ktok, kstr, w, U, kid)                               \
    DEF_INT_BIN(ktok, kstr, w, kid, Bitand, kvlangBuiltinBitand, a & b)        \
    DEF_INT_BIN(ktok, kstr, w, kid, Bitor, kvlangBuiltinBitor, a | b)          \
    DEF_INT_BIN(ktok, kstr, w, kid, Bitxor, kvlangBuiltinBitxor, a ^ b)        \
    DEF_INT_BIN(ktok, kstr, w, kid, Shl, kvlangBuiltinShl,                     \
                a << (uint64_t)b)                                              \
    DEF_INT_BIN(ktok, kstr, w, kid, Shr, kvlangBuiltinShr,                     \
                a >> (uint64_t)b)

KVLANG_INT_BASE_KINDS(DEF_INT_BASE_FUNCS)
KVLANG_INT_SIGNED_KINDS(DEF_INT_SIGNED_FUNCS)
KVLANG_INT_U64_KINDS(DEF_INT_U64_FUNCS)
