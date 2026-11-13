
#include <pathspace/pathspace.h>
#include <pathspace/pathspace_xvalue.h>
#include "shm/test_wire.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, ...)                                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

static char err[512];
static void clear_err(void) { memset(err, 0, sizeof err); }

static void test_offsets_unsupported(void *kv) {
    uint64_t offset = 0x5a5a5a5a5a5a5a5aULL;

    void *dl = dlopen("libpathspace_durable.so.1", RTLD_NOW | RTLD_NOLOAD);
    CHECK(dl != NULL, "failed to dlopen already-loaded durable backend to verify symbols");
    if (dl) {
        CHECK(dlsym(dl, "pathspaceXvalueInProcessAddress") != NULL,
              "durable backend must export pathspaceXvalueInProcessAddress and return NULL");
        dlclose(dl);
    }
    CHECK(pathspaceXvalueInProcessAddress(kv) == NULL,
          "durable pathspaceXvalueInProcessAddress must return NULL");

    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/du/a", 0, &offset, err, sizeof err) ==
              PATHSPACE_UNSUPPORTED,
          "durable resolve must return PATHSPACE_UNSUPPORTED");
    CHECK(err[0] != '\0', "durable resolve rejection must populate err");

    offset = 0x5a5a5a5a5a5a5a5aULL;
    clear_err();
    CHECK(pathspaceRealloc(kv, "/du/a", 8, 0, &offset, err, sizeof err) ==
              PATHSPACE_UNSUPPORTED,
          "durable realloc must return PATHSPACE_UNSUPPORTED");
    CHECK(err[0] != '\0', "durable realloc rejection must populate err");
}

static void test_makeindex_free(void *kv) {
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "makeindex failed: %s", err);
    
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "second makeindex should succeed idempotently: %s", err);

    clear_err();
    CHECK(pathspaceFree(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "free empty index failed: %s", err);

    CHECK(pathspaceMkindex(kv, "/idx2/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /idx2/ failed: %s", err);
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx2/child/", err, sizeof err) == PATHSPACE_OK,
          "create child index failed: %s", err);
    clear_err();
    CHECK(pathspaceFree(kv, "/idx2/", err, sizeof err) == PATHSPACE_HAS_CHILDREN,
          "free non-empty index must return HAS_CHILDREN");
}

static void test_list(void *kv) {
    int64_t count = -1;
    uint32_t max_name = 0;

    CHECK(pathspaceMkindex(kv, "/ls/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /ls/ failed: %s", err);

    clear_err();
    CHECK(pathspaceListlen(kv, "/ls/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "listlen empty index failed: %s", err);
    CHECK(count == 0 && max_name == 0, "empty index N/M expected 0/0, got %lld/%u",
          (long long)count, max_name);

    const char *names[] = {"aa", "bbb", "c"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char key[64];
        snprintf(key, sizeof key, "/ls/%s/", names[i]);
        clear_err();
        CHECK(pathspaceMkindex(kv, key, err, sizeof err) == PATHSPACE_OK,
              "create child index %s failed: %s", key, err);
    }

    clear_err();
    CHECK(pathspaceListlen(kv, "/ls/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "listlen failed: %s", err);
    CHECK(count == 3, "expected N=3, got %lld", (long long)count);
    
    CHECK(max_name == 4, "expected M=4 (longest child entry bytes), got %u", max_name);

    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &generation, err,
                      sizeof err) == PATHSPACE_OK,
          "list(NULL probe) failed: %s", err);
    
    CHECK(listed == 3, "expected list probe count=3, got %u", listed);

    uint32_t short_len = 0, short_count = 0;
    uint64_t short_gen = 0;
    char buf[4];
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", buf, sizeof buf, &short_len, &short_count,
                      &short_gen, err, sizeof err) == PATHSPACE_CAPACITY,
          "insufficient capacity must return CAPACITY");
    CHECK(short_count == 3, "insufficient capacity must still report count=3, got %u", short_count);
}

static void test_existence_three_states(void *kv) {
    int64_t count = -1;
    uint32_t max_name = 0;
    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    char names[64];

    clear_err();
    CHECK(pathspaceListlen(kv, "/never/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "nonexistent /never/ must return MISSING, not OK/0/0");
    clear_err();
    CHECK(pathspaceListlen(kv, "/never", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "nonexistent /never must return MISSING");
    clear_err();
    CHECK(pathspaceList(kv, "/never/", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_MISSING,
          "list on nonexistent /never/ must return MISSING");
    clear_err();
    CHECK(pathspaceList(kv, "/never", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_MISSING,
          "list on nonexistent /never must return MISSING");

    clear_err();
    CHECK(pathspaceMkindex(kv, "/empty/", err, sizeof err) == PATHSPACE_OK,
          "create empty index /empty/ failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/empty/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "existing empty index must return OK: %s", err);
    CHECK(count == 0 && max_name == 0,
          "existing empty index expected N/M=0/0, got %lld/%u", (long long)count,
          max_name);
    clear_err();
    CHECK(pathspaceList(kv, "/empty/", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_OK,
          "list on existing empty index must return OK: %s", err);
    CHECK(length == 0 && listed == 0,
          "list on existing empty index expected len=0 count=0, got %u/%u", length, listed);

    clear_err();
    CHECK(pathspaceMkindex(kv, "/full/", err, sizeof err) == PATHSPACE_OK,
          "create /full/ failed: %s", err);
    clear_err();
    CHECK(pathspaceMkindex(kv, "/full/ab/", err, sizeof err) == PATHSPACE_OK,
          "create /full/ab/ failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/full/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "non-empty index must return OK: %s", err);
    
    CHECK(count == 1 && max_name == 3,
          "non-empty index expected N=1 M=3, got %lld/%u", (long long)count, max_name);

    clear_err();
    CHECK(pathspaceMkindex(kv, "/desc/child/", err, sizeof err) == PATHSPACE_OK,
          "create /desc/child/ failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/desc/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "/desc/ with descendants and no root value must return OK: %s",
          err);
    
    CHECK(count == 1 && max_name == 6,
          "descendant-only index expected N=1 M=6, got %lld/%u", (long long)count, max_name);
    clear_err();
    CHECK(pathspaceListlen(kv, "/desc", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "value-form /desc without root value must return MISSING");

}

static void test_copy_and_delete(void *kv) {
    CHECK(pathspaceMkindex(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "create /cd/src/ failed: %s", err);
    CHECK(pathspaceMkindex(kv, "/cd/src/m/", err, sizeof err) == PATHSPACE_OK,
          "create /cd/src/m/ failed: %s", err);

    clear_err();
    CHECK(pathspaceCpdir(kv, "/cd/src/", "/cd/dst/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceCpdir failed: %s", err);

    int64_t count = 0;
    uint32_t max_name = 0;
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/dst/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "destination index not listable after cpdir: %s", err);
    CHECK(count == 1, "expected 1 child in destination after cpdir, got %lld",
          (long long)count);

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceDeltree failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/src/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "root index still exists after deltree");
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/src/m/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "descendant still exists after deltree");

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "deltree on nonexistent key must succeed idempotently");

    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/dst/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "deltree deleted sibling subtree /cd/dst/");
}

int main(void) {
    char dsn[256], path[256];
    snprintf(dsn, sizeof dsn, "fs:///tmp/pathspace_storage_durable_%ld",
             (long)getpid());
    snprintf(path, sizeof path, "/tmp/pathspace_storage_durable_%ld",
             (long)getpid());

    void *kv = pathspaceConnect(dsn);
    if (!kv) {
        fprintf(stderr, "connect %s failed (does backend export the storage ABI?)\n", dsn);
        return 2;
    }

    {
        clear_err();
        const uint8_t *langtype = NULL;
        uint16_t langtype_len = 0;
        (void)pathlangXvalueHeadLangtypeBounded(NULL, 0, &langtype);
        int64_t probe_count = 0;
        uint32_t probe_max = 0;
        const int probe_rc =
            pathspaceListlen(kv, "/__probe_never__/", &probe_count, &probe_max,
                           err, sizeof err);
        if (strstr(err, "has no new storage ABI") != NULL)
            printf("probe: gated: pathspaceResolveOffset/pathspaceXvalueInProcessAddress missing\n");
        else
            printf("probe: passed (backend exports storage ABI symbols)\n");
        (void)probe_rc;
        (void)langtype_len;
    }

    test_offsets_unsupported(kv);
    test_makeindex_free(kv);
    test_list(kv);
    test_existence_three_states(kv);
    test_copy_and_delete(kv);

    pathspaceClose(kv);

    char cmd[320];
    snprintf(cmd, sizeof cmd, "rm -rf %s", path);
    if (system(cmd) != 0) {
        fprintf(stderr, "cleanup %s failed (non-fatal)\n", path);
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
