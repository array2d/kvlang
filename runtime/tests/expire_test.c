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
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-expire-%d", (int)getpid());
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv) fail("connect");
    char err[256];
    kvlangKvMkindex(kv, "/e/", err, sizeof err);
    kvlangKvMkindex(kv, "/e2/", err, sizeof err);
    kvlangKvMkindex(kv, "/e/kdir/", err, sizeof err);
    return kv;
}

static void set_char(kvlangKv_t *kv, const char *key, const char *s) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    kvlangKvPair_t p = { (char *)key, v };
    char err[256];
    if (kvlangKvSet(kv, &p, 1, err, sizeof err) != 0) fail(err[0] ? err : "set");
    kvlangXvalueFree(&v);
}

static char *get_char(kvlangKv_t *kv, const char *key) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return s;
}

static int list_has(kvlangKv_t *kv, const char *dir, const char *name) {
    char **names = NULL; int n = 0;
    kvlangKvList(kv, dir, false, false, &names, &n);
    int hit = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(names[i], name) == 0) hit = 1;
        free(names[i]);
    }
    free(names);
    return hit;
}

int main(void) {
    kvlangKv_t *kv = open_kv();
    char err[256];

    set_char(kv, "/e2/only", "v");
    expect(kvlangKvExpire(kv, "/e2/only", 0, err, sizeof err) != 0, "zero duration must fail");
    expect(kvlangKvExpire(kv, "relkey", 40000000ULL, err, sizeof err) != 0, "relative key must fail");
    expect(kvlangKvExpire(kv, "/e2/missing", 40000000ULL, err, sizeof err) != 0, "missing key must fail");
    expect(kvlangKvExpire(kv, "/e2/", 40000000ULL, err, sizeof err) != 0, "directory must fail");

    set_char(kv, "/e/k", "v");
    set_char(kv, "/e/kdir/x", "1");
    expect(kvlangKvExpire(kv, "/e/k", 40000000ULL, err, sizeof err) == 0, "expire file");
    char *got = get_char(kv, "/e/k");
    expect(got && strcmp(got, "v") == 0, "Get must still see the value before TTL");
    free(got);
    expect(!list_has(kv, "/e/", "k"), "List must drop the file name at Expire");
    expect(list_has(kv, "/e/", "kdir"), "Expire of a file must not drop the sibling dir");
    got = get_char(kv, "/e/kdir/x");
    expect(got && strcmp(got, "1") == 0, "sibling dir contents must survive Expire of the file");
    free(got);

    set_char(kv, "/e2/only2", "v");
    expect(kvlangKvExpire(kv, "/e2/only2", 40000000ULL, err, sizeof err) == 0, "expire only");
    expect(!list_has(kv, "/e2/", "only2"), "List must drop the lone file");

    int gone = 0;
    for (int i = 0; i < 40; i++) {
        got = get_char(kv, "/e/k");
        if (!got) { gone = 1; break; }
        free(got);
        usleep(10000);
    }
    expect(gone, "Get must be None after TTL");

    kvlangKvDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
