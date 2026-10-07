#pragma once
#include "runtime_internal.h"

/* 读参 head 的原位视图：变量（GetHead）路径解出 head；字面量路径直接持 wire 指针。
 * 不建 328B head、不校验——调用方按 langtype/body_offset/body_len/is_ptr 直取。 */
typedef struct {
    kvspaceHead_t head; /* 变量路径的数据源（其 langtype 为 langtype 宿主） */
    const uint8_t *langtype;
    int32_t langtype_len; /* langtype 字节数（不含 NUL），langtype 无 NUL 保证 */
    int32_t body_offset;
    int32_t body_len;
    bool is_ptr;
} xv_head_t;

static inline void xv_head_from_head(xv_head_t *o) {
    o->langtype = (const uint8_t *)o->head.langtype;
    o->langtype_len = o->head.langtype_len;
    o->body_offset = o->head.body_offset;
    o->body_len = o->head.body_len;
    o->is_ptr = o->head.ref == KVSPACE_REF_PTR;
}
/* 有界拷 head.langtype 成 NUL 终止串（langtype 本身无 NUL 保证）。 */
static inline void xv_head_langtype_str(const xv_head_t *o, char *buf,
                                        size_t cap) {
    if (cap == 0)
        return;
    int32_t n = o->langtype_len;
    if (n < 0)
        n = 0;
    if ((size_t)n > cap - 1)
        n = (int32_t)(cap - 1);
    memcpy(buf, o->langtype, (size_t)n);
    buf[n] = 0;
}
static inline void xv_head_from_data(xv_head_t *o, const uint8_t *d) {
    o->langtype = xh_langtype(d);
    o->langtype_len = (int32_t)xh_langtype_len(d);
    o->body_offset = (int32_t)xh_headlen(d);
    o->body_len = xh_content_len(d);
    o->is_ptr = xh_is_ptr(d);
}

/* ── 跨模块共享 helper ─────────────────────────────────────────────
   frame I/O 与 xvalue_at 定义在 rwir_func.c；容器 key/index helper 定义在 rwir_array.c / rwir_map.c。*/
int kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap);
void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n);
void kvlangBuiltinNextPc(kvlangFrame_t *f);
int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result);
int kvlangBuiltinSetErr(kvlangFrame_t *f, const char *fmt, ...);
void kvlangBuiltinXvalueAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out);
char *kvlangBuiltinScatterKey(const char *base, const int64_t *coords,
                              int ncoord);
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const char *langtype);
/* char 拼接（+ 与 string·concat 共用）：均须 char kind；编码不同返 false（调用方 throw），相同则 out=拼接结果并保持该编码。 */
bool kvlangBuiltinCharConcat(const kvlangXvalue_t *a, const kvlangXvalue_t *b,
                             kvlangXvalue_t *out);

/* ── 各 lib 的 rwir handler 原型（表在 rwir_func.c 引用）────────────── */
/* 单读参 head（定义在 rwir_xvalue.c，rwir_ndarray.c 复用） */
int xv_head1(kvlangFrame_t *f, xv_head_t *h);
/* 算术 / 位 / cast（rwir_int.c） */
int cmp_int(kvlangScalar_t a, kvlangScalar_t b);
int kvlangBuiltinAdd(kvlangFrame_t *f), kvlangBuiltinSub(kvlangFrame_t *f),
    kvlangBuiltinMul(kvlangFrame_t *f), kvlangBuiltinDiv(kvlangFrame_t *f),
    kvlangBuiltinMod(kvlangFrame_t *f), kvlangBuiltinBitand(kvlangFrame_t *f),
    kvlangBuiltinBitor(kvlangFrame_t *f), kvlangBuiltinBitxor(kvlangFrame_t *f),
    kvlangBuiltinShl(kvlangFrame_t *f), kvlangBuiltinShr(kvlangFrame_t *f),
    kvlangBuiltinSqrt(kvlangFrame_t *f), kvlangBuiltinExp(kvlangFrame_t *f),
    kvlangBuiltinLog(kvlangFrame_t *f), kvlangBuiltinNeg(kvlangFrame_t *f),
    kvlangBuiltinAbs(kvlangFrame_t *f), kvlangBuiltinSign(kvlangFrame_t *f),
    kvlangBuiltinPow(kvlangFrame_t *f), kvlangBuiltinMax(kvlangFrame_t *f),
    kvlangBuiltinMin(kvlangFrame_t *f),
    kvlangBuiltinCastBool(kvlangFrame_t *f), kvlangBuiltinCastInt8(kvlangFrame_t *f),
    kvlangBuiltinCastInt16(kvlangFrame_t *f), kvlangBuiltinCastInt32(kvlangFrame_t *f),
    kvlangBuiltinCastInt64(kvlangFrame_t *f), kvlangBuiltinCastUint8(kvlangFrame_t *f),
    kvlangBuiltinCastUint16(kvlangFrame_t *f), kvlangBuiltinCastUint32(kvlangFrame_t *f),
    kvlangBuiltinCastUint64(kvlangFrame_t *f), kvlangBuiltinCastF32(kvlangFrame_t *f),
    kvlangBuiltinCastF64(kvlangFrame_t *f), kvlangBuiltinCastChar32(kvlangFrame_t *f),
    kvlangBuiltinCastChar8(kvlangFrame_t *f), kvlangBuiltinCastCharAscii(kvlangFrame_t *f);
/* 比较 / 逻辑（rwir_bool.c） */
int kvlangBuiltinEq(kvlangFrame_t *f), kvlangBuiltinNeq(kvlangFrame_t *f),
    kvlangBuiltinLt(kvlangFrame_t *f), kvlangBuiltinGt(kvlangFrame_t *f),
    kvlangBuiltinLe(kvlangFrame_t *f), kvlangBuiltinGe(kvlangFrame_t *f),
    kvlangBuiltinAnd(kvlangFrame_t *f), kvlangBuiltinOr(kvlangFrame_t *f),
    kvlangBuiltinNot(kvlangFrame_t *f);
int kvlangBuiltinArray(kvlangFrame_t *f),
    kvlangBuiltinArrayFill(kvlangFrame_t *f),
    kvlangBuiltinNdarrayNumel(kvlangFrame_t *f),
    kvlangBuiltinNdarrayDim(kvlangFrame_t *f),
    kvlangBuiltinNdarrayShape(kvlangFrame_t *f),
    kvlangBuiltinXvAt(kvlangFrame_t *f), kvlangBuiltinXvSet(kvlangFrame_t *f),
    kvlangBuiltinXvReshape(kvlangFrame_t *f),
    kvlangBuiltinXvReinterpret(kvlangFrame_t *f),
    kvlangBuiltinXvLangtype(kvlangFrame_t *f),
    kvlangBuiltinXvBodylen(kvlangFrame_t *f),
    kvlangBuiltinXvParselangtype(kvlangFrame_t *f),
    kvlangBuiltinScatter(kvlangFrame_t *f),
    kvlangBuiltinCompact(kvlangFrame_t *f),
    kvlangBuiltinAppend(kvlangFrame_t *f), kvlangBuiltinSlice(kvlangFrame_t *f),
    kvlangBuiltinObj(kvlangFrame_t *f), kvlangBuiltinMap(kvlangFrame_t *f),
    kvlangBuiltinStructNew(kvlangFrame_t *f),
    kvlangBuiltinStringSet(kvlangFrame_t *f),
    kvlangBuiltinStringChar(kvlangFrame_t *f),
    kvlangBuiltinStringOrd(kvlangFrame_t *f),
    kvlangBuiltinStringCmp(kvlangFrame_t *f),
    kvlangBuiltinStringFind(kvlangFrame_t *f),
    kvlangBuiltinStringLen(kvlangFrame_t *f),
    kvlangBuiltinStringSlice(kvlangFrame_t *f),
    kvlangBuiltinStringConcat(kvlangFrame_t *f),
    kvlangBuiltinStringFormatInt(kvlangFrame_t *f),
    kvlangBuiltinStringFormatUint(kvlangFrame_t *f),
    kvlangBuiltinStringParseInt(kvlangFrame_t *f),
    kvlangBuiltinStringParseUint(kvlangFrame_t *f),
    kvlangBuiltinStringParseFloat(kvlangFrame_t *f),
    kvlangBuiltinTimeNow(kvlangFrame_t *f),
    kvlangBuiltinTimeSub(kvlangFrame_t *f),
    kvlangBuiltinTimeAdd(kvlangFrame_t *f),
    kvlangBuiltinDurFrom(kvlangFrame_t *f),
    kvlangBuiltinDurTo(kvlangFrame_t *f),
    kvlangBuiltinDurArith(kvlangFrame_t *f),
    kvlangBuiltinDurCmp(kvlangFrame_t *f),
    kvlangBuiltinTimeCmp(kvlangFrame_t *f),
    kvlangBuiltinRandUint64(kvlangFrame_t *f),
    kvlangBuiltinRandInt63(kvlangFrame_t *f),
    kvlangBuiltinRandIntn(kvlangFrame_t *f),
    kvlangCGet(kvlangFrame_t *f), kvlangCSet(kvlangFrame_t *f),
    kvlangCDel(kvlangFrame_t *f),
    kvlangCDelTree(kvlangFrame_t *f),
    kvlangCCp(kvlangFrame_t *f),
    kvlangCCpTree(kvlangFrame_t *f),
    kvlangCCpList(kvlangFrame_t *f),
    kvlangCList(kvlangFrame_t *f),
    kvlangCListLen(kvlangFrame_t *f),
    kvlangCListN(kvlangFrame_t *f),
    kvlangCMkindex(kvlangFrame_t *f),
    kvlangCExtIndex(kvlangFrame_t *f),
    kvlangCRmIndexExt(kvlangFrame_t *f),
    kvlangCWatch(kvlangFrame_t *f),
    kvlangCAbs(kvlangFrame_t *f),
    kvlangBuiltinDebugger(kvlangFrame_t *f),
    kvlangBuiltinVthreadCreate(kvlangFrame_t *f),
    kvlangBuiltinVthreadRun(kvlangFrame_t *f),
    kvlangBuiltinVthreadCall(kvlangFrame_t *f),
    kvlangBuiltinVthreadSleep(kvlangFrame_t *f),
    kvlangBuiltinVthreadSetstatus(kvlangFrame_t *f);
