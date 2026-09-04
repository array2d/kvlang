#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "const.h"

/* ── kvspace-durable C ABI ─────────────────────────────────────────── */

typedef struct {
    uint8_t kindexpr[256]; /* NUL 终止（含 ref 前缀与 [dims]，去 padding） */
    uint8_t ro;            /* 1=只读，0=可写 */
    uint32_t vid;          /* vthread id */
    int32_t body_len;      /* body 字节数 */
    int32_t body_offset;   /* body 在 data 内的起始偏移（= head_len） */
} kvspaceHead_t;

extern void *kvspaceConnect(const char *dsn);
extern void  kvspaceClose(void *h);
extern void  kvspaceBytesFree(uint8_t *p, uint32_t len);
extern int   kvspaceSet(void *h, const char *const *keys, const uint8_t *vals,
                         const uint32_t *lens, uint32_t n, char *err, uint32_t err_cap);
extern int   kvspaceGet(void *h, const char *key, uint8_t **out, uint32_t *out_len);
extern int   kvspaceGetBatch(void *h, const char *prefix, const char *const *names,
                               uint32_t nnames, uint8_t **out, uint32_t *out_len);
extern int   kvspaceList(void *h, const char *prefix, int expand_ext, int resolve,
                          uint8_t **out, uint32_t *out_len);
extern int   kvspaceDel(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspaceDelTree(void *h, const char *prefix, char *err, uint32_t err_cap);
extern int   kvspaceMkindex(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspaceMkindexExt(void *h, const char *path, const char *ext_path, char *err, uint32_t err_cap);
extern int   kvspaceRmindexExt(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspaceWatch(void *h, const char *key, const uint8_t *target, uint32_t target_len,
                           uint64_t tick_ns, uint8_t **out, uint32_t *out_len);
extern int   kvspaceTlvEncode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                                const int32_t *dims, int32_t ndim, uint8_t **out, uint32_t *out_len);
extern int   kvspaceDecodeHead(const uint8_t *data, uint32_t data_len, kvspaceHead_t *out);
extern int   kvspaceNewPtr(const char *target_kindexpr, const char *target,
                             uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewBool(uint8_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewInt64(int64_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewFloat64(double v, uint8_t **out, uint32_t *out_len);

/* ── 数值上限 ──────────────────────────────────────────────────────── */

#define MAX_PARAMS 128
#define MAX_STACK_DEPTH 256
#define X_MAX_NDIM 8

/* ── 派生 head：解析 kindexpr 得到（不落盘） ───────────────────────── */

typedef struct {
    const char *kind;   /* base kind（kindexpr 子串，非 NUL 终止） */
    int32_t     kind_len;
    int32_t     ref;    /* 0=内联 1=指针 2=扩展句柄 */
    int32_t     ndim;
    int32_t     dims[X_MAX_NDIM];
    int32_t     array_len;
} kvlang_kindexpr_t;

void kvlang_kindexpr_parse(const uint8_t *kindexpr, kvlang_kindexpr_t *out);

/* ── 基础类型 ──────────────────────────────────────────────────────── */

typedef struct { uint8_t *data; uint32_t len; } kvlangXvalue_t;

typedef struct { char *key; kvlangXvalue_t val; } kvlangKvPair_t;

typedef struct { void *h; } kvlangKv_t;

/* growable string buffer */
typedef struct { char *p; size_t len, cap; } kvlangStrbuf_t;

static inline void kvlangStrbufInit(kvlangStrbuf_t *b) { b->p = NULL; b->len = 0; b->cap = 0; }
void kvlangStrbufPutc(kvlangStrbuf_t *b, char c);
void kvlangStrbufPutn(kvlangStrbuf_t *b, const char *s, size_t n);
static inline void kvlangStrbufPuts(kvlangStrbuf_t *b, const char *s) { kvlangStrbufPutn(b, s, strlen(s)); }
void kvlangStrbufPrintf(kvlangStrbuf_t *b, const char *fmt, ...);
char *kvlangStrbufDetach(kvlangStrbuf_t *b);      /* malloc，调用方 free */
static inline void kvlangStrbufFree(kvlangStrbuf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

/* ── XValue 操作 ───────────────────────────────────────────────────── */

static inline bool kvlangXvalueNone(const kvlangXvalue_t *v) { return v->data == NULL || v->len == 0; }
static inline void kvlangXvalueZero(kvlangXvalue_t *v) { v->data = NULL; v->len = 0; }
void kvlangXvalueFree(kvlangXvalue_t *v);          /* kvspaceBytesFree */
void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data, uint32_t len);  /* 接管内存 */
int  kvlangXvalueHead(const kvlangXvalue_t *v, kvspaceHead_t *h);                 /* decode head */
const char *kvlangXvalueKind(const kvlangXvalue_t *v);                       /* 返回 kind，None="" */
bool kvlangXvalueKindIs(const kvlangXvalue_t *v, const char *kind);
bool kvlangXvalueIsPtr(const kvlangXvalue_t *v);
int32_t kvlangXvalueArrayLen(const kvlangXvalue_t *v);
const uint8_t *kvlangXvalueBody(const kvlangXvalue_t *v, const kvspaceHead_t *h, int32_t *out_len);
char *kvlangXvaluePtrTarget(const kvlangXvalue_t *v);                       /* malloc */
char *kvlangXvalueValueString(const kvlangXvalue_t *v);                     /* malloc，对齐 Go ValueString */
bool kvlangXvalueIsCharKind(const char *kind);
bool kvlangXvalueIsIntKind(const char *kind);
bool kvlangXvalueIsUintKind(const char *kind);
bool kvlangXvalueIsFloatKind(const char *kind);
bool kvlangXvalueIsNumKind(const char *kind);
/* 签名 kindexpr（runtime篇-07）校验/匹配 */
bool kvlang_rwirextKindexprValid(const char *expr);
bool kvlang_rwirextKindexprMatch(const char *expr, const char *kind, int32_t ndim, const int32_t *dims);
bool kvlang_rwirextKindexprVariadic(const char *expr);
int64_t kvlangXvalueAsInt64(const kvlangXvalue_t *v);
double  kvlangXvalueAsFloat64(const kvlangXvalue_t *v);
uint64_t kvlangXvalueAsUint64(const kvlangXvalue_t *v);
bool    kvlangXvalueAsBool(const kvlangXvalue_t *v);
uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx);
int32_t kvlangXvalueElemSize(const char *kind);

void kvlangXvalueNewInt64(kvlangXvalue_t *v, int64_t n);
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f);
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b);
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s);
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v, const char *s);  /* UTF-8 → UTF-32 LE body */
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind, const char *s);
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_kindexpr, const char *target);
void kvlangXvalueNewRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw, const char *sig);
void kvlangXvalueNewTlv(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len, int32_t al);
void kvlangXvalueNewTlvDims(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim);

void kvlangFormatFloat(char *out, size_t cap, double v);

/* ── KV 操作（封装 durable ABI）────────────────────────────────────── */

kvlangKv_t *kvlangKvConnect(const char *dsn);
void kvlangKvDisconnect(kvlangKv_t *k);
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out);   /* None → out len=0 */
int kvlangKvGetBatch(kvlangKv_t *k, const char *prefix, char **names, int n, kvlangXvalue_t *out);
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name, kvlangXvalue_t *out);
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap);
int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap);
int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err, uint32_t err_cap);
int kvlangKvMkindex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap);
int kvlangKvExtIndex(kvlangKv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap);
int kvlangKvDelExtIndex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap);
int kvlangKvList(kvlangKv_t *k, const char *prefix, bool expand_ext, bool resolve,
            char ***out_names, int *out_count);           /* split \n */
int kvlangKvWatch(kvlangKv_t *k, const char *key, const kvlangXvalue_t *target, uint64_t tick_ns, kvlangXvalue_t *out);

/* ── keytree ───────────────────────────────────────────────────────── */

#define SEG_LIB     RUNTIME_MEMBER_SEP "lib"
#define SEG_PC      "pc"
#define SEG_STATUS  "status"
#define SEG_CALLPC  "callpc"
#define SEG_RETURNPC "returnpc"
#define SEG_RO      "ro"
#define SEG_MSG     "msg"
#define LIB_ROOT    "/lib"
#define VTHREAD_ROOT "/vthread"

static inline void kvlangStrbufClear(kvlangStrbuf_t *b) { b->len = 0; if (b->p) b->p[0] = 0; }

const char *kvlangKeytreeVtidFromPc(const char *pc, kvlangStrbuf_t *out);   /* "" 无效 */
char *kvlangKeytreeStack(const char *root);                            /* malloc */
char *kvlangKeytreeFrameRoot(const char *pc);                         /* malloc，无效 NULL */
char *kvlangKeytreeEntryPc(const char *root);                         /* malloc */
char *kvlangKeytreeFrameAt(const char *vtid, int depth);             /* malloc */
int   kvlangKeytreeFrameNum(const char *path);                        /* [d]; panics if invalid */
char *kvlangKeytreeIrseqPc(const char *frame_root, int irseq);        /* malloc */
char *kvlangKeytreeMember(const char *base, const char *name);         /* malloc */
char *kvlangKeytreeLibFunc(const char *pkg, const char *name);        /* malloc */
char *kvlangKeytreeRwir(const char *opcode);                           /* malloc */
void kvlangKeytreeVthread(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadSlot(const char *vtid, const char *frame, int i, int j, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadPc(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadStatus(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadStatusMsg(const char *vtid, const char *status, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadDebugger(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeFrameCallpc(const char *root, kvlangStrbuf_t *out);
void kvlangKeytreeFrameReturnpc(const char *root, kvlangStrbuf_t *out);
void kvlangKeytreeFrameRo(const char *root, kvlangStrbuf_t *out);
bool kvlangKeytreeIsEntryPc(const char *pc);

/* ── rwir ──────────────────────────────────────────────────────────── */

#define OP_CALL   "call"
#define OP_RETURN "return"
#define OP_BR     "br"
#define OP_GOTO   "goto"
#define OP_ASSIGN "assign"

static inline bool op_is_control(const char *op) {
    return strcmp(op, OP_CALL) == 0 || strcmp(op, OP_RETURN) == 0 ||
           strcmp(op, OP_BR) == 0 || strcmp(op, OP_GOTO) == 0;
}

typedef struct { char *name; kvlangXvalue_t val; } kvlangParam_t;

typedef struct {
    char *opcode;
    kvlangParam_t *reads; int nr;
    kvlangParam_t *writes; int nw;
} kvlangRwirInst_t;

int kvlangRwirNextPc(const char *pc, kvlangStrbuf_t *out);
int kvlangRwirExtractAddr0(const char *coord);
int kvlangRwirDecode(kvlangKv_t *kv, const char *link_base, const char *pc, kvlangRwirInst_t *out, char *err, uint32_t err_cap);
void kvlangRwirInstFree(kvlangRwirInst_t *inst);
/* 外部扩展 handoff：写共享队列 /lib/<opcode>/vids/<vid>=pc，阻塞 watch 该 key 直至变 None
 * （外部执行器认领、驱动、置 nextpc 后删除该条目 → 本端解除阻塞）。 */
int handoff_external_rwir(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst);
/* opcode 是否他人 rwir：读 kvspace /lib/<opcode>，kind=defrwir 即是（能力唯一事实源）。 */
bool isothersrwir(kvlangKv_t *kv, const char *opcode);

/* ── vthread ───────────────────────────────────────────────────────── */

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status);
void kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status);
void kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret);
void kvlangVthreadSetError(kvlangKv_t *kv, const char *vtid, const char *pc, const char *msg);

/* ── builtin ───────────────────────────────────────────────────────── */

/* yield_pc：native builtin 把「须交回上层驱动就地派发的 pc」写入 *yield_pc（否则留 NULL）。
 * 唯 vthread·run 的 return 模式用：驱动一个子 vthread 遇非本执行器 rwir 时，把其 pc 冒泡给驱动。 */
typedef struct { kvlangKv_t *kv; const char *vtid; const char *pc; kvlangRwirInst_t *inst; char **yield_pc; } kvlangFrame_t;

bool kvlangBuiltinIsNative(const char *opcode);
bool kvlangBuiltinNumOp(const char *opcode);
int kvlangBuiltinNative(kvlangFrame_t *f);   /* dispatch + call，0 成功 */
int kvlangBuiltinExecuteCopy(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst);
void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_path, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out);
char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_path, const char *name);
char *kvlangBuiltinFuncFrameRoot(kvlangKv_t *kv, const char *frame_root);   /* malloc */
bool kvlangBuiltinTryParseNumber(const char *s, kvlangXvalue_t *out);          /* 成功 out 接管 */
void kvlangDisplay(const kvlangXvalue_t *v, char **out);                     /* malloc，对齐 Go Display */

/* ── kvcpu ─────────────────────────────────────────────────────────── */

/* 两种执行模式（详见 runtime篇-05）：
 *   KVMODE_WATCH   模式1：runtime 主导，遇 ext rwir → handoff(vids) + watch(vids/<vid>==None) 阻塞
 *   KVMODE_RETURN  模式2：扩展主导，遇 ext rwir → 不 handoff 不 watch，返回该 ext rwir 的 PC（单线程函数调用）
 * kvlangKvcpuExecuteMode 返回值：-1 错误；0 正常结束(done)；1 遇 ext rwir（仅 KVMODE_RETURN，*out_pc=其 PC）。 */
typedef enum { KVMODE_WATCH = 0, KVMODE_RETURN = 1 } kvmode_t;

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode, char **out_pc);
int kvlangKvcpuExecute(kvlangKv_t *kv, const char *pc);   /* = KVMODE_WATCH，out_pc 忽略 */
char *kvlangKvcpuBootstrap(kvlangKv_t *kv, const char *vtid, const char *funcname, const char *const *args, int nargs);
int kvlangKvcpuDynCall(kvlangKv_t *kv, const char *vtid, const char *pc, const char *funckey);
/* 创建 vthread（不运行），返回 vid（free）。运行由 RunVid 承接。vthread·create 用。 */
char *kvlangVthreadSpawn(kvlangKv_t *kv, const char *funcname, const char *const *args, int nargs);
/* 按 vid 从持久化 pc 跑到结束（KVMODE_WATCH），返回终态/错误。vthread·run 用。 */
int kvlangRuntimeRunVid(kvlangKv_t *kv, const char *vid, char **ret, char *err, uint32_t err_cap);
/* run funcname 到结束（Spawn + RunVid），返回终态/错误。 */
int kvlangRuntimeExecuteKv(kvlangKv_t *kv, const char *funcname, const char *const *args, int nargs, char **ret, char *err, uint32_t err_cap);

/* ── logx ──────────────────────────────────────────────────────────── */

void kvlangLogDebug(const char *fmt, ...);
void kvlangLogInfo(const char *fmt, ...);
void kvlangLogError(const char *fmt, ...);
