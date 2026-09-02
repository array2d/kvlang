#pragma once
#include "runtime_internal.h"

/* ── 跨模块共享 helper ─────────────────────────────────────────────
   frame I/O 与 xvalue_at 定义在 builtin.c；容器 key/index helper 定义在 builtin_coll.c。*/
int  kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap);
void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n);
void kvlangBuiltinNextPc(kvlangFrame_t *f);
int  kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result);
int  kvlangBuiltinSetErr(kvlangFrame_t *f, const char *fmt, ...);
void kvlangBuiltinXvalueAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out);
char *kvlangBuiltinScatterKey(const char *base, const int64_t *coords, int ncoord);
void kvlangBuiltinMemindex(kvlangXvalue_t *out, const char *const *names, int n);
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const int32_t *dims, int ndim);
/* char 拼接（+ 与 string·concat 共用）：均须 char kind；编码不同返 false（调用方 throw），相同则 out=拼接结果并保持该编码。 */
bool kvlangBuiltinCharConcat(const kvlangXvalue_t *a, const kvlangXvalue_t *b, kvlangXvalue_t *out);

/* ── 各 lib 的 rwir handler 原型（表在 builtin.c 引用）────────────── */
int kvlangBuiltinArray(kvlangFrame_t *f), kvlangBuiltinNdarrayNumel(kvlangFrame_t *f), kvlangBuiltinNdarrayDim(kvlangFrame_t *f), kvlangBuiltinNdarrayShape(kvlangFrame_t *f),
    kvlangBuiltinXvAt(kvlangFrame_t *f), kvlangBuiltinXvSet(kvlangFrame_t *f), kvlangBuiltinXvReshape(kvlangFrame_t *f), kvlangBuiltinXvReinterpret(kvlangFrame_t *f),
    kvlangBuiltinXvKindexpr(kvlangFrame_t *f), kvlangBuiltinXvBodylen(kvlangFrame_t *f),
    kvlangBuiltinScatter(kvlangFrame_t *f), kvlangBuiltinCompact(kvlangFrame_t *f),
    kvlangBuiltinAppend(kvlangFrame_t *f), kvlangBuiltinSlice(kvlangFrame_t *f), kvlangBuiltinObj(kvlangFrame_t *f), kvlangBuiltinMap(kvlangFrame_t *f), kvlangBuiltinStringSet(kvlangFrame_t *f),
    kvlangBuiltinStringChar(kvlangFrame_t *f), kvlangBuiltinStringOrd(kvlangFrame_t *f), kvlangBuiltinStringCmp(kvlangFrame_t *f),
    kvlangBuiltinStringFind(kvlangFrame_t *f), kvlangBuiltinStringLen(kvlangFrame_t *f), kvlangBuiltinStringSlice(kvlangFrame_t *f),
    kvlangBuiltinStringConcat(kvlangFrame_t *f),
    kvlangBuiltinStringFormatInt(kvlangFrame_t *f), kvlangBuiltinStringFormatUint(kvlangFrame_t *f),
    kvlangBuiltinStringParseInt(kvlangFrame_t *f), kvlangBuiltinStringParseUint(kvlangFrame_t *f),
    kvlangBuiltinTimeNow(kvlangFrame_t *f), kvlangBuiltinTimeSub(kvlangFrame_t *f),
    kvlangBuiltinTimeAdd(kvlangFrame_t *f), kvlangBuiltinDurFrom(kvlangFrame_t *f), kvlangBuiltinDurTo(kvlangFrame_t *f),
    kvlangBuiltinDurArith(kvlangFrame_t *f), kvlangBuiltinDurCmp(kvlangFrame_t *f), kvlangBuiltinTimeCmp(kvlangFrame_t *f),
    kvlangBuiltinRandUint64(kvlangFrame_t *f), kvlangBuiltinRandInt63(kvlangFrame_t *f), kvlangBuiltinRandIntn(kvlangFrame_t *f),
    kvlangBuiltinKvGet(kvlangFrame_t *f), kvlangBuiltinKvSet(kvlangFrame_t *f), kvlangBuiltinKvDel(kvlangFrame_t *f),
    kvlangBuiltinKvDelTree(kvlangFrame_t *f), kvlangBuiltinKvList(kvlangFrame_t *f), kvlangBuiltinKvListLen(kvlangFrame_t *f), kvlangBuiltinKvListN(kvlangFrame_t *f), kvlangBuiltinKvMkindex(kvlangFrame_t *f),
    kvlangBuiltinKvExtIndex(kvlangFrame_t *f), kvlangBuiltinKvRmIndexExt(kvlangFrame_t *f), kvlangBuiltinKvWatch(kvlangFrame_t *f),
    kvlangBuiltinDebugger(kvlangFrame_t *f), kvlangBuiltinVthreadCreate(kvlangFrame_t *f),
    kvlangBuiltinVthreadRun(kvlangFrame_t *f),
    kvlangBuiltinVthreadCall(kvlangFrame_t *f), kvlangBuiltinVthreadSleep(kvlangFrame_t *f),
    kvlangBuiltinVthreadSetstatus(kvlangFrame_t *f);
