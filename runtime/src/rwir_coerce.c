/* typed 槽赋值转换（[[赋值转换]]）。整段逻辑单独成 TU 并**最后链接**：
 * 加进热 TU（数组/映射/读写）会位移其后的字符串/算术代码，实测 k_nucleotide
 * 回退约 8%（与执行路径无关的布局效应，同 rwir_special_gen.c 的经验）。 */
#include "runtime_internal.h"
#include "rwir_internal.h"

__attribute__((visibility("hidden"))) int
kvlangXvalueCoerce(const char *type, const kvlangXvalue_t *v, uint8_t *buf,
                   size_t cap, kvlangXvalue_t *out) {
    *out = *v;
    if (!type || !type[0] || strcmp(type, "any") == 0 || strchr(type, '|'))
        return 0;
    int tid = kvlangLangtypeId(type, strlen(type));
    kvlangLangtype vkx;
    memset(&vkx, 0, sizeof vkx);
    int sk = KVLANG_LT_NONE;
    if (v->bytes && v->length) {
        kvlangLangtypeParse(xh_langtype_of(v),
                            (int32_t)xh_langtype_len_of(v), &vkx);
        sk = kvlangXvalueScalar(v).kind_id;
    }
    /* 数值/bool 目标只接受标量值：同 kind 的数组不是标量（typed 槽不变量）。
     * char 族是字符串（ndim≥1 合法），不在此列。 */
    if ((kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) && vkx.ndim != 0)
        return -1;
    if (kvlangLangtypeIsInt(tid) || kvlangLangtypeIsFloat(tid)) {
        if (sk == KVLANG_LT_NONE || !kvlangLangtypeIsNum(sk))
            return -1;
        if (sk == tid)
            return 0;
        return kvlangScalarCast(tid, kvlangXvalueScalar(v), buf, cap, out);
    }
    if (tid == KVLANG_LT_BOOL)
        return sk == KVLANG_LT_BOOL ? 0 : -1;
    if (kvlangLangtypeIsChar(tid))
        return sk == tid ? 0 : -1;
    if (tid == KVLANG_LT_TIME || tid == KVLANG_LT_DURATION)
        return sk == tid ? 0 : -1;
    if (type[0] == '*') {
        if (sk == KVLANG_LT_NONE || !v->bytes)
            return 0;
        return xh_is_ptr(v->bytes) ? 0 : -1;
    }
    return 0;
}

/* 单值：转换失败即置 TypeError，返回 -1。 */
__attribute__((visibility("hidden"))) int
kvlangCoerceAssign(kvlangFrame_t *f, const char *type, const kvlangXvalue_t *v,
                   uint8_t *buf, size_t cap, kvlangXvalue_t *out) {
    if (kvlangXvalueCoerce(type, v, buf, cap, out) == 0)
        return 0;
    return kvlangBuiltinSetError(f, "TypeError: cannot assign %s to %s at %s",
                                 kvlangXvalueKind(v), type, f->pc);
}

/* 整组：数组字面量逐元素按声明元素 kind 转换（bufs/out 由调用方提供 [n]）。 */
__attribute__((visibility("hidden"))) int
kvlangCoerceAll(kvlangFrame_t *f, const char *type, const kvlangXvalue_t *in,
                int n, uint8_t (*bufs)[64], kvlangXvalue_t *out) {
    for (int i = 0; i < n; i++) {
        uint8_t tmp[64];
        uint8_t *dst = bufs ? bufs[i] : tmp;
        if (kvlangCoerceAssign(f, type, &in[i], dst, 64, &out[i]) != 0)
            return -1;
    }
    return 0;
}
