#include "runtime_internal.h"

#define VT_MEMBER_GET(fn, want_status)                                        \
    void fn(kvlangKv_t *kv, const char *vtid, char **out) {                   \
        *out = NULL;                                                          \
        kvlangStrbuf_t k; kvlangStrbufInit(&k);                               \
        if (want_status) kvlangKeytreeVthreadStatus(vtid, &k);                \
        else kvlangKeytreeVthreadPc(vtid, &k);                                \
        kvlangXvalue_t v; kvlangXvalueZero(&v);                               \
        kvlangKvGetOne(kv, k.p, &v);                                          \
        if (!kvlangXvalueNone(&v)) *out = kvlangXvalueValueString(&v);        \
        kvlangXvalueFree(&v);                                                 \
        kvlangStrbufFree(&k);                                                 \
    }

/* Read PC and status separately from KVSpace. */
VT_MEMBER_GET(kvlangVthreadPcGet, false)
VT_MEMBER_GET(kvlangVthreadStatusGet, true)

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status) {
    kvlangVthreadPcGet(kv, vtid, pc);
    kvlangVthreadStatusGet(kv, vtid, status);
}

int kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status) {
    kvlangStrbuf_t k1, k2;
    kvlangStrbufInit(&k1); kvlangStrbufInit(&k2);
    kvlangKeytreeVthreadPc(vtid, &k1);
    kvlangKeytreeVthreadStatus(vtid, &k2);
    kvlangXvalue_t v1, v2; kvlangXvalueZero(&v1); kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = { { k1.p, v1 }, { k2.p, v2 } };
    char err[256];
    int rc = kvlangKvSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1); kvlangXvalueFree(&v2);
    kvlangStrbufFree(&k1); kvlangStrbufFree(&k2);
    return rc;
}

/* Persist PC and changed status after each instruction. */
void kvlangVthreadAdvance(kvlangFrame_t *f, const char *pc, const char *status) {
    kvlangStrbuf_t k1, k2;
    kvlangStrbufInit(&k1); kvlangStrbufInit(&k2);
    kvlangKeytreeVthreadPc(f->vtid, &k1);
    kvlangKeytreeVthreadStatus(f->vtid, &k2);
    if (kvlangKvSetChar(f->kv, k1.p, pc) != 0)
        f->persist_failed = true;
    if (!f->persist_failed &&
        (!f->status_known || strcmp(f->status_known, status) != 0) &&
        kvlangKvSetChar(f->kv, k2.p, status) != 0)
        f->persist_failed = true;
    kvlangStrbufFree(&k1); kvlangStrbufFree(&k2);
}

int kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0) ret = "ok";
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangKeytreeVthreadStatus(vtid, &k);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, ret);
    kvlangKvPair_t pair = { k.p, v };
    char err[256];
    int rc = kvlangKvSet(kv, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&v); kvlangStrbufFree(&k);
    return rc;
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
        kvlangKvMkindex(kv, dir.p, 0, err, sizeof err);
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
