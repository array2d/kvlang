
#include <pathspace/const.h>
#include <pathspace/pathspace.h>
#include <pathspace/pathspace_xvalue.h>

#include <stddef.h>
#include <stdio.h>

static const void *const exported_symbols[] = {
    
    (const void *)&pathspaceConnect,
    (const void *)&pathspaceClose,
    
    (const void *)&pathspaceResolveOffset,
    (const void *)&pathspaceRealloc,
    (const void *)&pathspaceFree,
    (const void *)&pathspaceCp,
    (const void *)&pathspaceMkindex,
    (const void *)&pathspaceDeltree,
    (const void *)&pathspaceCpdir,
    (const void *)&pathspaceListlen,
    (const void *)&pathspaceList,
    
    (const void *)&pathspaceXvalueInProcessAddress,
    
    (const void *)&pathspaceXvalueGetAttr,
    (const void *)&pathspaceXvalueSetAttr,
    (const void *)&pathspaceXvalueHeadEffectiveLen,
    
    (const void *)&pathspaceConstStorage,
};

_Static_assert(sizeof(exported_symbols) / sizeof(exported_symbols[0]) == 16U,
               "export surface changed: update this table and README together");

int main(void) {
    
    for (size_t at = 0; at < sizeof(exported_symbols) / sizeof(exported_symbols[0]);
         ++at) {
        if (exported_symbols[at] == NULL) {
            fprintf(stderr, "FAIL export_surface: symbol %zu resolved to NULL\n", at);
            return 1;
        }
    }
    
    const char *separator = pathspaceConstStorage("PATHSPACE_MEMBER_SEP");
    if (separator == NULL || separator[0] == '\0') {
        fprintf(stderr, "FAIL export_surface: pathspaceConstStorage returned empty\n");
        return 1;
    }
    printf("ok: %zu exported symbols linked, const query works\n",
           sizeof(exported_symbols) / sizeof(exported_symbols[0]));
    return 0;
}
