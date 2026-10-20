#include "kvcpu_internal.h"
#include "rwir_cache.h"

/* 统一错误出口：置 vthread error 并返回 -1。 */
static int execute_error(kvspace_t *kv, const char *vtid, const char *pc,
                      const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(kv, vtid, pc, msg);
    return -1;
}

/* KVMODE_RETURN 收尾：把下一个 PC 的所有权交给调用方（或释放），清缓存。 */
static int execute_yield(kvlangRwirInst_t *inst, bool from_cache,
                      rwir_cache_t *cache, char **out_pc, char *next_pc) {
    if (out_pc)
        *out_pc = next_pc;
    else
        free(next_pc);
    rwir_instruction_release(inst, from_cache);
    kvlangRwirCacheFree(cache);
    return 1;
}

int kvlangKvcpuExecuteMode(kvspace_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc) {
    if (out_pc)
        *out_pc = NULL;
    char vtid_b[KV_KEY_MAX];
    const char *vtid = kvlangKeytreeVthreadIdFromPc(pc, vtid_b, sizeof vtid_b);
    if (vtid[0] == 0) {
        return -1;
    }

    /* 每步复用的缓冲：pc/status 键与值、帧根、link base 全部跨步保留，免每步 malloc/free。 */
    char pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    char cur_b[KV_KEY_MAX], status_b[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);
    char fr_stk[KV_KEY_MAX];
    char lb_stk[KV_KEY_MAX];
    const char *cur_frame = NULL;
    rwir_cache_t cache = {0};
    int last_depth = 0; /* 0 = 尚未进入任何帧 */

    int rc = 0;
    for (;;) {
        /* Release borrowed values from the previous instruction. */
        kvlangKvspaceReadReset(kv);
        if (!kvlangVthreadMemberGetBuffer(kv, pc_key, cur_b, sizeof cur_b) ||
            !cur_b[0]) {
            rc = -1;
            break;
        }
        const char *cur = cur_b;
        /* Observe external status changes on every step. */
        if (!kvlangVthreadMemberGetBuffer(kv, st_key, status_b, sizeof status_b))
            break;
        const char *status = status_b;
        if (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 &&
            strcmp(status, "wait") != 0) {
            break;
        }

        int depth = kvlangKeytreeFrameNum(cur);
        if (depth != last_depth) {
            /* 帧深变化 = 帧被建（call）或删（return）：帧根重算、派生数据作废。 */
            if (!kvlangKeytreeFrameRootBuffer(cur, fr_stk, sizeof fr_stk)) {
                rc = execute_error(kv, vtid, cur,
                                "RuntimeError: invalid pc: %s", cur);
                break;
            }
            cur_frame = fr_stk;
            if (last_depth != 0)
                kvlangRwirCacheDrop(&cache, depth, depth > last_depth);
            last_depth = depth;
        }
        if (depth > MAX_STACK_DEPTH) {
            rc = execute_error(kv, vtid, cur,
                            "RecursionError: stack overflow: depth=%d pc=%s",
                            depth, cur);
            break;
        }

        kvlangRwirInst_t tmp;
        char err[256];
        rwir_cache_entry_t *hit = kvlangRwirCacheGet(&cache, cur);
        bool from_cache = hit != NULL;
        if (from_cache) {
            tmp = hit->instruction; /* 浅拷骨架；val 恒空，本步现读 */
        } else {
            if (kvlangKeytreeStackBuffer(cur_frame, lb_stk, sizeof lb_stk) == 0)
                kvlangDie("link base exceeds %zu bytes", sizeof lb_stk);
            int drc = kvlangRwirDecode(kv, lb_stk, cur, &tmp, err,
                                       sizeof err);
            if (drc != 0) {
                rc = execute_error(kv, vtid, cur, "decode: %.247s", err);
                break;
            }
            hit = kvlangRwirCachePut(kv, &cache, cur, depth, &tmp);
        }
        kvlangRwirInst_t *inst = &tmp;

        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur,
                       inst->opcode ? inst->opcode : "(empty)", inst->read_count,
                       inst->write_count);

        if (!inst->opcode || !inst->opcode[0]) {
            /* layout 对每条路径都补了 return（lower::terminate），走到空槽只能是 /lib 损坏
             * 或 goto/br 越界；报 RuntimeError 让该 vthread 停下，不拖垮整个进程。 */
            rc = execute_error(kv, vtid, cur,
                            "RuntimeError: no instruction at %s", cur);
            rwir_instruction_release(&tmp, from_cache);
            break;
        }

        if (!hit)
            kvlangDie("rwir cache entry missing for %s", cur);
        if (from_cache &&
            kvlangRwirCacheLoadValues(kv, inst, hit->load_write_value, err,
                                 sizeof err) != 0) {
            rc = execute_error(kv, vtid, cur, "%s", err);
            rwir_instruction_release(&tmp, true);
            break;
        }

        int exec_err = 0;
        char *yield = NULL;
        if (inst->opcode_id >= 0) {
            /* 单表派发：native 算子与 control/copy 同居 myrwircaps，opcode_id 直查一跳到底。 */
            kvlangFrame_t f = {kv, vtid, cur, inst, &yield};
            f.frame_root = cur_frame;
            f.status_known = status;   /* 本步开始前读到的 ‥status（源值，借用） */
            exec_err = kvlangBuiltinNative(&f);
            if (f.persist_failed)
                exec_err = -1;
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                return execute_yield(&tmp, from_cache, &cache, out_pc, yield);
            }
        } else if (kvlangDispatchIsDelegatedOp(kv, inst->opcode)) {
            exec_err = kvlangDispatchDelegate(kv, vtid, cur, inst);
            if (exec_err == KVLANG_DELEGATE_LOCAL) {
                exec_err = 0;
                goto user_call;
            }
        } else if (hit->is_def_rwir) {
            if (hit->def_read_sig)
                exec_err = check_read_types(kv, vtid, cur, inst->opcode,
                                            hit->def_read_sig, hit->def_read_count,
                                            hit->def_dynamic, inst->reads,
                                            inst->read_count);
            if (exec_err == 0 && hit->def_write_sig)
                exec_err = check_write_types(kv, vtid, cur, inst->opcode,
                                             hit->def_write_sig, hit->def_write_count);
            if (exec_err == 0 && mode == KVMODE_RETURN)
                return execute_yield(&tmp, from_cache, &cache, out_pc,
                                  out_pc ? strdup(cur) : NULL);
            if (exec_err == 0)
                exec_err = kvlangHandoffExternalRwir(kv, vtid, cur, inst);
        } else {
        user_call:
            exec_err = kvlangKvcpuCallOpcode(kv, vtid, cur, status,
                                             inst->opcode, inst->reads,
                                             inst->read_count, inst->writes,
                                             inst->write_count);
        }

        if (exec_err != 0) {
            rwir_instruction_release(&tmp, from_cache);
            rc = -1;
            break;
        }

        rwir_instruction_release(&tmp, from_cache);
    }

    kvlangRwirCacheFree(&cache);
    return rc;
}

int kvlangKvcpuExecute(kvspace_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc; /* WATCH 模式不返回 1，防御性归一 */
}
