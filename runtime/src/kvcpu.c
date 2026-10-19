#include "kvcpu_internal.h"

/* ── 指令派生数据缓存（消掉执行循环的每步 rwirDecode）────────────────────
 * 只缓存「派生数据」：opcode / op_id / nr / nw / 各操作数的 name·type·address
 * 与 load_write_value（该指令的写参是否要取值）。值（reads[].val / writes[].val）
 * 永不入缓存——真相源恒在 KVSpace，每步现读。故缓存随时可丢：删掉它只是回到每步
 * decode，崩溃恢复语义不变。
 * 键 = PC 绝对路径 `/vthread/<vtid>/[d]/[addr,seq]`，含 vthread 与帧坐标，帧内唯一。
 * 失效：call/return 后同深度帧会被整棵重建，故帧深变化即按帧作废——
 *   变深（call）→ 丢弃 depth >= 新深度（新帧全新）；
 *   变浅（return）→ 丢弃 depth > 新深度（更深的帧已随返回删除）。
 * PC 不可换「函数模板」键：操作数目标依赖「哪条调用路径到达本帧」——同为 [6]/[4,0]，
 * 经 fib(a) 与 fib(b) 到达时目标分别是 [5]/a 与 [5]/b——故跨调用路径复用会误用。
 * 帧深变化即作废正是为此。opcode/nr/nw 一类与实参无关的骨架数据另可按 (模板, addr0)
 * 缓存，但实测收益甚微（瓶颈在操作数物化，不在骨架），故未采用。 */

typedef struct {
    char *pc;              /* NULL = 空槽 */
    int depth;
    kvlangRwirInst_t inst; /* 自持 opcode/name/type；val 恒空 */
    bool load_write_value;
} rwir_cache_entry_t;

typedef struct {
    rwir_cache_entry_t *slot;
    size_t cap; /* 2 的幂；0 = 未分配 */
    size_t n;
} rwir_cache_t;

static size_t rwir_cache_hash(const char *s) {
    size_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ull;
    }
    return h;
}

static rwir_cache_entry_t *rwir_cache_get(rwir_cache_t *c, const char *pc) {
    if (c->cap == 0)
        return NULL;
    size_t i = rwir_cache_hash(pc) & (c->cap - 1);
    for (;;) {
        rwir_cache_entry_t *e = &c->slot[i];
        if (!e->pc)
            return NULL;
        if (strcmp(e->pc, pc) == 0)
            return e;
        i = (i + 1) & (c->cap - 1);
    }
}

static void rwir_cache_entry_free(rwir_cache_entry_t *e) {
    free(e->pc);
    free(e->inst.opcode);
    for (int i = 0; i < e->inst.nr; i++) {
        free(e->inst.reads[i].name);
        free(e->inst.reads[i].type);
    }
    for (int i = 0; i < e->inst.nw; i++) {
        free(e->inst.writes[i].name);
        free(e->inst.writes[i].type);
    }
    free(e->inst.reads);
    free(e->inst.writes);
    *e = (rwir_cache_entry_t){0};
}

static void rwir_cache_rehash(rwir_cache_t *c, size_t cap) {
    rwir_cache_entry_t *old = c->slot;
    size_t old_cap = c->cap;
    c->slot = calloc(cap, sizeof *c->slot);
    c->cap = cap;
    c->n = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].pc)
            continue;
        size_t j = rwir_cache_hash(old[i].pc) & (cap - 1);
        while (c->slot[j].pc)
            j = (j + 1) & (cap - 1);
        c->slot[j] = old[i];
        c->n++;
    }
    free(old);
}

/* 拷入派生数据（不外借 opcode/name/type）；值不入缓存。 */
static void rwir_cache_put(rwir_cache_t *c, const char *pc, int depth,
                           const kvlangRwirInst_t *inst, bool load_write_value) {
    if (!inst->opcode || !inst->opcode[0])
        return;
    if (c->cap == 0)
        rwir_cache_rehash(c, 256);
    else if (c->n * 4 >= c->cap * 3)
        rwir_cache_rehash(c, c->cap * 2);
    size_t i = rwir_cache_hash(pc) & (c->cap - 1);
    while (c->slot[i].pc && strcmp(c->slot[i].pc, pc) != 0)
        i = (i + 1) & (c->cap - 1);
    rwir_cache_entry_t *e = &c->slot[i];
    if (e->pc)
        rwir_cache_entry_free(e); /* 同键重放：丢弃旧的自持串，下面重新拷入 */
    else
        c->n++;
    e->pc = strdup(pc);
    e->depth = depth;
    e->load_write_value = load_write_value;
    e->inst.opcode = strdup(inst->opcode);
    e->inst.op_id = inst->op_id;
    e->inst.nr = inst->nr;
    e->inst.nw = inst->nw;
    e->inst.reads = malloc(sizeof(kvlangParam_t) * (size_t)(inst->nr ? inst->nr : 1));
    e->inst.writes = malloc(sizeof(kvlangParam_t) * (size_t)(inst->nw ? inst->nw : 1));
    for (int k = 0; k < inst->nr; k++) {
        e->inst.reads[k].name = strdup(inst->reads[k].name);
        e->inst.reads[k].type = inst->reads[k].type ? strdup(inst->reads[k].type) : NULL;
        e->inst.reads[k].address = inst->reads[k].address;
        kvlangXvalueZero(&e->inst.reads[k].val);
    }
    for (int k = 0; k < inst->nw; k++) {
        e->inst.writes[k].name = strdup(inst->writes[k].name);
        e->inst.writes[k].type = inst->writes[k].type ? strdup(inst->writes[k].type) : NULL;
        e->inst.writes[k].address = inst->writes[k].address;
        kvlangXvalueZero(&e->inst.writes[k].val);
    }
}

/* 帧深变化后按帧作废：丢 depth >= floor（变深）或 depth > floor（变浅）。 */
static void rwir_cache_drop(rwir_cache_t *c, int floor, bool inclusive) {
    for (size_t i = 0; i < c->cap; i++) {
        rwir_cache_entry_t *e = &c->slot[i];
        if (e->pc && (inclusive ? e->depth >= floor : e->depth > floor)) {
            rwir_cache_entry_free(e);
            c->n--;
        }
    }
}

static void rwir_cache_free(rwir_cache_t *c) {
    for (size_t i = 0; i < c->cap; i++) {
        if (c->slot[i].pc)
            rwir_cache_entry_free(&c->slot[i]);
    }
    free(c->slot);
}

/* 命中后按 name 现读值：缓存只存派生数据，值必须每步回 KVSpace 取。
 * 读参恒取值；写参与 decode 一致——仅 obj/map 指令取值。 */
static int rwir_cache_load_vals(kvlangKv_t *kv, kvlangRwirInst_t *inst,
                                bool load_write_value, char *err,
                                uint32_t err_cap) {
    for (int i = 0; i < inst->nr; i++) {
        kvlangParam_t *p = &inst->reads[i];
        if (kvlangKvGetOne(kv, p->name, &p->val) != 0) {
            snprintf(err, err_cap, "Decode: cannot read target for %s", p->name);
            return -1;
        }
        kvlangXvalueMaterialize(&p->val);
    }
    if (load_write_value) {
        for (int i = 0; i < inst->nw; i++) {
            kvlangParam_t *p = &inst->writes[i];
            if (kvlangKvGetOne(kv, p->name, &p->val) != 0) {
                snprintf(err, err_cap, "Decode: cannot read target for %s",
                         p->name);
                return -1;
            }
            kvlangXvalueMaterialize(&p->val);
        }
    }
    return 0;
}

/* 命中视角：opcode/name/type 借用缓存（不得 free），val 本步自持（须 kvlangXvalueFree）。 */
static void rwir_inst_release(kvlangRwirInst_t *inst, bool from_cache) {
    if (!from_cache) {
        kvlangRwirInstFree(inst);
        return;
    }
    for (int i = 0; i < inst->nr; i++)
        kvlangXvalueFree(&inst->reads[i].val);
    for (int i = 0; i < inst->nw; i++)
        kvlangXvalueFree(&inst->writes[i].val);
}

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc) {
    if (out_pc)
        *out_pc = NULL;
    char vtid_b[KV_KEY_MAX];
    const char *vtid = kvlangKeytreeVtidFromPc(pc, vtid_b, sizeof vtid_b);
    if (vtid[0] == 0) {
        return -1;
    }

    /* 每步复用的缓冲：pc/status 键与值、帧根、link base 全部跨步保留，免每步 malloc/free。 */
    char pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    char cur_b[KV_KEY_MAX], status_b[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);
    char fr_stk[512];
    char lb_stk[512];
    char *cur_frame = NULL; /* fr_stk，或超长帧根的 malloc 回退 */
    rwir_cache_t cache = {0};
    int last_depth = 0; /* 0 = 尚未进入任何帧 */

    int rc = 0;
    for (;;) {
        /* Release borrowed values from the previous instruction. */
        kvlangKvReadReset(kv);
        if (!kvlangVthreadMemberGetBuf(kv, pc_key, cur_b, sizeof cur_b) ||
            !cur_b[0]) {
            rc = -1;
            break;
        }
        const char *cur = cur_b;
        /* Observe external status changes on every step. */
        if (!kvlangVthreadMemberGetBuf(kv, st_key, status_b, sizeof status_b))
            break;
        const char *status = status_b;
        if (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 &&
            strcmp(status, "wait") != 0) {
            break;
        }

        cur_frame = kvlangKeytreeFrameRootBuf(cur, fr_stk, sizeof fr_stk)
                        ? fr_stk
                        : kvlangKeytreeFrameRoot(cur);
        if (!cur_frame) {
            char msg[256];
            snprintf(msg, sizeof msg, "RuntimeError: invalid pc: %s", cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rc = -1;
            break;
        }
        int depth = kvlangKeytreeFrameNum(cur);
        if (depth != last_depth) {
            /* 帧深变化 = 帧被建（call）或删（return）：同深度帧被整棵重建，派生数据作废。 */
            if (last_depth != 0)
                rwir_cache_drop(&cache, depth, depth > last_depth);
            last_depth = depth;
        }
        if (depth > MAX_STACK_DEPTH) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "RecursionError: stack overflow: depth=%d pc=%s", depth,
                     cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rc = -1;
            break;
        }

        kvlangRwirInst_t tmp;
        char err[256];
        rwir_cache_entry_t *hit = rwir_cache_get(&cache, cur);
        bool from_cache = hit != NULL;
        if (from_cache) {
            tmp = hit->inst; /* 浅拷骨架；val 恒空，本步现读 */
        } else {
            char *link_base =
                kvlangKeytreeStackBuf(cur_frame, lb_stk, sizeof lb_stk)
                    ? lb_stk
                    : kvlangKeytreeStack(cur_frame);
            int drc = kvlangRwirDecode(kv, link_base, cur, &tmp, err,
                                       sizeof err);
            if (link_base != lb_stk)
                free(link_base);
            if (drc != 0) {
                char msg[256];
                snprintf(msg, sizeof msg, "decode: %.247s", err);
                kvlangVthreadSetError(kv, vtid, cur, msg);
                rc = -1;
                break;
            }
            rwir_cache_put(&cache, cur, depth, &tmp,
                           tmp.opcode &&
                               (strcmp(tmp.opcode, "obj") == 0 ||
                                strcmp(tmp.opcode, "map") == 0));
        }
        kvlangRwirInst_t *inst = &tmp;

        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur,
                       inst->opcode ? inst->opcode : "(empty)", inst->nr,
                       inst->nw);

        if (!inst->opcode || !inst->opcode[0]) {
            /* layout 对每条路径都补了 return（lower::terminate），走到空槽只能是 /lib 损坏
             * 或 goto/br 越界；报 RuntimeError 让该 vthread 停下，不拖垮整个进程。 */
            char msg[512];
            snprintf(msg, sizeof msg, "RuntimeError: no instruction at %s",
                     cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rwir_inst_release(&tmp, from_cache);
            rc = -1;
            break;
        }

        if (from_cache &&
            rwir_cache_load_vals(kv, inst, hit->load_write_value, err,
                                 sizeof err) != 0) {
            kvlangVthreadSetError(kv, vtid, cur, err);
            rwir_inst_release(&tmp, true);
            rc = -1;
            break;
        }

        int exec_err = 0;
        char *yield = NULL;
        if (inst->op_id >= 0) {
            /* 单表派发：native 算子与 control/copy 同居 myrwircaps，op_id 直查一跳到底。 */
            kvlangFrame_t f = {kv, vtid, cur, inst, &yield};
            f.frame_root = cur_frame;
            f.status_known = status;   /* 本步开始前读到的 ‥status（源值，借用） */
            exec_err = kvlangBuiltinNative(&f);
            if (f.persist_failed)
                exec_err = -1;
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                if (out_pc)
                    *out_pc = yield;
                else
                    free(yield);
                rwir_inst_release(&tmp, from_cache);
                rwir_cache_free(&cache);
                return 1;
            }
        } else if (kvlangDispatchIsDelegatedOp(kv, inst->opcode)) {
            exec_err = kvlangDispatchDelegate(kv, vtid, cur, inst);
            if (exec_err == KVLANG_DELEGATE_LOCAL) {
                exec_err = 0;
                goto user_call;
            }
        } else if (notinmyrwircaps(kv, inst->opcode)) {
            int def_nr = 0, def_dyn = 0;
            char *rk = kvlangKeytreeRwir(inst->opcode);
            char *def_sig = load_def_reads(kv, rk, &def_nr, &def_dyn);
            int def_nw = 0;
            char *def_wsig = load_def_writes(kv, rk, &def_nw);
            free(rk);
            if (def_sig)
                exec_err = check_read_types(kv, vtid, cur, inst->opcode,
                                            def_sig, def_nr, def_dyn,
                                            inst->reads, inst->nr);
            if (exec_err == 0 && def_wsig)
                exec_err = check_write_types(kv, vtid, cur, inst->opcode,
                                             def_wsig, def_nw);
            free(def_sig);
            free(def_wsig);
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                if (out_pc)
                    *out_pc = strdup(cur);
                rwir_inst_release(&tmp, from_cache);
                rwir_cache_free(&cache);
                return 1;
            }
            if (exec_err == 0)
                exec_err = handoff_external_rwir(kv, vtid, cur, inst);
        } else {
        user_call:
            /* 用户函数 → call */
            kvlangRwirInst_t ci;
            ci.opcode = strdup(OP_CALL);
            ci.op_id = 0;
            ci.nr = inst->nr + 1;
            ci.nw = inst->nw;
            ci.reads = malloc(sizeof(kvlangParam_t) * (size_t)ci.nr);
            ci.reads[0].name = strdup(inst->opcode);
            ci.reads[0].type = NULL;
            ci.reads[0].address = 0;
            ci.reads[0].val.data = NULL;
            ci.reads[0].val.len = 0;
            for (int i = 0; i < inst->nr; i++) {
                ci.reads[i + 1] = inst->reads[i];
            }
            ci.writes = inst->writes;
            kvlangFrame_t cf = {kv, vtid, cur, &ci, NULL};
            cf.status_known = status;
            exec_err = kvlangCtlCall(&cf);
            if (cf.persist_failed)
                exec_err = -1;
            free(ci.opcode);
            free(ci.reads[0].name);
            free(ci.reads);
        }

        if (exec_err != 0) {
            rwir_inst_release(&tmp, from_cache);
            rc = -1;
            break;
        }

        rwir_inst_release(&tmp, from_cache);
    }

    if (cur_frame != fr_stk)
        free(cur_frame);
    rwir_cache_free(&cache);
    return rc;
}

int kvlangKvcpuExecute(kvlangKv_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc; /* WATCH 模式不返回 1，防御性归一 */
}
