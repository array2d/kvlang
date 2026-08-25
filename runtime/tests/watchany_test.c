#include "runtime_internal.h"
#include <unistd.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

static void expect(bool ok, const char *msg) {
    if (!ok) fail(msg);
}

static kvlangKv_t *open_kv(void) {
    char dsn[128];
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-watchany-%d", (int)getpid());
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv) fail("connect");
    return kv;
}

static void notify_char(kvlangKv_t *kv, const char *key, const char *s) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    char err[256];
    if (kvlangKvNotify(kv, key, &v, err, sizeof err) != 0) fail(err[0] ? err : "notify");
    kvlangXvalueFree(&v);
}

static char *take_char(kvlangKv_t *kv, const char *key, uint64_t ns) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    if (kvlangKvTake(kv, key, ns, &v) != 0) fail("take");
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return s;
}

int main(void) {
    kvlangKv_t *kv = open_kv();
    const uint64_t to = 200000000ULL;
    const char *a = "/wa/a";
    const char *b = "/wa/b";
    const char *keys[2] = { a, b };

    notify_char(kv, b, "B");
    char *got_key = NULL;
    kvlangXvalue_t got; kvlangXvalueZero(&got);
    if (kvlangKvWatchAny(kv, keys, 2, to, &got_key, &got) != 0) fail("watchany");
    char *got_s = kvlangXvalueNone(&got) ? NULL : kvlangXvalueValueString(&got);
    expect(got_key && strcmp(got_key, b) == 0 && got_s && strcmp(got_s, "B") == 0,
           "WatchAny must return the notified key");
    free(got_key); free(got_s); kvlangXvalueFree(&got);

    notify_char(kv, a, "A");
    notify_char(kv, b, "B2");
    got_key = NULL; kvlangXvalueZero(&got);
    if (kvlangKvWatchAny(kv, keys, 2, to, &got_key, &got) != 0) fail("watchany2");
    expect(got_key && !kvlangXvalueNone(&got), "WatchAny lost a queued Notify");
    const char *other = (got_key && strcmp(got_key, a) == 0) ? b : a;
    if (got_key && strcmp(got_key, a) != 0 && strcmp(got_key, b) != 0) fail("unexpected key");
    char *left = take_char(kv, other, to);
    expect(left != NULL, "WatchAny must consume only the delivered key");
    free(left); free(got_key); kvlangXvalueFree(&got);

    got_key = NULL; kvlangXvalueZero(&got);
    if (kvlangKvWatchAny(kv, keys, 2, to, &got_key, &got) != 0) fail("watchany empty");
    expect(!got_key && kvlangXvalueNone(&got), "WatchAny must time out when nothing is queued");
    kvlangXvalueFree(&got);

    kvlangKvDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
