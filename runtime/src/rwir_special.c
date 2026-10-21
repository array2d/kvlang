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

static int64_t arg_I64(const kvlangXvalue_t *v) {
    return kvlangScalarI64(kvlangXvalueScalar(v));
}

static double arg_F64(const kvlangXvalue_t *v) {
    return kvlangScalarF64(kvlangXvalueScalar(v));
}

#define DEF_I64_BIN(name, expr)                                                \
    HIDDEN int kvlangBuiltinI64##name(kvlangFrame_t *f) {                      \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 2);                             \
        if (n < 2) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");\
        }                                                                      \
        int64_t a = arg_I64(&in[0]), b = arg_I64(&in[1]);                      \
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
        int64_t a = arg_I64(&in[0]);                                           \
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
        double a = arg_F64(&in[0]), b = arg_F64(&in[1]);                       \
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
        double a = arg_F64(&in[0]);                                            \
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
    int64_t a = arg_I64(&in[0]);
    int64_t r = n == 1 ? -a : a - arg_I64(&in[1]);
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
    int64_t a = arg_I64(&in[0]), b = arg_I64(&in[1]);
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
    int64_t a = arg_I64(&in[0]), b = arg_I64(&in[1]);
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
    double a = arg_F64(&in[0]);
    double r = n == 1 ? -a : a - arg_F64(&in[1]);
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
    double a = arg_F64(&in[0]), b = arg_F64(&in[1]);
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
    double a = arg_F64(&in[0]);
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
            KT##_t a = arg_##KT(&in[0]), b = arg_##KT(&in[1]);                 \
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
