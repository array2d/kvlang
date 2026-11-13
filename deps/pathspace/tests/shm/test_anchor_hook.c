
#define _GNU_SOURCE

#include "pathspace/pathspace.h"
#include "pathspace_shm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

int main(void) {
    char path[] = "/tmp/pathspace-anchor-hook-XXXXXX";
    char *dir = mkdtemp(path);
    if (!dir) {
        perror("mkdtemp");
        return 1;
    }
    char dsn[512];
    
    snprintf(dsn, sizeof dsn, "shm://%s/db", dir);

    void *h = pathspaceConnect(dsn);
    if (!h) {
        fprintf(stderr, "FAIL: connect %s\n", dsn);
        return 1;
    }
    char err[256] = {0};
    pathspace_t *kv = (pathspace_t *)h;

    int rc = pathspaceMkindex(h, "/d/", err, sizeof err);
    CHECK(rc == PATHSPACE_OK, "mkindex /d/ rc=%d err=%s", rc, err);
    uint64_t off = 0;
    rc = pathspaceRealloc(h, "/d/x", 8, 0, &off, err, sizeof err);
    CHECK(rc == PATHSPACE_OK, "realloc /d/x rc=%d err=%s", rc, err);
    
    uint8_t *base = pathspaceXvalueInProcessAddress(h);
    CHECK(base != NULL, "in-process address must not be NULL for shm");
    if (base) {
        for (int i = 0; i < 8; i++)
            base[off + 16 + (uint64_t)i] = (uint8_t)(i + 1);
    }

    int64_t anchor = 0;
    rc = pathspaceShmTestAnchorOf(kv, "/d/", &anchor);
    CHECK(rc == PATHSPACE_OK, "anchor_of /d/ rc=%d", rc);
    CHECK(anchor > 0, "anchor must be a positive key-region offset, got %lld",
          (long long)anchor);

    uint64_t child_off = 0;
    memset(err, 0, sizeof err);
    rc = pathspaceResolveOffset(h, "/d/x", (uint64_t)anchor, &child_off, err,
                        sizeof err);
    CHECK(rc == PATHSPACE_OK,
          "resolving /d/x via hook anchor must succeed (rc=%d err=%s)",
          rc, err);
    CHECK(child_off == off,
          "offset resolved via anchor must match Realloc offset: %llu vs %llu",
          (unsigned long long)child_off, (unsigned long long)off);

    int64_t untouched = -12345;
    rc = pathspaceShmTestAnchorOf(kv, "/no-such-parent/", &untouched);
    CHECK(rc == PATHSPACE_MISSING,
          "nonexistent parent node must return MISSING (rc=%d, expected %d)", rc, PATHSPACE_MISSING);
    CHECK(untouched == -12345, "out_anchor must not be modified on failure, got %lld",
          (long long)untouched);

    CHECK(pathspaceShmTestAnchorOf(NULL, "/d/", &anchor) == PATHSPACE_INVALID,
          "NULL kv must return INVALID");
    CHECK(pathspaceShmTestAnchorOf(kv, NULL, &anchor) == PATHSPACE_INVALID,
          "NULL key must return INVALID");
    CHECK(pathspaceShmTestAnchorOf(kv, "/d/", NULL) == PATHSPACE_INVALID,
          "NULL out must return INVALID");

    pathspaceClose(h);
    
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "warning: cleanup %s failed\n", dir);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("anchor_hook: all checks passed\n");
    return 0;
}
