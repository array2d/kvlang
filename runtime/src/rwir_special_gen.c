#include "runtime_internal.h"

/* P1b4-a4：typed_core 读参快路径（晚链接 TU，控制热 TU 布局，见 #476）。
 * 值已在 operand_load 装入 reads[i].value，直接借用；限本指令、n<=cap。
 * 该函数必须保持 hidden 且真实被 ReadInputs 调用——不得用作布局补丁。 */
__attribute__((visibility("hidden"))) int
kvlangBuiltinReadInputsTyped(kvlangFrame_t *f, kvlangXvalue_t *out, int cap) {
    int n = f->instruction->read_count;
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; i++)
        out[i] = f->instruction->reads[i].value;
    return n;
}
#include "rwir_internal.h"
#include <math.h>

/* Phase 2 浮点族生成实现：单独 TU，且由 CMake 链接在最后，避免改动既有
 * 函数在 .text 中的布局（实测同 TU 追加代码会造成约 1.5% 的布局回退）。 */

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


#define F32_READ2(f, in, n)                                                    \
    kvlangXvalue_t in[2];                                                      \
    int n = kvlangBuiltinReadInputs(f, in, 2);                                 \
    if (n < 2) {                                                               \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return kvlangBuiltinSetError(f, "TypeError: binary op requires 2");    \
    }

/* ── Phase 2 浮点：float32 同型特化 ──────────────────────────────────
 * 通用实现一律在 double 上算、再 (float) 收窄；这里逐字复刻。
 * typed 操作数已被读侧 coercion 变成声明 kind，故实际 kind 必等于 float32；
 * 不等只可能来自动态（无声明）操作数 → 回退通用实现（debug 下声明不符直接 die）。 */

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
    if (f->instruction && f->instruction->typed_core)
        return kvlangBuiltinWriteResultTyped(f, &r, kind);
    return kvlangBuiltinWriteResult(f, &r);
}

#define DEF_F32_BIN(name, gfn, expr)                                           \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        kvlangScalar_t sb = kvlangScalarOf(f, &in[1], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id,                       \
                       KVLANG_LT_FLOAT32, gfn);                                \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        int rc = emit_float_kind(f, "float32", 4, (expr));                     \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_DIV(gfn)                                                       \
    HIDDEN int kvlangBuiltinFloat32Div(kvlangFrame_t *f) {                     \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        kvlangScalar_t sb = kvlangScalarOf(f, &in[1], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id,                       \
                       KVLANG_LT_FLOAT32, gfn);                                \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        int rc;                                                                \
        if (b == 0)                                                            \
            rc = kvlangBuiltinSetError(f,                                      \
                                       "ZeroDivisionError: division by zero"); \
        else                                                                   \
            rc = emit_float_kind(f, "float32", 4, a / b);                      \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_MAXMIN(name, gfn, is_max)                                      \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        kvlangScalar_t sb = kvlangScalarOf(f, &in[1], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id,                       \
                       KVLANG_LT_FLOAT32, gfn);                                \
        double a = kvlangScalarF64(sa), b = kvlangScalarF64(sb);               \
        bool take = (is_max && a >= b) || (!is_max && a <= b);                 \
        int rc = emit_float_kind(f, "float32", 4, take ? a : b);               \
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
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK1(f, in, n, sa.kind_id, KVLANG_LT_FLOAT32, gfn);          \
        double a = kvlangScalarF64(sa);                                        \
        int rc = emit_float_kind(f, "float32", 4, (expr));                     \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_EQ(name, gfn, want)                                            \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        kvlangScalar_t sb = kvlangScalarOf(f, &in[1], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id,                       \
                       KVLANG_LT_FLOAT32, gfn);                                \
        bool eq = kvlangScalarF64(sa) == kvlangScalarF64(sb);                  \
        int rc = kvlangBuiltinEmitBool(f, want ? eq : !eq);                    \
        kvlangBuiltinFreeInputs(in, n);                                        \
        return rc;                                                             \
    }

#define DEF_F32_ORD(name, gfn, op)                                             \
    HIDDEN int kvlangBuiltinFloat32##name(kvlangFrame_t *f) {                  \
        F32_READ2(f, in, n)                                                    \
        kvlangScalar_t sa = kvlangScalarOf(f, &in[0], KVLANG_LT_FLOAT32);      \
        kvlangScalar_t sb = kvlangScalarOf(f, &in[1], KVLANG_LT_FLOAT32);      \
        SPEC_FALLBACK2(f, in, n, sa.kind_id, sb.kind_id,                       \
                       KVLANG_LT_FLOAT32, gfn);                                \
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
