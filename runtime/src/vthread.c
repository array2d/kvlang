#include "runtime_internal.h"

#define VT_MEMBER_GET(fn, want_status)                                        \
    void fn(kvlangKv_t *kv, const char *vtid, char **out) {                   \
        *out = NULL;                                                          \
        char k[KV_KEY_MAX];                                                   \
        if (want_status) kvlangKeytreeVthreadStatus(vtid, k, sizeof k);       \
        else kvlangKeytreeVthreadPc(vtid, k, sizeof k);                       \
        kvlangXvalue_t v; kvlangXvalueZero(&v);                               \
        kvlangKvGetOne(kv, k, &v);                                            \
        if (!kvlangXvalueNone(&v)) *out = kvlangXvalueValueString(&v);        \
        kvlangXvalueFree(&v);                                                 \
    }

/* Read PC and status separately from KVSpace. */
VT_MEMBER_GET(kvlangVthreadPcGet, false)
VT_MEMBER_GET(kvlangVthreadStatusGet, true)

/* 读成员值到调用方缓冲；false = 键缺失或 None。 */
bool kvlangVthreadMemberGetBuf(kvlangKv_t *kv, const char *key, char *out,
                               size_t cap) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (kvlangKvGetOne(kv, key, &v) != 0)
        return false;
    if (kvlangXvalueNone(&v)) {
        kvlangXvalueFree(&v);
        return false;
    }
    char *tmp = NULL;
    const char *src;
    size_t n;
    if (kvlangXvalueKindIs(&v, KVSPACE_KIND_CHAR_UTF8)) {
        src = (const char *)xh_body(v.data);
        n = (size_t)xh_content_len(v.data);
    } else {
        tmp = kvlangXvalueValueString(&v);
        src = tmp;
        n = strlen(tmp);
    }
    if (n + 1 > cap)
        kvlangDie("member value exceeds %zu bytes", cap);
    memcpy(out, src, n);
    out[n] = 0;
    free(tmp);
    kvlangXvalueFree(&v);
    return true;
}

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status) {
    kvlangVthreadPcGet(kv, vtid, pc);
    kvlangVthreadStatusGet(kv, vtid, status);
}

int kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status) {
    char k1[KV_KEY_MAX], k2[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, k1, sizeof k1);
    kvlangKeytreeVthreadStatus(vtid, k2, sizeof k2);
    kvlangXvalue_t v1, v2; kvlangXvalueZero(&v1); kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = { { k1, v1 }, { k2, v2 } };
    char err[256];
    int rc = kvlangKvSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1); kvlangXvalueFree(&v2);
    return rc;
}

/* Persist PC and changed status after each instruction. */
void kvlangVthreadAdvance(kvlangFrame_t *f, const char *pc, const char *status) {
    char k1[KV_KEY_MAX], k2[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(f->vtid, k1, sizeof k1);
    kvlangKeytreeVthreadStatus(f->vtid, k2, sizeof k2);
    if (kvlangKvSetChar(f->kv, k1, pc) != 0)
        f->persist_failed = true;
    if (!f->persist_failed &&
        (!f->status_known || strcmp(f->status_known, status) != 0) &&
        kvlangKvSetChar(f->kv, k2, status) != 0)
        f->persist_failed = true;
}

int kvlangVthreadSetDone(kvlangKv_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0) ret = "ok";
    char k[KV_KEY_MAX];
    kvlangKeytreeVthreadStatus(vtid, k, sizeof k);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, ret);
    kvlangKvPair_t pair = { k, v };
    char err[256];
    int rc = kvlangKvSet(kv, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&v);
    return rc;
}

void kvlangVthreadSetError(kvlangKv_t *kv, const char *vtid, const char *pc, const char *msg) {
    char msg_path[KV_KEY_MAX], pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    kvlangKeytreeVthreadStatusMsg(vtid, "error", msg_path, sizeof msg_path);
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);

    /* 确保 .error/ 父目录存在 */
    char *sep = strrchr(msg_path, '/');
    if (sep) {
        size_t dl = (size_t)(sep - msg_path) + 1;
        char dir[KV_KEY_MAX];
        if (dl >= sizeof dir)
            kvlangDie("error dir exceeds %zu bytes", sizeof dir);
        memcpy(dir, msg_path, dl);
        dir[dl] = 0;
        char err[256];
        kvlangKvMkindex(kv, dir, 0, err, sizeof err);
    }

    kvlangXvalue_t vpc, vmsg, vst; kvlangXvalueZero(&vpc); kvlangXvalueZero(&vmsg); kvlangXvalueZero(&vst);
    kvlangXvalueNewCharUtf8(&vpc, pc);
    kvlangXvalueNewCharUtf8(&vmsg, msg);
    kvlangXvalueNewCharUtf8(&vst, "error");
    kvlangKvPair_t pairs[3] = { { pc_key, vpc }, { msg_path, vmsg }, { st_key, vst } };
    char err[256];
    kvlangKvSet(kv, pairs, 3, err, sizeof err);
    kvlangXvalueFree(&vpc); kvlangXvalueFree(&vmsg); kvlangXvalueFree(&vst);
}
