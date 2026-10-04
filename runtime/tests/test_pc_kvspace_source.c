#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char dir[] = "/tmp/kvs-pc-source-XXXXXX";
    char dsn[256];
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    const char *stored = "/vthread/vt0/[1]/[1,0]";
    const char *stale = "/vthread/vt0/[1]/[999,0]";
    if (!mkdtemp(dir))
        return 1;
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv)
        return 1;

    kvlangVthreadSet(kv, "vt0", stored, "running");
    char *pc = NULL;
    kvlangVthreadPcGet(kv, "vt0", &pc);
    int ok = pc && strcmp(pc, stored) == 0;
    free(pc);
    if (ok)
        ok = kvlangKvcpuExecuteMode(kv, stale, KVMODE_WATCH, NULL) == -1;
    pc = NULL;
    kvlangVthreadPcGet(kv, "vt0", &pc);
    ok = ok && pc && strcmp(pc, stored) == 0;
    if (!ok)
        fprintf(stderr, "PC did not follow kvspace state\n");
    free(pc);
    kvlangKvDisconnect(kv);
    return ok ? 0 : 1;
}
