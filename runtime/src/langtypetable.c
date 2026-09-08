#include "runtime_internal.h"

/* langtypetable：base kind 串 ↔ int id 双向映射（runtime 本地，IV-0，不入
 * kvspace）。 decode 期把 head.langtype 的 base kind intern 成
 * id，此后热路径纯整数比较。 */

typedef struct {
    const char *s;
    size_t len;
    int id;
} lt_ent_t;

#define LT(k, i) { k, sizeof(k) - 1, i }
static const lt_ent_t LANGTYPETABLE[] = {
    LT(KVSPACE_KIND_BOOL, KVLANG_LT_BOOL),
    LT(KVSPACE_KIND_INT8, KVLANG_LT_INT8),
    LT(KVSPACE_KIND_INT16, KVLANG_LT_INT16),
    LT(KVSPACE_KIND_INT32, KVLANG_LT_INT32),
    LT(KVSPACE_KIND_INT64, KVLANG_LT_INT64),
    LT(KVSPACE_KIND_UINT8, KVLANG_LT_UINT8),
    LT(KVSPACE_KIND_UINT16, KVLANG_LT_UINT16),
    LT(KVSPACE_KIND_UINT32, KVLANG_LT_UINT32),
    LT(KVSPACE_KIND_UINT64, KVLANG_LT_UINT64),
    LT(KVSPACE_KIND_FLOAT32, KVLANG_LT_FLOAT32),
    LT(KVSPACE_KIND_FLOAT64, KVLANG_LT_FLOAT64),
    LT(KVSPACE_KIND_CHAR, KVLANG_LT_CHAR_UTF32),
    LT(KVSPACE_KIND_CHAR_UTF8, KVLANG_LT_CHAR_UTF8),
    LT(KVSPACE_KIND_CHAR_ASCII, KVLANG_LT_CHAR_ASCII),
    LT(KVSPACE_KIND_OBJ, KVLANG_LT_OBJECT),
    LT(KVSPACE_KIND_MAP, KVLANG_LT_MAP),
    LT(KVSPACE_KIND_INDEX, KVLANG_LT_INDEX),
    LT(KVSPACE_KIND_EXT_INDEX, KVLANG_LT_EXTINDEX),
    LT(KVSPACE_KIND_RWIR, KVLANG_LT_RWIR),
    LT(KVSPACE_KIND_RWFUNC, KVLANG_LT_RWFUNC),
    LT(KVSPACE_KIND_SCOPE, KVLANG_LT_SCOPE),
    LT(KVSPACE_KIND_STRUCT, KVLANG_LT_STRUCT),
    LT(KVSPACE_KIND_TIME, KVLANG_LT_TIME),
    LT(KVSPACE_KIND_DURATION, KVLANG_LT_DURATION),
    LT(KVSPACE_KIND_NONE, KVLANG_LT_NONE),
};
#undef LT

int kvlangLangTypeId(const char *s, size_t len) {
    if (!s)
        return KVLANG_LT_UNKNOWN;
    for (size_t i = 0; i < sizeof(LANGTYPETABLE) / sizeof(LANGTYPETABLE[0]); i++)
        if (LANGTYPETABLE[i].len == len && memcmp(LANGTYPETABLE[i].s, s, len) == 0)
            return LANGTYPETABLE[i].id;
    return KVLANG_LT_UNKNOWN;
}

const char *kvlangLangTypeKind(int id) {
    for (size_t i = 0; i < sizeof(LANGTYPETABLE) / sizeof(LANGTYPETABLE[0]); i++)
        if (LANGTYPETABLE[i].id == id)
            return LANGTYPETABLE[i].s;
    return "";
}

int kvlangXvalueLangTypeId(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return KVLANG_LT_NONE;
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) < 0)
        return KVLANG_LT_NONE;
    kvlangLangtype kx;
    kvlangLangtypeParse(h.langtype, &kx);
    return kvlangLangTypeId(kx.kind, (size_t)kx.kind_len);
}
