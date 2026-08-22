#include "runtime_internal.h"
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define NTHREAD 8
#define NPER    100

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

static kvlangKv_t *open_kv(void) {
    char dsn[128];
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-incr-%d-%ld", (int)getpid(), (long)time(NULL));
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv) fail("connect");
    return kv;
}

typedef struct { kvlangKv_t *kv; const char *key; int64_t got[NPER]; } incr_arg_t;

static void *worker(void *p) {
    incr_arg_t *a = p;
    for (int i = 0; i < NPER; i++) a->got[i] = kvlangVthreadNextSeq(a->kv, a->key);
    return NULL;
}

int main(void) {
    kvlangKv_t *kv = open_kv();

    int64_t first = 0;
    char err[256];
    if (kvlangKvIncr(kv, "/seq-start", &first, err, sizeof err) != 0) fail("first incr");
    if (first != 1) fail("missing key must start at 1");
    for (int i = 2; i <= 12; i++) {
        int64_t n = 0;
        if (kvlangKvIncr(kv, "/seq-start", &n, err, sizeof err) != 0) fail("walk incr");
        if (n != i) fail("char counter must parse multi-digit values");
    }

    const char *key = "/seq-race";
    incr_arg_t args[NTHREAD];
    pthread_t th[NTHREAD];
    for (int i = 0; i < NTHREAD; i++) {
        args[i].kv = kv;
        args[i].key = key;
        pthread_create(&th[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < NTHREAD; i++) pthread_join(th[i], NULL);

    int seen[NTHREAD * NPER + 1];
    memset(seen, 0, sizeof seen);
    int max = 0;
    for (int t = 0; t < NTHREAD; t++) {
        for (int i = 0; i < NPER; i++) {
            int64_t v = args[t].got[i];
            if (v < 1 || v > NTHREAD * NPER) fail("seq out of range");
            if (seen[v]) fail("duplicate seq");
            seen[v] = 1;
            if ((int)v > max) max = (int)v;
        }
    }
    if (max != NTHREAD * NPER) fail("missing seq values");

    kvlangXvalue_t g; kvlangXvalueZero(&g);
    kvlangKvGetOne(kv, key, &g);
    char *s = kvlangXvalueNone(&g) ? NULL : kvlangXvalueValueString(&g);
    kvlangXvalueFree(&g);
    char want[32];
    snprintf(want, sizeof want, "%d", NTHREAD * NPER);
    if (!s || strcmp(s, want) != 0) fail("persisted counter != last seq");
    free(s);

    kvlangKvDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
