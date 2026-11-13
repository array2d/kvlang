
#ifndef PATHLANG_CONST_H
#define PATHLANG_CONST_H

#define PATHLANG_KIND_NONE       "None"
#define PATHLANG_KIND_BOOL       "bool"
#define PATHLANG_KIND_INT8       "int8"
#define PATHLANG_KIND_INT16      "int16"
#define PATHLANG_KIND_INT32      "int32"
#define PATHLANG_KIND_INT64      "int64"
#define PATHLANG_KIND_UINT8      "uint8"
#define PATHLANG_KIND_UINT16     "uint16"
#define PATHLANG_KIND_UINT32     "uint32"
#define PATHLANG_KIND_UINT64     "uint64"
#define PATHLANG_KIND_FLOAT32    "float32"
#define PATHLANG_KIND_FLOAT64    "float64"
#define PATHLANG_KIND_CHAR       "char/utf32"
#define PATHLANG_KIND_CHAR_UTF8  "char/utf8"
#define PATHLANG_KIND_CHAR_ASCII "char/ascii"
#define PATHLANG_KIND_MAP        "stringkeymap"
#define PATHLANG_KIND_RWIR       "rwir"
#define PATHLANG_KIND_RWFUNC     "rwfunc"
#define PATHLANG_KIND_DEF_RWIR   "def rwir"
#define PATHLANG_KIND_SCOPE      "scope"
#define PATHLANG_KIND_TIME       "time"
#define PATHLANG_KIND_DURATION   "duration"

#define PATHLANG_KV(X) \
    X(PATHLANG_KIND_NONE) \
    X(PATHLANG_KIND_BOOL) \
    X(PATHLANG_KIND_INT8) \
    X(PATHLANG_KIND_INT16) \
    X(PATHLANG_KIND_INT32) \
    X(PATHLANG_KIND_INT64) \
    X(PATHLANG_KIND_UINT8) \
    X(PATHLANG_KIND_UINT16) \
    X(PATHLANG_KIND_UINT32) \
    X(PATHLANG_KIND_UINT64) \
    X(PATHLANG_KIND_FLOAT32) \
    X(PATHLANG_KIND_FLOAT64) \
    X(PATHLANG_KIND_CHAR) \
    X(PATHLANG_KIND_CHAR_UTF8) \
    X(PATHLANG_KIND_CHAR_ASCII) \
    X(PATHLANG_KIND_MAP) \
    X(PATHLANG_KIND_RWIR) \
    X(PATHLANG_KIND_RWFUNC) \
    X(PATHLANG_KIND_DEF_RWIR) \
    X(PATHLANG_KIND_SCOPE) \
    X(PATHLANG_KIND_TIME) \
    X(PATHLANG_KIND_DURATION)

const char *pathlangConstLanguage(const char *name);

#endif
