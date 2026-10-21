#pragma once
#include "runtime_internal.h"

/* 读参 head 的原位视图：变量（GetHead）路径解出 head；字面量路径直接持 wire 指针。
 * 视图字段不拷 langtype、不校验——调用方按 langtype/body_offset/body_len 直取。
 * 内嵌的 head 只作变量路径 kvspaceGetHead 的输出缓冲，字面量路径不使用它。 */
typedef struct {
    kvspaceHead_t head; /* 变量路径的数据源（其 langtype 为 langtype 宿主） */
    const uint8_t *langtype;
    int32_t langtype_len; /* langtype 字节数（不含 NUL），langtype 无 NUL 保证 */
    int32_t body_offset;
    int32_t body_len;
} xv_head_t;

static inline void xv_head_from_head(xv_head_t *o) {
    o->langtype = (const uint8_t *)o->head.langtype;
    o->langtype_len = o->head.langtype_len;
    o->body_offset = o->head.body_offset;
    o->body_len = o->head.body_len;
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
}

/* ── 跨模块共享 helper ─────────────────────────────────────────────
   frame I/O 与 xvalue_at 定义在 rwir_func.c；容器 key/index helper 定义在 rwir_array.c / rwir_map.c。*/
int kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap);
void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n);
void kvlangBuiltinNextPc(kvlangFrame_t *f);
int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result);
/* A1 原型：layout 特化 opcode 的 int64/float64 族 handler。 */
int kvlangBuiltinI64Add(kvlangFrame_t *f);
int kvlangBuiltinI64Sub(kvlangFrame_t *f);
int kvlangBuiltinI64Mul(kvlangFrame_t *f);
int kvlangBuiltinI64Div(kvlangFrame_t *f);
int kvlangBuiltinI64Mod(kvlangFrame_t *f);
int kvlangBuiltinI64Bitand(kvlangFrame_t *f);
int kvlangBuiltinI64Bitor(kvlangFrame_t *f);
int kvlangBuiltinI64Bitxor(kvlangFrame_t *f);
int kvlangBuiltinI64Shl(kvlangFrame_t *f);
int kvlangBuiltinI64Shr(kvlangFrame_t *f);
int kvlangBuiltinI64Max(kvlangFrame_t *f);
int kvlangBuiltinI64Min(kvlangFrame_t *f);
int kvlangBuiltinI64Neg(kvlangFrame_t *f);
int kvlangBuiltinI64Abs(kvlangFrame_t *f);
int kvlangBuiltinI64Sign(kvlangFrame_t *f);
int kvlangBuiltinI64Eq(kvlangFrame_t *f);
int kvlangBuiltinI64Neq(kvlangFrame_t *f);
int kvlangBuiltinI64Lt(kvlangFrame_t *f);
int kvlangBuiltinI64Le(kvlangFrame_t *f);
int kvlangBuiltinI64Gt(kvlangFrame_t *f);
int kvlangBuiltinI64Ge(kvlangFrame_t *f);
int kvlangBuiltinF64Add(kvlangFrame_t *f);
int kvlangBuiltinF64Sub(kvlangFrame_t *f);
int kvlangBuiltinF64Mul(kvlangFrame_t *f);
int kvlangBuiltinF64Div(kvlangFrame_t *f);
int kvlangBuiltinF64Max(kvlangFrame_t *f);
int kvlangBuiltinF64Min(kvlangFrame_t *f);
int kvlangBuiltinF64Neg(kvlangFrame_t *f);
int kvlangBuiltinF64Abs(kvlangFrame_t *f);
int kvlangBuiltinF64Sign(kvlangFrame_t *f);
int kvlangBuiltinF64Sqrt(kvlangFrame_t *f);
int kvlangBuiltinF64Exp(kvlangFrame_t *f);
int kvlangBuiltinF64Log(kvlangFrame_t *f);
int kvlangBuiltinF64Pow(kvlangFrame_t *f);
int kvlangBuiltinF64Eq(kvlangFrame_t *f);
int kvlangBuiltinF64Neq(kvlangFrame_t *f);
int kvlangBuiltinF64Lt(kvlangFrame_t *f);
int kvlangBuiltinF64Le(kvlangFrame_t *f);
int kvlangBuiltinF64Gt(kvlangFrame_t *f);
int kvlangBuiltinF64Ge(kvlangFrame_t *f);

/* 写结果槽并释放构造值（encode→write→free 的唯一入口）。 */
int kvlangBuiltinEmitResult(kvlangFrame_t *f, kvlangXvalue_t *v);
/* 标量结果：栈上编码 + 直接写，不经过堆。 */
int kvlangBuiltinEmitInt64(kvlangFrame_t *f, int64_t n);
int kvlangBuiltinEmitFloat64(kvlangFrame_t *f, double d);
int kvlangBuiltinEmitBool(kvlangFrame_t *f, bool b);
int kvlangBuiltinSetError(kvlangFrame_t *f, const char *fmt, ...);
void kvlangXvalueElementAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out);
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
int compare_int(kvlangScalar_t a, kvlangScalar_t b);
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
    kvlangBuiltinCastUint64(kvlangFrame_t *f), kvlangBuiltinCastFloat32(kvlangFrame_t *f),
    kvlangBuiltinCastFloat64(kvlangFrame_t *f), kvlangBuiltinCastCharUtf32(kvlangFrame_t *f),
    kvlangBuiltinCastCharUtf8(kvlangFrame_t *f), kvlangBuiltinCastCharAscii(kvlangFrame_t *f);
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
    kvlangBuiltinXvBodyLen(kvlangFrame_t *f),
    kvlangBuiltinXvParseLangtype(kvlangFrame_t *f),
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
    kvlangBuiltinDurationFrom(kvlangFrame_t *f),
    kvlangBuiltinDurationTo(kvlangFrame_t *f),
    kvlangBuiltinDurationArith(kvlangFrame_t *f),
    kvlangBuiltinDurCompare(kvlangFrame_t *f),
    kvlangBuiltinTimeCompare(kvlangFrame_t *f),
    kvlangBuiltinRandUint64(kvlangFrame_t *f),
    kvlangBuiltinRandInt63(kvlangFrame_t *f),
    kvlangBuiltinRandIntn(kvlangFrame_t *f),
    kvlangBuiltinKvspaceGet(kvlangFrame_t *f), kvlangBuiltinKvspaceSet(kvlangFrame_t *f),
    kvlangBuiltinKvspaceDelete(kvlangFrame_t *f),
    kvlangBuiltinKvspaceDeleteTree(kvlangFrame_t *f),
    kvlangBuiltinKvspaceCopy(kvlangFrame_t *f),
    kvlangBuiltinKvspaceCopyTree(kvlangFrame_t *f),
    kvlangBuiltinKvspaceCopyList(kvlangFrame_t *f),
    kvlangBuiltinKvspaceListLength(kvlangFrame_t *f),
    kvlangBuiltinKvspaceListAt(kvlangFrame_t *f),
    kvlangBuiltinKvspaceMakeIndex(kvlangFrame_t *f),
    kvlangBuiltinKvspaceExtensionIndex(kvlangFrame_t *f),
    kvlangBuiltinKvspaceRemoveIndexExtension(kvlangFrame_t *f),
    kvlangBuiltinKvspaceWatch(kvlangFrame_t *f),
    kvlangBuiltinKvlangAbs(kvlangFrame_t *f),
    kvlangBuiltinDebugger(kvlangFrame_t *f),
    kvlangBuiltinVthreadCreate(kvlangFrame_t *f),
    kvlangBuiltinVthreadRun(kvlangFrame_t *f),
    kvlangBuiltinVthreadCall(kvlangFrame_t *f),
    kvlangBuiltinVthreadSleep(kvlangFrame_t *f),
    kvlangBuiltinVthreadSetStatus(kvlangFrame_t *f);

/* Phase 2：整数族同型特化的 kind 列表（X-macro，handler 与能力表同源）。
 * 参数：函数名 token、langtype 字面量、宽度、是否无符号。 */
#define KVLANG_INT_BASE_KINDS(X)                                               \
    X(Int8, "int8", 1, 0, KVLANG_LT_INT8)                                                      \
    X(Int16, "int16", 2, 0, KVLANG_LT_INT16)                                                    \
    X(Int32, "int32", 4, 0, KVLANG_LT_INT32)                                                    \
    X(Uint8, "uint8", 1, 1, KVLANG_LT_UINT8)                                                    \
    X(Uint16, "uint16", 2, 1, KVLANG_LT_UINT16)                                                  \
    X(Uint32, "uint32", 4, 1, KVLANG_LT_UINT32)                                                  \
    X(Uint64, "uint64", 8, 1, KVLANG_LT_UINT64)

#define KVLANG_INT_SIGNED_KINDS(X)                                             \
    X(Int8, "int8", 1, 0, KVLANG_LT_INT8)                                                      \
    X(Int16, "int16", 2, 0, KVLANG_LT_INT16)                                                    \
    X(Int32, "int32", 4, 0, KVLANG_LT_INT32)

#define KVLANG_INT_U64_KINDS(X) X(Uint64, "uint64", 8, 1, KVLANG_LT_UINT64)

#define DECL_INT_BASE(ktok, kstr, w, U, kid)                                        \
    int kvlangBuiltin##ktok##Add(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Sub(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Mul(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Div(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Mod(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Max(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Min(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Eq(kvlangFrame_t *f);                             \
    int kvlangBuiltin##ktok##Neq(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Lt(kvlangFrame_t *f);                             \
    int kvlangBuiltin##ktok##Le(kvlangFrame_t *f);                             \
    int kvlangBuiltin##ktok##Gt(kvlangFrame_t *f);                             \
    int kvlangBuiltin##ktok##Ge(kvlangFrame_t *f);

#define DECL_INT_SIGNED(ktok, kstr, w, U, kid)                                      \
    int kvlangBuiltin##ktok##Neg(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Abs(kvlangFrame_t *f);

#define DECL_INT_U64(ktok, kstr, w, U, kid)                                         \
    int kvlangBuiltin##ktok##Bitand(kvlangFrame_t *f);                         \
    int kvlangBuiltin##ktok##Bitor(kvlangFrame_t *f);                          \
    int kvlangBuiltin##ktok##Bitxor(kvlangFrame_t *f);                         \
    int kvlangBuiltin##ktok##Shl(kvlangFrame_t *f);                            \
    int kvlangBuiltin##ktok##Shr(kvlangFrame_t *f);

KVLANG_INT_BASE_KINDS(DECL_INT_BASE)
KVLANG_INT_SIGNED_KINDS(DECL_INT_SIGNED)
KVLANG_INT_U64_KINDS(DECL_INT_U64)
