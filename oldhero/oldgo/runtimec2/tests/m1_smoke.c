/* M1 冒烟：模拟 byteseek 的驱动循环，验证 runtimec2 的
 * Bootstrap / ExecuteVthread / Params / ResolveRead / NextPc。
 * 用法：m1_smoke <dsn> <funcname>   （kvspace 里需已有 layout 结果） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "kvlang_runtime.h"
#include "kvlang_vthread.h"

/* ‥pc 是系统键：无类型（langtype 空 = 空 attr）。 */
static void set_pc(void *kv, const char *vid, const char *pc) {
    char key[512];
    snprintf(key, sizeof key, "/vthread/%s/\xE2\x80\xA5pc", vid);
    uint8_t *buf = NULL;
    uint64_t len64 = 0;
    if (kvspaceXvalueHeadNewSlack((const uint8_t *)pc, (uint64_t)strlen(pc),
                                  (uint64_t)strlen(pc), "", &buf, &len64) != 0)
        return;
    char err[192] = {0};
    kvspaceSetValue(kv, key, buf, (uint32_t)len64, err, sizeof err);
    free(buf);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <dsn> <funcname>\n", argv[0]);
        return 2;
    }
    kvlangRuntime_t *rt = kvlangRuntimeConnect(argv[1]);
    if (!rt) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }
    void *kv = kvlangRuntimeKvspaceHandle(rt);
    char *vid = kvlangRuntimeBootstrap(rt, argv[2], NULL, 0);
    if (!vid || !vid[0]) {
        fprintf(stderr, "bootstrap failed\n");
        return 1;
    }
    int rc = -1;
    char *pc = NULL;
    for (;;) {
        rc = kvlangRuntimeExecuteVthread(rt, vid, &pc);
        if (rc == 0)
            break;
        if (rc != 1) {
            char mk[256];
            snprintf(mk, sizeof mk, "/vthread/%s/\xE2\x80\xA5msg", vid);
            uint8_t *md = NULL;
            uint32_t mn = 0;
            if (kvspaceGet(kv, mk, 1, &md, &mn) == 0 && md && mn &&
                mn >= KVSPACE_XVALUE_HEAD_LEN)
                fprintf(stderr, "execute failed rc=%d: %.*s\n", rc,
                        (int)kvspaceXvalueHeadLen((const kvspaceXvalueHead *)md),
                        (const char *)(md + KVSPACE_XVALUE_HEAD_LEN));
            else
                fprintf(stderr, "execute failed rc=%d\n", rc);
            return 1;
        }
        char *params = kvlangRwirextParams(kv, pc);
        char *nl = strchr(params, '\n');
        int n = 0;
        for (char *q = params; *q; q++)
            if (*q == '\n')
                n++;
        char *op = params;
        if (nl)
            *nl = 0;
        if (op && strcmp(op, "print") == 0) {
            for (int i = 0; i < n; i++) {
                char *v = kvlangRwirextResolveRead(kv, pc, i);
                fputs(v ? v : "", stdout);
                free(v);
            }
            fputc('\n', stdout);
        } else {
            fprintf(stderr, "unexpected op: %s\n", op ? op : "(null)");
            return 1;
        }
        free(params);
        char *next = kvlangRwirextNextPc(pc);
        free(pc);
        pc = NULL;
        if (!next) {
            fprintf(stderr, "next pc failed\n");
            return 1;
        }
        set_pc(kv, vid, next);
        free(next);
    }
    free(pc);
    free(vid);
    kvlangRuntimeDisconnect(rt);
    return 0;
}
