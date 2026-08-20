#include "runtime_internal.h"

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status) {
    *pc = NULL; *status = NULL;
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangKeytreeVthreadPc(vtid, &k);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetOne(kv, k.p, &pv);
    if (!kvlangXvalueNone(&pv)) *pc = kvlangXvalueValueString(&pv);
    kvlangXvalueFree(&pv);
    kvlangKeytreeVthreadStatus(vtid, &k);
    kvlangXvalue_t sv; kvlangXvalueZero(&sv);
    kvlangKvGetOne(kv, k.p, &sv);
    if (!kvlangXvalueNone(&sv)) *status = kvlangXvalueValueString(&sv);
    kvlangXvalueFree(&sv);
    kvlangStrbufFree(&k);
}

void kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status) {
    kvlangStrbuf_t k1, k2; kvlangStrbufInit(&k1); kvlangStrbufInit(&k2);
    kvlangKeytreeVthreadPc(vtid, &k1);
    kvlangKeytreeVthreadStatus(vtid, &k2);
    kvlangXvalue_t v1, v2; kvlangXvalueZero(&v1); kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = { { k1.p, v1 }, { k2.p, v2 } };
    char err[256];
    kvlangKvSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1); kvlangXvalueFree(&v2); kvlangStrbufFree(&k1); kvlangStrbufFree(&k2);
}

void kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0) ret = "ok";
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangKeytreeVthreadStatus(vtid, &k);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, ret);
    kvlangKvPair_t pair = { k.p, v };
    char err[256];
    kvlangKvSet(kv, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&v); kvlangStrbufFree(&k);
}

void kvlangVthreadSetError(kvlangKv_t *kv, const char *vtid, const char *pc, const char *msg) {
    kvlangStrbuf_t msg_path, pc_key, st_key; kvlangStrbufInit(&msg_path); kvlangStrbufInit(&pc_key); kvlangStrbufInit(&st_key);
    kvlangKeytreeVthreadStatusMsg(vtid, "error", &msg_path);
    kvlangKeytreeVthreadPc(vtid, &pc_key);
    kvlangKeytreeVthreadStatus(vtid, &st_key);

    /* 确保 .error/ 父目录存在 */
    char *sep = strrchr(msg_path.p, '/');
    if (sep) {
        kvlangStrbuf_t dir; kvlangStrbufInit(&dir);
        kvlangStrbufPutn(&dir, msg_path.p, (size_t)(sep - msg_path.p) + 1);
        char err[256];
        kvlangKvMkindex(kv, dir.p, err, sizeof err);
        kvlangStrbufFree(&dir);
    }

    kvlangXvalue_t vpc, vmsg, vst; kvlangXvalueZero(&vpc); kvlangXvalueZero(&vmsg); kvlangXvalueZero(&vst);
    kvlangXvalueNewCharUtf8(&vpc, pc);
    kvlangXvalueNewCharUtf8(&vmsg, msg);
    kvlangXvalueNewCharUtf8(&vst, "error");
    kvlangKvPair_t pairs[3] = { { pc_key.p, vpc }, { msg_path.p, vmsg }, { st_key.p, vst } };
    char err[256];
    kvlangKvSet(kv, pairs, 3, err, sizeof err);
    kvlangXvalueFree(&vpc); kvlangXvalueFree(&vmsg); kvlangXvalueFree(&vst);
    kvlangStrbufFree(&msg_path); kvlangStrbufFree(&pc_key); kvlangStrbufFree(&st_key);
}

int64_t kvlangVthreadNextSeq(kvlangKv_t *kv, const char *key) {
    int64_t n = 0;
    char err[256];
    if (kvlangKvIncr(kv, key, &n, err, sizeof err) != 0) {
        fprintf(stderr, "NextSeq: %s\n", err[0] ? err : "Incr failed");
        abort();
    }
    return n;
}
