
#include <pathspace/const.h>
#include <pathspace/pathspace_xvalue.h> 

#include <stddef.h>
#include <string.h>

PATHSPACE_PUBLIC const char *pathspaceConstStorage(const char *name) {
    static const struct { const char *key; const char *val; } table[] = {
#define PATHSPACE_CONST_ENTRY(name) { #name, name },
        PATHSPACE_KV(PATHSPACE_CONST_ENTRY)
#undef PATHSPACE_CONST_ENTRY
    };
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].key, name) == 0)
            return table[i].val;
    return NULL;
}
