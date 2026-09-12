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

/* 三正交轴 head（逐字段对齐 kvspace/include/kvspace/kvspace.h 的 kvspaceHead_t）。
 * langtype 即该 ABI 的 langtype 槽（本 runtime 内部沿用 langtype 命名）。 */
typedef struct {
    uint16_t headlen;      /* head 总字节数；body 起于偏移 headlen */
    uint8_t  ref;          /* 存储位置：见 KVSPACE_REF_* */
    uint8_t  storetype;    /* 物理布局：见 KVSPACE_STORETYPE_* */
    uint8_t  ro;           /* 1=只读，0=可写 */
    uint32_t vid;          /* vthread id */
    int32_t  body_len;     /* body 字节数 */
    int32_t  ndim;         /* ARRAYND：维数；index/extindex：3；NONE/ATOM：0 */
    int32_t  dims[8];      /* 各维长度 / [len,cap,M]（X_MAX_NDIM=8） */
    uint8_t  langtype[256];/* 语义类型 langtype 串，NUL 终止（含 [dims]、无 ref/ext 前缀） */
    int32_t  langtype_len; /* langtype 内容长度（去 padding） */
    int32_t  body_offset;  /* body 在 data 内的起始偏移（= headlen） */
} kvspaceHead_t;

#define KVSPACE_REF_INLINE 0
#define KVSPACE_REF_PTR    1
#define KVSPACE_REF_EXT    2

#define KVSPACE_STORETYPE_NONE     0
#define KVSPACE_STORETYPE_ATOM     1
#define KVSPACE_STORETYPE_ARRAYND  2
#define KVSPACE_STORETYPE_INDEX    3
#define KVSPACE_STORETYPE_EXTINDEX 4

extern void *kvspaceConnect(const char *dsn);
extern void  kvspaceClose(void *h);
/* 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。 */
extern int   kvspaceGet(void *h, const char *key, int resolve, uint8_t **out, uint32_t *out_len);

typedef struct {
    uint32_t block_id;
    uint32_t gen;
    uint32_t parent_id;
    uint32_t depth;
} kvspaceRef_t;
extern int kvspaceResolveRef(void *h, const char *key, kvspaceRef_t *ref) __attribute__((weak));
extern int kvspaceGetByRef(void *h, kvspaceRef_t *ref, const char *key_fallback,
                           uint8_t **out, uint32_t *out_len) __attribute__((weak));
extern int kvspaceSetPartByRef(void *h, kvspaceRef_t *ref, const char *key_fallback,
                               uint32_t offset, const uint8_t *buf, uint32_t buf_len,
                               char *err, uint32_t err_cap) __attribute__((weak));
/* 指令边界回收读借用池；定位读/写（分片）；只读 head 前缀。见 kvspace.h 契约。 */
extern void  kvspaceReadReset(void *h);
extern int   kvspaceGetPart(void *h, const char *key, uint32_t offset, uint32_t len, uint8_t **out, uint32_t *out_len);
extern int   kvspaceSetPart(void *h, const char *key, uint32_t offset, const uint8_t *buf, uint32_t buf_len, char *err, uint32_t err_cap);
extern int   kvspaceGetHead(void *h, const char *key, kvspaceHead_t *out);
/* 就地写：key 已存在、body_len==原 body_len → 返回原 box body 偏移指针；否则非 0 + err。 */
extern int   kvspaceWriteInPlace(void *h, const char *key, int resolve, uint32_t body_len,
                                  uint8_t **body, char *err, uint32_t err_cap);
/* 新位置写：按 (ref, storetype, ro, vid, langtype, body_len) 分配新 box、写 head，返回 body 偏移指针。 */
extern int   kvspaceWriteNewPlace(void *h, const char *key, uint8_t ref, uint8_t storetype,
                                   uint8_t ro, uint32_t vid, const char *langtype, uint32_t body_len,
                                   uint8_t **body, char *err, uint32_t err_cap);
/* 前缀遍历：listlen 定计数，逐 idx 取名（借用回收缓冲，不得 free），不一次性返回整段名单。 */
extern int   kvspaceListLen(void *h, const char *prefix, int expand_ext, int resolve, int32_t *out_count);
extern int   kvspaceListAt(void *h, const char *prefix, int expand_ext, int resolve, int32_t idx,
                            uint8_t *buf, uint32_t buf_cap, uint32_t *out_len);
extern int   kvspaceDel(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspaceDelTree(void *h, const char *prefix, char *err, uint32_t err_cap);
extern int   kvspaceCp(void *h, const char *src, const char *dst, char *err, uint32_t err_cap);
extern int   kvspaceCpTree(void *h, const char *src, const char *dst, char *err, uint32_t err_cap);
extern int   kvspaceCpList(void *h, const char *src, const char *dst, char *err, uint32_t err_cap);
extern int   kvspaceMkindex(void *h, const char *path, uint32_t capacity, char *err, uint32_t err_cap);
extern int   kvspaceMkindexExt(void *h, const char *path, const char *ext_path, char *err, uint32_t err_cap);
extern int   kvspaceRmindexExt(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspaceWatch(void *h, const char *key, const uint8_t *target, uint32_t target_len,
                           uint64_t tick_ns, uint8_t **out, uint32_t *out_len);
extern int   kvspaceTlvEncode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                                const int32_t *dims, int32_t ndim, uint8_t **out, uint32_t *out_len);
extern int   kvspaceDecodeHead(const uint8_t *data, uint32_t data_len, kvspaceHead_t *out);
extern int   kvspaceNewPtr(const char *target_langtype, const char *target,
                             uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewBool(uint8_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewInt64(int64_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewFloat64(double v, uint8_t **out, uint32_t *out_len);

/* ── 数值上限 ──────────────────────────────────────────────────────── */

#define MAX_PARAMS 128
#define MAX_STACK_DEPTH 256
#define X_MAX_NDIM 8

/* ── 派生 head：解析 langtype 得到（不落盘） ───────────────────────── */

typedef struct {
    const char *kind;   /* base kind（langtype 子串，非 NUL 终止） */
    int32_t     kind_len;
    int32_t     ndim;
    int32_t     dims[X_MAX_NDIM];
    int32_t     array_len;
} kvlangLangtype;

void kvlangLangtypeParse(const uint8_t *langtype, kvlangLangtype *out);

/* ── 基础类型 ──────────────────────────────────────────────────────── */

typedef struct { uint8_t *data; uint32_t len; uint8_t borrowed; } kvlangXvalue_t;

typedef struct { char *key; kvlangXvalue_t val; } kvlangKvPair_t;

#define KVLANG_REF_CAP 64
typedef struct { char *key; uint32_t block_id, gen; } kvlangRefEnt_t;
typedef struct {
    void *h;
    kvlangRefEnt_t ref[KVLANG_REF_CAP];
    int nref;
    int ref_on;
    /* Sticky ART dir ancestor of the last coord key (`…/[k]`). */
    char *pdir;
    size_t pdl;
    uint32_t pblock, pgen;
} kvlangKv_t;

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
static inline void kvlangXvalueZero(kvlangXvalue_t *v) { v->data = NULL; v->len = 0; v->borrowed = 0; }
void kvlangXvalueFree(kvlangXvalue_t *v);          /* free 自持 data（借用读已拷贝为自持） */
void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data, uint32_t len);  /* 接管内存 */
void kvlangXvalueMaterialize(kvlangXvalue_t *v);   /* 借用值落地为自持（存入跨指令结构前必调） */
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

/* langtypetable：base kind 串 ↔ int id（runtime 本地，IV-0，不入 kvspace）。
 * 枚举有序：数值家族连续 → 谓词即区间判定，int_width 由序号位移求得。 */
enum {
    KVLANG_LT_UNKNOWN = 0,
    KVLANG_LT_NONE,
    KVLANG_LT_BOOL,
    KVLANG_LT_INT8, KVLANG_LT_INT16, KVLANG_LT_INT32, KVLANG_LT_INT64,
    KVLANG_LT_UINT8, KVLANG_LT_UINT16, KVLANG_LT_UINT32, KVLANG_LT_UINT64,
    KVLANG_LT_FLOAT32, KVLANG_LT_FLOAT64,
    KVLANG_LT_CHAR_UTF32, KVLANG_LT_CHAR_UTF8, KVLANG_LT_CHAR_ASCII,
    KVLANG_LT_MAP, KVLANG_LT_INDEX, KVLANG_LT_EXTINDEX,
    KVLANG_LT_RWIR, KVLANG_LT_RWFUNC, KVLANG_LT_SCOPE, KVLANG_LT_STRUCT,
    KVLANG_LT_TIME, KVLANG_LT_DURATION,
    KVLANG_LT_COUNT
};
int kvlangLangTypeId(const char *s, size_t len);
const char *kvlangLangTypeKind(int id);
int kvlangXvalueLangTypeId(const kvlangXvalue_t *v);
static inline bool kvlangLtIsSint(int id) { return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64; }
static inline bool kvlangLtIsUint(int id) { return id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64; }
static inline bool kvlangLtIsInt(int id) { return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_UINT64; }
static inline bool kvlangLtIsFloat(int id) { return id == KVLANG_LT_FLOAT32 || id == KVLANG_LT_FLOAT64; }
static inline bool kvlangLtIsNum(int id) { return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_FLOAT64; }
static inline bool kvlangLtIsChar(int id) { return id >= KVLANG_LT_CHAR_UTF32 && id <= KVLANG_LT_CHAR_ASCII; }
static inline int kvlangLtIntWidth(int id) {
    if (id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64) return 8 << (id - KVLANG_LT_INT8);
    if (id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64) return 8 << (id - KVLANG_LT_UINT8);
    return 0;
}
static inline int kvlangLtElemSize(int id) {
    if (kvlangLtIsInt(id)) return kvlangLtIntWidth(id) / 8;
    if (id == KVLANG_LT_FLOAT32) return 4;
    if (id == KVLANG_LT_FLOAT64) return 8;
    if (id == KVLANG_LT_BOOL) return 1;
    if (id == KVLANG_LT_CHAR_UTF32) return 4;
    if (id == KVLANG_LT_CHAR_UTF8 || id == KVLANG_LT_CHAR_ASCII) return 1;
    if (id == KVLANG_LT_TIME || id == KVLANG_LT_DURATION) return 8;
    return 0;
}

/* 签名 langtype（runtime篇-07）校验/匹配 */
bool kvlangLangtypeValid(const char *expr);
bool kvlangLangtypeMatch(const char *expr, const char *kind, int32_t ndim, const int32_t *dims);
/* 标量 0copy 视图（取代 kvlangXvalueAsInt64 等按值转换）：decode head 一次，
 * 持 langtype id + 指向 body 首字节的借用指针，热路径按 id 直读 body。 */
typedef struct {
    int id;
    const uint8_t *body;
    int32_t len;
} kvlangScalar_t;
kvlangScalar_t kvlangXvalueScalar(const kvlangXvalue_t *v);
int64_t kvlangScalarReadI64(int id, const uint8_t *body);
double kvlangScalarReadF64(int id, const uint8_t *body);
uint64_t kvlangScalarReadU64(int id, const uint8_t *body);
static inline int64_t kvlangScalarI64(kvlangScalar_t s) { return kvlangScalarReadI64(s.id, s.body); }
static inline double kvlangScalarF64(kvlangScalar_t s) { return kvlangScalarReadF64(s.id, s.body); }
static inline uint64_t kvlangScalarU64(kvlangScalar_t s) { return kvlangScalarReadU64(s.id, s.body); }
uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx);
int32_t kvlangXvalueElemSize(const char *kind);

void kvlangXvalueNewInt64(kvlangXvalue_t *v, int64_t n);
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f);
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b);
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s);
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v, const char *s);  /* UTF-8 → UTF-32 LE body */
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind, const char *s);
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_langtype, const char *target);
void kvlangXvalueNewDefRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw, int dynamic);
void kvlangXvalueNewDefLangtype(kvlangXvalue_t *v, const char *langtype);
void kvlangXvalueNewTlv(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len, int32_t al);
void kvlangXvalueNewTlvDims(kvlangXvalue_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim);

void kvlangFormatFloat(char *out, size_t cap, double v);

/* ── KV 操作（封装 durable ABI）────────────────────────────────────── */

kvlangKv_t *kvlangKvConnect(const char *dsn);
void kvlangKvDisconnect(kvlangKv_t *k);
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out);   /* None → out len=0 */
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name, kvlangXvalue_t *out);
void kvlangKvReadReset(kvlangKv_t *k);   /* 指令边界回收读借用池 */
int kvlangKvGetPart(kvlangKv_t *k, const char *key, uint32_t off, uint32_t len, kvlangXvalue_t *out);  /* 借用读分片 body 字节 */
int kvlangKvSetPart(kvlangKv_t *k, const char *key, uint32_t off, const uint8_t *buf, uint32_t buf_len, char *err, uint32_t err_cap);  /* 就地写分片 */
int kvlangKvGetHead(kvlangKv_t *k, const char *key, kvspaceHead_t *out);   /* 只读 head，不取 body */
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap);
int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap);
int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err, uint32_t err_cap);
void kvlangKvInvalidateFrame(kvlangKv_t *k, const char *frame_root);
int kvlangKvCp(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap);
int kvlangKvCpTree(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap);
int kvlangKvCpList(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap);
int kvlangKvMkindex(kvlangKv_t *k, const char *path, uint32_t capacity, char *err, uint32_t err_cap);
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
#define OP_COPY   "="

/* op_id：decode 期一次固化的统一派发码（quickening），主循环据此纯整数跳表、热路径零 strcmp。
 * ≥0    = 在本 runtime myrwircaps（native 算子 + control/copy 均为其中一行），直查 myrwircaps[op_id].fn
 * -1    = 不在表内（执行期查 /lib：def rwir 路由头→路由，否则用户 rwfunc→调用） */
enum {
    OPID_notinmyrwircaps = -1,
};

int kvlangBuiltinCapIndex(const char *opcode);

/* decode 期分类：control/copy 与 native 同在 myrwircaps 一张表，全走 CapIndex；
 * miss 落 OPID_notinmyrwircaps（执行期再查 /lib）。 */
static inline int kvlangOpClassify(const char *op) {
    int n = kvlangBuiltinCapIndex(op);
    return n >= 0 ? n : OPID_notinmyrwircaps;
}

typedef struct { char *name; kvlangXvalue_t val; } kvlangParam_t;

typedef struct {
    char *opcode;
    int op_id;   /* 统一派发码，见上 enum；decode 期固化，永不随帧变化 */
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
/* notinmyrwircaps：opcode 是不在本 runtime myrwircaps 内、须经 def rwir 路由给能兑现它的
 * 其它 runtime 的 rwir。判据=读 kvspace /lib/<opcode> 存在 def rwir 路由头（能力唯一事实源）。 */
bool notinmyrwircaps(kvlangKv_t *kv, const char *opcode);

/* ── vthread ───────────────────────────────────────────────────────── */

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status);
void kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status);
void kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret);
void kvlangVthreadSetError(kvlangKv_t *kv, const char *vtid, const char *pc, const char *msg);

/* ── builtin ───────────────────────────────────────────────────────── */

/* yield_pc：native builtin 把「须交回上层驱动就地派发的 pc」写入 *yield_pc（否则留 NULL）。
 * 唯 vthread·run 的 return 模式用：驱动一个子 vthread 遇非本执行器 rwir 时，把其 pc 冒泡给驱动。 */
typedef struct { kvlangKv_t *kv; const char *vtid; const char *pc; kvlangRwirInst_t *inst; char **yield_pc; } kvlangFrame_t;

/* notinmycaps：查 myrwircaps table，opcode 不在本 runtime 能力表内 → true。 */
bool notinmycaps(const char *opcode);
bool kvlangBuiltinNumOp(const char *opcode);
int kvlangBuiltinNative(kvlangFrame_t *f);   /* dispatch + call，0 成功 */
int kvlangBuiltinExecuteCopy(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst);
/* control 算子：与 native 同居 myrwircaps 一张表，frame 签名统一派发（call/return/goto/br）。 */
int kvlangCtlCall(kvlangFrame_t *f);
int kvlangCtlReturn(kvlangFrame_t *f);
int kvlangCtlGoto(kvlangFrame_t *f);
int kvlangCtlBr(kvlangFrame_t *f);
void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_root, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out);
char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_root, const char *name);
char *kvlangBuiltinResolveReadKey(kvlangKv_t *kv, const char *frame_root, const char *name, const kvlangXvalue_t *val);
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
