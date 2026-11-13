
#include <pathspace/pathspace.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void test_resolve_realloc(void *kv) {
    uint64_t first = 0, again = 0, grown = 0;

    uint8_t *base = pathspaceXvalueInProcessAddress(kv);
    CHECK(base != NULL, "shm backend must return non-NULL pathspaceXvalueInProcessAddress");

    clear_err();
    CHECK(pathspaceRealloc(kv, "/ct/a", 8, 0, &first, err, sizeof err) == PATHSPACE_OK,
          "realloc create failed: %s", err);

    if (base) {
        uint64_t probe = 0x0123456789ABCDEFULL;
        memcpy(base + first, &probe, sizeof probe);
        uint64_t back = 0;
        memcpy(&back, base + first, sizeof back);
        CHECK(back == probe, "bytes read back from base+offset did not match");
    }

    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/ct/a", 0, &again, err, sizeof err) == PATHSPACE_OK,
          "resolve existing key failed: %s", err);
    CHECK(again == first,
          "resolved offset (%llu) does not match realloc offset (%llu)",
          (unsigned long long)again, (unsigned long long)first);

    clear_err();
    int rc = pathspaceResolveOffset(kv, "/ct/never", 0, &again, err, sizeof err);
    CHECK(rc == PATHSPACE_MISSING,
          "resolve missing key expected MISSING, got %d (%s)", rc, err);

    clear_err();
    CHECK(pathspaceRealloc(kv, "/ct/a", 8, 0, &grown, err, sizeof err) == PATHSPACE_OK,
          "same-size realloc failed: %s", err);
    CHECK(grown == first,
          "same-size realloc must not relocate: offset changed from %llu to %llu",
          (unsigned long long)first, (unsigned long long)grown);
}

static void test_makeindex_free(void *kv) {
    uint64_t off = 0;

    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "makeindex failed: %s", err);
    
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "second makeindex should succeed idempotently: %s", err);

    clear_err();
    CHECK(pathspaceFree(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "free empty index failed: %s", err);

    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx2/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /idx2/ failed: %s", err);
    clear_err();
    CHECK(pathspaceRealloc(kv, "/idx2/member", 4, 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "create member under /idx2/ failed: %s", err);
    clear_err();
    CHECK(pathspaceFree(kv, "/idx2/", err, sizeof err) == PATHSPACE_HAS_CHILDREN,
          "free non-empty index must return HAS_CHILDREN");
    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/idx2/member", 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "member must remain intact after failed free on non-empty index: %s", err);
}

static void test_list(void *kv) {
    uint64_t off = 0;
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
        snprintf(key, sizeof key, "/ls/%s", names[i]);
        clear_err();
        CHECK(pathspaceRealloc(kv, key, 1, 0, &off, err, sizeof err) == PATHSPACE_OK,
              "create member %s failed: %s", key, err);
    }

    clear_err();
    CHECK(pathspaceListlen(kv, "/ls/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "listlen failed: %s", err);
    CHECK(count == 3, "expected N=3, got %lld", (long long)count);
    
    CHECK(max_name == 3, "expected M=3 (longest child name bytes), got %u", max_name);

    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &generation, err,
                      sizeof err) == PATHSPACE_OK,
          "list(NULL probe) failed: %s", err);
    CHECK(length == 9 && listed == 3,
          "list probe expected length=9 count=3, got %u/%u", length, listed);

    char buf[32];
    uint32_t length2 = 0, listed2 = 0;
    uint64_t generation2 = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", buf, sizeof buf, &length2, &listed2,
                      &generation2, err, sizeof err) == PATHSPACE_OK,
          "list copy failed: %s", err);
    CHECK(length2 == 9 && listed2 == 3,
          "list copy expected length=9 count=3, got %u/%u", length2, listed2);
    CHECK(memcmp(buf, "aa\0bbb\0c\0", 9) == 0,
          "list NUL-separated output mismatch (first 9 bytes)");

    uint32_t short_len = 0, short_count = 0;
    uint64_t short_gen = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", buf, 4, &short_len, &short_count, &short_gen,
                      err, sizeof err) == PATHSPACE_CAPACITY,
          "insufficient capacity must return CAPACITY");
    CHECK(short_len == 9 && short_count == 3,
          "insufficient capacity must still report required length=9 count=3, got %u/%u", short_len,
          short_count);

    uint64_t gen_before = 0, gen_add = 0, gen_modify = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &gen_before, err,
                      sizeof err) == PATHSPACE_OK,
          "read baseline generation failed: %s", err);
    char key[64];
    snprintf(key, sizeof key, "/ls/%s", "dddd");
    CHECK(pathspaceRealloc(kv, key, 1, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "add member failed: %s", err);
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &gen_add, err,
                      sizeof err) == PATHSPACE_OK,
          "read generation after add failed: %s", err);
    CHECK(gen_add != gen_before, "generation must advance after adding member, still %llu",
          (unsigned long long)gen_add);

    CHECK(pathspaceRealloc(kv, key, 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "update member value failed: %s", err);
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &gen_modify, err,
                      sizeof err) == PATHSPACE_OK,
          "read generation after value update failed: %s", err);
    CHECK(gen_modify == gen_add,
          "value-only update must not advance generation, changed from %llu to %llu",
          (unsigned long long)gen_add, (unsigned long long)gen_modify);

    clear_err();
    CHECK(pathspaceListlen(kv, "/ct/a", &count, &max_name, err, sizeof err) ==
              PATHSPACE_NOT_INDEX,
          "listlen on non-index must return NOT_INDEX");
}

static void test_existence_three_states(void *kv) {
    int64_t count = -1;
    uint32_t max_name = 0;
    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    char names[64];
    uint64_t off = 0;

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
    CHECK(pathspaceRealloc(kv, "/plain", 4, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "create plain value /plain failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/plain", &count, &max_name, err, sizeof err) ==
              PATHSPACE_NOT_INDEX,
          "existing plain value /plain must return NOT_INDEX");
    clear_err();
    CHECK(pathspaceList(kv, "/plain", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_NOT_INDEX,
          "list on existing plain value /plain must return NOT_INDEX");

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
    CHECK(pathspaceRealloc(kv, "/full/ab", 1, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "create /full/ab failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/full/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "non-empty index must return OK: %s", err);
    CHECK(count == 1 && max_name == 2,
          "non-empty index expected N=1 M=2, got %lld/%u", (long long)count, max_name);

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

    clear_err();
    CHECK(pathspaceRealloc(kv, "/only/x", 4, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "create /only/x failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/only", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "/only with descendants only (no root value) must return MISSING");
    clear_err();
    CHECK(pathspaceRealloc(kv, "/only", 4, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "populate root value /only failed: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/only", &count, &max_name, err, sizeof err) ==
              PATHSPACE_NOT_INDEX,
          "once root value is set, /only must return NOT_INDEX");
}

static void test_copy_and_delete(void *kv) {
    uint64_t off = 0;
    int64_t count_for_root = 0;
    uint32_t max_for_root = 0;

    CHECK(pathspaceRealloc(kv, "/cp/src", 4, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "create /cp/src failed: %s", err);
    clear_err();
    CHECK(pathspaceCp(kv, "/cp/src", "/cp/dst", err, sizeof err) == PATHSPACE_OK,
          "pathspaceCp failed: %s", err);
    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/cp/dst", 0, &off, err, sizeof err) == PATHSPACE_OK,
          "destination missing after cp: %s", err);

    CHECK(pathspaceMkindex(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "create /cd/src/ failed: %s", err);
    CHECK(pathspaceRealloc(kv, "/cd/src/m", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "create /cd/src/m failed: %s", err);
    clear_err();
    CHECK(pathspaceCpdir(kv, "/cd/src/", "/cd/dst/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceCpdir failed: %s", err);
    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/cd/dst/m", 0, &off, err, sizeof err) == PATHSPACE_OK,
          "child missing after cpdir: %s", err);

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceDeltree failed: %s", err);
    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/cd/src/m", 0, &off, err, sizeof err) ==
              PATHSPACE_MISSING,
          "child still resolvable after deltree");
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/src/", &count_for_root, &max_for_root, err,
                         sizeof err) == PATHSPACE_MISSING,
          "root index still exists after deltree");

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "deltree on nonexistent key must succeed idempotently");
    
    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/cd/dst/m", 0, &off, err, sizeof err) == PATHSPACE_OK,
          "deltree deleted sibling subtree /cd/dst/");
}

int main(void) {
    
    char dsn[256];
    snprintf(dsn, sizeof dsn, "shm:///tmp/pathspace_storage_shm_%ld.shm",
             (long)getpid());

    void *kv = pathspaceConnect(dsn);
    if (!kv) {
        fprintf(stderr, "connect %s failed (does backend export the storage ABI?)\n", dsn);
        return 2;
    }

    test_resolve_realloc(kv);
    test_makeindex_free(kv);
    test_list(kv);
    test_existence_three_states(kv);
    test_copy_and_delete(kv);

    pathspaceClose(kv);

    char path[256];
    snprintf(path, sizeof path, "/tmp/pathspace_storage_shm_%ld.shm",
             (long)getpid());
    unlink(path);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
