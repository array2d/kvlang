
#include <kvlang/const.h>

#include <stddef.h>
#include <string.h>

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
