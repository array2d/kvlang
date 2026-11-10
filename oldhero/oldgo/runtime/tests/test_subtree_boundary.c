/* Unit tests for `kvlangKeyInSubtree`: subtree of `P` (after stripping one trailing `/` or `·`)
 * matches `P` itself and keys continuing with `/`, `·`, or `‥` (including `/m·k` on `DelTree("/m")`). */
#include "runtime_internal.h"
#include <stdio.h>
static int failures;
static void check(const char *prefix, const char *key, bool want) {
    size_t n = kvlangSubtreeBaseLen(prefix);
    bool got = kvlangKeyInSubtree(key, prefix, n);
    if (got != want) {
        failures++;
        fprintf(stderr, "FAIL: prefix=%s key=%s got %d want %d\n", prefix,
                key, got, want);
    }
}
int main(void) {
    /* Exact `P` and the three subtree separators */
    check("/m", "/m", true);
    check("/m", "/m/x", true);
    check("/m", "/m\xC2\xB7k", true);          /* /m·k */
    check("/m", "/m\xC2\xB7k\xC2\xB7j", true); /* /m·k·j */
    check("/m", "/m\xE2\x80\xA5lib", true);    /* /m‥lib */
    /* Shared string prefix outside the subtree */
    check("/m", "/mm", false);
    check("/m", "/m.k", false);
    check("/m", "/m\xC2\xB8", false);          /* C2 B8 != C2 B7 (`·`) */
    check("/m", "/m\xE2\x80\xA6", false);      /* E2 80 A6 != E2 80 A5 (`‥`) */
    check("/m", "/n\xC2\xB7k", false);
    check("/m", "/", false);
    /* Single trailing `/` or `·` normalizes to base */
    check("/m/", "/m", true);
    check("/m/", "/m\xC2\xB7k", true);
    check("/m/", "/m/x", true);
    check("/m/", "/mm", false);
    check("/m\xC2\xB7", "/m", true);
    check("/m\xC2\xB7", "/m\xC2\xB7k", true);
    check("/m\xC2\xB7", "/m/x", true);
    check("/m\xC2\xB7", "/mx", false);
    /* Strip at most one trailing separator */
    check("/m//", "/m/", true);
    check("/m//", "/m", false);
    /* Member prefix `/m·k` excludes sibling `/m·kk` */
    check("/m\xC2\xB7k", "/m\xC2\xB7k/0", true);
    check("/m\xC2\xB7k", "/m\xC2\xB7kk", false);
    /* Root `/` matches all absolute keys */
    check("/", "/a", true);
    check("/", "/a\xC2\xB7k", true);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("subtree_boundary: ok\n");
    return 0;
}
