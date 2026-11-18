/* kvlangxvalue.h — kvlang 语言层对 XValue 的完整约定。
 *
 * 这一份头合并了原来分在两处的语言层定义：
 *
 *   - `xvalue.h`：tag 1 = langtype、构造器、坐标段比较；
 *   - `const.h` ：kind 串常量与运行期查询。
 *
 * 分层的硬边界不变：
 *   - `kvspace/kvspace_xvalue.h` 只认字节（head/body/attr，tag 只是编号），
 *     **不定义任何字节偏移**——偏移、长度、越界检查全部在字节层头里；
 *   - 本头只补语言层才知道的部分：哪个 tag 是类型、构造器语义、坐标比较、kind 串。
 *
 * 源码与构建：本头是**唯一公共头**；实现分散在同目录的各个 .c 里（构造器与 wire、
 * langtype 解析、值与文本、char 编码、浮点格式化、坐标比较、kind 常量）。构建入口
 * 同目录 `CMakeLists.txt`，产物是共享库 `libkvlang_xvalue.so`。
 */

#ifndef KVLANG_KVLANGXVALUE_H
#define KVLANG_KVLANGXVALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kvspace/kvspace_xvalue.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══ 一、langtype（attr tag 1）══════════════════════════════════════ */

/* langtype 的 tag 编号。它定义在语言层：存储层只知道「attr 里有若干条记录」，
 * 不知道哪一条是类型。 */
#define KVLANG_XVALUE_ATTR_TAG_LANGTYPE 1u

/* langtype 便捷包装：等价于 GetAttr(tag = 1)，只是少了 tag 参数。
   Unbounded 版假定调用方已确保 wire 够长（head 自己会校验总长）；
   Bounded 版先验 wire_len。两者都返回字节数，取不到返回 0 且 *out = NULL。 */
static inline uint16_t kvlangXvalueAttrLangtype(const uint8_t *wire,
                                                const uint8_t **out) {
    if (out)
        *out = NULL;
    const kvspaceXvalueHead *h = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(h);
    if (total == 0)
        return 0;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, total, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

/* Bounded variant: 先验 wire_len 再扫记录。
   取不到（缺失/畸形/截断）返回 0 且 *out = NULL。 */
static inline uint16_t kvlangXvalueAttrLangtypeBounded(const uint8_t *wire,
                                                       uint64_t wire_len,
                                                       const uint8_t **out) {
    if (out)
        *out = NULL;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, wire_len, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

/* ══ 二、wire 分三段写 ═══════════════════════════════════════════════ */

/* 在 kvspace 里按 key 分配一条 wire 并写好 head：body_len 字节 body、
   attr_len 字节 attr 区。返回**映射内的 wire 指针**（借用，生命周期到该 key
   下一次 Realloc/Free）；申请失败返回 NULL。

   attr_len 必须等于随后 `kvlangxvalueWriteLangtype` 那条记录的长度
   （3 + 前缀位 + langtype 文本；空 langtype 且无前缀时为 0）。 */
uint8_t *kvlangxvalueNew(void *handle, const char *key, uint64_t body_len,
                         uint16_t attr_len);

/* 把 body 写进 head 之后。body_len 必须与 head 记录的一致；body 为 NULL 且
   body_len 为 0 合法（空 body）。 */
int kvlangxvalueWriteBody(uint8_t *wire, const uint8_t *body,
                          uint64_t body_len);

/* 在 attr 区写 tag 1 的 langtype 记录。prefix 为 `*`（Ptr）时写在文本前；
   记录长度必须与 head 的 attrslen 一致。 */
int kvlangxvalueWriteLangtype(uint8_t *wire, uint64_t body_len, char prefix,
                              const char *langtype);

/* ══ 三、构造器（16 字节 head + body + attr 区，一条完整 wire）════════ */

/* 空/NULL langtype 省略 langtype 记录（attrslen = 0）。
   成功返回 0；失败返回 -1 并清空 *out（out 非 NULL 时）。

   `bodylen` 恒为有效字节数；容量是存储后端的物理分配属性，不进入 wire。
   构造器没有 capacity 参数，也不写 attr tag 2。

   NewPtr 是唯一知道「这个值是引用」的构造点：调用方传 pointed-to 的
   基 kindexpr（不带 `*`），构造器补 `*` 前缀（空 langtype → 裸 "*"）。
   带 `*` 的输入直接拒绝——双重前缀说明两处对格式理解不一致。 */
uint8_t *kvlangXvalueNewT(void *handle, const char *key,
                               const uint8_t *raw, uint32_t raw_len,
                               const char *langtype);
/* 缓冲数组：`[]T`（长度待定）或 `[N]T`（长度已定，N 可含 `-1` 标记）；
   T 只允许基础数据类型（bool、int8..int64、uint8..uint64、float32、float64、
   char/utf8|utf32|ascii）。其它 langtype 返回 NULL。判定是内部的，不导出。 */
uint8_t *kvlangXvalueNewBufferArray(void *handle, const char *key,
                                    const uint8_t *data, uint64_t data_len,
                                    const char *langtype);
/* `[dims]T` 紧凑数组，T 允许基础数据类型但排除变长的 char/utf8
   （bool、int8..int64、uint8..uint64、float32、float64、char/utf32、
   char/ascii）；维度必须非空且只含十进制数字与逗号。其它 langtype 返回
   NULL。判定是内部的，不导出。 */
uint8_t *kvlangXvalueNewCompactArray(void *handle, const char *key,
                                     const uint64_t *dims, uint32_t ndim,
                                     const uint8_t *raw, uint64_t raw_len,
                                     const char *langtype);
uint8_t *kvlangXvalueNewPtr(void *handle, const char *key,
                            const uint8_t *path, uint32_t path_len,
                            const char *langtype);
uint8_t *kvlangXvalueNewCode(void *handle, const char *key,
                             const uint8_t *body, uint64_t body_len,
                             const char *langtype);
/* 预留 = body 区 body_len 字节零填；容量由存储后端的 reserve 参数承担。 */
uint8_t *kvlangXvalueHeadReserve(void *handle, const char *key,
                                 uint64_t body_len, const char *langtype);

/* ── 便利构造器（codec.c）─────────────────────────────────────────────
 * 与上面的 `kvlangXvalueNew*` 是**两条路**：上面那条接受任意 langtype 串，
 * 这条接受强类型实参（int64/double/bool/字节串）并自己拼 langtype。
 * 两者产出的 wire 必须逐字节相同——差异只在调用姿势。 */
uint8_t *kvlangXvalueNewPtrFromKindexpr(void *handle, const char *key,
                                        const char *target_kindexpr,
                                        const char *target);
uint8_t *kvlangXvalueNewChar(void *handle, const char *key,
                             const uint8_t *bytes, uint32_t len,
                             const char *langtype);
uint8_t *kvlangXvalueNewBool(void *handle, const char *key, uint8_t v,
                             const char *langtype);
uint8_t *kvlangXvalueNewInt64(void *handle, const char *key, int64_t v,
                              const char *langtype);
uint8_t *kvlangXvalueNewFloat64(void *handle, const char *key, double v,
                                const char *langtype);

/* ══ 三·五、值与文本（value_text.c）══════════════════════════════════
 *
 * 值与文本之间**唯一的转换点**。只覆盖有天然文本形态的类型：整数、浮点、bool、
 * `char/<enc>`、`[]char/<enc>`；Ptr / map / struct / 定长数组没有唯一文本形态，
 * 一律失败——不猜格式。 */

typedef enum kvlangXvalueTextFailure {
    KVLANG_XVALUE_TEXT_OK = 0,
    KVLANG_XVALUE_TEXT_BAD_LANGTYPE,       /* langtype 语法畸形 */
    KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE,   /* 形态合法但没有唯一文本形态 */
    KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,  /* 文本不是该类型的合法表示 */
    KVLANG_XVALUE_TEXT_OUT_OF_RANGE,       /* 数值超出该类型表示范围 */
    KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL    /* 输出缓冲装不下；不截断 */
} kvlangXvalueTextFailure;

/* 文本 → body。`output` 为 NULL 且 capacity 为 0 时只问所需长度。
   整数按小端定宽；bool 写 1 字节 0/1；char/utf32 写码点序列（4 字节小端）；
   char/utf8 写 UTF-8 字节；char/ascii 只接受 ASCII。 */
bool kvlangXvalueTextToBytes(const char *langtype, size_t langtype_length,
                             const char *value_text, size_t value_text_length,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_length,
                             kvlangXvalueTextFailure *failure);

/* body → 文本，与上面严格互逆（浮点走最短往返表示，文本可能归一化）。
   `output` 为 NULL 时只问长度；成功时输出以 NUL 结尾，长度不含 NUL。 */
bool kvlangXvalueTextFromBytes(const char *langtype, size_t langtype_length,
                               const uint8_t *body, size_t body_length,
                               char *output, size_t output_capacity,
                               size_t *output_length,
                               kvlangXvalueTextFailure *failure);

/* 失败原因的可读文本；不带句号。 */
const char *kvlangXvalueTextFailureText(kvlangXvalueTextFailure failure);

/* ══ 五、kind 串常量（const.c）══════════════════════════════════════ */

#define KVLANG_KIND_NONE       "None"
#define KVLANG_KIND_BOOL       "bool"
#define KVLANG_KIND_INT8       "int8"
#define KVLANG_KIND_INT16      "int16"
#define KVLANG_KIND_INT32      "int32"
#define KVLANG_KIND_INT64      "int64"
#define KVLANG_KIND_UINT8      "uint8"
#define KVLANG_KIND_UINT16     "uint16"
#define KVLANG_KIND_UINT32     "uint32"
#define KVLANG_KIND_UINT64     "uint64"
#define KVLANG_KIND_FLOAT32    "float32"
#define KVLANG_KIND_FLOAT64    "float64"
#define KVLANG_KIND_CHAR       "char/utf32"
#define KVLANG_KIND_CHAR_UTF8  "char/utf8"
#define KVLANG_KIND_CHAR_ASCII "char/ascii"
#define KVLANG_KIND_MAP        "stringkeymap"
#define KVLANG_KIND_RWIR       "rwir"
#define KVLANG_KIND_RWFUNC     "rwfunc"
#define KVLANG_KIND_DEF_RWIR   "def rwir"
#define KVLANG_KIND_SCOPE      "scope"
#define KVLANG_KIND_TIME       "time"
#define KVLANG_KIND_DURATION   "duration"
#define KVLANG_RUNTIME_MEMBER_SEP "\xE2\x80\xA5"

#define KVLANG_KV(X) \
    X(KVLANG_KIND_NONE) \
    X(KVLANG_KIND_BOOL) \
    X(KVLANG_KIND_INT8) \
    X(KVLANG_KIND_INT16) \
    X(KVLANG_KIND_INT32) \
    X(KVLANG_KIND_INT64) \
    X(KVLANG_KIND_UINT8) \
    X(KVLANG_KIND_UINT16) \
    X(KVLANG_KIND_UINT32) \
    X(KVLANG_KIND_UINT64) \
    X(KVLANG_KIND_FLOAT32) \
    X(KVLANG_KIND_FLOAT64) \
    X(KVLANG_KIND_CHAR) \
    X(KVLANG_KIND_CHAR_UTF8) \
    X(KVLANG_KIND_CHAR_ASCII) \
    X(KVLANG_KIND_MAP) \
    X(KVLANG_KIND_RWIR) \
    X(KVLANG_KIND_RWFUNC) \
    X(KVLANG_KIND_DEF_RWIR) \
    X(KVLANG_KIND_SCOPE) \
    X(KVLANG_KIND_TIME) \
    X(KVLANG_KIND_DURATION) \
    X(KVLANG_RUNTIME_MEMBER_SEP)

/* 运行期查询：kvlangConstLanguage("KVLANG_KIND_MAP") -> "stringkeymap"。
 * 供 Go/Python/Rust 扩展经 C ABI 取常量，避免硬编码。 */
const char *kvlangConstLanguage(const char *name);

#ifdef __cplusplus
}
#endif

#endif
