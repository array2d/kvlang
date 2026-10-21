#include "runtime_internal.h"

#define VT_MEMBER_GET(fn, want_status)                                         \
    void fn(kvspace_t *kv, const char *vtid, char **out) {                     \
        *out = NULL;                                                           \
        char k[KV_KEY_MAX];                                                    \
        if (want_status)                                                       \
            kvlangKeytreeVthreadStatus(vtid, k, sizeof k);                     \
        else                                                                   \
            kvlangKeytreeVthreadPc(vtid, k, sizeof k);                         \
        kvlangXvalue_t v;                                                      \
        kvlangXvalueZero(&v);                                                  \
        kvlangKvspaceGetOne(kv, k, &v);                                        \
        if (!kvlangXvalueNone(&v))                                             \
            *out = kvlangXvalueValueString(&v);                                \
    }

/* Read PC and status separately from KVSpace. */
VT_MEMBER_GET(kvlangVthreadPcGet, false)
VT_MEMBER_GET(kvlangVthreadStatusGet, true)

/* 系统变量值 → 调用方缓冲；false = 键缺失或 None，超长直接 abort。 */
static bool value_to_buffer(kvlangXvalue_t *v, char *out, size_t cap) {
    if (kvlangXvalueNone(v)) {
        return false;
    }
    char *tmp = NULL;
    const char *src;
    size_t n;
    if (kvlangXvalueKindIs(v, KVSPACE_KIND_CHAR_UTF8)) {
        src = (const char *)xh_body(v->bytes);
        n = (size_t)xh_content_len(v->bytes);
    } else {
        tmp = kvlangXvalueValueString(v);
        src = tmp;
        n = strlen(tmp);
    }
    if (n + 1 > cap)
        kvlangDie("system var value exceeds %zu bytes", cap);
    memcpy(out, src, n);
    out[n] = 0;
    free(tmp);
    return true;
}

/* 绑定：key 由调用方持有；只缓存 key 的 64 位地址，值仍每次读。 */
void kvspaceKeyOffBind(kvspaceKeyOff *ko, kvspace_t *kvspace, const char *key) {
    ko->kvspace = kvspace;
    ko->key = key;
    ko->fast = NULL;
    ko->resolved = kvspaceResolveRef(kvspace->handle, key, &ko->off) == 0;
}

bool kvspaceKeyOffRead(kvspaceKeyOff *ko, char *out, size_t cap) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (ko->resolved) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGetByRef(ko->kvspace->handle, &ko->off, ko->key, &d, &len) !=
            0)
            return false;
        if (d && len > 0) {
            v.bytes = d;
            v.length = len;
        }
    } else if (kvlangKvspaceGetOne(ko->kvspace, ko->key, &v) != 0) {
        return false;
    }
    return value_to_buffer(&v, out, cap);
}

/* 整值写：已解析走 SetValueByRef；未解析/失败退回按 key 写并补一次绑定。 */
int kvspaceKeyOffSet(kvspaceKeyOff *ko, const uint8_t *val, uint32_t len,
                     char *err, uint32_t err_cap) {
    if (ko->resolved &&
        kvspaceSetValueByRef(ko->kvspace->handle, &ko->off, ko->key, val, len,
                             0, 0, err, err_cap) == 0)
        return 0;
    if (kvspaceSetValue(ko->kvspace->handle, ko->key, val, len, 0, 0, err,
                        err_cap) != 0)
        return -1;
    if (kvspaceResolveRef(ko->kvspace->handle, ko->key, &ko->off) == 0)
        ko->resolved = true;
    return 0;
}

void kvlangVthreadGet(kvspace_t *kv, const char *vtid, char **pc,
                      char **status) {
    kvlangVthreadPcGet(kv, vtid, pc);
    kvlangVthreadStatusGet(kv, vtid, status);
}

int kvlangVthreadSet(kvspace_t *kv, const char *vtid, const char *pc,
                     const char *status) {
    char k1[KV_KEY_MAX], k2[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, k1, sizeof k1);
    kvlangKeytreeVthreadStatus(vtid, k2, sizeof k2);
    kvlangXvalue_t v1, v2;
    kvlangXvalueZero(&v1);
    kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = {{k1, v1}, {k2, v2}};
    char err[256];
    int rc = kvlangKvspaceSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1);
    kvlangXvalueFree(&v2);
    return rc;
}

/* 槽容量：pc 常态长度一档、status 更短一档；超长值退回全尺寸，仍不截断。 */
#define VT_PC_SLACK_CAP 256
#define VT_STATUS_SLACK_CAP 32
#define VT_SLACK_MAX_CAP KV_KEY_MAX

/* 整值写：已绑定的 keyoff 直接写；独立帧没有绑定，按 key 冷写。 */
static void member_write(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                         bool is_status, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    uint32_t cap = is_status ? VT_STATUS_SLACK_CAP : VT_PC_SLACK_CAP;
    if (len > cap)
        cap = VT_SLACK_MAX_CAP;
    uint8_t val[64 + VT_SLACK_MAX_CAP];
    uint32_t n = kvlangSlackEncodeChar((const uint8_t *)s, len, cap, val,
                                       sizeof val);
    if (ko) {
        if (kvspaceKeyOffSet(ko, val, n, NULL, 0) != 0)
            f->persist_failed = true;
        return;
    }
    char k[KV_KEY_MAX];
    if (is_status)
        kvlangKeytreeVthreadStatus(f->vthread_id, k, sizeof k);
    else
        kvlangKeytreeVthreadPc(f->vthread_id, k, sizeof k);
    if (kvspaceSetValue(f->kvspace->handle, k, val, n, 0, 0, NULL, 0) != 0)
        f->persist_failed = true;
}

/* Persist PC and changed status after each instruction. */
void kvlangVthreadNextPc(kvlangFrame_t *f, const char *pc,
                          const char *status) {
    member_write(f, f->pc_off, false, pc);
    if (!f->persist_failed &&
        (!f->status_known || strcmp(f->status_known, status) != 0))
        member_write(f, f->status_off, true, status);
    /* 回传：值就是刚写进 kvspace 的那条 PC，不引入第二份事实源。 */
    if (!f->persist_failed && f->pc_out) {
        size_t n = strlen(pc);
        if (n + 1 > f->pc_out_cap)
            kvlangDie("feedback pc exceeds %zu bytes", f->pc_out_cap);
        memcpy(f->pc_out, pc, n + 1);
        f->pc_feedback = true;
    }
}

int kvlangVthreadSetDone(kvspace_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0)
        ret = "ok";
    char k[KV_KEY_MAX];
    kvlangKeytreeVthreadStatus(vtid, k, sizeof k);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, ret);
    kvlangKvPair_t pair = {k, v};
    char err[256];
    int rc = kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&v);
    return rc;
}

void kvlangVthreadSetError(kvspace_t *kv, const char *vtid, const char *pc,
                           const char *msg) {
    char msg_key[KV_KEY_MAX], pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    kvlangKeytreeVthreadMessage(vtid, msg_key, sizeof msg_key);
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);

    kvlangXvalue_t vpc, vmsg, vst;
    kvlangXvalueZero(&vpc);
    kvlangXvalueZero(&vmsg);
    kvlangXvalueZero(&vst);
    kvlangXvalueNewCharUtf8(&vpc, pc);
    kvlangXvalueNewCharUtf8(&vmsg, msg);
    kvlangXvalueNewCharUtf8(&vst, "error");
    kvlangKvPair_t pairs[3] = {{pc_key, vpc}, {msg_key, vmsg}, {st_key, vst}};
    char err[256];
    kvlangKvspaceSet(kv, pairs, 3, err, sizeof err);
    kvlangXvalueFree(&vpc);
    kvlangXvalueFree(&vmsg);
    kvlangXvalueFree(&vst);
}
