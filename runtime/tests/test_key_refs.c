#include "rwir_internal.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); return 1; } } while (0)

static int put(kvlangKv_t *kv, const char *key, kvlangXvalue_t *value) {
    kvlangKvPair_t pair = {(char *)key, *value};
    char err[256];
    return kvlangKvSet(kv, &pair, 1, err, sizeof err);
}

static int put_i64(kvlangKv_t *kv, const char *key, int64_t n) {
    kvlangXvalue_t value;
    kvlangXvalueNewInt64(&value, n);
    int rc = put(kv, key, &value);
    kvlangXvalueFree(&value);
    return rc;
}

static int64_t read_i64(kvlangKv_t *kv, const char *key, kvlangKvRef_t *ref) {
    kvlangXvalue_t value;
    if (kvlangKvGetOneRef(kv, key, ref, &value) != 0)
        return -1;
    int64_t n = kvlangScalarI64(kvlangXvalueScalar(&value));
    kvlangXvalueFree(&value);
    return n;
}

static bool rejects_result(kvlangFrame_t *frame, kvlangKv_t *observer,
                            uint8_t *data, uint32_t len) {
    kvlangXvalue_t value = {data, len, 1};
    if (kvlangBuiltinWriteResult(frame, &value) == 0)
        return false;
    char *pc = NULL;
    kvlangVthreadPcGet(observer, frame->vtid, &pc);
    bool unchanged = pc && strcmp(pc, frame->pc) == 0;
    free(pc);
    kvlangKvRef_t ref = {0};
    return unchanged && read_i64(observer, "/b", &ref) == 7;
}

int main(void) {
    char dir[] = "/tmp/kvlang-key-refs-XXXXXX", dsn[256], err[256];
    CHECK(mkdtemp(dir));
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvlangKv_t *a = kvlangKvConnect(dsn), *b = kvlangKvConnect(dsn);
    CHECK(a && b);
    kvlangKvRef_t ref = {0};
    CHECK(put_i64(b, "/value", 5) == 0);
    CHECK(read_i64(a, "/value", &ref) == 5);
    CHECK(put_i64(b, "/value", 9) == 0);
    CHECK(read_i64(a, "/value", &ref) == 9);
    char text[4097];
    memset(text, 'x', sizeof text - 1);
    text[sizeof text - 1] = 0;
    kvlangXvalue_t value;
    kvlangXvalueNewCharUtf8(&value, text);
    CHECK(put(b, "/value", &value) == 0);
    kvlangXvalueFree(&value);
    CHECK(kvlangKvGetOneRef(a, "/value", &ref, &value) == 0);
    char *got = kvlangXvalueValueString(&value);
    CHECK(got && strcmp(got, text) == 0);
    free(got);
    kvlangXvalueFree(&value);
    CHECK(kvlangKvDel(b, "/value", err, sizeof err) == 0);
    CHECK(kvlangKvGetOneRef(a, "/value", &ref, &value) == 0);
    CHECK(kvlangXvalueNone(&value));
    kvlangXvalueFree(&value);
    CHECK(put_i64(b, "/value", 17) == 0);
    CHECK(read_i64(a, "/value", &ref) == 17);
    kvlangXvalueNewInt64(&value, 23);
    CHECK(kvlangKvSetOneRef(a, "/value", &ref, &value, err, sizeof err) == 0);
    kvlangXvalueFree(&value);
    kvlangKvRef_t other = {0};
    CHECK(read_i64(b, "/value", &other) == 23);
    if (kvspaceSetValueByRef) {
        kvlangXvalueNewInt64(&value, 29);
        CHECK(kvspaceSetValueByRef(a->h, &ref.ref, "/value", value.data,
                                   value.len, 0, 0, err, sizeof err) == 0);
        kvlangXvalueFree(&value);
        CHECK(read_i64(b, "/value", &other) == 29);
    }

    const char *pc = "/vthread/vt/[1]/[1,0]";
    const char *changed_pc = "/vthread/vt/[1]/[7,0]";
    CHECK(kvlangVthreadSet(b, "vt", pc, "running") == 0);
    kvlangStrbuf_t pc_key, buffer;
    kvlangStrbufInit(&pc_key);
    kvlangStrbufInit(&buffer);
    kvlangKeytreeVthreadPc("vt", &pc_key);
    kvlangKvRef_t pc_ref = {0};
    CHECK(kvlangVthreadMemberGetBuf(a, pc_key.p, &pc_ref, &buffer));
    CHECK(strcmp(buffer.p, pc) == 0);
    CHECK(kvlangVthreadSet(b, "vt", changed_pc, "wait") == 0);
    CHECK(kvlangVthreadMemberGetBuf(a, pc_key.p, &pc_ref, &buffer));
    CHECK(strcmp(buffer.p, changed_pc) == 0);

    kvlangParam_t write = {.name = "*p"};
    kvlangRwirInst_t inst = {.writes = &write, .nw = 1};
    kvlangFrame_t frame = {.kv = a, .vtid = "vt", .pc = pc, .inst = &inst};
    const char *targets[] = {"/a", "/b"};
    for (int i = 0; i < 2; i++) {
        kvlangXvalueNewPtr(&value, "int64", targets[i]);
        CHECK(put(b, "/vthread/vt/[1]/p", &value) == 0);
        kvlangXvalueFree(&value);
        kvlangXvalueNewInt64(&value, i ? 7 : 5);
        CHECK(kvlangBuiltinWriteResult(&frame, &value) == 0);
        kvlangXvalueFree(&value);
    }
    other = (kvlangKvRef_t){0};
    CHECK(read_i64(b, "/a", &other) == 5);
    other = (kvlangKvRef_t){0};
    CHECK(read_i64(b, "/b", &other) == 7);

    CHECK(kvlangVthreadSet(b, "vt", pc, "running") == 0);
    write.name = "/b";
    frame.cached_targets = true;
    uint8_t invalid[40] = {5};
    for (uint32_t len = 1; len <= sizeof invalid; len++)
        if (len != 32)
            CHECK(rejects_result(&frame, b, invalid, len));
    const unsigned fields[] = {0, 2, 10};
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        unsigned field = fields[i];
        invalid[field]++;
        CHECK(rejects_result(&frame, b, invalid, 32));
        invalid[field]--;
    }
    value = (kvlangXvalue_t){invalid, 32, 1};
    CHECK(kvlangBuiltinWriteResult(&frame, &value) == 0);
    CHECK(kvlangKvGetOne(b, "/b", &value) == 0);
    CHECK(kvlangXvalueNone(&value));
    kvlangXvalueFree(&value);
    kvlangStrbufFree(&pc_key);
    kvlangStrbufFree(&buffer);
    kvlangKvDisconnect(b);
    kvlangKvDisconnect(a);
    return 0;
}
