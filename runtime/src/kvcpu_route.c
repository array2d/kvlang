#include "runtime_internal.h"

/* ── 路由判定 + 扩展 handoff（自 rwirext.c 迁入）────────────── */

int kvlangHandoffExternalRwir(kvspace_t *kv, const char *vtid, const char *pc,
                          kvlangRwirInst_t *inst) {
    /* handoff：把 pc 挂到共享队列 /lib/<opcode>/vids/<vtid>（各 rwir 的 vids 已 Ptr 统一到
     * 第一个 rwir 的 vids 下，Set 经路径穿透落到同一 strkeymap）。外部执行器认领并驱动该 vthread，
     * 完成后删除该条目。本端 watch 同一 key 直至变 None（== 认领方已完成），单键交接、无 id。 */
    char *base = kvlangKeytreeRwir(inst->opcode);
    char vids[KV_KEY_MAX];
    kvlangKeyFormat(vids, sizeof vids, "%s/vids/%s", base, vtid);
    kvlangXvalue_t pv;
    kvlangXvalueNewCharUtf8(&pv, pc);
    kvlangKvPair_t p = {vids, pv};
    char err[256];
    int set_rc = kvlangKvspaceSet(kv, &p, 1, err, sizeof err);
    kvlangXvalueFree(&pv);
    if (set_rc != 0) {
        free(base);
        return -1;
    }

    kvlangXvalue_t none;
    kvlangXvalueZero(&none); /* 目标 None：等条目被删除 */
    kvlangXvalue_t got;
    kvlangXvalueZero(&got);
    int rc = kvlangKvspaceWatch(kv, vids, &none, 30000000000ULL, &got);
    free(base);
    if (rc != 0) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "RuntimeError: external rwir %s handoff failed", inst->opcode);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        return -1;
    }
    return 0;
}


/* kvlangNotInMyRwirCaps：opcode 是一条不在本 runtime myrwircaps 内、须经 def rwir 路由给
 * 能兑现它的其它 runtime 的 rwir。判据是 /lib/<opcode> 存在 def rwir 路由头
 * （langtype=def rwir，storetype=index）。kvspace 是能力唯一事实源；本判定在独立
 * kvlang 进程内发生，进程内 myrwircaps 恒不含它，故只有 /lib 路由头可信。 */
bool kvlangNotInMyRwirCaps(kvspace_t *k, const char *opcode) {
    if (opcode[0] == '/')
        return false;
    char *key = kvlangKeytreeRwir(opcode);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(k, key, &v);
    bool yes =
        !kvlangXvalueNone(&v) && kvlangXvalueKindIs(&v, KVSPACE_KIND_DEF_RWIR);
    free(key);
    return yes;
}

int kvlangRwirextHandoff(void *kvspace, const char *vtid, const char *pc) {
    kvspace_t k = {kvspace};
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr)
        return -1;
    char *lb = kvlangKeytreeStack(fr);
    kvlangRwirInst_t inst;
    char err[256];
    if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0) {
        free(fr);
        free(lb);
        return -1;
    }
    free(lb);
    int rc = kvlangHandoffExternalRwir(&k, vtid, pc, &inst);
    free(fr);
    kvlangRwirInstructionFree(&inst);
    return rc;
}
