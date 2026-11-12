/* kvlangxvalue —— kvlang 语言层 XValue 的唯一实现文件。
 *
 * 一个头、一个 C：wire 构造、强类型构造器、坐标比较、kind 串查询。
 * 所有值都**直接落在 kvspace 里**：`kvlangxvalueNew(handle, key, …)` 按 key
 * 分配并返回映射内的 wire 指针，WriteBody/WriteLangtype 原地补齐；
 * 不再有堆缓冲、不再有 free。字节布局全部委托给 kvspace_xvalue.h 的 head 访问器。
 */

#include "kvlangxvalue.h"

#include <kvspace/kvspace.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── wire 构造：New / WriteBody / WriteLangtype 与各类型构造器 ───────── */

/* langtype 记录的 payload 长度（前缀位 + 文本）；空 langtype 且无前缀 = 0。
   记录头里的 `len` 字段写的是 payload 长度，不含 3 字节头。 */
static uint64_t langtype_payload_len(const char *langtype, char prefix) {
    const uint64_t text = langtype ? (uint64_t)strlen(langtype) : 0;
    return text + (prefix ? 1u : 0u);
}

/* attr 区字节数：有 payload 才有一条 3 字节头 + payload。 */
static uint64_t langtype_attr_len(const char *langtype, char prefix) {
    const uint64_t payload = langtype_payload_len(langtype, prefix);
    return payload ? 3 + payload : 0;
}

uint8_t *kvlangxvalueNew(void *handle, const char *key, uint64_t body_len,
                         uint16_t attr_len) {
    if (!handle || !key)
        return NULL;
    uint64_t offset = 0;
    char error[256] = {0};
    const int status = kvspaceRealloc(handle, key, body_len, attr_len, &offset,
                                      error, (uint32_t)sizeof error);
    if (status != KVSPACE_OK)
        return NULL;
    uint8_t *base = kvspaceXvalueInProcessAddress(handle);
    if (!base)
        return NULL;
    uint8_t *wire = base + offset;
    kvspaceXvalueHead head = {.padding = {0u, 0u, 0u, 0u, 0u, 0u}};
    kvspaceXvalueHeadSetLen(&head, body_len);
    kvspaceXvalueHeadSetAttrsLen(&head, attr_len);
    memcpy(wire, &head, sizeof head);
    return wire;
}

/* 把 body 写进 head 之后。`body == NULL` 时保持零填（Reserve 占位）；
   body_len 必须与 head 里记录的一致。 */
int kvlangxvalueWriteBody(uint8_t *wire, const uint8_t *body,
                          uint64_t body_len) {
    if (!wire)
        return -1;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (kvspaceXvalueHeadLen(head) != body_len)
        return -1;
    if (body && body_len)
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, (size_t)body_len);
    return 0;
}

/* 在 attr 区写一条 tag 1 的 langtype 记录。prefix 为 `*`（Ptr）时写在文本前；
   记录长度必须与 head 的 attrslen 一致。 */
int kvlangxvalueWriteLangtype(uint8_t *wire, uint64_t body_len, char prefix,
                              const char *langtype) {
    if (!wire)
        return -1;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (kvspaceXvalueHeadLen(head) != body_len)
        return -1;
    const uint64_t payload = langtype_payload_len(langtype, prefix);
    const uint64_t attr_len = payload ? 3 + payload : 0;
    if (kvspaceXvalueHeadAttrsLen(head) != attr_len)
        return -1;
    if (attr_len == 0)
        return 0;
    uint8_t *record = wire + KVSPACE_XVALUE_HEAD_LEN + body_len;
    record[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
    record[1] = (uint8_t)payload;
    record[2] = (uint8_t)(payload >> 8);
    uint64_t at = 3;
    if (prefix)
        record[at++] = (uint8_t)prefix;
    const uint64_t text = langtype ? (uint64_t)strlen(langtype) : 0;
    if (text)
        memcpy(record + at, langtype, (size_t)text);
    return 0;
}

/* 唯一的通用构造器：分配 + 写 head + 写 body + 写 langtype。
   `raw == NULL` 时 body 保持零填（Reserve 占位）；`langtype` 原样写进 attr tag 1，
   **不做类型校验**——宽度/形态由调用方保证。
   NewPtr 把 `*` 前缀预先拼进 langtype 文本，同样是原样写入。 */
uint8_t *kvlangXvalueNewT(void *handle, const char *key,
                          const uint8_t *raw, uint32_t raw_len,
                          const char *langtype) {
    const uint64_t attr_len = langtype_attr_len(langtype, 0);
    if (attr_len > UINT16_MAX)
        return NULL;
    uint8_t *wire = kvlangxvalueNew(handle, key, raw_len, (uint16_t)attr_len);
    if (!wire)
        return NULL;
    if (kvlangxvalueWriteBody(wire, raw, raw_len) != 0 ||
        kvlangxvalueWriteLangtype(wire, raw_len, 0, langtype) != 0)
        return NULL;
    return wire;
}

/* 基础数据类型（固定宽度标量 + 三种 char）。内部函数，不导出。 */
static int is_base_langtype(const char *langtype) {
    static const char *const base[] = {
        "bool",   "int8",      "int16",     "int32",     "int64",
        "uint8",  "uint16",    "uint32",    "uint64",    "float32",
        "float64", "char/utf8", "char/utf32", "char/ascii",
    };
    if (!langtype)
        return 0;
    for (size_t i = 0; i < sizeof base / sizeof base[0]; i++)
        if (strcmp(langtype, base[i]) == 0)
            return 1;
    return 0;
}

/* `[]T`：BufferArray 只收基础类型元素的数组。内部函数，不导出。 */
static int is_buffer_array_langtype(const char *langtype) {
    if (!langtype || langtype[0] != '[' || langtype[1] != ']')
        return 0;
    return is_base_langtype(langtype + 2);
}

/* 基础类型里的紧凑数组元素：去掉变长的 char/utf8，char/utf32、char/ascii
   是定宽编码，保留。内部函数，不导出。 */
static int is_compact_array_element_langtype(const char *langtype) {
    return is_base_langtype(langtype) && strcmp(langtype, "char/utf8") != 0;
}

/* `[dims]T`：CompactArray 只收数值元素、且维度非空。内部函数，不导出。 */
static int is_compact_array_langtype(const char *langtype) {
    if (!langtype || langtype[0] != '[')
        return 0;
    const char *p = langtype + 1;
    int dimension_count = 0;
    int digits = 0;
    for (; *p && *p != ']'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits = 1;
        } else if (*p == ',' && digits) {
            dimension_count++;
            digits = 0;
        } else {
            return 0;
        }
    }
    if (*p != ']' || !digits)
        return 0;
    dimension_count++;
    if (dimension_count > 64)
        return 0;
    return is_compact_array_element_langtype(p + 1);
}

uint8_t *kvlangXvalueNewBufferArray(void *handle, const char *key,
                                    const uint8_t *data, uint64_t data_len,
                                    const char *langtype) {
    if (!is_buffer_array_langtype(langtype))
        return NULL;
    if (data_len > UINT32_MAX || (data_len && !data))
        return NULL;
    return kvlangXvalueNewT(handle, key, data, (uint32_t)data_len, langtype);
}

uint8_t *kvlangXvalueNewCompactArray(void *handle, const char *key,
                                     const uint64_t *dims, uint32_t ndim,
                                     const uint8_t *raw, uint64_t raw_len,
                                     const char *langtype) {
    /* Shape lives in leading [dims] of langtype; body byte length is raw_len. */
    if (!dims || ndim == 0 || ndim > 64)
        return NULL;
    if (!is_compact_array_langtype(langtype))
        return NULL;
    if (raw_len > UINT32_MAX || (raw_len && !raw))
        return NULL;
    return kvlangXvalueNewT(handle, key, raw, (uint32_t)raw_len, langtype);
}

/* path_len == 0 是合法的 None 指针：`*T` + 空 body。 */
uint8_t *kvlangXvalueNewPtr(void *handle, const char *key,
                            const uint8_t *path, uint32_t path_len,
                            const char *langtype) {
    if (path_len && !path)
        return NULL;
    if (langtype && (langtype[0] == '*' || langtype[0] == '@'))
        return NULL;
    const size_t text = langtype ? strlen(langtype) : 0;
    char *want = (char *)malloc(text + 2);
    if (!want)
        return NULL;
    want[0] = '*';
    if (text)
        memcpy(want + 1, langtype, text);
    want[text + 1] = '\0';
    uint8_t *wire = kvlangXvalueNewT(handle, key, path, path_len, want);
    free(want);
    return wire;
}

uint8_t *kvlangXvalueNewCode(void *handle, const char *key,
                             const uint8_t *body, uint64_t body_len,
                             const char *langtype) {
    if (body_len > UINT32_MAX || (body_len && !body))
        return NULL;
    return kvlangXvalueNewT(handle, key, body, (uint32_t)body_len, langtype);
}

uint8_t *kvlangXvalueHeadReserve(void *handle, const char *key,
                                 uint64_t body_len, const char *langtype) {
    /* 预留 = body_len 字节零 body 的占位（后由调用方填充）。 */
    if (body_len > UINT32_MAX)
        return NULL;
    return kvlangXvalueNewT(handle, key, NULL, (uint32_t)body_len, langtype);
}

/* ── 强类型便利构造器（接受强类型实参并自己拼 langtype）─────────────── */

uint8_t *kvlangXvalueNewPtrFromKindexpr(void *handle, const char *key,
                                        const char *target_kindexpr,
                                        const char *target) {
    if (!target_kindexpr || !target)
        return NULL;
    /* `any` 不是类型名（空 kindexpr = 不约束）。 */
    if (strcmp(target_kindexpr, "any") == 0)
        return NULL;
    /* 空 target 不是可用的指针目标。 */
    if (*target == '\0')
        return NULL;
    /* target_kindexpr 是被指向类型的基 kindexpr；`*` 前缀由 NewPtr 唯一补上。 */
    return kvlangXvalueNewPtr(handle, key, (const uint8_t *)target,
                              (uint32_t)strlen(target), target_kindexpr);
}

uint8_t *kvlangXvalueNewChar(void *handle, const char *key,
                             const uint8_t *bytes, uint32_t len,
                             const char *langtype) {
    if (len && !bytes)
        return NULL;
    /* 单个字符（char/utf8 等），不是 `[]T` 数组，直接走 NewT。 */
    return kvlangXvalueNewT(handle, key, bytes, len, langtype);
}

uint8_t *kvlangXvalueNewBool(void *handle, const char *key, uint8_t v,
                             const char *langtype) {
    uint8_t b = v ? 1 : 0;
    return kvlangXvalueNewT(handle, key, &b, 1, langtype);
}

uint8_t *kvlangXvalueNewInt64(void *handle, const char *key, int64_t v,
                              const char *langtype) {
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)((uint64_t)v >> (8 * i));
    return kvlangXvalueNewT(handle, key, raw, 8, langtype);
}

uint8_t *kvlangXvalueNewFloat64(void *handle, const char *key, double v,
                                const char *langtype) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)(bits >> (8 * i));
    return kvlangXvalueNewT(handle, key, raw, 8, langtype);
}

/* ── 坐标段工具（cmp_coord 的唯一实现）（原 coord.c）────────────────────────────────────────── */

/* 坐标段工具：`[1,2]` 这类成员名的判定与比较。
 *
 * 这是**唯一一份** `cmp_coord`。存储层（`kvspaceList`）按契约返回后端顺序、不排序；
 * 需要坐标序的调用方（runtimec / layout / CLI）一律走这里，不各写一套——两套比较器
 * 迟早对「小数坐标 / 字符串坐标 / 目录尾斜杠」这类边角判不同，症状是「同一个目录，
 * 两个算子取到不同的第 i 项」。 */


int kvlangCoordIsCoord(const char *name) {
    if (!name || name[0] != '[')
        return 0;
    size_t n = strlen(name);
    if (n < 3 || name[n - 1] != ']')
        return 0;
    for (size_t i = 1; i + 1 < n; i++)
        if (name[i] == '[' || name[i] == ']')
            return 0;
    return 1;
}

/* Parse integer coordinates; return -1 on mismatch. */
int kvlangParseCoord(const char *name, int64_t *coords, int maxn) {
    if (!name || name[0] != '[')
        return -1;
    int n = 0;
    int64_t cur = 0;
    bool has = false;
    for (const char *p = name + 1; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (*p - '0');
            has = true;
        } else if (*p == ',') {
            if (!has || n >= maxn)
                return -1;
            coords[n++] = cur;
            cur = 0;
            has = false;
        } else if (*p == ']') {
            if (!has || n >= maxn)
                return -1;
            coords[n++] = cur;
            return (p[1] == '\0') ? n : -1;
        } else {
            return -1;
        }
    }
    return -1;
}

int kvlangCoordCmp(const char *a, const char *b) {
    int ia = kvlangCoordIsCoord(a), ib = kvlangCoordIsCoord(b);
    if (!ia && !ib)
        return strcmp(a, b);
    if (!ia)
        return 1; /* 非坐标段排后 */
    if (!ib)
        return -1;
    int64_t ca[8], cb[8];
    int na = kvlangParseCoord(a, ca, 8);
    int nb = kvlangParseCoord(b, cb, 8);
    if (na < 0 || nb < 0)
        return strcmp(a, b); /* 含小数/字符串坐标 → 字典序 */
    for (int i = 0; i < na && i < nb; i++) {
        if (ca[i] != cb[i])
            return ca[i] < cb[i] ? -1 : 1;
    }
    if (na != nb)
        return na < nb ? -1 : 1;
    return 0;
}

/* ── 语言层 kind 串常量的运行期查询（原 const.c）────────────────────────────────────────── */

/* 语言常量运行期查询：供 Go/Python/Rust 扩展经 C ABI 取常量，避免各语言硬编码
 * 一份 kind 串。存储常量不在这里——它们在 kvspace 的 `kvspaceConstStorage`。
 *
 * 两层的查询入口必须分开：同一个函数返回两种层的东西，等于把「这个名字属于哪层」
 * 重新变成调用方要猜的问题。 */


const char *kvlangConstLanguage(const char *name) {
    static const struct { const char *key; const char *val; } table[] = {
#define KVLANG_CONST_ENTRY(name) { #name, name },
        KVLANG_KV(KVLANG_CONST_ENTRY)
#undef KVLANG_CONST_ENTRY
    };
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].key, name) == 0)
            return table[i].val;
    return NULL;
}
