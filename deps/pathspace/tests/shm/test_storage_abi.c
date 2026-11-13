
#include <pathspace/pathspace.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SBO_DATA_SIZE (8UL * 64 * 64 * 64 * 64)

static int failures;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

int main(void) {
    char dir[] = "/tmp/pathspace-storage-abi-XXXXXX";
    if (!mkdtemp(dir))
        return 2;
    char db[256];
    snprintf(db, sizeof db, "shm://%s/db", dir);
    void *h = pathspaceConnect(db);
    CHECK(h != NULL, "connect");
    if (!h)
        return 2;

    uint64_t off = 0;
    char err[256];

    memset(err, 0, sizeof err);
    CHECK(pathspaceRealloc(h, "/v", 8, 5, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc create: %s", err);
    CHECK(off != 0, "realloc returned offset 0 (forbidden by contract)");
    uint64_t created = off;

    uint64_t got = 0;
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK,
          "resolve created: %s", err);
    CHECK(got == created, "resolve offset %llu != realloc %llu",
          (unsigned long long)got, (unsigned long long)created);

    CHECK(pathspaceResolveOffset(h, "/nope", 0 , &got, err,
                         sizeof err) == PATHSPACE_MISSING,
          "resolve missing key");

    CHECK(pathspaceRealloc(h, "/v", 4, 5, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc shrink: %s", err);
    CHECK(off == created, "in-capacity shrink must stay in place (%llu != %llu)",
          (unsigned long long)off, (unsigned long long)created);
    
    CHECK(pathspaceRealloc(h, "/v", 4, 5, &off, err, sizeof err) == PATHSPACE_OK &&
              off == created,
          "same-size realloc must stay in place");

    CHECK(pathspaceRealloc(h, "/v", 65536, 5, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc grow: %s", err);
    CHECK(off != created, "over-capacity grow must relocate (offset unchanged: %llu)",
          (unsigned long long)off);
    uint64_t moved = off;
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK && got == moved,
          "resolve must point to new offset");

    CHECK(pathspaceRealloc(h, "/v", 8, 5, &off, err, sizeof err) == PATHSPACE_OK &&
              off == moved,
          "shrink after relocation must stay in place in new box");

    CHECK(pathspaceFree(h, "/v", err, sizeof err) == PATHSPACE_OK, "free value");
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_MISSING,
          "key must be MISSING after free");
    CHECK(pathspaceFree(h, "/v", err, sizeof err) == PATHSPACE_MISSING,
          "repeated free must return MISSING");

    CHECK(pathspaceMkindex(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /d/: %s", err);
    
    uint64_t d_off = 0;
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &d_off, err,
                         sizeof err) == PATHSPACE_OK,
          "resolve /d/");
    CHECK(pathspaceMkindex(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "makeindex idempotent");
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK && got == d_off,
          "idempotent makeindex must not relocate index itself");
    
    CHECK(pathspaceMkindex(h, "/notindex", err, sizeof err) == PATHSPACE_INVALID,
          "makeindex on non-index key must return INVALID");

    {
        uint8_t *base = pathspaceXvalueInProcessAddress(h);
        CHECK(base != NULL, "shm must expose non-NULL value region base");

        uint64_t voff = 0;
        CHECK(pathspaceRealloc(h, "/base/probe", 8, 0, &voff, err, sizeof err) ==
                  PATHSPACE_OK,
              "realloc probe: %s", err);
        uint64_t roff = 0;
        CHECK(pathspaceResolveOffset(h, "/base/probe", 0, &roff, err, sizeof err) ==
                  PATHSPACE_OK,
              "resolve probe: %s", err);
        CHECK(roff == voff, "resolve offset must match realloc");
        
        const uint8_t *wire = base + roff;
        uint64_t bodylen = 0;
        for (int i = 0; i < 8; i++)
            bodylen |= (uint64_t)wire[i] << (8 * i);
        CHECK(bodylen == 8, "head bodylen expected 8 (got %llu)",
              (unsigned long long)bodylen);
        CHECK(wire[8] == 0 && wire[9] == 0, "attrslen expected 0");
        {
            int zero = 1;
            for (int i = 10; i < 16; i++)
                if (wire[i] != 0)
                    zero = 0;
            CHECK(zero, "padding must be all zeros");
        }
        CHECK(wire[16] == 0 && wire[23] == 0, "newly allocated body must be zeroed");

        uint64_t big_offset = 0;
        CHECK(pathspaceRealloc(h, "/base/grow", 1u << 20, 0, &big_offset, err,
                             sizeof err) == PATHSPACE_OK,
              "large realloc triggering expansion: %s", err);
        CHECK(pathspaceXvalueInProcessAddress(h) == base, "base address must remain unchanged after expansion");
    }

    int64_t n = -1;
    uint32_t m = 0;
    
    CHECK(pathspaceRealloc(h, "/notindex", 1, 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "seed /notindex");
    CHECK(pathspaceListlen(h, "/notindex", &n, &m, err, sizeof err) ==
              PATHSPACE_NOT_INDEX,
          "listlen on existing non-index must return NOT_INDEX");
    CHECK(pathspaceListlen(h, "/d/", &n, &m, err, sizeof err) == PATHSPACE_OK,
          "listlen empty index");
    CHECK(n == 0 && m == 0, "empty index expected N=M=0 (got N=%lld M=%u)",
          (long long)n, (unsigned)m);

    CHECK(pathspaceRealloc(h, "/d/aa", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc /d/aa");
    CHECK(pathspaceRealloc(h, "/d/bbbbbb", 2, 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "realloc /d/bbbbbb");
    CHECK(pathspaceMkindex(h, "/d/c/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /d/c/");

    CHECK(pathspaceListlen(h, "/d/", &n, &m, err, sizeof err) == PATHSPACE_OK,
          "listlen three children");
    CHECK(n == 3, "expected N=3 (got %lld)", (long long)n);
    
    CHECK(m == 6, "expected M=6 (got %u)", (unsigned)m);

    uint32_t len = 0, cnt = 0;
    uint64_t gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list NULL probe: %s", err);
    
    CHECK(len == 13, "probe length expected 13 (got %u)", (unsigned)len);
    CHECK(cnt == 3, "probe count expected 3 (got %u)", (unsigned)cnt);
    CHECK(gen != 0, "generation must advance (got 0)");
    uint64_t gen_before = gen;

    char small[8];
    len = 0;
    cnt = 0;
    gen = 0;
    CHECK(pathspaceList(h, "/d/", small, sizeof small, &len, &cnt, &gen, err,
                      sizeof err) == PATHSPACE_CAPACITY,
          "insufficient capacity must return CAPACITY");
    CHECK(len == 13 && cnt == 3, "insufficient capacity must still write required len/cnt (len=%u cnt=%u)",
          (unsigned)len, (unsigned)cnt);

    char buf[64];
    len = 0;
    cnt = 0;
    memset(buf, 0x7f, sizeof buf);
    CHECK(pathspaceList(h, "/d/", buf, sizeof buf, &len, &cnt, &gen, err,
                      sizeof err) == PATHSPACE_OK,
          "list normal: %s", err);
    CHECK(len == 13 && cnt == 3, "normal path len/cnt");
    {
        
        int seen_aa = 0, seen_bb = 0, seen_c = 0;
        uint32_t p = 0;
        while (p < len) {
            const char *name = buf + p;
            size_t l = strlen(name);
            if (strcmp(name, "aa") == 0)
                seen_aa = 1;
            else if (strcmp(name, "bbbbbb") == 0)
                seen_bb = 1;
            else if (strcmp(name, "c/") == 0)
                seen_c = 1;
            p += (uint32_t)l + 1;
        }
        CHECK(seen_aa && seen_bb && seen_c,
              "list missing children (aa=%d bb=%d c/=%d)", seen_aa, seen_bb, seen_c);
    }

    CHECK(pathspaceRealloc(h, "/d/aa", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc overwrite existing value");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list generation after value overwrite");
    CHECK(gen == gen_before,
          "value-only modification must not advance generation (%llu -> %llu)",
          (unsigned long long)gen_before, (unsigned long long)gen);

    CHECK(pathspaceRealloc(h, "/d/cc", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc add new key");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list generation after add");
    CHECK(gen != gen_before, "adding key must advance generation");
    uint64_t gen_after_add = gen;

    CHECK(pathspaceFree(h, "/d/cc", err, sizeof err) == PATHSPACE_OK,
          "free /d/cc");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list generation after delete");
    CHECK(gen != gen_after_add, "deleting key must advance generation");

    CHECK(pathspaceFree(h, "/d/", err, sizeof err) == PATHSPACE_HAS_CHILDREN,
          "non-empty index must return HAS_CHILDREN (err: %s)", err);
    CHECK(pathspaceFree(h, "/d/aa", err, sizeof err) == PATHSPACE_OK, "free aa");
    CHECK(pathspaceFree(h, "/d/bbbbbb", err, sizeof err) == PATHSPACE_OK, "free bb");
    
    CHECK(pathspaceFree(h, "/d/c/", err, sizeof err) == PATHSPACE_OK, "free c/");
    CHECK(pathspaceFree(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "empty index must be deletable (err: %s)", err);
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &got, err, sizeof err) ==
              PATHSPACE_MISSING,
          "deleted empty index must be MISSING");

    {
        uint64_t x = 0;
        CHECK(pathspaceMkindex(h, "/dt/", err, sizeof err) == PATHSPACE_OK,
              "deltree seed /dt/");
        CHECK(pathspaceMkindex(h, "/dt/src/", err, sizeof err) == PATHSPACE_OK,
              "deltree seed /dt/src/");
        
        CHECK(pathspaceRealloc(h, "/dt/src", 2, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree seed /dt/src value form");
        CHECK(pathspaceRealloc(h, "/dt/src/a", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree seed /dt/src/a");
        CHECK(pathspaceMkindex(h, "/dt/src/sub/", err, sizeof err) == PATHSPACE_OK,
              "deltree seed /dt/src/sub/");
        CHECK(pathspaceRealloc(h, "/dt/src/sub/y", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree seed /dt/src/sub/y");
        CHECK(pathspaceRealloc(h, "/dt/keep", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree seed /dt/keep");
        CHECK(pathspaceRealloc(h, "/dt/keepx/a", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree seed /dt/keepx/a");

        CHECK(pathspaceDeltree(h, "/dt/src/", err, sizeof err) == PATHSPACE_OK,
              "deltree: %s", err);
        
        CHECK(pathspaceResolveOffset(h, "/dt/src", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "root value form must be MISSING after deltree");
        CHECK(pathspaceResolveOffset(h, "/dt/src/", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "root index form must be MISSING after deltree");
        CHECK(pathspaceResolveOffset(h, "/dt/src/a", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "descendant a must be MISSING after deltree");
        CHECK(pathspaceResolveOffset(h, "/dt/src/sub/y", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "descendant sub/y must be MISSING after deltree");
        
        CHECK(pathspaceResolveOffset(h, "/dt/keep", 0, &x, err, sizeof err) == PATHSPACE_OK,
              "deltree must not delete sibling /dt/keep");
        CHECK(pathspaceResolveOffset(h, "/dt/keepx/a", 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree must not delete /dt/keepx/a");
        
        CHECK(pathspaceDeltree(h, "/dt/src/", err, sizeof err) == PATHSPACE_OK,
              "deltree must be idempotent");
        CHECK(pathspaceDeltree(h, "/dt/nope/", err, sizeof err) == PATHSPACE_OK,
              "deltree on nonexistent key must succeed idempotently");
        CHECK(pathspaceResolveOffset(h, "/dt/keep", 0, &x, err, sizeof err) == PATHSPACE_OK,
              "idempotent deltree must not affect siblings");
    }

    {
        uint64_t x = 0;
        CHECK(pathspaceMkindex(h, "/gone/", err, sizeof err) == PATHSPACE_OK,
              "seed /gone/");
        CHECK(pathspaceFree(h, "/gone/", err, sizeof err) == PATHSPACE_OK,
              "free /gone/");
        int64_t gc = -1;
        uint32_t gm = 99;
        CHECK(pathspaceListlen(h, "/gone/", &gc, &gm, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "listlen on deleted index must return MISSING (got n=%lld)", (long long)gc);
        uint32_t glen = 77, gcnt = 77;
        uint64_t ggen = 0;
        CHECK(pathspaceList(h, "/gone/", NULL, 0, &glen, &gcnt, &ggen, err,
                          sizeof err) == PATHSPACE_MISSING,
              "list on deleted index must return MISSING");

        CHECK(pathspaceMkindex(h, "/empty/", err, sizeof err) == PATHSPACE_OK,
              "seed /empty/");
        CHECK(pathspaceListlen(h, "/empty/", &gc, &gm, err, sizeof err) ==
                  PATHSPACE_OK && gc == 0 && gm == 0,
              "existing empty index must return OK/0/0 (got n=%lld m=%u)",
              (long long)gc, (unsigned)gm);
        CHECK(pathspaceList(h, "/empty/", NULL, 0, &glen, &gcnt, &ggen, err,
                          sizeof err) == PATHSPACE_OK && glen == 0 && gcnt == 0,
              "list on existing empty index must return OK/0/0");
    }

    {
        uint64_t x = 0;
        int64_t tc = -1;
        uint32_t tm = 99;
        uint32_t tl = 99, tn = 99;
        uint64_t tg = 0;

        CHECK(pathspaceListlen(h, "/never", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "nonexistent plain key must return MISSING (not NOT_INDEX)");
        CHECK(pathspaceListlen(h, "/never/", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "nonexistent index-form key must return MISSING");
        CHECK(pathspaceList(h, "/never", NULL, 0, &tl, &tn, &tg, err,
                          sizeof err) == PATHSPACE_MISSING,
              "list on nonexistent plain key must return MISSING");

        CHECK(pathspaceRealloc(h, "/plain", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "seed /plain");
        CHECK(pathspaceListlen(h, "/plain", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_NOT_INDEX,
              "existing non-index key must return NOT_INDEX");
        CHECK(pathspaceList(h, "/plain", NULL, 0, &tl, &tn, &tg, err,
                          sizeof err) == PATHSPACE_NOT_INDEX,
              "list on existing non-index key must return NOT_INDEX");

        CHECK(pathspaceListlen(h, "/empty/", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_OK && tc == 0 && tm == 0,
              "control check: existing empty index must still return OK/0/0");
    }

    {
        uint64_t y = 0;
        int64_t oc = -1;
        uint32_t om = 0;

        CHECK(pathspaceMkindex(h, "/only/child/", err, sizeof err) == PATHSPACE_OK,
              "seed /only/child/");
        
        CHECK(pathspaceListlen(h, "/only/", &oc, &om, err, sizeof err) ==
                  PATHSPACE_OK && oc == 1,
              "descendant-only index listlen must return OK with n=1");
        CHECK(om == 6, "longest child name expected \"child/\" (6 bytes), got %u",
              (unsigned)om);
        
        CHECK(pathspaceResolveOffset(h, "/only/", 0, &y, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "resolve on index without root value must return MISSING");
        
        CHECK(pathspaceListlen(h, "/only", &oc, &om, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "nonexistent value-form /only must return MISSING");
        
        CHECK(pathspaceRealloc(h, "/only/", 1, 0, &y, err, sizeof err) ==
                  PATHSPACE_OK,
              "populate root value for /only/");
        CHECK(pathspaceListlen(h, "/only", &oc, &om, err, sizeof err) ==
                  PATHSPACE_NOT_INDEX,
              "value-form /only must return NOT_INDEX once /only/ has a root value");
    }

    const int64_t dead_anchor = INT64_C(1) << 40;
    CHECK(pathspaceResolveOffset(h, "/x", dead_anchor, &got, err, sizeof err) ==
              PATHSPACE_ANCHOR_INVALID,
          "out-of-bounds key-region offset must return ANCHOR_INVALID");
    
    CHECK(pathspaceResolveOffset(h, "/x", 1, &got, err, sizeof err) ==
              PATHSPACE_ANCHOR_INVALID,
          "misaligned offset must return ANCHOR_INVALID");

    pathspaceClose(h);
    char cmd[320];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0)
        fprintf(stderr, "cleanup failed\n");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("ok: storage abi\n");
    return 0;
}
