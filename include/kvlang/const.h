/* const.h — kvlang 语言层常量：kind 串。
 *
 * 存储常量（路径分隔符、错误串）在 kvspace 的 `<kvspace/const.h>`，不在这里。
 * 两个头都提供运行期查询入口，互不越界：
 *
 *   kvlangConstLanguage("KVLANG_KIND_MAP")  -> "stringkeymap"
 *   kvspaceConstStorage("KVSPACE_MEMBER_SEP") -> "·"
 *
 * `KVLANG_KV(X)` 是全部语言常量的唯一清单；C 侧展开成查找表，Rust 侧用 build.rs
 * 解析同一文件的 `#define KVLANG_* "…"` 行生成常量。
 */

#ifndef KVLANG_CONST_H
#define KVLANG_CONST_H

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
    X(KVLANG_KIND_DURATION)

/* 运行期查询：kvlangConstLanguage("KVLANG_KIND_MAP") -> "stringkeymap"。
 * 供 Go/Python/Rust 扩展经 C ABI 取常量，避免硬编码。 */
const char *kvlangConstLanguage(const char *name);

#endif
