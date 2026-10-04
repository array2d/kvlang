#include "runtime_internal.h"

#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    char dir[] = "/tmp/kvlang-crash-resume-XXXXXX";
    char dsn[256];
    if (!mkdtemp(dir))
        return 1;
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        kvlangKv_t *kv = kvlangKvConnect(dsn);
        if (!kv)
            _exit(2);
        kvlangStrbuf_t key;
        kvlangStrbufInit(&key);
        kvlangKeytreeVthreadPc("vt0", &key);
        int rc = kvlangKvSetChar(kv, key.p, "/lib/f/[2,0]");
        kvlangStrbufFree(&key);
        kvlangXvalue_t value;
        kvlangXvalueNewInt64(&value, 73);
        kvlangKvPair_t pair = {"/value", value};
        char err[128];
        if (rc == 0)
            rc = kvlangKvSet(kv, &pair, 1, err, sizeof err);
        kvlangXvalueFree(&value);
        _exit(rc == 0 ? 0 : 3);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
        return 1;
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv)
        return 1;
    char *pc = NULL;
    kvlangVthreadPcGet(kv, "vt0", &pc);
    int ok = pc && strcmp(pc, "/lib/f/[2,0]") == 0;
    free(pc);
    kvlangXvalue_t value;
    kvlangXvalueZero(&value);
    kvspaceHead_t head;
    if (kvlangKvGetOne(kv, "/value", &value) != 0 ||
        kvlangXvalueHead(&value, &head) != 0 || head.body_len != 8)
        ok = 0;
    else {
        const uint8_t *body = value.data + head.body_offset;
        ok = ok && body[0] == 73;
        for (int i = 1; i < 8; i++)
            ok = ok && body[i] == 0;
    }
    kvlangXvalueFree(&value);
    kvlangKvDisconnect(kv);
    return ok ? 0 : 1;
}
