#include "runtime_internal.h"
#include "rwir_internal.h"
#include <math.h>

/* Phase 2 浮点族生成实现：单独 TU，且由 CMake 链接在最后，避免改动既有
 * 函数在 .text 中的布局（实测同 TU 追加代码会造成约 1.5% 的布局回退）。 */

#define HIDDEN __attribute__((visibility("hidden")))

#define F32_READ2(f, in, n)                                                    \
    kvlangXvalue_t in[2];                                                      \
    int n = kvlangBuiltinReadInputs(f, in, 2);                                 \
    if (n < 2) {                                                               \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");    \
    }

/* ── Phase 2 浮点：float32 同型特化 ──────────────────────────────────
 * 通用实现一律在 double 上算、再 (float) 收窄；这里逐字复刻。
 * 静态声明与实际 kind 可能不一致：两操作数实际同为 float 时按该 kind 编码，
 * 混合/非浮点/None 回退通用实现。float64 族沿用上面的 A1 实现。 */

static int emit_float_kind(kvlangFrame_t *f, const char *kind, uint32_t width,
                           double v) {
    uint8_t raw[8];
    if (width == 4) {
        float x = (float)v;
        memcpy(raw, &x, 4);
    } else {
        memcpy(raw, &v, 8);
    }
    if (kvlangBuiltinWriteBody(f, kind, raw, width))
        return 0;
    uint8_t buf[64];
    kvlangXvalue_t r = {buf,
                        kvlangScalarEncode(kind, raw, width, buf, sizeof buf)};
    return kvlangBuiltinWriteResult(f, &r);
}

static bool float_pair_kind(kvlangScalar_t a, kvlangScalar_t b,
                            const char **kind, uint32_t *width) {
    if (a.kind_id != b.kind_id || !kvlangLangtypeIsFloat(a.kind_id))
        return false;
    *kind = kvlangLangtypeKind(a.kind_id);
    *width = (uint32_t)kvlangLangtypeElemSize(a.kind_id);
    return true;
}

#define DEF_F32_BIN(name, gfn, expr)                                           \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        const char *ek = "float32";                                            \
        uint32_t ew = 4;                                                       \
        if (sa.kind_id != KVLANG_LT_FLOAT32 ||                                 \
            sb.kind_id != KVLANG_LT_FLOAT32) {                                 \
            if (!float_pair_kind(sa, sb, &ek, &ew)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
        }                                                                      \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        int rc = emit_float_kind(f, ek, ew, (expr));                           \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_DIV(gfn)                                                       \
    HIDDEN int kvlangBuiltinFloat32Div(kvlangFrame_t *f) {                     \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        const char *ek = "float32";                                            \
        uint32_t ew = 4;                                                       \
        if (sa.kind_id != KVLANG_LT_FLOAT32 ||                                 \
            sb.kind_id != KVLANG_LT_FLOAT32) {                                 \
            if (!float_pair_kind(sa, sb, &ek, &ew)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
        }                                                                      \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        int rc;                                                                \
        if (b == 0)                                                            \
            rc = kvlangBuiltinSetError(f,                                      \
                                       "ZeroDivisionError: division by zero"); \
        else                                                                   \
            rc = emit_float_kind(f, ek, ew, a / b);                            \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_MAXMIN(name, gfn, is_max)                                      \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        const char *ek = "float32";                                            \
        uint32_t ew = 4;                                                       \
        if (sa.kind_id != KVLANG_LT_FLOAT32 ||                                 \
            sb.kind_id != KVLANG_LT_FLOAT32) {                                 \
            if (!float_pair_kind(sa, sb, &ek, &ew)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
        }                                                                      \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        bool take = (is_max && a >= b) || (!is_max && a <= b);                 \
        int rc = emit_float_kind(f, ek, ew, take ? a : b);                     \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_UN(name, gfn, expr)                                            \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        kvlangXvalue_t in[2];                                                  \
        int n = kvlangBuiltinReadInputs(f, in, 1);                             \
        if (n < 1) {                                                           \
            kvlangBuiltinFreeInputs(in, n);                                    \
            return kvlangBuiltinSetError(f, "TypeError: unary op requires 1"); \
        }                                                                      \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        const char *ek = "float32";                                            \
        uint32_t ew = 4;                                                       \
        if (sa.kind_id != KVLANG_LT_FLOAT32) {                                 \
            if (!kvlangLangtypeIsFloat(sa.kind_id)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
            ek = kvlangLangtypeKind(sa.kind_id);                               \
            ew = (uint32_t)kvlangLangtypeElemSize(sa.kind_id);                 \
        }                                                                      \
        double a = kvlangScalarF64(sa);                                        \
        int rc = emit_float_kind(f, ek, ew, (expr));                           \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_EQ(name, gfn, want)                                            \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        if (sa.kind_id != KVLANG_LT_FLOAT32 ||                                 \
            sb.kind_id != KVLANG_LT_FLOAT32) {                                 \
            const char *ek;                                                    \
            uint32_t ew;                                                       \
            if (!float_pair_kind(sa, sb, &ek, &ew)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
        }                                                                      \
        bool eq = kvlangScalarF64(sa) == kvlangScalarF64(sb);                  \
        int rc = kvlangBuiltinEmitBool(f, want ? eq : !eq);                    \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_ORD(name, gfn, op)                                             \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangXvalueScalar(&in[0]);                        \
        kvlangScalar_t sb = kvlangXvalueScalar(&in[1]);                        \
        if (sa.kind_id != KVLANG_LT_FLOAT32 ||                                 \
            sb.kind_id != KVLANG_LT_FLOAT32) {                                 \
            const char *ek;                                                    \
            uint32_t ew;                                                       \
            if (!float_pair_kind(sa, sb, &ek, &ew)) {                          \
                kvlangBuiltinFreeInputs(in, n);                                \
                return gfn(f);                                                 \
            }                                                                  \
        }                                                                      \
        bool r = kvlangScalarF64(sa) op kvlangScalarF64(sb);                   \
        int rc = kvlangBuiltinEmitBool(f, r);                                  \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

DEF_F32_BIN(Add, kvlangBuiltinAdd, a + b)
DEF_F32_BIN(Sub, kvlangBuiltinSub, a - b)
DEF_F32_BIN(Mul, kvlangBuiltinMul, a * b)
DEF_F32_DIV(kvlangBuiltinDiv)
DEF_F32_MAXMIN(Max, kvlangBuiltinMax, true)
DEF_F32_MAXMIN(Min, kvlangBuiltinMin, false)
DEF_F32_UN(Neg, kvlangBuiltinNeg, -a)
DEF_F32_UN(Abs, kvlangBuiltinAbs, fabs(a))
DEF_F32_EQ(Eq, kvlangBuiltinEq, 1)
DEF_F32_EQ(Neq, kvlangBuiltinNeq, 0)
DEF_F32_ORD(Lt, kvlangBuiltinLt, <)
DEF_F32_ORD(Le, kvlangBuiltinLe, <=)
DEF_F32_ORD(Gt, kvlangBuiltinGt, >)
DEF_F32_ORD(Ge, kvlangBuiltinGe, >=)
