#include "runtime_internal.h"
#include "rwir_internal.h"

/* ── 比较 / 逻辑 ──────────────────────────────────────────── */

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
    } else if (kvlangXvalueIsPtr(&in[0]) || kvlangXvalueIsPtr(&in[1])) {
        // 指针比较恒判身份（target 串），且只允许 Ptr 对 Ptr——空指针即 None，已在上方
        // None 分支处理；不设 char 哨兵（`p == ""` 是非法比较，正是被砍掉的歧义分支）。
        if (op != CMP_EQ && op != CMP_NEQ) {
            kvlangBuiltinSetErr(f, "TypeError: cannot order ptr with %s", kvlangXvalueKind(&in[0])); kvlangBuiltinFreeInputs(in, n); return -1;
        }
        if (!kvlangXvalueIsPtr(&in[0]) || !kvlangXvalueIsPtr(&in[1])) {
            kvlangBuiltinSetErr(f, "TypeError: cannot compare %s with ptr; use None for a null pointer",
                                kvlangXvalueIsPtr(&in[0]) ? kvlangXvalueKind(&in[1]) : kvlangXvalueKind(&in[0]));
            kvlangBuiltinFreeInputs(in, n); return -1;
        }
        char *as = kvlangXvaluePtrTarget(&in[0]);
        char *bs = kvlangXvaluePtrTarget(&in[1]);
        int c = strcmp(as, bs);
        r = op == CMP_EQ ? c == 0 : c != 0;
        free(as); free(bs);
    } else {
        kvlangBuiltinSetErr(f, "TypeError: cannot compare %s with %s", kvlangXvalueKind(&in[0]), kvlangXvalueKind(&in[1])); kvlangBuiltinFreeInputs(in, n); return -1;
    }
    kvlangXvalue_t rv; kvlangXvalueNewBool(&rv, r);
    int rc = kvlangBuiltinWriteResult(f, &rv); kvlangXvalueFree(&rv); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinEq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_EQ); }
int kvlangBuiltinNeq(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_NEQ); }
int kvlangBuiltinLt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LT); }
int kvlangBuiltinGt(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GT); }
int kvlangBuiltinLe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_LE); }
int kvlangBuiltinGe(kvlangFrame_t *f) { return kvlangBuiltinCmp(f, CMP_GE); }

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
int kvlangBuiltinAnd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "&&", in, n, 2)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) != 0 && kvlangScalarI64(kvlangXvalueScalar(&in[1])) != 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
int kvlangBuiltinOr(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "||", in, n, 2)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) != 0 || kvlangScalarI64(kvlangXvalueScalar(&in[1])) != 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
int kvlangBuiltinNot(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (!require_bool(f, "!", in, n, 1)) { kvlangBuiltinFreeInputs(in, n); return -1; }
    kvlangXvalue_t r; kvlangXvalueNewBool(&r, kvlangScalarI64(kvlangXvalueScalar(&in[0])) == 0);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
