#pragma once
#include "const.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Match kvspaceHead_t in the KVSpace C ABI. */
typedef struct {
    uint16_t headlen;
    uint8_t ref;
    uint8_t storetype;
    uint8_t ro;
    uint32_t vid;
    int32_t body_len;
    int32_t ndim;
    int32_t dims[8];
    uint8_t langtype[256];
    int32_t langtype_len;
    int32_t body_offset;
    uint64_t body_cap;
} kvspaceHead_t;

#define KVSPACE_REF_INLINE 0
#define KVSPACE_REF_PTR 1
#define KVSPACE_REF_EXT 2

extern void *kvspaceConnect(const char *dsn);
extern void kvspaceClose(void *h);
/* 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。 */
extern int kvspaceGet(void *h, const char *key, int resolve, uint8_t **out,
                      uint32_t *out_len);
typedef struct {
    uint32_t block_id, gen, parent_id, depth;
} kvspaceRef_t;
typedef struct {
    kvspaceRef_t ref;
    bool attempted, resolved;
} kvlangKvRef_t;
extern int kvspaceResolveRef(void *h, const char *key, kvspaceRef_t *ref);
extern int kvspaceGetByRef(void *h, kvspaceRef_t *ref, const char *key,
                          uint8_t **out, uint32_t *out_len);
extern int kvspaceSetValue(void *h, const char *key, const uint8_t *value,
                           uint32_t value_len, uint8_t ro, uint32_t vid,
                           char *err, uint32_t err_cap);
extern int kvspaceSetValueByRef(void *h, kvspaceRef_t *ref, const char *key,
                                const uint8_t *value, uint32_t value_len,
                                uint8_t ro, uint32_t vid, char *err,
                                uint32_t err_cap)
#ifdef __APPLE__
    __attribute__((weak_import));
#else
    __attribute__((weak));
#endif

/* 指令边界回收读借用池；定位读/写（分片）；只读 head 前缀。见 kvspace.h 契约。 */
extern void kvspaceReadReset(void *h);
extern int kvspaceGetPart(void *h, const char *key, uint32_t offset,
                          uint32_t len, uint8_t **out, uint32_t *out_len);
extern int kvspaceSetPart(void *h, const char *key, uint32_t offset,
                          const uint8_t *buf, uint32_t buf_len, char *err,
                          uint32_t err_cap);
extern int kvspaceGetHead(void *h, const char *key, kvspaceHead_t *out);
/* 前缀遍历：listlen 定计数，逐 idx 取名（借用回收缓冲，不得 free），不一次性返回整段名单。 */
extern int kvspaceListLen(void *h, const char *prefix, int expand_ext,
                          int resolve, int32_t *out_count);
extern int kvspaceListAt(void *h, const char *prefix, int expand_ext,
                         int resolve, int32_t idx, uint8_t *buf,
                         uint32_t buf_cap, uint32_t *out_len);
extern int kvspaceDel(void *h, const char *const *keys, uint32_t nkeys,
                      char *err, uint32_t err_cap);
extern int kvspaceDelTree(void *h, const char *prefix, char *err,
                          uint32_t err_cap);
extern int kvspaceCp(void *h, const char *src, const char *dst, char *err,
                     uint32_t err_cap);
extern int kvspaceCpTree(void *h, const char *src, const char *dst, char *err,
                         uint32_t err_cap);
extern int kvspaceCpList(void *h, const char *src, const char *dst, char *err,
                         uint32_t err_cap);
extern int kvspaceMkindex(void *h, const char *path, uint32_t capacity,
                          char *err, uint32_t err_cap);
extern int kvspaceMkindexExt(void *h, const char *path, const char *ext_path,
                             char *err, uint32_t err_cap);
extern int kvspaceRmindexExt(void *h, const char *path, char *err,
                             uint32_t err_cap);
extern int kvspaceWatch(void *h, const char *key, const uint8_t *target,
                        uint32_t target_len, uint64_t tick_ns, uint8_t **out,
                        uint32_t *out_len);
extern int kvspaceTlvEncode(const char *kind, const uint8_t *raw,
                            uint32_t raw_len, const int32_t *dims, int32_t ndim,
                            uint8_t **out, uint32_t *out_len);
extern int kvspaceDecodeHead(const uint8_t *data, uint32_t data_len,
                             kvspaceHead_t *out);
extern int kvspaceNewPtr(const char *target_langtype, const char *target,
                         uint8_t **out, uint32_t *out_len);
extern int kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out,
                          uint32_t *out_len);
extern int kvspaceNewBool(uint8_t v, uint8_t **out, uint32_t *out_len);
extern int kvspaceNewInt64(int64_t v, uint8_t **out, uint32_t *out_len);
extern int kvspaceNewFloat64(double v, uint8_t **out, uint32_t *out_len);

/* ── 数值上限 ──────────────────────────────────────────────────────── */

#define MAX_PARAMS 128
#define MAX_STACK_DEPTH 256
#define X_MAX_NDIM 8

/* ── 派生 head：解析 langtype 得到（不落盘） ───────────────────────── */

typedef struct {
    const char *kind; /* base kind（langtype 子串，非 NUL 终止） */
    int32_t kind_len;
    int32_t ndim;
    int32_t dims[X_MAX_NDIM];
    int32_t array_len;
} kvlangLangtype;

void kvlangLangtypeParse(const uint8_t *langtype, int32_t langtype_len,
                         kvlangLangtype *out);

/* ── 基础类型 ──────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint8_t borrowed;
} kvlangXvalue_t;

typedef struct {
    char *key;
    kvlangXvalue_t val;
} kvlangKvPair_t;

typedef struct {
    void *h;
} kvlangKv_t;

/* growable string buffer */
typedef struct {
    char *p;
    size_t len, cap;
} kvlangStrbuf_t;

static inline void kvlangStrbufInit(kvlangStrbuf_t *b) {
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}
void kvlangStrbufPutc(kvlangStrbuf_t *b, char c);
void kvlangStrbufPutn(kvlangStrbuf_t *b, const char *s, size_t n);
static inline void kvlangStrbufPuts(kvlangStrbuf_t *b, const char *s) {
    kvlangStrbufPutn(b, s, strlen(s));
}
void kvlangStrbufPrintf(kvlangStrbuf_t *b, const char *fmt, ...);
char *kvlangStrbufDetach(kvlangStrbuf_t *b); /* malloc，调用方 free */
static inline void kvlangStrbufFree(kvlangStrbuf_t *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

/* ── xvalue head 零解码访问 ──────────────────────────────────────────
 * wire: [pow:u8][flags:u8][a:u64le][b:u64le][langtype][padding][body]
 * headlen = 1 << pow；body 起于 headlen；langtype 起于 18。
 * 不建 328B head 结构体、不拷贝 langtype、不校验——全部原位取。 */

#define XH_PREFIX 18u

static inline uint32_t xh_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static inline uint64_t xh_rd64(const uint8_t *p) {
    return (uint64_t)xh_rd32(p) | ((uint64_t)xh_rd32(p + 4) << 32);
}

static inline uint32_t xh_headlen(const uint8_t *d) { return 1u << d[0]; }
static inline const uint8_t *xh_body(const uint8_t *d) {
    return d + (1u << d[0]);
}
/* langtype 区起始（无 NUL 保证；长度见 xh_langtype_len）。 */
static inline const uint8_t *xh_langtype(const uint8_t *d) {
    return d + XH_PREFIX;
}
/* langtype 字节数（不含 NUL）。等于 headlen-18 表示填满、区内无 NUL。 */
static inline uint32_t xh_langtype_len(const uint8_t *d) {
    uint32_t region = xh_headlen(d) - XH_PREFIX;
    uint32_t n = 0;
    while (n < region && d[XH_PREFIX + n])
        n++;
    return n;
}
static inline uint8_t xh_class(const uint8_t *d) { return d[1] & 3u; }
static inline bool xh_is_ptr(const uint8_t *d) { return (d[1] & 4u) != 0; }
static inline bool xh_is_none(const uint8_t *d) {
    return d[1] == 0u && d[XH_PREFIX] == 0u;
}
static inline uint64_t xh_a(const uint8_t *d) { return xh_rd64(d + 2); }
static inline uint64_t xh_b(const uint8_t *d) { return xh_rd64(d + 10); }

/* ── XValue 操作 ───────────────────────────────────────────────────── */

static inline bool kvlangXvalueNone(const kvlangXvalue_t *v) {
    if (!v->data || v->len == 0)
        return true;
    /* None has a complete 32-byte scalar head and zero counts. */
    return v->len == 32 && v->data[0] == 5 && xh_is_none(v->data) &&
           xh_a(v->data) == 0 && xh_b(v->data) == 0;
}
static inline void kvlangXvalueZero(kvlangXvalue_t *v) {
    v->data = NULL;
    v->len = 0;
    v->borrowed = 0;
}

/* langtype 原位指针 + 字节数；None → 空串 / 0。指针无 NUL 保证，勿当 C 串用。 */
static inline const uint8_t *xh_langtype_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? (const uint8_t *)"" : xh_langtype(v->data);
}
static inline uint32_t xh_langtype_len_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? 0u : xh_langtype_len(v->data);
}
/* 有界拷进调用方缓冲并 NUL 终止，返回写入长度（不含 NUL）。 */
static inline uint32_t xh_langtype_copy_of(const kvlangXvalue_t *v, char *buf,
                                           size_t cap) {
    if (cap == 0)
        return 0;
    uint32_t n = xh_langtype_len_of(v);
    if (n > cap - 1)
        n = (uint32_t)(cap - 1);
    memcpy(buf, xh_langtype_of(v), n);
    buf[n] = 0;
    return n;
}
/* 有界拷出完整 langtype（malloc，调用方 free）。None → ""。 */
char *kvlangXvalueLangtypeDup(const kvlangXvalue_t *v);
void kvlangXvalueFree(
    kvlangXvalue_t *v); /* free 自持 data（借用读已拷贝为自持） */
void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data,
                          uint32_t len); /* 接管内存 */
void kvlangXvalueMaterialize(
    kvlangXvalue_t *v); /* 借用值落地为自持（存入跨指令结构前必调） */
int kvlangXvalueHead(const kvlangXvalue_t *v,
                     kvspaceHead_t *h);                /* decode head */
const char *kvlangXvalueKind(const kvlangXvalue_t *v); /* 返回 kind，None="" */
bool kvlangXvalueKindIs(const kvlangXvalue_t *v, const char *kind);
int kvlangXvalueLangtype(const kvlangXvalue_t *v, char *buf,
                         size_t cap); /* 完整 langtype → buf */
bool kvlangKindIsMap(
    const char *kind); /* stringkeymap 或 map langtype（`…·…`） */
bool kvlangXvalueIsPtr(const kvlangXvalue_t *v);
int32_t kvlangXvalueArrayLen(const kvlangXvalue_t *v);
const uint8_t *kvlangXvalueBody(const kvlangXvalue_t *v, const kvspaceHead_t *h,
                                int32_t *out_len);
char *kvlangXvaluePtrTarget(const kvlangXvalue_t *v); /* malloc */
char *kvlangXvalueValueString(
    const kvlangXvalue_t *v); /* malloc，对齐 Go ValueString */
char *kvlangXvalueSlotName(const kvlangXvalue_t *v); /* malloc，指令槽名 */
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
    KVLANG_LT_INT8,
    KVLANG_LT_INT16,
    KVLANG_LT_INT32,
    KVLANG_LT_INT64,
    KVLANG_LT_UINT8,
    KVLANG_LT_UINT16,
    KVLANG_LT_UINT32,
    KVLANG_LT_UINT64,
    KVLANG_LT_FLOAT32,
    KVLANG_LT_FLOAT64,
    KVLANG_LT_CHAR_UTF32,
    KVLANG_LT_CHAR_UTF8,
    KVLANG_LT_CHAR_ASCII,
    KVLANG_LT_MAP,
    KVLANG_LT_RWIR,
    KVLANG_LT_RWFUNC,
    KVLANG_LT_SCOPE,
    KVLANG_LT_STRUCT,
    KVLANG_LT_TIME,
    KVLANG_LT_DURATION,
    KVLANG_LT_COUNT
};
int kvlangLangTypeId(const char *s, size_t len);
const char *kvlangLangTypeKind(int id);
int kvlangXvalueLangTypeId(const kvlangXvalue_t *v);
static inline bool kvlangLtIsSint(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64;
}
static inline bool kvlangLtIsUint(int id) {
    return id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64;
}
static inline bool kvlangLtIsInt(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_UINT64;
}
static inline bool kvlangLtIsFloat(int id) {
    return id == KVLANG_LT_FLOAT32 || id == KVLANG_LT_FLOAT64;
}
static inline bool kvlangLtIsNum(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_FLOAT64;
}
static inline bool kvlangLtIsChar(int id) {
    return id >= KVLANG_LT_CHAR_UTF32 && id <= KVLANG_LT_CHAR_ASCII;
}
static inline int kvlangLtIntWidth(int id) {
    if (id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64)
        return 8 << (id - KVLANG_LT_INT8);
    if (id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64)
        return 8 << (id - KVLANG_LT_UINT8);
    return 0;
}
static inline int kvlangLtElemSize(int id) {
    if (kvlangLtIsInt(id))
        return kvlangLtIntWidth(id) / 8;
    if (id == KVLANG_LT_FLOAT32)
        return 4;
    if (id == KVLANG_LT_FLOAT64)
        return 8;
    if (id == KVLANG_LT_BOOL)
        return 1;
    if (id == KVLANG_LT_CHAR_UTF32)
        return 4;
    if (id == KVLANG_LT_CHAR_UTF8 || id == KVLANG_LT_CHAR_ASCII)
        return 1;
    if (id == KVLANG_LT_TIME || id == KVLANG_LT_DURATION)
        return 8;
    return 0;
}

/* 签名 langtype（runtime篇-07）校验/匹配 */
bool kvlangLangtypeValid(const char *expr);
bool kvlangLangtypeMatch(const char *expr, const char *kind, int32_t ndim,
                         const int32_t *dims);
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
static inline int64_t kvlangScalarI64(kvlangScalar_t s) {
    return kvlangScalarReadI64(s.id, s.body);
}
static inline double kvlangScalarF64(kvlangScalar_t s) {
    return kvlangScalarReadF64(s.id, s.body);
}
static inline uint64_t kvlangScalarU64(kvlangScalar_t s) {
    return kvlangScalarReadU64(s.id, s.body);
}
uint32_t kvlangXvalueChar32At(const kvlangXvalue_t *v, int32_t idx);
int32_t kvlangXvalueElemSize(const char *kind);

/* body 内容长度：按 storage class 原位推导（不读 head.body_len、不校验）。
 *   class0 短定长 → 标量宽度（def rwir 特例 5）；class1 slack / class3 ext → a；class2 tensor → a*b。 */
static inline int32_t xh_content_len(const uint8_t *d) {
    uint8_t cls = d[1] & 3u;
    if (cls == 0u) {
        uint32_t n = xh_langtype_len(d);
        if (n == sizeof(KVSPACE_KIND_DEF_RWIR) - 1 &&
            memcmp(d + XH_PREFIX, KVSPACE_KIND_DEF_RWIR, n) == 0)
            return 5;
        return kvlangLtElemSize(
            kvlangLangTypeId((const char *)(d + XH_PREFIX), n));
    }
    if (cls == 2u)
        return (int32_t)(xh_a(d) * xh_b(d));
    return (int32_t)xh_a(d);
}
static inline const uint8_t *xh_body_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? NULL : xh_body(v->data);
}
static inline int32_t xh_content_len_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? 0 : xh_content_len(v->data);
}

void kvlangXvalueNewInt64(kvlangXvalue_t *v, int64_t n);
void kvlangXvalueNewFloat64(kvlangXvalue_t *v, double f);
void kvlangXvalueNewBool(kvlangXvalue_t *v, bool b);
void kvlangXvalueNewCharUtf8(kvlangXvalue_t *v, const char *s);
void kvlangXvalueNewCharUtf32(kvlangXvalue_t *v,
                              const char *s); /* UTF-8 → UTF-32 LE body */
void kvlangXvalueNewCharKind(kvlangXvalue_t *v, const char *kind,
                             const char *s);
void kvlangXvalueNewPtr(kvlangXvalue_t *v, const char *target_langtype,
                        const char *target);
void kvlangXvalueNewDefRwir(kvlangXvalue_t *v, int32_t nr, int32_t nw,
                            int dynamic);
void kvlangXvalueNewDefLangtype(kvlangXvalue_t *v, const char *langtype);
void kvlangXvalueNewTlv(kvlangXvalue_t *v, const char *kind, const uint8_t *raw,
                        uint32_t raw_len, int32_t al);
void kvlangXvalueNewTlvDims(kvlangXvalue_t *v, const char *kind,
                            const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim);

void kvlangFormatFloat(char *out, size_t cap, double v);

/* ── KV 操作（封装 durable ABI）────────────────────────────────────── */

kvlangKv_t *kvlangKvConnect(const char *dsn);
int kvlangKvGetOneRef(kvlangKv_t *k, const char *key, kvlangKvRef_t *ref,
                      kvlangXvalue_t *out);
int kvlangKvSetOneRef(kvlangKv_t *k, const char *key, kvlangKvRef_t *ref,
                      const kvlangXvalue_t *value, char *err, uint32_t err_cap);
int kvlangKvSetCharRef(kvlangKv_t *k, const char *key, kvlangKvRef_t *ref,
                       const char *value);
void kvlangKvDisconnect(kvlangKv_t *k);
int kvlangKvGetOne(kvlangKv_t *k, const char *key,
                   kvlangXvalue_t *out); /* None → out len=0 */
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name,
                      kvlangXvalue_t *out);
void kvlangKvReadReset(kvlangKv_t *k); /* 指令边界回收读借用池 */
int kvlangKvGetPart(kvlangKv_t *k, const char *key, uint32_t off, uint32_t len,
                    kvlangXvalue_t *out); /* 借用读分片 body 字节 */
int kvlangKvSetPart(kvlangKv_t *k, const char *key, uint32_t off,
                    const uint8_t *buf, uint32_t buf_len, char *err,
                    uint32_t err_cap); /* 就地写分片 */
int kvlangKvGetHead(kvlangKv_t *k, const char *key,
                    kvspaceHead_t *out); /* 只读 head，不取 body */
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err,
                uint32_t err_cap);
int kvlangKvSetChar(kvlangKv_t *k, const char *key, const char *s);
int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap);
int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err,
                    uint32_t err_cap);
int kvlangKvCp(kvlangKv_t *k, const char *src, const char *dst, char *err,
               uint32_t err_cap);
int kvlangKvCpTree(kvlangKv_t *k, const char *src, const char *dst, char *err,
                   uint32_t err_cap);
int kvlangKvCpList(kvlangKv_t *k, const char *src, const char *dst, char *err,
                   uint32_t err_cap);
int kvlangKvMkindex(kvlangKv_t *k, const char *path, uint32_t capacity,
                    char *err, uint32_t err_cap);
int kvlangKvExtIndex(kvlangKv_t *k, const char *path, const char *ext,
                     char *err, uint32_t err_cap);
int kvlangKvDelExtIndex(kvlangKv_t *k, const char *path, char *err,
                        uint32_t err_cap);
int kvlangKvList(kvlangKv_t *k, const char *prefix, bool expand_ext,
                 bool resolve, char ***out_names,
                 int *out_count); /* split \n */
int kvlangKvWatch(kvlangKv_t *k, const char *key, const kvlangXvalue_t *target,
                  uint64_t tick_ns, kvlangXvalue_t *out);

/* ── keytree ───────────────────────────────────────────────────────── */

#define SEG_LIB RUNTIME_MEMBER_SEP "lib"
#define SEG_PC "pc"
#define SEG_STATUS "status"
#define SEG_CALLPC "callpc"
#define SEG_RETURNPC "returnpc"
#define SEG_RO "ro"
#define SEG_MSG "msg"
#define LIB_ROOT "/lib"
#define VTHREAD_ROOT "/vthread"

static inline void kvlangStrbufClear(kvlangStrbuf_t *b) {
    b->len = 0;
    if (b->p)
        b->p[0] = 0;
}

const char *kvlangKeytreeVtidFromPc(const char *pc,
                                    kvlangStrbuf_t *out); /* "" 无效 */
char *kvlangKeytreeStack(const char *root);               /* malloc */
size_t kvlangKeytreeStackBuf(const char *root, char *buf, size_t cap); /* 栈缓冲，返长度 */
char *kvlangKeytreeFrameRoot(const char *pc); /* malloc，无效 NULL */
size_t kvlangKeytreeFrameRootBuf(const char *pc, char *buf, size_t cap); /* 栈缓冲，返长度 */
char *kvlangKeytreeEntryPc(const char *root); /* malloc */
char *kvlangKeytreeFrameAt(const char *vtid, int depth); /* malloc */
int kvlangKeytreeFrameNum(const char *path); /* [d]; panics if invalid */
char *kvlangKeytreeIrseqPc(const char *frame_root, int irseq); /* malloc */
char *kvlangKeytreeMember(const char *base, const char *name); /* malloc */
char *kvlangKeytreeLibFunc(const char *pkg, const char *name); /* malloc */
char *kvlangKeytreeRwir(const char *opcode);                   /* malloc */
void kvlangKeytreeVthread(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadSlot(const char *vtid, const char *frame, int i, int j,
                              kvlangStrbuf_t *out);
void kvlangKeytreeVthreadPc(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadStatus(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeVthreadStatusMsg(const char *vtid, const char *status,
                                   kvlangStrbuf_t *out);
void kvlangKeytreeVthreadDebugger(const char *vtid, kvlangStrbuf_t *out);
void kvlangKeytreeFrameCallpc(const char *root, kvlangStrbuf_t *out);
void kvlangKeytreeFrameReturnpc(const char *root, kvlangStrbuf_t *out);
void kvlangKeytreeFrameRo(const char *root, kvlangStrbuf_t *out);
bool kvlangKeytreeIsEntryPc(const char *pc);

/* ── rwir ──────────────────────────────────────────────────────────── */

#define OP_CALL "call"
#define OP_RETURN "return"
#define OP_BR "br"
#define OP_GOTO "goto"
#define OP_ASSIGN "assign"
#define OP_COPY "="

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

typedef struct {
    char *name;
    char *type;
    int address;
    kvlangXvalue_t val;
    kvlangKvRef_t ref;
    uint8_t scratch[64];
} kvlangParam_t;

typedef struct {
    char *opcode;
    int op_id; /* 统一派发码，见上 enum；decode 期固化，永不随帧变化 */
    kvlangParam_t *reads;
    int nr;
    kvlangParam_t *writes;
    int nw;
} kvlangRwirInst_t;

int kvlangRwirNextPc(const char *pc, kvlangStrbuf_t *out);
size_t kvlangRwirNextPcBuf(const char *pc, char *buf, size_t cap); /* 栈缓冲，返长度 */
int kvlangRwirExtractAddr0(const char *coord);
int kvlangRwirDecode(kvlangKv_t *kv, const char *link_base, const char *pc,
                     kvlangRwirInst_t *out, char *err, uint32_t err_cap);
void kvlangRwirInstFree(kvlangRwirInst_t *inst);
/* 外部扩展 handoff：写共享队列 /lib/<opcode>/vids/<vid>=pc，阻塞 watch 该 key 直至变 None
 * （外部执行器认领、驱动、置 nextpc 后删除该条目 → 本端解除阻塞）。 */
int handoff_external_rwir(kvlangKv_t *kv, const char *vtid, const char *pc,
                          kvlangRwirInst_t *inst);
/* notinmyrwircaps：opcode 是不在本 runtime myrwircaps 内、须经 def rwir 路由给能兑现它的
 * 其它 runtime 的 rwir。判据=读 kvspace /lib/<opcode> 存在 def rwir 路由头（能力唯一事实源）。 */
bool notinmyrwircaps(kvlangKv_t *kv, const char *opcode);

/* ── vthread ───────────────────────────────────────────────────────── */

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc,
                      char **status);
void kvlangVthreadPcGet(kvlangKv_t *kv, const char *vtid, char **pc);
void kvlangVthreadStatusGet(kvlangKv_t *kv, const char *vtid, char **status);
/* 复用调用方 strbuf 读成员值；false = 键缺失 / None。 */
bool kvlangVthreadMemberGetBuf(kvlangKv_t *kv, const char *key,
                               kvlangKvRef_t *ref, kvlangStrbuf_t *out);
int kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc,
                     const char *status);
int kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret);
void kvlangVthreadSetError(kvlangKv_t *kv, const char *vtid, const char *pc,
                           const char *msg);

/* ── builtin ───────────────────────────────────────────────────────── */

/* yield_pc returns a child PC to the external runtime. */
typedef struct {
    kvlangKv_t *kv;
    const char *vtid;
    const char *pc;
    kvlangRwirInst_t *inst;
    char **yield_pc;
    const char *frame_root;   /* Borrowed for this instruction. */
    const char *status_known; /* 本步开始前从 kvspace 读到的 ‥status（借用）；NULL = 未知 */
    bool persist_failed;
    const char *pc_key;
    const char *status_key;
    const char *next_pc;
    kvlangKvRef_t *pc_ref;
    bool cached_targets;
} kvlangFrame_t;

/* Write PC to kvspace; write status when it changes. */
void kvlangVthreadAdvance(kvlangFrame_t *f, const char *pc, const char *status);

/* notinmycaps：查 myrwircaps table，opcode 不在本 runtime 能力表内 → true。 */
bool notinmycaps(const char *opcode);
bool kvlangBuiltinNumOp(const char *opcode);
int kvlangBuiltinNative(kvlangFrame_t *f); /* dispatch + call，0 成功 */
int kvlangBuiltinExecuteCopy(kvlangFrame_t *f);
/* control 算子：与 native 同居 myrwircaps 一张表，frame 签名统一派发（call/return/goto/br）。 */
int kvlangCtlCall(kvlangFrame_t *f);
int kvlangCtlReturn(kvlangFrame_t *f);
int kvlangCtlGoto(kvlangFrame_t *f);
int kvlangCtlBr(kvlangFrame_t *f);
void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_root,
                                   const char *name, const kvlangXvalue_t *val,
                                   kvlangXvalue_t *out);
char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_root,
                                    const char *name);
/* 成员写的 base 尚无值 → 落空 stringkeymap 值（`/lib` 下跳过，见 rwir_kv.c）。 */
/* 成员写（memitem）前置条件：memhead（base 容器值）必须已存在；缺则返回 -1 拒绝写入。 */
int kvlangBuiltinCheckMemhead(kvlangKv_t *kv, const char *frame_root,
                              const char *base);
char *kvlangBuiltinResolveReadKey(kvlangKv_t *kv, const char *frame_root,
                                  const char *name, const kvlangXvalue_t *val);
bool kvlangBuiltinTryParseNumber(const char *s,
                                 kvlangXvalue_t *out); /* 成功 out 接管 */
void kvlangDisplay(const kvlangXvalue_t *v,
                   char **out); /* malloc，对齐 Go Display */

/* ── kvcpu ─────────────────────────────────────────────────────────── */

/* 两种执行模式（详见 runtime篇-05）：
 *   KVMODE_WATCH   模式1：runtime 主导，遇 ext rwir → handoff(vids) + watch(vids/<vid>==None) 阻塞
 *   KVMODE_RETURN  模式2：扩展主导，遇 ext rwir → 不 handoff 不 watch，返回该 ext rwir 的 PC（单线程函数调用）
 * kvlangKvcpuExecuteMode 返回值：-1 错误；0 正常结束(done)；1 遇 ext rwir（仅 KVMODE_RETURN，*out_pc=其 PC）。 */
typedef enum { KVMODE_WATCH = 0, KVMODE_RETURN = 1 } kvmode_t;

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc);
int kvlangKvcpuExecute(kvlangKv_t *kv,
                       const char *pc); /* = KVMODE_WATCH，out_pc 忽略 */
char *kvlangKvcpuBootstrap(kvlangKv_t *kv, const char *vtid,
                           const char *funcname, const char *const *args,
                           int nargs);
int kvlangKvcpuDynCall(kvlangKv_t *kv, const char *vtid, const char *pc,
                       const char *funckey);
/* 创建 vthread（不运行），返回 vid（free）。运行由 RunVid 承接。vthread·create 用。 */
char *kvlangVthreadSpawn(kvlangKv_t *kv, const char *funcname,
                         const char *const *args, int nargs);
/* 按 vid 从持久化 pc 跑到结束（KVMODE_WATCH），返回终态/错误。vthread·run 用。 */
int kvlangRuntimeRunVid(kvlangKv_t *kv, const char *vid, char **ret, char *err,
                        uint32_t err_cap);
/* run funcname 到结束（Spawn + RunVid），返回终态/错误。 */
int kvlangRuntimeExecuteKv(kvlangKv_t *kv, const char *funcname,
                           const char *const *args, int nargs, char **ret,
                           char *err, uint32_t err_cap);

/* ── logx ──────────────────────────────────────────────────────────── */

void kvlangLogDebug(const char *fmt, ...);
void kvlangLogInfo(const char *fmt, ...);
void kvlangLogError(const char *fmt, ...);
