
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

const char *kvlangConstLanguage(const char *name);

#endif
