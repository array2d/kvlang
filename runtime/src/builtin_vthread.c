// builtin_vthread —— vthread·* 虚拟线程 rwir

#include "builtin_internal.h"
#include <time.h>

/* vthread·create(funckey) -> vid：分配 vid、建栈索引、bootstrap 首指令、置 init，返回 vid 句柄。
 * 只创建不运行——首指令不执行。运行交给 vthread·run(vid)。与 vthread·call 不同：新开独立 vid。 */
int kvlangBuiltinVthreadCreate(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0]))) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: vthread.create requires 1 string arg");
    }
    char *fn = kvlangXvalueValueString(&in[0]);
    char *vid = kvlangVthreadSpawn(f->kv, fn, NULL, 0);
    free(fn); kvlangBuiltinFreeInputs(in, n);
    if (!vid) return kvlangBuiltinSetErr(f, "RuntimeError: vthread.create failed");
    kvlangXvalue_t r; kvlangXvalueNewCharUtf8(&r, vid);
    free(vid);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    return rc;
}

/* vthread·run(vid)：以 vid 为参数，从其持久化 pc 驱动到结束（阻塞）。子 vid 命中 ext rwir 时经
 * yield_pc 冒泡给上层驱动派发、推进子 pc 后重入本指令续驱；子 vid done 后推进本指令 NextPc。 */
int kvlangBuiltinVthreadRun(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0]))) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: vthread.run requires 1 string (vid) arg");
    }
    char *vid = kvlangXvalueValueString(&in[0]);
    kvlangBuiltinFreeInputs(in, n);
    char *subpc = NULL, *status = NULL;
    kvlangVthreadGet(f->kv, vid, &subpc, &status);
    free(status); free(vid);
    if (!subpc || !subpc[0]) { free(subpc); kvlangBuiltinNextPc(f); return 0; }
    char *subout = NULL;
    int rc = kvlangKvcpuExecuteMode(f->kv, subpc, KVMODE_RETURN, &subout);
    free(subpc);
    if (rc < 0) { free(subout); return kvlangBuiltinSetErr(f, "RuntimeError: vthread.run failed"); }
    if (rc == 1) { *f->yield_pc = subout; return 0; }
    kvlangBuiltinNextPc(f);
    return 0;
}

/* vthread·call(funckey)：在当前 vthread（同 vid，进程↔vid 1:1）按运行时 funckey 造一次
 * 动态 OP_CALL，跑到被调函数结束再回到本指令 NextPc。与 vthread·run 不同：不新开 vid、
 * 不 WATCH 挂起——被调代码里的 rwir（println/shell·run…）由当前驱动就地派发。 */
int kvlangBuiltinVthreadCall(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0]))) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: vthread.call requires 1 string arg");
    }
    char *fn = kvlangXvalueValueString(&in[0]);
    int rc = kvlangKvcpuDynCall(f->kv, f->vtid, f->pc, fn);
    free(fn); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* vthread·sleep(dur)：让当前 vthread 阻塞 dur（duration，纳秒），到点后 NextPc。
 * 单进程模型下即 nanosleep 阻塞驱动线程。 */
int kvlangBuiltinVthreadSleep(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: vthread.sleep requires 1 duration arg"); }
    int64_t ns = kvlangXvalueAsInt64(&in[0]);
    kvlangBuiltinFreeInputs(in, n);
    if (ns > 0) {
        struct timespec ts = { .tv_sec = ns / 1000000000LL, .tv_nsec = ns % 1000000000LL };
        nanosleep(&ts, NULL);
    }
    kvlangBuiltinNextPc(f); return 0;
}

/* 设状态并把 PC 前进到下一指令。execute 循环只在 init/running/wait 下继续，
 * 其余状态（paused/done/error…）即停。恢复：外部把 /vthread/{vid}/·status 改回 running。 */
static int set_status(kvlangFrame_t *f, const char *status) {
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc);
    kvlangRwirNextPc(f->pc, &npc);
    kvlangVthreadSet(f->kv, f->vtid, npc.p, status);
    kvlangStrbufFree(&npc);
    return 0;
}

int kvlangBuiltinVthreadSetstatus(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    if (n < 1 || !kvlangXvalueIsCharKind(kvlangXvalueKind(&in[0]))) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: vthread.setstatus requires 1 string arg");
    }
    char *status = kvlangXvalueValueString(&in[0]);
    int rc = set_status(f, status);
    free(status); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDebugger(kvlangFrame_t *f) {
    return set_status(f, "paused");
}
