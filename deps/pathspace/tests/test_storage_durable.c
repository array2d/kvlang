
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
    CHECK(dl != NULL, "无法打开已加载的 durable 后端以核对符号");
    if (dl) {
        CHECK(dlsym(dl, "pathspaceXvalueInProcessAddress") != NULL,
              "durable 未导出 pathspaceXvalueInProcessAddress：契约要求导出并返回 NULL，"
              "而非靠前端「缺符号即 NULL」蒙对");
        dlclose(dl);
    }
    CHECK(pathspaceXvalueInProcessAddress(kv) == NULL, "durable 的 pathspaceXvalueInProcessAddress 必须返回 NULL");

    clear_err();
    CHECK(pathspaceResolveOffset(kv, "/du/a", 0, &offset, err, sizeof err) ==
              PATHSPACE_UNSUPPORTED,
          "durable 的 resolve 应返回 PATHSPACE_UNSUPPORTED");
    CHECK(err[0] != '\0', "durable 的 resolve 拒绝时 err 不该为空");

    offset = 0x5a5a5a5a5a5a5a5aULL;
    clear_err();
    CHECK(pathspaceRealloc(kv, "/du/a", 8, 0, &offset, err, sizeof err) ==
              PATHSPACE_UNSUPPORTED,
          "durable 的 realloc 应返回 PATHSPACE_UNSUPPORTED");
    CHECK(err[0] != '\0', "durable 的 realloc 拒绝时 err 不该为空");
}

static void test_makeindex_free(void *kv) {
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "makeindex 失败: %s", err);
    
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "makeindex 第二次应幂等成功: %s", err);

    clear_err();
    CHECK(pathspaceFree(kv, "/idx/", err, sizeof err) == PATHSPACE_OK,
          "free 空索引失败: %s", err);

    CHECK(pathspaceMkindex(kv, "/idx2/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /idx2/ 失败: %s", err);
    clear_err();
    CHECK(pathspaceMkindex(kv, "/idx2/child/", err, sizeof err) == PATHSPACE_OK,
          "建子索引失败: %s", err);
    clear_err();
    CHECK(pathspaceFree(kv, "/idx2/", err, sizeof err) == PATHSPACE_HAS_CHILDREN,
          "free 非空索引应返回 HAS_CHILDREN");
}

static void test_list(void *kv) {
    int64_t count = -1;
    uint32_t max_name = 0;

    CHECK(pathspaceMkindex(kv, "/ls/", err, sizeof err) == PATHSPACE_OK,
          "makeindex /ls/ 失败: %s", err);

    clear_err();
    CHECK(pathspaceListlen(kv, "/ls/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "listlen 空索引失败: %s", err);
    CHECK(count == 0 && max_name == 0, "空索引 N/M 应为 0/0，实得 %lld/%u",
          (long long)count, max_name);

    const char *names[] = {"aa", "bbb", "c"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char key[64];
        snprintf(key, sizeof key, "/ls/%s/", names[i]);
        clear_err();
        CHECK(pathspaceMkindex(kv, key, err, sizeof err) == PATHSPACE_OK,
              "建子索引 %s 失败: %s", key, err);
    }

    clear_err();
    CHECK(pathspaceListlen(kv, "/ls/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "listlen 失败: %s", err);
    CHECK(count == 3, "N 应为 3，实得 %lld", (long long)count);
    
    CHECK(max_name == 4, "M 应为最长目录项字节数 4，实得 %u", max_name);

    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", NULL, 0, &length, &listed, &generation, err,
                      sizeof err) == PATHSPACE_OK,
          "list(NULL 探测) 失败: %s", err);
    
    CHECK(listed == 3, "list 探测 count 应为 3，实得 %u", listed);

    uint32_t short_len = 0, short_count = 0;
    uint64_t short_gen = 0;
    char buf[4];
    clear_err();
    CHECK(pathspaceList(kv, "/ls/", buf, sizeof buf, &short_len, &short_count,
                      &short_gen, err, sizeof err) == PATHSPACE_CAPACITY,
          "容量不足应返回 CAPACITY");
    CHECK(short_count == 3, "容量不足仍须回报 count=3，实得 %u", short_count);
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
          "从没建过的 /never/ 应返回 MISSING，而非 OK/0/0");
    clear_err();
    CHECK(pathspaceListlen(kv, "/never", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "从没建过的 /never 应返回 MISSING");
    clear_err();
    CHECK(pathspaceList(kv, "/never/", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_MISSING,
          "从没建过的 /never/ 上 list 应返回 MISSING");
    clear_err();
    CHECK(pathspaceList(kv, "/never", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_MISSING,
          "从没建过的 /never 上 list 应返回 MISSING");

    clear_err();
    CHECK(pathspaceMkindex(kv, "/empty/", err, sizeof err) == PATHSPACE_OK,
          "建空索引 /empty/ 失败: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/empty/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "已存在的空索引应返回 OK: %s", err);
    CHECK(count == 0 && max_name == 0,
          "已存在的空索引 N/M 应为 0/0，实得 %lld/%u", (long long)count,
          max_name);
    clear_err();
    CHECK(pathspaceList(kv, "/empty/", names, sizeof names, &length, &listed,
                      &generation, err, sizeof err) == PATHSPACE_OK,
          "已存在的空索引上 list 应返回 OK: %s", err);
    CHECK(length == 0 && listed == 0,
          "已存在的空索引 list 应得 len=0 count=0，实得 %u/%u", length, listed);

    clear_err();
    CHECK(pathspaceMkindex(kv, "/full/", err, sizeof err) == PATHSPACE_OK,
          "建 /full/ 失败: %s", err);
    clear_err();
    CHECK(pathspaceMkindex(kv, "/full/ab/", err, sizeof err) == PATHSPACE_OK,
          "建 /full/ab/ 失败: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/full/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "非空索引应返回 OK: %s", err);
    
    CHECK(count == 1 && max_name == 3,
          "非空索引应得 N=1 M=3，实得 %lld/%u", (long long)count, max_name);

    clear_err();
    CHECK(pathspaceMkindex(kv, "/desc/child/", err, sizeof err) == PATHSPACE_OK,
          "建 /desc/child/ 失败: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/desc/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "有后代但无根值的 /desc/ 应返回 OK（index 形态可由后代产生）: %s",
          err);
    
    CHECK(count == 1 && max_name == 6,
          "有后代无根值应得 N=1 M=6，实得 %lld/%u", (long long)count, max_name);
    clear_err();
    CHECK(pathspaceListlen(kv, "/desc", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "非索引形态 /desc 自己没有根值 → MISSING（「有后代即存在」只管 index 形态）");

}

static void test_copy_and_delete(void *kv) {
    CHECK(pathspaceMkindex(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "建 /cd/src/ 失败: %s", err);
    CHECK(pathspaceMkindex(kv, "/cd/src/m/", err, sizeof err) == PATHSPACE_OK,
          "建 /cd/src/m/ 失败: %s", err);

    clear_err();
    CHECK(pathspaceCpdir(kv, "/cd/src/", "/cd/dst/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceCpdir 失败: %s", err);

    int64_t count = 0;
    uint32_t max_name = 0;
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/dst/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "cpdir 之后目标索引不可列出: %s", err);
    CHECK(count == 1, "cpdir 之后目标应有 1 个子项，实得 %lld",
          (long long)count);

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "pathspaceDeltree 失败: %s", err);
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/src/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "deltree 之后根索引仍存在（应为连根删）");
    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/src/m/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_MISSING,
          "deltree 之后后代仍存在");

    clear_err();
    CHECK(pathspaceDeltree(kv, "/cd/src/", err, sizeof err) == PATHSPACE_OK,
          "deltree 不存在的 key 应视为成功");

    clear_err();
    CHECK(pathspaceListlen(kv, "/cd/dst/", &count, &max_name, err, sizeof err) ==
              PATHSPACE_OK,
          "deltree 连带删掉了兄弟子树 /cd/dst/");
}

int main(void) {
    char dsn[256], path[256];
    snprintf(dsn, sizeof dsn, "fs:///tmp/pathspace_storage_durable_%ld",
             (long)getpid());
    snprintf(path, sizeof path, "/tmp/pathspace_storage_durable_%ld",
             (long)getpid());

    void *kv = pathspaceConnect(dsn);
    if (!kv) {
        fprintf(stderr, "connect %s failed（后端是否已导出新符号？）\n", dsn);
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
            printf("探测: 闸拦下: pathspaceResolveOffset/pathspaceXvalueInProcessAddress 均缺失\n");
        else
            printf("探测: 闸放行（后端已导出新 ABI 独有符号）\n");
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
        fprintf(stderr, "清理 %s 失败（不影响结论）\n", path);
    }

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
