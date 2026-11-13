
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
    CHECK(off != 0, "realloc 返回 0 偏移（契约禁止）");
    uint64_t created = off;

    uint64_t got = 0;
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK,
          "resolve created: %s", err);
    CHECK(got == created, "resolve 偏移 %llu != realloc %llu",
          (unsigned long long)got, (unsigned long long)created);

    CHECK(pathspaceResolveOffset(h, "/nope", 0 , &got, err,
                         sizeof err) == PATHSPACE_MISSING,
          "resolve missing key");

    CHECK(pathspaceRealloc(h, "/v", 4, 5, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc shrink: %s", err);
    CHECK(off == created, "容量内原地必须返回同一偏移（%llu != %llu）",
          (unsigned long long)off, (unsigned long long)created);
    
    CHECK(pathspaceRealloc(h, "/v", 4, 5, &off, err, sizeof err) == PATHSPACE_OK &&
              off == created,
          "realloc 同尺寸仍应原地");

    CHECK(pathspaceRealloc(h, "/v", 65536, 5, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc grow: %s", err);
    CHECK(off != created, "超容量必须搬移（偏移没变：%llu）",
          (unsigned long long)off);
    uint64_t moved = off;
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK && got == moved,
          "resolve 应指向新偏移");

    CHECK(pathspaceRealloc(h, "/v", 8, 5, &off, err, sizeof err) == PATHSPACE_OK &&
              off == moved,
          "搬移后缩小应在新盒子内原地");

    CHECK(pathspaceFree(h, "/v", err, sizeof err) == PATHSPACE_OK, "free value");
    CHECK(pathspaceResolveOffset(h, "/v", 0 , &got, err, sizeof err) ==
              PATHSPACE_MISSING,
          "free 后应 MISSING");
    CHECK(pathspaceFree(h, "/v", err, sizeof err) == PATHSPACE_MISSING,
          "重复 free 应 MISSING");

    CHECK(pathspaceMkindex(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /d/: %s", err);
    
    uint64_t d_off = 0;
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &d_off, err,
                         sizeof err) == PATHSPACE_OK,
          "resolve /d/");
    CHECK(pathspaceMkindex(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "makeindex 幂等");
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &got, err, sizeof err) ==
              PATHSPACE_OK && got == d_off,
          "幂等 makeindex 不得搬移索引自身");
    
    CHECK(pathspaceMkindex(h, "/notindex", err, sizeof err) == PATHSPACE_INVALID,
          "makeindex 非索引 key 应 INVALID");

    {
        uint8_t *base = pathspaceXvalueInProcessAddress(h);
        CHECK(base != NULL, "shm 必须有值区基址");

        uint64_t voff = 0;
        CHECK(pathspaceRealloc(h, "/base/probe", 8, 0, &voff, err, sizeof err) ==
                  PATHSPACE_OK,
              "realloc probe: %s", err);
        uint64_t roff = 0;
        CHECK(pathspaceResolveOffset(h, "/base/probe", 0, &roff, err, sizeof err) ==
                  PATHSPACE_OK,
              "resolve probe: %s", err);
        CHECK(roff == voff, "resolve 偏移应与 realloc 一致");
        
        const uint8_t *wire = base + roff;
        uint64_t bodylen = 0;
        for (int i = 0; i < 8; i++)
            bodylen |= (uint64_t)wire[i] << (8 * i);
        CHECK(bodylen == 8, "head bodylen 应为 8（实际 %llu）",
              (unsigned long long)bodylen);
        CHECK(wire[8] == 0 && wire[9] == 0, "attrslen 应为 0");
        {
            int zero = 1;
            for (int i = 10; i < 16; i++)
                if (wire[i] != 0)
                    zero = 0;
            CHECK(zero, "padding 必须全零");
        }
        CHECK(wire[16] == 0 && wire[23] == 0, "新建 body 必须清零");

        uint64_t big_offset = 0;
        CHECK(pathspaceRealloc(h, "/base/grow", 1u << 20, 0, &big_offset, err,
                             sizeof err) == PATHSPACE_OK,
              "realloc 大块触发扩容: %s", err);
        CHECK(pathspaceXvalueInProcessAddress(h) == base, "扩容后基址必须不变");
    }

    int64_t n = -1;
    uint32_t m = 0;
    
    CHECK(pathspaceRealloc(h, "/notindex", 1, 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "seed /notindex");
    CHECK(pathspaceListlen(h, "/notindex", &n, &m, err, sizeof err) ==
              PATHSPACE_NOT_INDEX,
          "listlen 存在的非索引应 NOT_INDEX");
    CHECK(pathspaceListlen(h, "/d/", &n, &m, err, sizeof err) == PATHSPACE_OK,
          "listlen 空索引");
    CHECK(n == 0 && m == 0, "空索引应 N=M=0（实际 N=%lld M=%u）",
          (long long)n, (unsigned)m);

    CHECK(pathspaceRealloc(h, "/d/aa", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc /d/aa");
    CHECK(pathspaceRealloc(h, "/d/bbbbbb", 2, 0, &off, err, sizeof err) ==
              PATHSPACE_OK,
          "realloc /d/bbbbbb");
    CHECK(pathspaceMkindex(h, "/d/c/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /d/c/");

    CHECK(pathspaceListlen(h, "/d/", &n, &m, err, sizeof err) == PATHSPACE_OK,
          "listlen 三子项");
    CHECK(n == 3, "N 应为 3（实际 %lld）", (long long)n);
    
    CHECK(m == 6, "M 应为 6（实际 %u）", (unsigned)m);

    uint32_t len = 0, cnt = 0;
    uint64_t gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list NULL 探测: %s", err);
    
    CHECK(len == 13, "探测长度应为 13（实际 %u）", (unsigned)len);
    CHECK(cnt == 3, "探测个数应为 3（实际 %u）", (unsigned)cnt);
    CHECK(gen != 0, "generation 应前进（实际 0）");
    uint64_t gen_before = gen;

    char small[8];
    len = 0;
    cnt = 0;
    gen = 0;
    CHECK(pathspaceList(h, "/d/", small, sizeof small, &len, &cnt, &gen, err,
                      sizeof err) == PATHSPACE_CAPACITY,
          "容量不足应 CAPACITY");
    CHECK(len == 13 && cnt == 3, "容量不足仍须写出所需值（len=%u cnt=%u）",
          (unsigned)len, (unsigned)cnt);

    char buf[64];
    len = 0;
    cnt = 0;
    memset(buf, 0x7f, sizeof buf);
    CHECK(pathspaceList(h, "/d/", buf, sizeof buf, &len, &cnt, &gen, err,
                      sizeof err) == PATHSPACE_OK,
          "list 正常: %s", err);
    CHECK(len == 13 && cnt == 3, "正常路径 len/cnt");
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
              "list 缺少子项（aa=%d bb=%d c/= %d）", seen_aa, seen_bb, seen_c);
    }

    CHECK(pathspaceRealloc(h, "/d/aa", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc 覆盖已有值");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list 取值后 generation");
    CHECK(gen == gen_before,
          "纯值修改不得前进 generation（%llu -> %llu）",
          (unsigned long long)gen_before, (unsigned long long)gen);

    CHECK(pathspaceRealloc(h, "/d/cc", 2, 0, &off, err, sizeof err) == PATHSPACE_OK,
          "realloc 新增 key");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list 新增后 generation");
    CHECK(gen != gen_before, "新增 key 必须前进 generation");
    uint64_t gen_after_add = gen;

    CHECK(pathspaceFree(h, "/d/cc", err, sizeof err) == PATHSPACE_OK,
          "free /d/cc");
    gen = 0;
    CHECK(pathspaceList(h, "/d/", NULL, 0, &len, &cnt, &gen, err, sizeof err) ==
              PATHSPACE_OK,
          "list 删除后 generation");
    CHECK(gen != gen_after_add, "删除 key 必须前进 generation");

    CHECK(pathspaceFree(h, "/d/", err, sizeof err) == PATHSPACE_HAS_CHILDREN,
          "非空索引应 HAS_CHILDREN（实际 rc 见 err: %s）", err);
    CHECK(pathspaceFree(h, "/d/aa", err, sizeof err) == PATHSPACE_OK, "free aa");
    CHECK(pathspaceFree(h, "/d/bbbbbb", err, sizeof err) == PATHSPACE_OK, "free bb");
    
    CHECK(pathspaceFree(h, "/d/c/", err, sizeof err) == PATHSPACE_OK, "free c/");
    CHECK(pathspaceFree(h, "/d/", err, sizeof err) == PATHSPACE_OK,
          "空索引应可删（实际：%s）", err);
    CHECK(pathspaceResolveOffset(h, "/d/", 0 , &got, err, sizeof err) ==
              PATHSPACE_MISSING,
          "空索引删后应 MISSING");

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
              "deltree 后根（值形态）应 MISSING");
        CHECK(pathspaceResolveOffset(h, "/dt/src/", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "deltree 后根（目录形态）应 MISSING");
        CHECK(pathspaceResolveOffset(h, "/dt/src/a", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "deltree 后后代 a 应 MISSING");
        CHECK(pathspaceResolveOffset(h, "/dt/src/sub/y", 0, &x, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "deltree 后后代 sub/y 应 MISSING");
        
        CHECK(pathspaceResolveOffset(h, "/dt/keep", 0, &x, err, sizeof err) == PATHSPACE_OK,
              "deltree 不得误删兄弟 /dt/keep");
        CHECK(pathspaceResolveOffset(h, "/dt/keepx/a", 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "deltree 不得误删 /dt/keepx/a");
        
        CHECK(pathspaceDeltree(h, "/dt/src/", err, sizeof err) == PATHSPACE_OK,
              "deltree 应幂等");
        CHECK(pathspaceDeltree(h, "/dt/nope/", err, sizeof err) == PATHSPACE_OK,
              "deltree 不存在的 key 应幂等成功");
        CHECK(pathspaceResolveOffset(h, "/dt/keep", 0, &x, err, sizeof err) == PATHSPACE_OK,
              "幂等 deltree 不得波及兄弟");
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
              "不存在的索引 listlen 必须 MISSING（实际 n=%lld）", (long long)gc);
        uint32_t glen = 77, gcnt = 77;
        uint64_t ggen = 0;
        CHECK(pathspaceList(h, "/gone/", NULL, 0, &glen, &gcnt, &ggen, err,
                          sizeof err) == PATHSPACE_MISSING,
              "不存在的索引 list 必须 MISSING");

        CHECK(pathspaceMkindex(h, "/empty/", err, sizeof err) == PATHSPACE_OK,
              "seed /empty/");
        CHECK(pathspaceListlen(h, "/empty/", &gc, &gm, err, sizeof err) ==
                  PATHSPACE_OK && gc == 0 && gm == 0,
              "存在的空索引必须 OK/0/0（实际的 n=%lld m=%u）",
              (long long)gc, (unsigned)gm);
        CHECK(pathspaceList(h, "/empty/", NULL, 0, &glen, &gcnt, &ggen, err,
                          sizeof err) == PATHSPACE_OK && glen == 0 && gcnt == 0,
              "存在的空索引 list 必须 OK/0/0");
    }

    {
        uint64_t x = 0;
        int64_t tc = -1;
        uint32_t tm = 99;
        uint32_t tl = 99, tn = 99;
        uint64_t tg = 0;

        CHECK(pathspaceListlen(h, "/never", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "不存在的普通 key 应 MISSING（不是 NOT_INDEX）");
        CHECK(pathspaceListlen(h, "/never/", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "不存在的索引形态 key 应 MISSING");
        CHECK(pathspaceList(h, "/never", NULL, 0, &tl, &tn, &tg, err,
                          sizeof err) == PATHSPACE_MISSING,
              "list 不存在的普通 key 应 MISSING");

        CHECK(pathspaceRealloc(h, "/plain", 1, 0, &x, err, sizeof err) ==
                  PATHSPACE_OK,
              "seed /plain");
        CHECK(pathspaceListlen(h, "/plain", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_NOT_INDEX,
              "存在的非索引 key 应 NOT_INDEX");
        CHECK(pathspaceList(h, "/plain", NULL, 0, &tl, &tn, &tg, err,
                          sizeof err) == PATHSPACE_NOT_INDEX,
              "list 存在的非索引 key 应 NOT_INDEX");

        CHECK(pathspaceListlen(h, "/empty/", &tc, &tm, err, sizeof err) ==
                  PATHSPACE_OK && tc == 0 && tm == 0,
              "同批对照：存在的空索引仍应 OK/0/0");
    }

    {
        uint64_t y = 0;
        int64_t oc = -1;
        uint32_t om = 0;

        CHECK(pathspaceMkindex(h, "/only/child/", err, sizeof err) == PATHSPACE_OK,
              "seed /only/child/");
        
        CHECK(pathspaceListlen(h, "/only/", &oc, &om, err, sizeof err) ==
                  PATHSPACE_OK && oc == 1,
              "只有后代的索引 listlen 必须 OK n=1（实际 rc/n 见上）");
        CHECK(om == 6, "最长子项名应是 \"child/\"（6 字节），实际 %u",
              (unsigned)om);
        
        CHECK(pathspaceResolveOffset(h, "/only/", 0, &y, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "无根值的索引 resolve 应 MISSING（问值，不问子项）");
        
        CHECK(pathspaceListlen(h, "/only", &oc, &om, err, sizeof err) ==
                  PATHSPACE_MISSING,
              "不存在的值形态 /only 应 MISSING");
        
        CHECK(pathspaceRealloc(h, "/only/", 1, 0, &y, err, sizeof err) ==
                  PATHSPACE_OK,
              "给 /only/ 补根值");
        CHECK(pathspaceListlen(h, "/only", &oc, &om, err, sizeof err) ==
                  PATHSPACE_NOT_INDEX,
              "同名的值形态 /only 在同名目录有根值后应 NOT_INDEX");
    }

    const int64_t dead_anchor = INT64_C(1) << 40;
    CHECK(pathspaceResolveOffset(h, "/x", dead_anchor, &got, err, sizeof err) ==
              PATHSPACE_ANCHOR_INVALID,
          "越界 key 区偏移应 ANCHOR_INVALID");
    
    CHECK(pathspaceResolveOffset(h, "/x", 1, &got, err, sizeof err) ==
              PATHSPACE_ANCHOR_INVALID,
          "错位偏移应 ANCHOR_INVALID");

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
