
#include <kvspace/const.h>
#include <kvspace/kvspace_xvalue.h> 

#include <stddef.h>
#include <string.h>

KVSPACE_PUBLIC const char *kvspaceConstStorage(const char *name) {
    static const struct { const char *key; const char *val; } table[] = {
#define KVSPACE_CONST_ENTRY(name) { #name, name },
        KVSPACE_KV(KVSPACE_CONST_ENTRY)
#undef KVSPACE_CONST_ENTRY
    };
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].key, name) == 0)
            return table[i].val;
    return NULL;
}
