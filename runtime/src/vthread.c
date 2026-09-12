#include "runtime_internal.h"

/* vthread pc/status key 路径缓存（单条目）：运行期同一 vtid 连续多步，
 * 免每步重建 2 条 key（strbuf 分配 + 格式化）。key 纯由 vtid 派生、内容恒定，
 * 不承载任何执行状态——状态一律每步回读 kvspace（见 kvlangVthreadAdvance）。vtid 变化即失效。 */
static char g_vt_id[128];
static bool g_vt_valid = false;
static char *g_vt_pc_key = NULL;
static char *g_vt_status_key = NULL;

/* 取（必要时重建）vtid 的 pc/status key；vtid 过长则返回 false（调用方退化为逐次构造）。 */
static bool vt_keys(const char *vtid, const char **pc_key, const char **status_key) {
    if (g_vt_valid && strcmp(g_vt_id, vtid) == 0) {
        *pc_key = g_vt_pc_key; *status_key = g_vt_status_key;
        return true;
    }
    size_t n = strlen(vtid);
    if (n == 0 || n >= sizeof g_vt_id) return false;
    memcpy(g_vt_id, vtid, n + 1);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangKeytreeVthreadPc(vtid, &b);
    free(g_vt_pc_key); g_vt_pc_key = kvlangStrbufDetach(&b);
    kvlangStrbufInit(&b);
    kvlangKeytreeVthreadStatus(vtid, &b);
    free(g_vt_status_key); g_vt_status_key = kvlangStrbufDetach(&b);
    g_vt_valid = true;
    *pc_key = g_vt_pc_key; *status_key = g_vt_status_key;
    return true;
}

/* 取单条 vthread 成员 key（pc / status）：命中缓存则借用之，否则构造到调用方 strbuf。 */
static const char *vt_member_key(const char *vtid, bool status, kvlangStrbuf_t *k) {
    const char *pk, *sk;
    if (vt_keys(vtid, &pk, &sk)) return status ? sk : pk;
    if (status) kvlangKeytreeVthreadStatus(vtid, k);
    else kvlangKeytreeVthreadPc(vtid, k);
    return k->p;
}

#define VT_MEMBER_GET(fn, want_status)                                        \
    void fn(kvlangKv_t *kv, const char *vtid, char **out) {                   \
        *out = NULL;                                                          \
        kvlangStrbuf_t k; kvlangStrbufInit(&k);                               \
        const char *key = vt_member_key(vtid, (want_status), &k);             \
        kvlangXvalue_t v; kvlangXvalueZero(&v);                               \
        kvlangKvGetOne(kv, key, &v);                                          \
        if (!kvlangXvalueNone(&v)) *out = kvlangXvalueValueString(&v);        \
        kvlangXvalueFree(&v);                                                 \
        kvlangStrbufFree(&k);                                                 \
    }

/* 单读 ‥pc / ‥status：主循环每指令边界各取所需（状态门只读 status），
 * 免掉「取一个成员却连带读另一个」的多余后端 Get。 */
VT_MEMBER_GET(kvlangVthreadPcGet, false)
VT_MEMBER_GET(kvlangVthreadStatusGet, true)

void kvlangVthreadGet(kvlangKv_t *kv, const char *vtid, char **pc, char **status) {
    kvlangVthreadPcGet(kv, vtid, pc);
    kvlangVthreadStatusGet(kv, vtid, status);
}

void kvlangVthreadSet(kvlangKv_t *kv, const char *vtid, const char *pc, const char *status) {
    const char *pk, *sk;
    kvlangStrbuf_t k1, k2;
    kvlangStrbufInit(&k1); kvlangStrbufInit(&k2);
    if (!vt_keys(vtid, &pk, &sk)) {
        kvlangKeytreeVthreadPc(vtid, &k1); pk = k1.p;
        kvlangKeytreeVthreadStatus(vtid, &k2); sk = k2.p;
    }
    kvlangXvalue_t v1, v2; kvlangXvalueZero(&v1); kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = { { (char *)pk, v1 }, { (char *)sk, v2 } };
    char err[256];
    kvlangKvSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1); kvlangXvalueFree(&v2);
    kvlangStrbufFree(&k1); kvlangStrbufFree(&k2);
}

/* 循环内 PC/status 推进（见 kvlangFrame_t 注释）：先写 kvspace（崩溃恢复 + 唯一事实源）。
 * - PC 恒写：它是本指令自己算出来的新地址；写后经 fb_pc 回传，主循环免「刚写就回读」。
 * - status 只在**与 f->status_known 不同**时写：status_known 是本步开始前从 kvspace 读到的
 *   ‥status（源值，非进程内副本）。正常执行期两者都是 "running"，每步省 1 次后端 Set；
 *   而一旦外部把 ‥status 改成 paused/error，主循环下一步的状态门就会读到并停机——
 *   既不拿副本当依据，也不会把 kvspace 留在与执行不符的状态。
 * 两写都走 kvlangKvSetChar 直写 body，跳过 TLV 编码往返。 */
void kvlangVthreadAdvance(kvlangFrame_t *f, const char *pc, const char *status) {
    const char *pk, *sk;
    kvlangStrbuf_t k1, k2;
    kvlangStrbufInit(&k1); kvlangStrbufInit(&k2);
    if (!vt_keys(f->vtid, &pk, &sk)) {
        kvlangKeytreeVthreadPc(f->vtid, &k1); pk = k1.p;
        kvlangKeytreeVthreadStatus(f->vtid, &k2); sk = k2.p;
    }
    kvlangKvSetChar(f->kv, pk, pc);
    if (!f->status_known || strcmp(f->status_known, status) != 0)
        kvlangKvSetChar(f->kv, sk, status);
    kvlangStrbufFree(&k1); kvlangStrbufFree(&k2);
    if (f->fb_pc) {
        free(*f->fb_pc);
        *f->fb_pc = strdup(pc);
    }
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
