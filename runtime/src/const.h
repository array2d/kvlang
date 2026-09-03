#pragma once

/* ── 字符串常量唯一定义处（对齐 kvspace-durable/src/const.rs）──────────
 * kind 常量 + 路径/成员分隔符。C 源码一律引用此处，禁止硬编码裸字符串。 */

/* ── XValueHead kind ───────────────────────────────────────────────── */

#define KVSPACE_KIND_NONE       "None"
#define KVSPACE_KIND_BOOL       "bool"
#define KVSPACE_KIND_INT8       "int8"
#define KVSPACE_KIND_INT16      "int16"
#define KVSPACE_KIND_INT32      "int32"
#define KVSPACE_KIND_INT64      "int64"
#define KVSPACE_KIND_UINT8      "uint8"
#define KVSPACE_KIND_UINT16     "uint16"
#define KVSPACE_KIND_UINT32     "uint32"
#define KVSPACE_KIND_UINT64     "uint64"
#define KVSPACE_KIND_FLOAT32    "float32"
#define KVSPACE_KIND_FLOAT64    "float64"
#define KVSPACE_KIND_CHAR       "char/utf32"
#define KVSPACE_KIND_CHAR_UTF8  "char/utf8"
#define KVSPACE_KIND_CHAR_ASCII "char/ascii"
#define KVSPACE_KIND_OBJ        "object"
#define KVSPACE_KIND_MAP        "stringkeymap"
#define KVSPACE_KIND_INDEX      "index"
#define KVSPACE_KIND_EXT_INDEX  "extindex"
#define KVSPACE_KIND_RWIR       "rwir"
#define KVSPACE_KIND_RWFUNC     "rwfunc"
#define KVSPACE_KIND_DEF_RWIR   "defrwir"
#define KVSPACE_KIND_DEF_RWFUNC "defrwfunc"
#define KVSPACE_KIND_RWIR_OR_RWFUNC "rwir|rwfunc"
#define KVSPACE_KIND_SCOPE      "scope"
#define KVSPACE_KIND_STRUCT     "struct"
#define KVSPACE_KIND_TIME       "time"
#define KVSPACE_KIND_DURATION   "duration"

/* ── 路径/成员分隔符 ───────────────────────────────────────────────── */

#define PATH_SEP           "/"
#define DIR_INDEX_SUF      "/"
#define MEMBER_SEP         "\xC2\xB7"   /* · U+00B7 中点号：成员分隔符，释放 '.' 供小数 key */
#define MEMBER_SEP_LEN     2            /* MEMBER_SEP 的 UTF-8 字节数 */
#define INDEX_VALUE_SEP    "\n"
#define RUNTIME_MEMBER_SEP "\xE2\x80\xA5"   /* ‥ U+2025 */
#define EXT_INDEX_HEAD     "\xE2\x80\xA6"   /* … U+2026 */
