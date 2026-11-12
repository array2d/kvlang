
#define _GNU_SOURCE

#include "kvspace/kvspace.h"
#include "kvspace_shm.h"

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
    char path[] = "/tmp/kvspace-anchor-hook-XXXXXX";
    char *dir = mkdtemp(path);
    if (!dir) {
        perror("mkdtemp");
        return 1;
    }
    char dsn[512];
    
    snprintf(dsn, sizeof dsn, "shm://%s/db", dir);

    void *h = kvspaceConnect(dsn);
    if (!h) {
        fprintf(stderr, "FAIL: connect %s\n", dsn);
        return 1;
    }
    char err[256] = {0};
    kvspace_t *kv = (kvspace_t *)h;

    int rc = kvspaceMkindex(h, "/d/", err, sizeof err);
    CHECK(rc == KVSPACE_OK, "mkindex /d/ rc=%d err=%s", rc, err);
    uint64_t off = 0;
    rc = kvspaceRealloc(h, "/d/x", 8, 0, &off, err, sizeof err);
    CHECK(rc == KVSPACE_OK, "realloc /d/x rc=%d err=%s", rc, err);
    
    uint8_t *base = kvspaceXvalueInProcessAddress(h);
    CHECK(base != NULL, "in-process address must not be NULL for shm");
    if (base) {
        for (int i = 0; i < 8; i++)
            base[off + 16 + (uint64_t)i] = (uint8_t)(i + 1);
    }

    int64_t anchor = 0;
    rc = kvspaceShmTestAnchorOf(kv, "/d/", &anchor);
    CHECK(rc == KVSPACE_OK, "anchor_of /d/ rc=%d", rc);
    CHECK(anchor > 0, "anchor 必须是正的 key 区偏移，实际 %lld",
          (long long)anchor);

    uint64_t child_off = 0;
    memset(err, 0, sizeof err);
    rc = kvspaceResolveOffset(h, "/d/x", (uint64_t)anchor, &child_off, err,
                        sizeof err);
    CHECK(rc == KVSPACE_OK,
          "用钩子给的 anchor 解析 /d/x 必须成功（rc=%d err=%s）—— "
          "否则这个 anchor 就不是可用锚点",
          rc, err);
    CHECK(child_off == off,
          "经 anchor 解析出的偏移应与 Realloc 返回的一致：%llu vs %llu",
          (unsigned long long)child_off, (unsigned long long)off);

    int64_t untouched = -12345;
    rc = kvspaceShmTestAnchorOf(kv, "/no-such-parent/", &untouched);
    CHECK(rc == KVSPACE_MISSING,
          "不存在的父节点必须 MISSING（rc=%d，期望 %d）", rc, KVSPACE_MISSING);
    CHECK(untouched == -12345, "失败时不得写 out_anchor，实际 %lld",
          (long long)untouched);

    CHECK(kvspaceShmTestAnchorOf(NULL, "/d/", &anchor) == KVSPACE_INVALID,
          "NULL kv 必须 INVALID");
    CHECK(kvspaceShmTestAnchorOf(kv, NULL, &anchor) == KVSPACE_INVALID,
          "NULL key 必须 INVALID");
    CHECK(kvspaceShmTestAnchorOf(kv, "/d/", NULL) == KVSPACE_INVALID,
          "NULL out 必须 INVALID");

    kvspaceClose(h);
    
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
