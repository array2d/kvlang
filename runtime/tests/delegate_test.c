#include "runtime_internal.h"

#include <pthread.h>
#include <time.h>
#include <unistd.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

static void expect(bool ok, const char *msg) {
    if (!ok)
        fail(msg);
}

static kvspace_t *open_kv(void) {
    char dsn[128];
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-delegate-%d", (int)getpid());
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        fail("connect");
    char err[256];
    kvlangKvspaceMakeIndex(kv, "/sys/rwir-backend/", 0, err, sizeof err);
    kvlangKvspaceMakeIndex(kv, "/sys/task/", 0, err, sizeof err);
    kvlangKvspaceMakeIndex(kv, "/vthread/", 0, err, sizeof err);
    kvlangKvspaceMakeIndex(kv, "/data/", 0, err, sizeof err);
    return kv;
}

static void set_tlv(kvspace_t *kv, const char *key, const char *kind, const uint8_t *raw, uint32_t len) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewTlv(&v, kind, raw, len, 1);
    kvlangKvPair_t p = {(char *)key, v};
    char err[256];
    if (kvlangKvspaceSet(kv, &p, 1, err, sizeof err) != 0)
        fail(err[0] ? err : "set");
    kvlangXvalueFree(&v);
}

static void set_char(kvspace_t *kv, const char *key, const char *s) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    kvlangKvPair_t p = {(char *)key, v};
    char err[256];
    if (kvlangKvspaceSet(kv, &p, 1, err, sizeof err) != 0)
        fail(err[0] ? err : "set");
    kvlangXvalueFree(&v);
}

static char *get_char(kvspace_t *kv, const char *key) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, key, &v);
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return s;
}

static char *error_msg(kvspace_t *kv, const char *vtid) {
    char k[KV_KEY_MAX];
    kvlangKeytreeVthreadStatusMessage(vtid, "error", k, sizeof k);
    return get_char(kv, k);
}

static char *take_cmd(kvspace_t *kv, const char *cmd_dir, uint64_t ns) {
    char *pref = kvlangKeytreeStack(cmd_dir);
    struct timespec t0, tn;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        char **names = NULL;
        int n = 0;
        kvlangKvspaceList(kv, pref, false, false, &names, &n);
        for (int i = 0; i < n; i++) {
            size_t ln = strlen(names[i]);
            if (ln && names[i][ln - 1] == '/')
                names[i][ln - 1] = 0;
            if (!names[i][0])
                continue;
            char *key = malloc(strlen(pref) + strlen(names[i]) + 1);
            sprintf(key, "%s%s", pref, names[i]);
            char *json = get_char(kv, key);
            char err[64];
            kvlangKvspaceDelete(kv, key, err, sizeof err);
            free(key);
            for (int j = 0; j < n; j++)
                free(names[j]);
            free(names);
            free(pref);
            return json;
        }
        for (int i = 0; i < n; i++)
            free(names[i]);
        free(names);
        clock_gettime(CLOCK_MONOTONIC, &tn);
        uint64_t elapsed = (uint64_t)(tn.tv_sec - t0.tv_sec) * 1000000000ULL +
                           (uint64_t)(tn.tv_nsec - t0.tv_nsec);
        if (elapsed >= ns) {
            free(pref);
            return NULL;
        }
        usleep(1000);
    }
}

static void register_backend(kvspace_t *kv, const char *name, const char *opcode, const char *status) {
    char *dir = kvlangKeytreeSysRwirBackend(name);
    char path[256];
    snprintf(path, sizeof path, "%s/", dir);
    char err[256];
    kvlangKvspaceMakeIndex(kv, path, 0, err, sizeof err);
    snprintf(path, sizeof path, "%s/op/", dir);
    kvlangKvspaceMakeIndex(kv, path, 0, err, sizeof err);
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
    inst.opcode_id = -1;
    inst.read_count = 0;
    inst.write_count = 1;
    inst.reads = NULL;
    inst.writes = calloc(1, sizeof(kvlangParam_t));
    inst.writes[0].name = strdup(write_key);
    kvlangXvalueZero(&inst.writes[0].value);
    return inst;
}

typedef struct {
    kvspace_t *kvspace;
    char *command;
    char *output_key;
    int write_output;
    int mark_done;
    char *json;
} exec_arg_t;

static void finish_task(kvspace_t *kv, const char *json, const char *out_key, int write_out, int mark_done) {
    if (write_out)
        set_char(kv, out_key, "ok");
    if (!mark_done)
        return;
    const char *rid = strstr(json, "\"request_id\":\"");
    if (!rid)
        return;
    rid += 14;
    const char *end = strchr(rid, '"');
    if (!end)
        return;
    char id[256];
    size_t n = (size_t)(end - rid);
    if (n >= sizeof id)
        n = sizeof id - 1;
    memcpy(id, rid, n);
    id[n] = 0;
    char *sk = kvlangKeytreeSysTask(id, SEG_STATUS);
    set_char(kv, sk, "done");
    free(sk);
}

static void *executor(void *p) {
    exec_arg_t *a = p;
    char *json = take_cmd(a->kvspace, a->command, 2000000000ULL);
    if (!json)
        return NULL;
    finish_task(a->kvspace, json, a->output_key, a->write_output, a->mark_done);
    a->json = json;
    return NULL;
}

static const char *PC = "/vthread/1/[1]/[1,0]";

static void test_check_write_key(void) {
    expect(kvlangKeytreeCheckWriteKey("1", "/data/out") == NULL, "user global allowed");
    char *e = kvlangKeytreeCheckWriteKey("1", "/lib/x");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/lib rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/sys/rwir-backend/fake/cmd");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/sys rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/dev/null");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/dev rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/done/rwir/x");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "/done rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "/vthread/2/x");
    expect(e && strncmp(e, "PermissionError", 15) == 0, "other vthread rejected");
    free(e);
    e = kvlangKeytreeCheckWriteKey("1", "rel");
    expect(e && strncmp(e, "ValueError", 10) == 0, "relative rejected");
    free(e);
}

static void test_is_delegated(kvspace_t *kv) {
    expect(!kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "no backend");
    usleep(400000);
    register_backend(kv, "fake", "fake.echo", "ready");
    expect(kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "on duty");
    expect(kvlangDispatchIsDelegatedOp(kv, "/lib/fake.echo"), "folded opcode");
    char *sk = kvlangKeytreeSysRwirBackendStatus("fake");
    set_char(kv, sk, "offline");
    free(sk);
    expect(!kvlangDispatchIsDelegatedOp(kv, "fake.echo"), "offline not delegated");
}

static void test_stale_heartbeat(kvspace_t *kv) {
    register_backend(kv, "stale", "stale.op", "ready");
    char *hk = kvlangKeytreeSysRwirBackendHeartbeat("stale");
    set_char(kv, hk, "1");
    free(hk);
    expect(!kvlangDispatchIsDelegatedOp(kv, "stale.op"), "stale heartbeat");
}

static void test_delegate_ok(kvspace_t *kv) {
    register_backend(kv, "okb", "ok.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCommand("okb");
    exec_arg_t a = {kv, cmd, "/data/out", 1, 1, NULL};
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangRwirInst_t inst = make_inst("ok.echo", "/data/out");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    pthread_join(th, NULL);
    free(a.json);
    expect(rc == KVLANG_DELEGATE_OK, "delegate ok");
    char *out = get_char(kv, "/data/out");
    expect(out && strcmp(out, "ok") == 0, "output written");
    free(out);
    char *pc = NULL, *st = NULL;
    kvlangVthreadGet(kv, "1", &pc, &st);
    expect(pc && strcmp(pc, "/vthread/1/[1]/[2,0]") == 0, "pc advanced");
    expect(st && strcmp(st, "running") == 0, "running after done");
    free(pc);
    free(st);
    char **names = NULL;
    int n = 0;
    kvlangKvspaceList(kv, "/sys/task/", false, false, &names, &n);
    expect(n == 1, "done status stays listed; kvspace 0.3 has no Expire");
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
    kvlangRwirInstructionFree(&inst);
    free(cmd);
}

static void test_write_target(kvspace_t *kv) {
    register_backend(kv, "wb", "w.echo", "ready");
    kvlangRwirInst_t inst = make_inst("w.echo", "/lib/evil");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    expect(rc == KVLANG_DELEGATE_ERR, "write-target fails");
    char *pc = NULL, *st = NULL;
    kvlangVthreadGet(kv, "1", &pc, &st);
    expect(st && strcmp(st, "error") == 0, "error status");
    free(pc);
    free(st);
    char *msg = error_msg(kv, "1");
    expect(msg && strstr(msg, "PermissionError"), "write-target class");
    free(msg);
    kvlangRwirInstructionFree(&inst);
}

static void test_done_without_output(kvspace_t *kv) {
    register_backend(kv, "nb", "n.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCommand("nb");
    exec_arg_t a = {kv, cmd, "/data/nout", 0, 1, NULL};
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangRwirInst_t inst = make_inst("n.echo", "/data/nout");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    pthread_join(th, NULL);
    free(a.json);
    expect(rc == KVLANG_DELEGATE_ERR, "empty output is failure");
    kvlangRwirInstructionFree(&inst);
    free(cmd);
}

static void test_timeout(kvspace_t *kv) {
    register_backend(kv, "tb", "t.echo", "ready");
    kvlangDispatchSetDefaultTimeoutNanoseconds(40000000LL);
    kvlangRwirInst_t inst = make_inst("t.echo", "/data/tout");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    kvlangDispatchSetDefaultTimeoutNanoseconds(30LL * 1000000000LL);
    expect(rc == KVLANG_DELEGATE_ERR, "timeout");
    kvlangRwirInstructionFree(&inst);
}

static void test_one_definition(kvspace_t *kv) {
    register_backend(kv, "db", "dup.op", "ready");
    uint8_t raw[5] = {0, 0, 0, 0, 0};
    char *sk = kvlangKeytreeLibSignature("dup.op");
    set_tlv(kv, sk, KVSPACE_KIND_RWFUNC, raw, sizeof raw);
    kvlangRwirInst_t inst = make_inst("dup.op", "/data/dup");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    expect(rc == KVLANG_DELEGATE_ERR, "rwfunc + backend conflict");
    char *msg = error_msg(kv, "1");
    expect(msg && strstr(msg, "one opcode, one definition"), "conflict names both definitions");
    free(msg);
    kvlangRwirInstructionFree(&inst);
    free(sk);
}

static void test_output_without_done(kvspace_t *kv) {
    register_backend(kv, "od", "od.echo", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCommand("od");
    exec_arg_t a = {kv, cmd, "/data/odout", 1, 0, NULL};
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangDispatchSetDefaultTimeoutNanoseconds(80000000LL);
    kvlangRwirInst_t inst = make_inst("od.echo", "/data/odout");
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    pthread_join(th, NULL);
    free(a.json);
    kvlangDispatchSetDefaultTimeoutNanoseconds(30LL * 1000000000LL);
    expect(rc == KVLANG_DELEGATE_ERR, "output without done is failure");
    kvlangRwirInstructionFree(&inst);
    free(cmd);
}

static void test_missing_heartbeat(kvspace_t *kv) {
    char *dir = kvlangKeytreeSysRwirBackend("nohb");
    char path[256];
    snprintf(path, sizeof path, "%s/", dir);
    char err[256];
    kvlangKvspaceMakeIndex(kv, path, 0, err, sizeof err);
    snprintf(path, sizeof path, "%s/op/", dir);
    kvlangKvspaceMakeIndex(kv, path, 0, err, sizeof err);
    free(dir);
    char *opk = kvlangKeytreeSysRwirBackendOp("nohb", "nohb.op");
    set_char(kv, opk, "1");
    free(opk);
    char *sk = kvlangKeytreeSysRwirBackendStatus("nohb");
    set_char(kv, sk, "ready");
    free(sk);
    expect(!kvlangDispatchIsDelegatedOp(kv, "nohb.op"), "missing heartbeat is off duty");
}

static void test_decoded_inputs(kvspace_t *kv) {
    char err[256];
    kvlangKvspaceMakeIndex(kv, "/vthread/1/[1]/", 0, err, sizeof err);
    const uint8_t op_raw[] = {3, 0, 1, 0, 0, 'd', 'e', 'c', '.', 'o', 'p'};
    const uint8_t x_raw[] = {0, 0, 0, 0, 0, 'x', 0, 'c', 'h', 'a', 'r', '/', 'u', 't', 'f', '8'};
    const uint8_t y_raw[] = {0, 0, 0, 0, 0, 'y', 0, 'c', 'h', 'a', 'r', '/', 'u', 't', 'f', '8'};
    set_tlv(kv, "/vthread/1/[1]/[1,0]", KVSPACE_KIND_RWIR, op_raw, sizeof op_raw);
    set_char(kv, "/vthread/1/[1]/[1,-1]", "lit-val");
    set_tlv(kv, "/vthread/1/[1]/[1,-2]", KVSPACE_KIND_RWIR, x_raw, sizeof x_raw);
    set_tlv(kv, "/vthread/1/[1]/[1,-3]", KVSPACE_KIND_RWIR, y_raw, sizeof y_raw);
    set_tlv(kv, "/vthread/1/[1]/[1,1]", KVSPACE_KIND_RWIR, y_raw, sizeof y_raw);
    set_char(kv, "/vthread/1/[1]/x", "x-val");
    set_char(kv, "/vthread/1/[1]/y", "before");

    kvlangRwirInst_t inst;
    if (kvlangRwirDecode(kv, "/vthread/1/[1]/", PC, &inst, err, sizeof err) != 0)
        fail(err[0] ? err : "decode");

    register_backend(kv, "decb", "dec.op", "ready");
    char *cmd = kvlangKeytreeSysRwirBackendCommand("decb");
    exec_arg_t a = {kv, cmd, "/vthread/1/[1]/y", 1, 1, NULL};
    pthread_t th;
    pthread_create(&th, NULL, executor, &a);
    kvlangVthreadSet(kv, "1", PC, "running");
    int rc = kvlangDispatchDelegate(kv, "1", PC, &inst);
    pthread_join(th, NULL);
    expect(rc == KVLANG_DELEGATE_OK, "decoded delegate ok");
    expect(a.json && strstr(a.json, "\"inputs\":[{\"value\":\"lit-val\"},{\"key\":\"/vthread/1/[1]/x\"},{\"value\":\"before\"}]") != NULL,
           "decoded literal is value, var is key, aliased rw var is value");
    free(a.json);
    free(cmd);
    kvlangRwirInstructionFree(&inst);
}

int main(void) {
    test_check_write_key();
    kvspace_t *kv = open_kv();
    test_is_delegated(kv);
    test_stale_heartbeat(kv);
    test_delegate_ok(kv);
    test_write_target(kv);
    test_done_without_output(kv);
    test_timeout(kv);
    test_one_definition(kv);
    test_output_without_done(kv);
    test_missing_heartbeat(kv);
    test_decoded_inputs(kv);
    kvlangKvspaceDisconnect(kv);
    fprintf(stderr, "ok\n");
    return 0;
}
