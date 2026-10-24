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
#define KVSPACE_STORETYPE_FIXED_SMALL 0

extern void *kvspaceConnect(const char *dsn);
extern void kvspaceClose(void *h);
/* 借用读：*out 指向后端常驻/回收空间，调用方不得 free。resolve=1 穿透 link。 */
extern int kvspaceGet(void *h, const char *key, int resolve, uint8_t **out,
                      uint32_t *out_len);
extern int kvspaceSetValue(void *h, const char *key, const uint8_t *value,
                           uint32_t value_len, uint8_t ro, uint32_t vid,
                           char *err, uint32_t err_cap);

/* key 的 64 位寻址结果（派生量，非真相）。见 kvspace.h 契约：
 * block_id=叶子、gen=0；GetByRef 遇到 ART 节点搬移会沿 MOVED 链更新 ref。 */
typedef struct {
    uint32_t block_id;
    uint32_t gen;
    uint32_t parent_id;
    uint32_t depth;
} kvspaceRef_t;

/* Match kvspaceSlotView_t in the KVSpace C ABI（shm-only）。 */
typedef struct {
    uint8_t *head;
    uint32_t headlen;
    uint8_t *body;
    uint64_t body_cap;
    uint64_t cap;
} kvspaceSlotView_t;
/* 0=成功；-1=不存在/不可视；-2=后端不支持。key_fallback 必填。 */
extern int kvspaceSlotView(void *h, const kvspaceRef_t *ref,
                           const char *key_fallback, kvspaceSlotView_t *out);

extern int kvspaceResolveRef(void *h, const char *key, kvspaceRef_t *ref);
extern int kvspaceGetByRef(void *h, kvspaceRef_t *ref, const char *key_fallback,
                           uint8_t **out, uint32_t *out_len);
/* 局部写 by cached ref（只写 [offset, offset+len)）；失败时调用方回退整值写。 */
extern int kvspaceSetPartByRef(void *h, kvspaceRef_t *ref,
                               const char *key_fallback, uint32_t offset,
                               const uint8_t *buf, uint32_t buf_len, char *err,
                               uint32_t err_cap);
/* 整值写 by cached ref；ref 失效时后端内部回退并重解析。 */
extern int kvspaceSetValueByRef(void *h, kvspaceRef_t *ref,
                                const char *key_fallback,
                                const uint8_t *value, uint32_t value_len,
                                uint8_t ro, uint32_t vid, char *err,
                                uint32_t err_cap);

/* 定位读/写（分片）；只读 head 前缀。见 kvspace.h 契约。 */
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
extern int kvspaceXvalueEncode(const char *kind, const uint8_t *raw,
                            uint32_t raw_len, const int32_t *dims, int32_t ndim,
                            uint8_t **out, uint32_t *out_len);
extern int kvspaceDecodeHead(const uint8_t *data, uint32_t data_len,
                             kvspaceHead_t *out);
extern int kvspaceNewPtr(const char *target_langtype, const char *target,
                         uint8_t **out, uint32_t *out_len);
extern int kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out,
                          uint32_t *out_len);
/* 正典 slack 编码（elem 1 = UTF-8），tests/test_scalar_encode.c 作对照。 */
extern int kvspaceXhNewSlack(int elem, const uint8_t *data, uint64_t data_len,
                             uint64_t cap, uint8_t **out, uint64_t *out_len);
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
    uint8_t *bytes; /* 视图：mmap / durable 常驻快照 / 行内字面量 / 显式 copy */
    uint32_t length;
} kvlangXvalue_t;

typedef struct {
    char *key;
    kvlangXvalue_t value;
} kvlangKvPair_t;

typedef struct {
    void *handle;    /* opaque KVSpace connection（数据 store） */
    bool shm;        /* DSN scheme == shm：允许 body 就地写快路径 */
    void *isa_handle; /* P1a 控制 store 句柄；NULL/同 handle 表示与数据 store 同库 */
    bool isa_shared; /* isa_handle == handle（redis：控制键共用数据 store） */
    bool slotview_unsupported; /* slot view 返回 -2：本句柄永久回退 */
} kvspace_t;

/* ── 字符串工具 ──────────────────────────────────────────────────────
 * 只有一次性操作：有界栈格式化 / 堆分配拷贝。没有可增长缓冲、没有复用语义。 */

#define KV_KEY_MAX 1024 /* kvspace 写键上限：key_len < 1024 */

void kvlangDie(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
/* 有界写入调用方缓冲；超长或格式化错误直接 abort，绝不截断。 */
size_t kvlangKeyFormat(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/* 一次性堆字符串（调用方 free）；OOM abort。 */
char *kvlangStringFormat(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
char *kvlangStringCopyN(const char *s, size_t n);

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
    if (!v->bytes || v->length == 0)
        return true;
    /* flags 无 class/ptr 位 且 langtype 首字节为 0 ⇒ langtype 长度 0 ⇒ None。 */
    return xh_is_none(v->bytes);
}
static inline void kvlangXvalueZero(kvlangXvalue_t *v) {
    v->bytes = NULL;
    v->length = 0;
}

/* langtype 原位指针 + 字节数；None → 空串 / 0。指针无 NUL 保证，勿当 C 串用。 */
static inline const uint8_t *xh_langtype_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? (const uint8_t *)"" : xh_langtype(v->bytes);
}
static inline uint32_t xh_langtype_len_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? 0u : xh_langtype_len(v->bytes);
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
void kvlangXvalueFree(kvlangXvalue_t *v);
void kvlangXvalueCopy(kvlangXvalue_t *v);
/* 标量 inline 编码到调用方缓冲：pow=5(headlen 32)、flags=0、a=b=0、
 * langtype@18、body@32。与正典 codec 的字节一致性由 ctest scalar_encode 守护。 */
/* 变长 char/utf8 的 SLACK 编码（pow=6、flags=1、a=len、b=cap、body@64）。
 * 固定 cap 让每步写同尺寸值，后端得以原地覆写；返回值 = 编码总长。 */
__attribute__((visibility("hidden"))) uint32_t
kvlangSlackEncodeChar(const uint8_t *raw, uint32_t len, uint32_t cap,
                      uint8_t *buf, uint32_t bufsize);

uint32_t kvlangScalarEncode(const char *kind, const uint8_t *raw, uint32_t len,
                            uint8_t *buf, uint32_t cap);
void kvlangXvalueSetBytes(kvlangXvalue_t *v, uint8_t *data,
                          uint32_t len); /* 视图赋值 */
int kvlangXvalueHead(const kvlangXvalue_t *v,
                     kvspaceHead_t *h); /* 完整 head；测试/外部用 */
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
int kvlangLangtypeId(const char *s, size_t len);
const char *kvlangLangtypeKind(int id);
int kvlangXvalueLangtypeId(const kvlangXvalue_t *v);
static inline bool kvlangLangtypeIsSint(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64;
}
static inline bool kvlangLangtypeIsUint(int id) {
    return id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64;
}
static inline bool kvlangLangtypeIsInt(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_UINT64;
}
static inline bool kvlangLangtypeIsFloat(int id) {
    return id == KVLANG_LT_FLOAT32 || id == KVLANG_LT_FLOAT64;
}
static inline bool kvlangLangtypeIsNum(int id) {
    return id >= KVLANG_LT_INT8 && id <= KVLANG_LT_FLOAT64;
}
static inline bool kvlangLangtypeIsChar(int id) {
    return id >= KVLANG_LT_CHAR_UTF32 && id <= KVLANG_LT_CHAR_ASCII;
}
static inline int kvlangLangtypeIntWidth(int id) {
    if (id >= KVLANG_LT_INT8 && id <= KVLANG_LT_INT64)
        return 8 << (id - KVLANG_LT_INT8);
    if (id >= KVLANG_LT_UINT8 && id <= KVLANG_LT_UINT64)
        return 8 << (id - KVLANG_LT_UINT8);
    return 0;
}
static inline int kvlangLangtypeElemSize(int id) {
    if (kvlangLangtypeIsInt(id))
        return kvlangLangtypeIntWidth(id) / 8;
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
    int kind_id;
    const uint8_t *bytes;
    int32_t length;
} kvlangScalar_t;
kvlangScalar_t kvlangXvalueScalar(const kvlangXvalue_t *v);
/* 标量转换（唯一转换路径，[[赋值转换]]）：target 为数值 kind id。
 * 成功返回 0，out 指向 buf 上的标量编码；失败 -1（None/非数值）。 */
__attribute__((visibility("hidden"))) int
kvlangScalarCast(int target_id, kvlangScalar_t s, uint8_t *buf, size_t cap,
                 kvlangXvalue_t *out);

/* 按声明类型 type 转换 v（仅数值/布尔/字符等标量目标）；成功 out 是 v 或 buf 视图。 */
__attribute__((visibility("hidden"))) int
kvlangXvalueCoerce(const char *type, const kvlangXvalue_t *v, uint8_t *buf,
                   size_t cap, kvlangXvalue_t *out);

/* 声明类型 ty 的元素/成员值 kind（`[]T`/`[N]T` → T，map `k·v` → v）；非容器 NULL。 */
static inline const char *kvlangDeclElemKind(const char *ty, char *buf,
                                             size_t cap) {
    if (!ty || !ty[0] || !strchr(ty, '['))
        return NULL;
    const char *p = strstr(ty, MEMBER_SEP);
    if (p)
        p += MEMBER_SEP_LEN;
    else {
        p = strchr(ty, ']');
        if (!p)
            return NULL;
        p++;
    }
    /* 嵌套数组元素（[][]T 的 []T）不在此转换，交给各自 builtin。 */
    if (!p[0] || p[0] == '[' || strchr(p, '[') || strlen(p) >= cap)
        return NULL;
    snprintf(buf, cap, "%s", p);
    return buf;
}
int64_t kvlangScalarReadInt64(int id, const uint8_t *body);
double kvlangScalarReadFloat64(int id, const uint8_t *body);
uint64_t kvlangScalarReadUint64(int id, const uint8_t *body);
static inline int64_t kvlangScalarI64(kvlangScalar_t s) {
    return kvlangScalarReadInt64(s.kind_id, s.bytes);
}
static inline double kvlangScalarF64(kvlangScalar_t s) {
    return kvlangScalarReadFloat64(s.kind_id, s.bytes);
}
static inline uint64_t kvlangScalarU64(kvlangScalar_t s) {
    return kvlangScalarReadUint64(s.kind_id, s.bytes);
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
        return kvlangLangtypeElemSize(
            kvlangLangtypeId((const char *)(d + XH_PREFIX), n));
    }
    if (cls == 2u)
        return (int32_t)(xh_a(d) * xh_b(d));
    return (int32_t)xh_a(d);
}
static inline const uint8_t *xh_body_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? NULL : xh_body(v->bytes);
}
static inline int32_t xh_content_len_of(const kvlangXvalue_t *v) {
    return kvlangXvalueNone(v) ? 0 : xh_content_len(v->bytes);
}

/* P1b4-a4：typed_core 行的标量视图——kind 由行静态签名定型，跳过 langtype
 * 解析。bytes 为空（None/空值）返回 false；调用方不得读成 0。 */
static inline bool kvlangXvalueScalarTyped(const kvlangXvalue_t *v, int kid,
                                           kvlangScalar_t *out) {
    if (!v->bytes || v->length < XH_PREFIX)
        return false;
    out->kind_id = kid;
    out->bytes = xh_body(v->bytes);
    /* 宽度由静态 kind 决定，不经 xh_content_len（class0 会重解析 langtype）。 */
    out->length = kvlangLangtypeElemSize(kid);
    return true;
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
void kvlangXvalueNew(kvlangXvalue_t *v, const char *kind, const uint8_t *raw,
                        uint32_t raw_len, int32_t al);
void kvlangXvalueNewDims(kvlangXvalue_t *v, const char *kind,
                            const uint8_t *raw, uint32_t raw_len,
                            const int32_t *dims, int32_t ndim);

void kvlangFormatFloat(char *out, size_t cap, double v);

/* ── KV 操作（封装 durable ABI）────────────────────────────────────── */

kvspace_t *kvlangKvspaceConnect(const char *dsn);
void kvlangKvspaceDisconnect(kvspace_t *k);
int kvlangKvspaceGetOne(kvspace_t *k, const char *key,
                        kvlangXvalue_t *out); /* None → out len=0 */
int kvlangKvspaceGetMember(kvspace_t *k, const char *dir, const char *name,
                           kvlangXvalue_t *out);
int kvlangKvspaceGetPart(kvspace_t *k, const char *key, uint32_t off,
                         uint32_t len,
                         kvlangXvalue_t *out); /* 借用读分片 body 字节 */
int kvlangKvspaceSetPart(kvspace_t *k, const char *key, uint32_t off,
                         const uint8_t *buf, uint32_t buf_len, char *err,
                         uint32_t err_cap); /* 就地写分片 */
int kvlangKvspaceGetHead(kvspace_t *k, const char *key,
                         kvspaceHead_t *out); /* 只读 head，不取 body */
int kvlangKvspaceSet(kvspace_t *k, const kvlangKvPair_t *pairs, int n,
                     char *err, uint32_t err_cap);
int kvlangKvspaceSetChar(kvspace_t *k, const char *key, const char *s);
int kvlangKvspaceDelete(kvspace_t *k, const char *key, char *err,
                        uint32_t err_cap);
int kvlangKvspaceDeleteTree(kvspace_t *k, const char *prefix, char *err,
                            uint32_t err_cap);
int kvlangKvspaceCopy(kvspace_t *k, const char *src, const char *dst, char *err,
                      uint32_t err_cap);
int kvlangKvspaceCopyTree(kvspace_t *k, const char *src, const char *dst,
                          char *err, uint32_t err_cap);
int kvlangKvspaceCopyList(kvspace_t *k, const char *src, const char *dst,
                          char *err, uint32_t err_cap);
int kvlangKvspaceMakeIndex(kvspace_t *k, const char *path, uint32_t capacity,
                           char *err, uint32_t err_cap);
int kvlangKvspaceExtensionIndex(kvspace_t *k, const char *path, const char *ext,
                                char *err, uint32_t err_cap);
int kvlangKvspaceDeleteExtensionIndex(kvspace_t *k, const char *path, char *err,
                                      uint32_t err_cap);
/* 只取数量 / 只取第 idx 项：单次后端调用，不枚举、不分配名字数组。 */
int kvlangKvspaceListLen(kvspace_t *k, const char *prefix, bool expand_ext,
                         bool resolve, int *out_count);
int kvlangKvspaceListAt(kvspace_t *k, const char *prefix, bool expand_ext,
                        bool resolve, int idx, char **out_name);
int kvlangKvspaceList(kvspace_t *k, const char *prefix, bool expand_ext,
                      bool resolve, char ***out_names,
                      int *out_count); /* split \n */
int kvlangKvspaceWatch(kvspace_t *k, const char *key,
                       const kvlangXvalue_t *target, uint64_t tick_ns,
                       kvlangXvalue_t *out);

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

const char *kvlangKeytreeVthreadIdFromPc(const char *pc, char *out,
                                         size_t cap); /* "" 无效 */
char *kvlangKeytreeStack(const char *root);           /* malloc */
size_t kvlangKeytreeStackBuffer(const char *root, char *buf,
                                size_t cap);  /* 栈缓冲，返长度 */
char *kvlangKeytreeFrameRoot(const char *pc); /* malloc，无效 NULL */
size_t kvlangKeytreeFrameRootBuffer(const char *pc, char *buf,
                                    size_t cap); /* 栈缓冲，返长度 */
char *kvlangKeytreeEntryPc(const char *root);    /* malloc */
char *kvlangKeytreeFrameAt(const char *vtid, int depth); /* malloc */
int kvlangKeytreeFrameNum(const char *path); /* [d]; panics if invalid */
char *kvlangKeytreeIrseqPc(const char *frame_root, int irseq); /* malloc */
/* 同上，写入调用方缓冲（跳转热路径：无 malloc、无 snprintf）。
 * frame_root 尾部连续 '/' 先剥掉；cap 不足/非法 irseq 返回 0。 */
static inline size_t kvlangKeytreeIrseqPcBuffer(const char *frame_root,
                                                int irseq, char *buf,
                                                size_t cap) {
    if (!frame_root || !buf || irseq < 0)
        return 0;
    size_t n = strlen(frame_root);
    while (n > 0 && frame_root[n - 1] == '/')
        n--;
    char digits[12];
    int d = 0;
    unsigned v = (unsigned)irseq;
    do {
        digits[d++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    /* 尾部 "/[<digits>,0]" + NUL：n + 1 + 1 + d + 1 + 1 + 1 = n+d+6 */
    if (n + (size_t)d + 6 > cap)
        return 0;
    memcpy(buf, frame_root, n);
    buf[n++] = '/';
    buf[n++] = '[';
    while (d)
        buf[n++] = digits[--d];
    buf[n++] = ',';
    buf[n++] = '0';
    buf[n++] = ']';
    buf[n] = 0;
    return n;
}
char *kvlangKeytreeMember(const char *base, const char *name); /* malloc */
char *kvlangKeytreeLibFunc(const char *pkg, const char *name); /* malloc */
char *kvlangKeytreeRwir(const char *opcode);                   /* malloc */
const char *kvlangKeytreeCanonicalOp(const char *opcode);
bool kvlangKeytreeValidSegment(const char *s);
char *kvlangKeytreeCheckWriteKey(const char *vtid, const char *key);
char *kvlangKeytreeSysRwirBackendRoot(void);
char *kvlangKeytreeSysRwirBackend(const char *name);
char *kvlangKeytreeSysRwirBackendOp(const char *name, const char *opcode);
char *kvlangKeytreeSysRwirBackendCommand(const char *name);
char *kvlangKeytreeSysRwirBackendStatus(const char *name);
char *kvlangKeytreeSysRwirBackendLoad(const char *name);
char *kvlangKeytreeSysRwirBackendHeartbeat(const char *name);
char *kvlangKeytreeSysRwirBackendCategoryRoot(const char *name);
char *kvlangKeytreeSysTask(const char *task_id, const char *field);
char *kvlangKeytreeDoneRwir(const char *task_id);
char *kvlangKeytreeLibSignature(const char *opcode);
void kvlangKeytreeVthread(const char *vtid, char *out, size_t cap);
void kvlangKeytreeVthreadSlot(const char *vtid, const char *frame, int i, int j,
                              char *out, size_t cap);
void kvlangKeytreeVthreadPc(const char *vtid, char *out, size_t cap);
void kvlangKeytreeVthreadStatus(const char *vtid, char *out, size_t cap);
void kvlangKeytreeVthreadMessage(const char *vtid,
                                       char *out, size_t cap);
void kvlangKeytreeVthreadDebugger(const char *vtid, char *out, size_t cap);
void kvlangKeytreeFrameCallPc(const char *root, char *out, size_t cap);
void kvlangKeytreeFrameReturnPc(const char *root, char *out, size_t cap);
void kvlangKeytreeFrameReadOnly(const char *root, char *out, size_t cap);
bool kvlangKeytreeIsEntryPc(const char *pc);

/* ── rwir ──────────────────────────────────────────────────────────── */

#define OP_CALL "call"
#define OP_RETURN "return"
#define OP_BR "br"
#define OP_GOTO "goto"
#define OP_ASSIGN "assign"
#define OP_COPY "="

/* opcode_id：decode 期一次固化的统一派发码（quickening），主循环据此纯整数跳表、热路径零 strcmp。
 * ≥0    = 在本 runtime myrwircaps（native 算子 + control/copy 均为其中一行），直查 myrwircaps[opcode_id].fn
 * -1    = 不在表内（执行期查 /lib：def rwir 路由头→路由，否则用户 rwfunc→调用） */
enum {
    OPID_notinmyrwircaps = -1,
};

int kvlangBuiltinCapabilityIndex(const char *opcode);
/* 该算子是否需读回写参旧值（obj/map）；opcode_id<0 恒 false。 */
bool kvlangBuiltinLoadWriteValue(int opcode_id);

/* decode 期分类：control/copy 与 native 同在 myrwircaps 一张表，全走 CapIndex；
 * miss 落 OPID_notinmyrwircaps（执行期再查 /lib）。 */
static inline int kvlangOpClassify(const char *op) {
    int n = kvlangBuiltinCapabilityIndex(op);
    return n >= 0 ? n : OPID_notinmyrwircaps;
}

typedef struct kvspaceKeyOff kvspaceKeyOff;

typedef struct {
    char *name;
    char *type;
    bool is_address;
    kvlangXvalue_t value;
} kvlangParam_t;

typedef struct {
    char *opcode;
    int opcode_id; /* 统一派发码，见上 enum；decode 期固化，永不随帧变化 */
    bool typed_core; /* P1b3：mrwircaps sig ∈ BIN/CMP/UN 的行 */
    kvlangParam_t *reads;
    int read_count;
    kvlangParam_t *writes;
    int write_count;
    kvspaceKeyOff **write_offs; /* 本步写槽的帧层地址；NULL = 按 key 写 */
} kvlangRwirInst_t;

__attribute__((visibility("hidden"))) size_t
kvlangRwirNextPcFromPrefix(const char *prefix, size_t plen, int irseq, char *buf,
                           size_t cap);
size_t kvlangRwirNextPcBuffer(const char *pc, char *buf,
                              size_t cap); /* 栈缓冲，返长度 */
int kvlangRwirExtractAddress(const char *coord);
int kvlangRwirDecode(kvspace_t *kv, const char *link_base, const char *pc,
                     kvlangRwirInst_t *out, char *err, uint32_t err_cap);
void kvlangRwirInstructionFree(kvlangRwirInst_t *inst);
/* 操作数槽解析：1=有；0=None；-1=错误。literal -> out_literal；
 * Ptr -> descriptor=目标 key 且 is_absolute_key=true；rwir/rwfunc -> descriptor=槽名。 */
int kvlangRwirOperandParse(kvspace_t *kv, const char *link_base,
                           const char *slot, char **out_descriptor,
                           bool *out_is_literal, bool *out_is_absolute_key,
                           kvlangXvalue_t *out_literal, char **out_type,
                           char *err, uint32_t err_cap);
/* 描述符 -> 当前帧绝对 key。 */
char *kvlangRwirOperandTarget(kvspace_t *kv, const char *frame_root,
                              const char *descriptor);
/* 外部扩展 handoff：写共享队列 /lib/<opcode>/vids/<vid>=pc，阻塞 watch 该 key 直至变 None
 * （外部执行器认领、驱动、置 nextpc 后删除该条目 → 本端解除阻塞）。 */
int kvlangHandoffExternalRwir(kvspace_t *kv, const char *vtid, const char *pc,
                              kvlangRwirInst_t *inst);
/* kvlangNotInMyRwirCaps：opcode 是不在本 runtime myrwircaps 内、须经 def rwir 路由给能兑现它的
 * 其它 runtime 的 rwir。判据=读 kvspace /lib/<opcode> 存在 def rwir 路由头（能力唯一事实源）。 */
#ifdef KV_COUNT
extern unsigned long kvCNarrow, kvCCompare;
#ifdef KV_COUNT
extern unsigned long kvCSpec, kvCGen;
extern unsigned long kvCBodyFast, kvCBodyFallback;
extern unsigned long kvCBodyCells, kvCBodyCellsMax;
extern unsigned long kvCBodyCapAttempt, kvCBodyCapOk;
#endif
#endif
bool kvlangNotInMyRwirCaps(kvspace_t *kv, const char *opcode);

/* ── vthread ───────────────────────────────────────────────────────── */

void kvlangVthreadGet(kvspace_t *kv, const char *vtid, char **pc,
                      char **status);
void kvlangVthreadPcGet(kvspace_t *kv, const char *vtid, char **pc);
void kvlangVthreadStatusGet(kvspace_t *kv, const char *vtid, char **status);
/* key 的 64 位地址（偏移量）。key 由调用方持有；off 是 key→地址的派生缓存。
 * value 仍每步从 kvspace 读；缓存的只是寻址结果，丢了可按 key 重算。 */
struct kvspaceKeyOff {
    kvspace_t *kvspace;
    const char *key;
    kvspaceRef_t off;
    bool resolved;
    /* 定长标量写槽（只写参、只候选行设置）：emit 快路径由此取绑定。 */
    void *fast;
};

void kvspaceKeyOffBind(kvspaceKeyOff *ko, kvspace_t *kvspace, const char *key);
/* 读值到调用方缓冲；false = 键缺失 / None，超长直接 abort。 */
bool kvspaceKeyOffRead(kvspaceKeyOff *ko, char *out, size_t cap);
/* 整值写：已解析走 SetValueByRef，未解析/失败退回按 key 写。0 = 成功。 */
int kvspaceKeyOffSet(kvspaceKeyOff *ko, const uint8_t *val, uint32_t len,
                     char *err, uint32_t err_cap);
int kvlangVthreadSet(kvspace_t *kv, const char *vtid, const char *pc,
                     const char *status);
int kvlangVthreadSetDone(kvspace_t *kv, const char *vtid, const char *ret);
void kvlangVthreadSetError(kvspace_t *kv, const char *vtid, const char *pc,
                           const char *msg);

#define KVLANG_DELEGATE_OK 0
#define KVLANG_DELEGATE_ERR -1
#define KVLANG_DELEGATE_LOCAL 1

bool kvlangDispatchIsDelegatedOp(kvspace_t *kv, const char *opcode);
int kvlangDispatchDelegate(kvspace_t *kv, const char *vtid, const char *pc,
                           kvlangRwirInst_t *inst);
void kvlangDispatchSetDefaultTimeoutNanoseconds(int64_t ns);

/* ── builtin ───────────────────────────────────────────────────────── */

/* yield_pc returns a child PC to the external runtime. */
/* 执行游标（C-iii）：帧深/irseq 的派生缓存；值仍以 kvspace 的 ‥pc 为准。
 * dirty 由每次 dispatch 置位，只有 kvlangBuiltinNextPc（顺序推进）清位。 */
typedef struct {
    int irseq;
    bool dirty;
    /* goto/br 已把游标设为目标：本步结束时不要 +1、也不要判 dirty 重解析。 */
    bool jump;
    /* PC 前缀（到末个 '/'，含）缓存：顺序推进只需重写数字尾，免 strrchr/strtol。 */
    char prefix[KV_KEY_MAX];
    int prefix_len;
    bool prefix_ok;
} kvlangExecCursor;

typedef struct {
    kvspace_t *kvspace;
    const char *vthread_id;
    const char *pc;
    kvlangRwirInst_t *instruction;
    char **yield_pc;
    const char *frame_root; /* Borrowed for this instruction. */
    const char *
        status_known; /* 本步开始前从 kvspace 读到的 ‥status（借用）；NULL = 未知 */
    bool persist_failed;
    /* 删除/委托/外部 handoff 时自增；帧层 keyoff 据此全部失效重绑。 */
    uint32_t *addr_epoch;
    /* PC 值回传：NextPc 持久化成功后把新 PC 拷进主循环的零分配缓冲。 */
    char *pc_out;
    size_t pc_out_cap;
    bool pc_feedback;
    /* PC/status 槽的 keyoff：ExecuteMode 入口绑定一次，NextPc 只做整值写。
     * NULL = 调用方未绑定（独立帧），NextPc 按 key 冷写。 */
    struct kvspaceKeyOff *pc_off;
    struct kvspaceKeyOff *status_off;
    /* Phase 1 派发：route 需要的只读上下文。 */
    const void *row;  /* const kvlangCodeRow_t * */
    const void *bind; /* const kvlangRowBind_t *：goto/br 预格式化目标 */
    int mode;        /* kvmode_t */
    char **out_pc;   /* ExecuteMode 的 out 参数（仅判空） */
    kvlangExecCursor *cursor; /* 执行游标（C-iii），可为 NULL */
} kvlangFrame_t;

/* typed_core 行按静态 kind 取标量；None 属 layout 未定义，直接 die（不读成 0）。 */
static inline kvlangScalar_t kvlangScalarOf(const kvlangFrame_t *f,
                                                 const kvlangXvalue_t *v,
                                                 int kid) {
#ifdef KV_DEBUG_TYPES
    /* debug 构建保留原解析，由 SPEC_FALLBACK/typed check 给出既有诊断。 */
    (void)f;
    (void)kid;
    return kvlangXvalueScalar(v);
#else
    if (f->instruction && f->instruction->typed_core) {
        kvlangScalar_t s;
        if (!kvlangXvalueScalarTyped(v, kid, &s))
            kvlangDie("typed %s: None operand",
                      f->instruction->opcode ? f->instruction->opcode : "?");
        return s;
    }
    return kvlangXvalueScalar(v);
#endif
}

/* 特化 handler 不变量（仅 debug 生效）：声明为 kid 的操作数经读侧 coercion 必为 kid；
 * 实际 kind 不符 = 不变量被破坏，直接 die。动态/无声明操作数（type 为空/空 kindexp/
 * union）合法地可能任意 kind，不检查——它们由 handler 的通用回退处理。 */
#ifndef NDEBUG
static inline void kvlangSpecCheckDeclared(const kvlangFrame_t *f, int i,
                                           int actual_kind, int kid) {
    if (!f->instruction || i >= f->instruction->read_count)
        return;
    const char *t = f->instruction->reads[i].type;
    if (!t || !t[0] || strchr(t, '|'))
        return;
    if (kvlangLangtypeId(t, strlen(t)) == kid && actual_kind != kid)
        kvlangDie("specialized %s: operand %d kind %d != declared %s",
                  f->instruction->opcode, i, actual_kind, t);
}
#else
#define kvlangSpecCheckDeclared(f, i, actual_kind, kid) ((void)0)
#endif

/* P1b3：typed_core 行的实际值 kind 断言（仅 KV_DEBUG_TYPES 构建）。
 * 比较实际值 head kind 与 manifest 声明（kid），不依赖槽字符串。 */
#ifdef KV_DEBUG_TYPES
static inline void kvlangSpecCheckTyped(const kvlangFrame_t *f, int i,
                                        int actual_kind, int kid) {
    if (f->instruction && f->instruction->typed_core && actual_kind != kid)
        kvlangDie("typed %s: operand %d kind %d != manifest %d",
                  f->instruction->opcode, i, actual_kind, kid);
}
#else
#define kvlangSpecCheckTyped(f, i, actual_kind, kid) ((void)0)
#endif

/* P1b3 typed 写原语：结果 kind 已定型，跳过 CoerceAssign/分派。 */
__attribute__((visibility("hidden"))) int
kvlangBuiltinWriteResultTyped(kvlangFrame_t *f, const kvlangXvalue_t *v,
                              const char *kind);

/* 写侧转换出口：失败即置 TypeError；CoerceAll 供数组字面量逐元素转换。 */
__attribute__((visibility("hidden"))) int
kvlangCoerceAssign(kvlangFrame_t *f, const char *type, const kvlangXvalue_t *v,
                   uint8_t *buf, size_t cap, kvlangXvalue_t *out);
__attribute__((visibility("hidden"))) int
kvlangCoerceAll(kvlangFrame_t *f, const char *type, const kvlangXvalue_t *in,
                int n, uint8_t (*bufs)[64], kvlangXvalue_t *out);
/* 定长标量写：命中 body 快路径则原地写并 NextPc，返回 1；未命中返回 0。
 * 供 emit_inline_scalar 与各特化 handler 共用。 */
__attribute__((visibility("hidden"))) int
kvlangBuiltinWriteBody(kvlangFrame_t *f, const char *kind, const uint8_t *raw,
                       uint32_t len);

/* Write PC to kvspace; write status when it changes. */
void kvlangVthreadNextPc(kvlangFrame_t *f, const char *pc, const char *status);

/* kvlangNotInMyCaps：查 myrwircaps table，opcode 不在本 runtime 能力表内 → true。 */
bool kvlangNotInMyCaps(const char *opcode);
bool kvlangBuiltinNumericOp(const char *opcode);
/* 该 key 被整值写/删除：清掉所有命中 cell 的 body 指针，允许重试。 */
__attribute__((visibility("hidden"))) void kvlangBodyInvalidate(const char *key);
/* 整值写命中：只有写入 kind 与该槽静态结果类型一致时才允许重试捕获。 */
__attribute__((visibility("hidden"))) void
kvlangBodyInvalidateKind(const char *key, const uint8_t *langtype,
                         uint32_t langtype_len);
__attribute__((visibility("hidden"))) void
kvlangBodyInvalidateTree(const char *prefix);

/* 指令派发表：native 项 + 表尾 route。row 加载期定型 handler_id，执行期一次间接调用。 */
typedef int (*kvlangBuiltinFn)(kvlangFrame_t *f);
typedef struct {
    const char *opcode;
    kvlangBuiltinFn fn;
    bool load_write_value;
    /* 定长结果类型（仅 A1 特化项）；result_kind=NULL 表示结果类型不静态固定。 */
    const char *result_kind;
    uint32_t result_width;
} kvlangRwirCap;

/* P1b1 ISA 签名列：与 myrwircaps 同序、逐项一行（see workspace P1 spec §1.3/§2.1）。
 * rp/wp 类型由 sig 规则 + opcode 生成，不在发布器里另列 opcode 清单。 */
enum { KV_ISA_CORE = 0, KV_ISA_EXT = 1 };
enum {
    KV_ISA_ANY = 0, /* 槽类型全 any（arity 显式；dynamic 时末读参 any...） */
    KV_ISA_BIN,     /* <kind>·op：rp=kind×2，wp=kind */
    KV_ISA_CMP,     /* <kind>·op：rp=kind×2，wp=bool */
    KV_ISA_UN,      /* <kind>·op：rp=kind，wp=kind */
    KV_ISA_CAST,    /* rp=any，wp=opcode kind */
};
enum {
    KV_EFF_PURE = 1,
    KV_EFF_READ = 2,
    KV_EFF_WRITE = 4,
    KV_EFF_CONTROL = 8,
    KV_EFF_IO = 16,
    KV_EFF_DEBUG = 32,
};
typedef struct {
    uint8_t cls;
    uint8_t sig;
    uint8_t nr;
    uint8_t nw;
    uint8_t dynamic;
    uint8_t effects;
} kvlangIsaSig;
__attribute__((visibility("hidden"))) const kvlangIsaSig *kvlangIsaSigs(uint32_t *n_out);
/* ISA registry（P1a）：core manifest 发布与 per-function hash 闸门。 */
__attribute__((visibility("hidden"))) int kvlangIsaPublishCore(kvspace_t *kv);
__attribute__((visibility("hidden"))) bool kvlangIsaCtlDsn(const char *dsn, char *out,
                                                          size_t cap);
__attribute__((visibility("hidden"))) int kvlangIsaCheck(kvspace_t *kv,
                                                         const char *func_dir,
                                                         char *err,
                                                         uint32_t err_cap);
__attribute__((visibility("hidden"))) const char *kvlangIsaCoreHash(void);

__attribute__((visibility("hidden"))) const kvlangRwirCap *kvlangRwirCaps(void);
__attribute__((visibility("hidden"))) int kvlangRwirRouteId(void);
__attribute__((visibility("hidden"))) int kvlangKvcpuRoute(kvlangFrame_t *f);
int kvlangBuiltinExecuteCopy(kvlangFrame_t *f);
/* control 算子：与 native 同居 myrwircaps 一张表，frame 签名统一派发（call/return/goto/br）。 */
int kvlangControlCall(kvlangFrame_t *f);
int kvlangControlReturn(kvlangFrame_t *f);
int kvlangControlGoto(kvlangFrame_t *f);
int kvlangControlBr(kvlangFrame_t *f);
void kvlangBuiltinResolveReadValue(kvspace_t *kv, const char *frame_root,
                                   const char *name, const kvlangXvalue_t *val,
                                   kvlangXvalue_t *out);
char *kvlangBuiltinResolveWriteSlot(kvspace_t *kv, const char *frame_root,
                                    const char *name);
/* 成员写的 base 尚无值 → 落空 stringkeymap 值（`/lib` 下跳过，见 rwir_kv.c）。 */
/* 成员写（memitem）前置条件：memhead（base 容器值）必须已存在；缺则返回 -1 拒绝写入。 */
int kvlangBuiltinCheckMemoryHead(kvspace_t *kv, const char *frame_root,
                                 const char *base);
char *kvlangBuiltinResolveReadKey(kvspace_t *kv, const char *frame_root,
                                  const char *name, const kvlangXvalue_t *val);
void kvlangDisplay(const kvlangXvalue_t *v,
                   char **out); /* malloc，对齐 Go Display */

/* ── kvcpu ─────────────────────────────────────────────────────────── */

/* 两种执行模式（详见 runtime篇-05）：
 *   KVMODE_WATCH   模式1：runtime 主导，遇 ext rwir → handoff(vids) + watch(vids/<vid>==None) 阻塞
 *   KVMODE_RETURN  模式2：扩展主导，遇 ext rwir → 不 handoff 不 watch，返回该 ext rwir 的 PC（单线程函数调用）
 * kvlangKvcpuExecuteMode 返回值：-1 错误；0 正常结束(done)；1 遇 ext rwir（仅 KVMODE_RETURN，*out_pc=其 PC）。 */
typedef enum { KVMODE_WATCH = 0, KVMODE_RETURN = 1 } kvmode_t;

int kvlangKvcpuExecuteMode(kvspace_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc);
int kvlangKvcpuExecute(kvspace_t *kv,
                       const char *pc); /* = KVMODE_WATCH，out_pc 忽略 */
char *kvlangKvcpuBootstrap(kvspace_t *kv, const char *vtid,
                           const char *funcname, const char *const *args,
                           int nargs);
int kvlangKvcpuDynamicCall(kvlangFrame_t *f, const char *funckey);
/* 创建 vthread（不运行），返回 vid（free）。运行由 RunVid 承接。vthread·create 用。 */
char *kvlangVthreadSpawn(kvspace_t *kv, const char *funcname,
                         const char *const *args, int nargs);
/* 按 vid 从持久化 pc 跑到结束（KVMODE_WATCH），返回终态/错误。vthread·run 用。 */
int kvlangRuntimeRunVthreadId(kvspace_t *kv, const char *vid, char **ret,
                              char *err, uint32_t err_cap);
/* run funcname 到结束（Spawn + RunVid），返回终态/错误。 */
int kvlangRuntimeExecuteKvspace(kvspace_t *kv, const char *funcname,
                                const char *const *args, int nargs, char **ret,
                                char *err, uint32_t err_cap);

/* ── logx ──────────────────────────────────────────────────────────── */

void kvlangLogDebug(const char *fmt, ...);
/* 热路径日志：内联 level 判断挡住可变参数准备（默认 warn 下零调用）。 */
extern __attribute__((visibility("hidden"))) int kvlangLogDebugOn;
#define KV_LOG_DEBUG(...)                                                      \
    do {                                                                       \
        if (kvlangLogDebugOn)                                                  \
            kvlangLogDebug(__VA_ARGS__);                                       \
    } while (0)
void kvlangLogInfo(const char *fmt, ...);
void kvlangLogError(const char *fmt, ...);
