#include "runtime_internal.h"
#include <pthread.h>
#include <time.h>
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
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-delegate-%d", (int)getpid());
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv) fail("connect");
    char err[256];
    kvlangKvMkindex(kv, "/sys/rwir-backend/", err, sizeof err);
    kvlangKvMkindex(kv, "/sys/task/", err, sizeof err);
    kvlangKvMkindex(kv, "/vthread/", err, sizeof err);
    kvlangKvMkindex(kv, "/data/", err, sizeof err);
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

static void register_backend(kvlangKv_t *kv, const char *name, const char *opcode, const char *status) {
    char *dir = kvlangKeytreeSysRwirBackend(name);
    char path[256];
    snprintf(path, sizeof path, "%s/", dir);
    char err[256];
    kvlangKvMkindex(kv, path, err, sizeof err);
    snprintf(path, sizeof path, "%s/op/", dir);
    kvlangKvMkindex(kv, path, err, sizeof err);
    free(dir);
    char *opk = kvlangKeytreeSysRwirBackendOp(name, opcode);
    set_char(kv, opk, "1");
    free(opk);
    char *sk = kvlangKeytreeSysRwirBackendStatus(name);
    set_char(kv, sk, status);
    free(sk);
    char *hk = kvlangKeytreeSysRwirBackendHeartbeat(name);
    char beat[32];
    snprintf(beat, sizeof beat, "%lld", (long long)time(NULL));
    set_char(kv, hk, beat);
    free(hk);
}

static kvlangRwirInst_t make_inst(const char *op, const char *write_key) {
    kvlangRwirInst_t inst;
    memset(&inst, 0, sizeof inst);
    inst.opcode = strdup(op);
    inst.nr = 0;
    inst.nw = 1;
    inst.reads = NULL;
    inst.writes = calloc(1, sizeof(kvlangParam_t));
    inst.writes[0].name = strdup(write_key);
    kvlangXvalueZero(&inst.writes[0].val);
    return inst;
}

typedef struct {
    kvlangKv_t *kv;
    char *cmd;
    char *out_key;
    char *status_prefix;
    int write_out;
    int delay_ms;
    volatile int ran;
} exec_arg_t;

static void *executor(void *p) {
    exec_arg_t *a = p;
    for (int i = 0; i < 2000; i++) {
        char *cmd = get_char(a->kv, a->cmd);
        if (cmd && cmd[0]) {
            free(cmd);
            if (a->delay_ms > 0) usleep((useconds_t)a->delay_ms * 1000);
            if (a->write_out) set_char(a->kv, a->out_key, "ok");
            /* status key is /sys/task/<id>.status; id is inside cmd JSON */
            char *json = get_char(a->kv, a->cmd);
            const char *rid = json ? strstr(json, "\"request_id\":\"") : NULL;
            if (!rid) { free(json); a->ran = 1; return NULL; }
            rid += 14;
            const char *end = strchr(rid, '"');
            if (!end) { free(json); a->ran = 1; return NULL; }
            char id[160];
            size_t n = (size_t)(end - rid);
            if (n >= sizeof id) n = sizeof id - 1;
            memcpy(id, rid, n); id[n] = 0;
            free(json);
            char *sk = kvlangKeytreeSysTask(id, "status");
            set_char(a->kv, sk, "done");
            free(sk);
            a->ran = 1;
            return NULL;
        }
        free(cmd);
        usleep(1000);
    }
    return NULL;
}

static void test_check_write_key(void) {
    expect(kvlangKeytreeCheckWriteKey("1", "/data/out") == NULL, "user global allowed");
    char *e = kvlangKeytreeCheckWriteKey("1", "/lib/x");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/lib rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/sys/rwir-backend/fake/cmd");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/sys rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/vthread/2/x");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "other vthread rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "rel");
    expect(e && strncmp(e, "ValueError", 10) == 0, "relative rejected");
    free(e);
}

static void test_is_delegated(kvlangKv_t *kv) {
    expect(!kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "no backend");
    usleep(300000);
    register_backend(kv, "fake", "fake.echo", "ready");
    expect(kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "on duty");
    expect(kvlangDispatchIsDelegatedOp(kv, "/lib/fake.echo"), "folded opcode");
    char *sk = kvlangKeytreeSysRwirBackendStatus("fake");
    set_char(kv, sk, "offline");
    free(sk);
    usleep(300000);
    expect(!kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "offline not delegated");
}

static void test_stale_heartbeat(kvlangKv_t *kv) {
    register_backend(kv, "stale", "stale.op", "ready");
    char *hk = kvlangKeytreeSysRwirBackendHeartbeat("stale");
    set_char(kv, hk, "1");
    free(hk);
    expect(!kvlangDispatchIsDelegatedOp(kv, "stale.op"), "stale heartbeat");
}

static void test_delegate_ok(kvlangKv_t *kv) {
    register_backend(kv, "okb", "ok.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCmd("okb");
    exec_arg_t a = { kv, cmd, "/data/out", NULL, 1, 0, 0 };
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangRwirInst_t inst = make_inst("ok.echo", "/data/out");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    pthread_join(th, NULL);
    expect(rc == KVLANG_DELEGATE_OK, "delegate ok");
    char *out = get_char(kv, "/data/out");
    expect(out && strcmp(out, "ok") == 0, "output written");
    free(out);
    kvlangRwirInstFree(&inst);
    free(cmd);
}

static void test_write_target(kvlangKv_t *kv) {
    register_backend(kv, "wb", "w.echo", "ready");
    kvlangRwirInst_t inst = make_inst("w.echo", "/lib/evil");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    expect(rc == KVLANG_DELEGATE_ERR, "write-target fails");
    char *pc = NULL, *st = NULL;
    kvlangVthreadGet(kv, "1", &pc, &st);
    expect(st && strcmp(st, "error") == 0, "error status");
    free(pc); free(st);
    kvlangRwirInstFree(&inst);
}

static void test_done_without_output(kvlangKv_t *kv) {
    register_backend(kv, "nb", "n.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCmd("nb");
    exec_arg_t a = { kv, cmd, "/data/nout", NULL, 0, 0, 0 };
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangRwirInst_t inst = make_inst("n.echo", "/data/nout");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    pthread_join(th, NULL);
    expect(rc == KVLANG_DELEGATE_ERR, "empty output is failure");
    kvlangRwirInstFree(&inst);
    free(cmd);
}

static void test_timeout(kvlangKv_t *kv) {
    register_backend(kv, "tb", "t.echo", "ready");
    kvlangDispatchSetDefaultTimeoutNs(40000000LL);
    kvlangRwirInst_t inst = make_inst("t.echo", "/data/tout");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    kvlangDispatchSetDefaultTimeoutNs(30LL * 1000000000LL);
    expect(rc == KVLANG_DELEGATE_ERR, "timeout");
    kvlangRwirInstFree(&inst);
}

static char *error_msg(kvlangKv_t *kv, const char *vtid) {
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangKeytreeVthreadStatusMsg(vtid, "error", &k);
    char *s = get_char(kv, k.p);
    kvlangStrbufFree(&k);
    return s;
}

static void test_one_definition(kvlangKv_t *kv) {
    register_backend(kv, "db", "dup.op", "ready");
    uint8_t raw[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewTlv(&v, KVSPACE_KIND_RWFUNC, raw, 4, 1);
    char *sk = kvlangKeytreeLibSig("dup.op");
    kvlangKvPair_t p = { sk, v };
    char err[256];
    kvlangKvSet(kv, &p, 1, err, sizeof err);
    kvlangXvalueFree(&v);
    kvlangRwirInst_t inst = make_inst("dup.op", "/data/dup");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    expect(rc == KVLANG_DELEGATE_ERR, "rwfunc + backend conflict");
    char *msg = error_msg(kv, "1");
    expect(msg && strstr(msg, "one opcode, one definition"), "conflict names both definitions");
    free(msg);
    kvlangRwirInstFree(&inst);
    free(sk);
}

static void *executor_out_only(void *p) {
    exec_arg_t *a = p;
    for (int i = 0; i < 2000; i++) {
        char *cmd = get_char(a->kv, a->cmd);
        if (cmd && cmd[0]) {
            free(cmd);
            set_char(a->kv, a->out_key, "ok");
            a->ran = 1;
            return NULL;
        }
        free(cmd);
        usleep(1000);
    }
    return NULL;
}

static void test_output_without_done(kvlangKv_t *kv) {
    register_backend(kv, "od", "od.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCmd("od");
    exec_arg_t a = { kv, cmd, "/data/odout", NULL, 1, 0, 0 };
    pthread_t th;
    pthread_create(&th, NULL, executor_out_only, &a);
    kvlangDispatchSetDefaultTimeoutNs(80000000LL);
    kvlangRwirInst_t inst = make_inst("od.echo", "/data/odout");
    kvlangVthreadSet(kv, "1", "/vthread/1/[1,0]", "running");
    int rc = kvlangDispatchDelegate(kv, "1", "/vthread/1/[1,0]", &inst);
    pthread_join(th, NULL);
    kvlangDispatchSetDefaultTimeoutNs(30LL * 1000000000LL);
    expect(rc == KVLANG_DELEGATE_ERR, "output without done is failure");
    kvlangRwirInstFree(&inst);
    free(cmd);
}

static void test_missing_heartbeat(kvlangKv_t *kv) {
    char *dir = kvlangKeytreeSysRwirBackend("nohb");
    char path[256];
    snprintf(path, sizeof path, "%s/", dir);
    char err[256];
    kvlangKvMkindex(kv, path, err, sizeof err);
    snprintf(path, sizeof path, "%s/op/", dir);
    kvlangKvMkindex(kv, path, err, sizeof err);
    free(dir);
    char *opk = kvlangKeytreeSysRwirBackendOp("nohb", "nohb.op");
    set_char(kv, opk, "1");
    free(opk);
    char *sk = kvlangKeytreeSysRwirBackendStatus("nohb");
    set_char(kv, sk, "ready");
    free(sk);
    expect(!kvlangDispatchIsDelegatedOp(kv, "nohb.op"), "missing heartbeat is off duty");
}

int main(void) {
    test_check_write_key();
    kvlangKv_t *kv = open_kv();
    test_is_delegated(kv);
    test_stale_heartbeat(kv);
    test_delegate_ok(kv);
    test_write_target(kv);
    test_done_without_output(kv);
    test_timeout(kv);
    test_one_definition(kv);
    test_output_without_done(kv);
    test_missing_heartbeat(kv);
    kvlangKvDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
