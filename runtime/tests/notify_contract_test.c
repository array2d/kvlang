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
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-notify-%d", (int)getpid());
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

    notify_char(kv, "/q/early", "1");
    char *got = take_char(kv, "/q/early", to);
    expect(got && strcmp(got, "1") == 0, "a Notify posted before Take was lost");
    free(got);

    notify_char(kv, "/q/queued", "first");
    notify_char(kv, "/q/queued", "second");
    char *a = take_char(kv, "/q/queued", to);
    char *b = take_char(kv, "/q/queued", to);
    expect(a && b, "only one of 2 queued tasks was delivered");
    int saw_first = (strcmp(a, "first") == 0) || (strcmp(b, "first") == 0);
    int saw_second = (strcmp(a, "second") == 0) || (strcmp(b, "second") == 0);
    expect(saw_first && saw_second, "both tasks must survive");
    free(a); free(b);

    notify_char(kv, "/q/consumed", "x");
    got = take_char(kv, "/q/consumed", to);
    expect(got && strcmp(got, "x") == 0, "the only Notify was not delivered");
    free(got);
    got = take_char(kv, "/q/consumed", to);
    expect(!got, "Take redelivered a consumed task");
    free(got);

    notify_char(kv, "/q/outside", "hidden");
    kvlangXvalue_t tree; kvlangXvalueZero(&tree);
    kvlangKvGetOne(kv, "/q/outside", &tree);
    expect(kvlangXvalueNone(&tree), "Notify must not write the user-visible key");
    kvlangXvalueFree(&tree);
    got = take_char(kv, "/q/outside", to);
    expect(got && strcmp(got, "hidden") == 0, "side queue lost the Notify");
    free(got);

    kvlangKvDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
