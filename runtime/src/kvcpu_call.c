#include "kvcpu_internal.h"
#include "func_code.h"
#include <assert.h>

/* 找末个成员分隔符（·，多字节），对齐 strrchr('.') 的单字节旧语义。 */
static const char *rfind_separator(const char *s) {
    const char *found = NULL, *p = s;
    while ((p = strstr(p, MEMBER_SEP)) != NULL) {
        found = p;
        p += MEMBER_SEP_LEN;
    }
    return found;
}

/* goto/br 目标：layout 已把 label 解析为 int64 irseq（≥1）。非 int64 / 越界返回 -1。 */
static int irseq_of(const kvlangParam_t *p) {
    if (kvlangXvalueNone(&p->value) ||
        !kvlangXvalueKindIs(&p->value, KVSPACE_KIND_INT64))
        return -1;
    int64_t n = kvlangScalarI64(kvlangXvalueScalar(&p->value));
    if (n < 1 || n > 0x7fffffff)
        return -1;
    return (int)n;
}

/* 函数内跳转：只改 PC 的 [irseq]，帧不变。目标非法 → RuntimeError，返回 -1。
 * 目标 irseq 已在行绑定期从 int64 字面量固化；PC 在栈缓冲里格式化，
 * 不按帧/按步分配。异常形状回退动态路径。 */
static int jump_to(kvlangFrame_t *f, int which, const kvlangParam_t *target,
                   const char *op) {
    const kvlangRowBind_t *b = f->bind;
    int known = (b && which >= 0 && which < 2) ? b->jump_irseq[which] : 0;
    int irseq = known > 0 ? known : irseq_of(target);
    if (irseq < 1) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "RuntimeError: %s target is not an int64 irseq: %s (kind=%s)",
                 op, target->name ? target->name : "",
                 kvlangXvalueKind(&target->value));
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    char pcbuf[KV_KEY_MAX];
    char *np = NULL;
    const char *next = NULL;
    if (kvlangKeytreeIrseqPcBuffer(fr, irseq, pcbuf, sizeof pcbuf) > 0) {
        next = pcbuf;
    } else {
        np = kvlangKeytreeIrseqPc(fr, irseq);
        next = np;
    }
    kvlangVthreadNextPc(f, next, "running");
    KV_LOG_DEBUG("[%s] %s → %s", f->vthread_id, op, next);
    /* 跳转后游标直接指向目标：下一步不重新解析完整 PC。 */
    if (f->cursor) {
        kvlangExecCursor *c = f->cursor;
        /* 与 kvlangKeytreeIrseqPcBuffer 相同的 stripped 帧根：去掉尾部 '/'。 */
        size_t pl = strlen(fr);
        while (pl > 0 && fr[pl - 1] == '/')
            pl--;
        if (pl + 1 < sizeof c->prefix) {
            memcpy(c->prefix, fr, pl);
            c->prefix[pl] = '/';
            c->prefix[pl + 1] = 0;
            c->prefix_len = (int)pl + 1;
            c->prefix_ok = true;
        } else {
            c->prefix_ok = false;
        }
        c->irseq = irseq;
        c->jump = true;
    }
    free(np);
    free(owned);
    return 0;
}

static bool is_literal(const char *s) {
    if (!s || !s[0])
        return false;
    return s[0] == '"' || strcmp(s, "true") == 0 ||
           strcmp(s, "false") == 0 || strcmp(s, "null") == 0 ||
           (s[0] >= '0' && s[0] <= '9') ||
           (s[0] == '-' && s[1] >= '0' && s[1] <= '9');
}


static char *frame_slot_key(const char *frame_root, const char *slot) {
    if (!slot || !slot[0])
        return NULL;
    if (slot[0] == '/')
        return strdup(slot);
    if (strncmp(slot, MEMBER_SEP, MEMBER_SEP_LEN) == 0)
        return NULL;
    char *stk = kvlangKeytreeStack(frame_root);
    char *r = kvlangStringFormat("%s%s", stk, slot);
    free(stk);
    return r;
}

/* Resolve a caller operand to its KVSpace key. */
static char *resolve_read_path(kvspace_t *kv, const char *frame_root,
                               const char *name) {
    if (is_literal(name))
        return NULL;
    if (name[0] == '*')
        return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
    return frame_slot_key(frame_root, name);
}

static int handle_return(kvspace_t *kv, const char *vtid, const char *pc,
                         char **out_next) {
    *out_next = NULL;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(fr);
    char *next = NULL;
    if (d > 1) {
        char rk[KV_KEY_MAX];
        kvlangKeytreeFrameReturnPc(fr, rk, sizeof rk);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(kv, rk, &v);
        if (!kvlangXvalueNone(&v))
            next = kvlangXvalueValueString(&v);
        if (!next || !next[0]) {
            char msg[512];
            snprintf(msg, sizeof msg,
                     "RuntimeError: broken return chain: frame %s has no "
                     "returnpc (pc=%s)",
                     fr, pc);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            free(next);
            free(fr);
            return -1;
        }
    }
    free(fr);
    *out_next = next;
    return 0;
}

static void param_decl_type(kvspace_t *kv, const char *func_key, int x,
                            char *lt, size_t cap);

#define MAX_BIND_PAIRS (MAX_PARAMS * 5)

static int add_param_binding(kvlangKvPair_t *pairs, bool *owned, int *np,
                             const char *frame_root, int x, const char *type,
                             const char *value_key) {
    if (!value_key || *np > MAX_BIND_PAIRS - 2)
        return -1;
    while (*type == '*' || *type == '@')
        type++;
    /* 空 kindexp（`name:` / `*`）：Ptr 目标不约束，照常绑定（[[文法与合法性]]）。 */
    char *arg_key = kvlangStringFormat("%s/%sarg[0,%d]", frame_root,
                               RUNTIME_MEMBER_SEP, x);
    char *slot_key = kvlangStringFormat("%s/[0,%d]", frame_root, x);
    kvlangXvalue_t arg_ptr, frame_ptr;
    kvlangXvalueNewPtr(&arg_ptr, type, value_key);
    kvlangXvalueNewPtr(&frame_ptr, type, arg_key);
    if (kvlangXvalueNone(&arg_ptr) || kvlangXvalueNone(&frame_ptr)) {
        kvlangXvalueFree(&arg_ptr);
        kvlangXvalueFree(&frame_ptr);
        free(arg_key);
        free(slot_key);
        return -1;
    }
    pairs[*np] = (kvlangKvPair_t){arg_key, arg_ptr};
    owned[(*np)++] = true;
    pairs[*np] = (kvlangKvPair_t){slot_key, frame_ptr};
    owned[(*np)++] = true;
    return 0;
}

/* Create the callee frame and return its entry PC. */
static char *handle_call(kvspace_t *kv, const char *pc,
                         kvlangRwirInst_t *inst) {
    char vtid_b[KV_KEY_MAX];
    const char *vtid = kvlangKeytreeVthreadIdFromPc(pc, vtid_b, sizeof vtid_b);
    /* 目标名由 kvlangKvcpuCallOpcode 用 strdup(opcode) 造出（静态调用取 opcode、
     * vthread·call 取运行时 funckey），不是行内操作数，故不会 NULL/字面量。 */
    const char *fn = inst->reads[0].name;
    assert(fn);
    char *pkg = strdup("");
    char *name = strdup(fn);
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = rfind_separator(rest);
        if (dot) {
            free(pkg);
            pkg = strndup(rest, (size_t)(dot - rest));
            free(name);
            name = strdup(dot + MEMBER_SEP_LEN);
        } else {
            free(name);
            name = strdup(rest);
        }
    } else {
        const char *dot = rfind_separator(fn);
        if (dot) {
            free(pkg);
            pkg = strndup(fn, (size_t)(dot - fn));
            free(name);
            name = strdup(dot + MEMBER_SEP_LEN);
        } else {
            /* 裸名调用（无 /lib/ 无 ·）：同 pkg 优先——当前函数所在 lib 下有同名 rwfunc 就用之
             * （lib aaa/bbb/math 内 sum(A,A) → /lib/aaa/bbb/math·sum），否则退回根 /lib/<fn>。 */
            char *ff = kvlangKeytreeFrameRoot(pc);
            if (ff) {
                /* 函数目录在帧的 ‥lib 槽（/lib/aaa/bbb/math·double/）：由此取调用者 pkg。 */
                char *stk = kvlangKeytreeStack(ff);
                char lk[KV_KEY_MAX];
                kvlangKeyFormat(lk, sizeof lk, "%s%s", stk, SEG_LIB);
                free(stk);
                kvlangXvalue_t lv;
                kvlangXvalueZero(&lv);
                kvlangKvspaceGetOne(kv, lk, &lv);
                char *funcdir =
                    kvlangXvalueNone(&lv) ? NULL : kvlangXvalueValueString(&lv);
                if (funcdir) {
                    char *rel = funcdir + 5; // 剥 /lib/
                    size_t rl = strlen(rel);
                    if (rl > 0 && rel[rl - 1] == '/')
                        rel[rl - 1] = '\0'; // 剥尾 /
                    const char *sep = rfind_separator(rel);
                    if (sep) {
                        char *cand_pkg = strndup(rel, (size_t)(sep - rel));
                        char *cand = kvlangKeytreeLibFunc(cand_pkg, fn);
                        char sk[KV_KEY_MAX];
                        kvlangKeyFormat(sk, sizeof sk, "%s/[0,0]", cand);
                        kvlangXvalue_t sv;
                        kvlangXvalueZero(&sv);
                        kvlangKvspaceGetOne(kv, sk, &sv);
                        bool ok = !kvlangXvalueNone(&sv) &&
                                  kvlangXvalueKindIs(&sv, KVSPACE_KIND_RWFUNC);
                        if (ok) {
                            free(pkg);
                            pkg = cand_pkg;
                        } else
                            free(cand_pkg);
                        free(cand);
                    }
                    free(funcdir);
                }
                free(ff);
            }
        }
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    char func_dir[KV_KEY_MAX];
    kvlangKeyFormat(func_dir, sizeof func_dir, "%s/", func_key);

    char sig_key[KV_KEY_MAX];
    kvlangKeyFormat(sig_key, sizeof sig_key, "%s[0,0]", func_dir);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvspaceGetOne(kv, sig_key, &sig);
    if (kvlangXvalueNone(&sig) ||
        !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        /* 按 xvalue 的 kind 精确区分缺 rwir 还是缺 rwfunc：
         * 到这里说明 opcode 已被 kvlangNotInMyRwirCaps 判否（/lib/<op> 非 def rwir 路由头）。 */
        char *rk = kvlangKeytreeRwir(fn);
        kvlangXvalue_t rv;
        kvlangXvalueZero(&rv);
        kvlangKvspaceGetOne(kv, rk, &rv);
        char msg[256];
        if (!kvlangXvalueNone(&rv) &&
            kvlangXvalueKindIs(&rv, KVSPACE_KIND_DEF_RWIR))
            snprintf(msg, sizeof msg, "NameError: rwir 未注册/签名不匹配: %s",
                     fn);
        else if (!kvlangXvalueNone(&sig))
            snprintf(msg, sizeof msg, "NameError: %s 不是 rwfunc (kind=%s)", fn,
                     kvlangXvalueKind(&sig));
        else
            snprintf(msg, sizeof msg, "NameError: rwfunc not found: %s", fn);
        free(rk);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        goto fail;
    }
    const uint8_t *sbody = xh_body(sig.bytes);
    int nr = sbody[0] | (sbody[1] << 8);
    int nw = sbody[2] | (sbody[3] << 8);
    if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
        kvlangVthreadSetError(kv, vtid, pc, "TypeError: too many parameters");
        goto fail;
    }

    /* rwfunc 不做派发期位置化读参校验：类型随命名参数键 funcDir/<name>（Ptr 的
     * target_langtype）承载，函数体在帧内按名解析、各 native 算子在使用点自校验操作数
     * kind。位置化的有序签名校验（join_read_signature+check_read_types）是 rwir 的需求——
     * 跨 runtime 队列按位置收参，故仅 def rwir 路由路径保留（见本文件 m->def_read_sig）。 */

    char *caller_fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(pc);
    char *frame_root = kvlangKeytreeFrameAt(vtid, d + 1);
    char err[256];
    kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    if (kvlangKvspaceMakeIndex(kv, stack_fr, 0, err, sizeof err) != 0 ||
        kvlangKvspaceExtensionIndex(kv, stack_fr, func_dir, err, sizeof err) != 0) {
        kvlangVthreadSetError(kv, vtid, pc, err);
        free(caller_fr);
        free(stack_fr);
        free(frame_root);
        goto fail;
    }

    /* 系统变量 */
    char npc[KV_KEY_MAX], retpc[KV_KEY_MAX], callpc[KV_KEY_MAX];
    if (kvlangRwirNextPcBuffer(pc, npc, sizeof npc) == 0)
        kvlangDie("next pc exceeds %zu bytes", sizeof npc);
    kvlangKeytreeFrameReturnPc(frame_root, retpc, sizeof retpc);
    kvlangKeytreeFrameCallPc(frame_root, callpc, sizeof callpc);
    char *ep = kvlangKeytreeEntryPc(frame_root);
    char seglib[KV_KEY_MAX];
    kvlangKeyFormat(seglib, sizeof seglib, "%s%s", stack_fr, SEG_LIB);
    kvlangXvalue_t v_npc, v_ep, v_fn;
    kvlangXvalueZero(&v_npc);
    kvlangXvalueZero(&v_ep);
    kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_npc, npc);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[3] = {
        {retpc, v_npc}, {callpc, v_ep}, {seglib, v_fn}};
    int sys_rc = kvlangXvalueNone(&v_npc) || kvlangXvalueNone(&v_ep) ||
                 kvlangXvalueNone(&v_fn) ? -1 :
                 kvlangKvspaceSet(kv, sys, 3, err, sizeof err);
    kvlangXvalueFree(&v_npc);
    kvlangXvalueFree(&v_ep);
    kvlangXvalueFree(&v_fn);
    if (sys_rc != 0) {
        kvlangVthreadSetError(kv, vtid, pc, "RuntimeError: cannot persist call frame");
        goto fail_frame;
    }

    kvlangKvPair_t pairs[MAX_BIND_PAIRS];
    bool owned[MAX_BIND_PAIRS] = {0};
    int np = 0;
    int bind_error = 0;
    for (int i = 0; i < nr && !bind_error; i++) {
        if (i + 1 >= inst->read_count) {
            snprintf(err, sizeof err, "TypeError: missing read argument %d", i + 1);
            bind_error = 1;
            break;
        }
        kvlangParam_t *arg = &inst->reads[i + 1];
        char lt[256] = {0};
        param_decl_type(kv, func_key, -(i + 1), lt, sizeof lt);
        bool concrete = !arg->is_address && !kvlangXvalueNone(&arg->value) &&
                        !kvlangXvalueKindIs(&arg->value, KVSPACE_KIND_RWIR) &&
                        !kvlangXvalueKindIs(&arg->value, KVSPACE_KIND_RWFUNC);
        bool literal = concrete || is_literal(arg->name);
        char *rk = literal ? NULL : resolve_read_path(kv, caller_fr, arg->name);
        if (literal) {
            rk = kvlangStringFormat("%s/._lit%d", frame_root, i);
            if (concrete) {
                if (np >= MAX_BIND_PAIRS) {
                    free(rk);
                    snprintf(err, sizeof err, "TypeError: too many arguments");
                    bind_error = 1;
                    break;
                }
                kvlangXvalue_t copy = arg->value;
                kvlangXvalueCopy(&copy);
                pairs[np] = (kvlangKvPair_t){strdup(rk), copy};
                owned[np++] = true;
            }
        }
        if (add_param_binding(pairs, owned, &np, frame_root, -(i + 1), lt, rk) != 0) {
            snprintf(err, sizeof err, "TypeError: cannot bind read argument %d", i + 1);
            bind_error = 1;
        }
        free(rk);
    }
    for (int i = 0; i < nw && !bind_error; i++) {
        char lt[256] = {0};
        param_decl_type(kv, func_key, i + 1, lt, sizeof lt);
        char *wk = i < inst->write_count ?
            resolve_read_path(kv, caller_fr, inst->writes[i].name) : NULL;
        if (add_param_binding(pairs, owned, &np, frame_root, i + 1, lt, wk) != 0) {
            snprintf(err, sizeof err, "TypeError: cannot bind write argument %d", i + 1);
            bind_error = 1;
        }
        free(wk);
    }
    int wrc = bind_error ? -1 : np > 0 ? kvlangKvspaceSet(kv, pairs, np, err, sizeof err) : 0;
    for (int i = 0; i < np; i++) {
        free(pairs[i].key);
        if (owned[i])
            kvlangXvalueFree(&pairs[i].value);
    }
    if (wrc != 0) {
        kvlangVthreadSetError(kv, vtid, pc, err);
        goto fail_frame;
    }

    free(caller_fr);
    free(stack_fr);
    free(func_key);
    free(pkg);
    free(name);
    free(frame_root);
    return ep;

fail_frame:
    kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
    free(caller_fr);
    free(stack_fr);
    free(ep);
    free(frame_root);
fail:
    free(func_key);
    free(pkg);
    free(name);
    return NULL;
}

/* Read the declared target type; the argument value may be None. */
static void param_decl_type(kvspace_t *kv, const char *func_key, int x,
                            char *lt, size_t cap) {
    if (cap)
        lt[0] = 0; /* 空 kindexp：body 只含 name+NUL，类型为空（不得沿用上一个参数） */
    char pk[KV_KEY_MAX];
    kvlangKeyFormat(pk, sizeof pk, "%s.[0,%d]", func_key, x);
    kvlangXvalue_t dv;
    kvlangXvalueZero(&dv);
    kvlangKvspaceGetOne(kv, pk, &dv);
    if (!kvlangXvalueNone(&dv)) {
        const uint8_t *ab = xh_body_of(&dv);
        int32_t al = xh_content_len_of(&dv);
        for (int bi = 0; ab && bi < al; bi++) {
            if (ab[bi] != 0)
                continue;
            int tl = al - (bi + 1);
            if (tl > 0 && tl < (int)cap) {
                memcpy(lt, ab + bi + 1, (size_t)tl);
                lt[tl] = 0;
            }
            break;
        }
    }
}

int kvlangControlCall(kvlangFrame_t *f) {
    char *sub = handle_call(f->kvspace, f->pc, f->instruction);
    if (!sub)
        return -1;
    kvlangVthreadNextPc(f, sub, "running");
    free(sub);
    return 0;
}

int kvlangControlReturn(kvlangFrame_t *f) {
    char *parent = NULL;
    if (handle_return(f->kvspace, f->vthread_id, f->pc, &parent) != 0)
        return -1;
    if (!parent) {
        f->persist_failed = kvlangVthreadSetDone(f->kvspace, f->vthread_id, "ok") != 0;
    } else {
        kvlangVthreadNextPc(f, parent, "running");
    }
    free(parent);
    if (f->persist_failed)
        return -1;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *stk = kvlangKeytreeStack(fr);
    char err[256];
    kvlangKvspaceDeleteExtensionIndex(f->kvspace, stk, err, sizeof err);
    kvlangKvspaceDeleteTree(f->kvspace, fr, err, sizeof err);
    free(stk);
    free(fr);
    return 0;
}

int kvlangControlGoto(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->instruction;
    if (inst->read_count != 1) {
        char msg[128];
        snprintf(msg, sizeof msg, "RuntimeError: goto expects 1 irseq, got %d",
                 inst->read_count);
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    return jump_to(f, 0, &inst->reads[0], OP_GOTO);
}

int kvlangControlBr(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->instruction;
    if (inst->read_count != 3) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "RuntimeError: br expects cond trueIrseq falseIrseq, got %d",
                 inst->read_count);
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    kvlangXvalue_t cond;
    kvlangXvalueZero(&cond);
    kvlangBuiltinResolveReadValue(f->kvspace, fr, inst->reads[0].name,
                                  &inst->reads[0].value, &cond);
    free(owned);
    if (kvlangXvalueNone(&cond)) {
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc,
                              "TypeError: None in branch condition");
        return -1;
    }
    if (!kvlangXvalueKindIs(&cond, KVSPACE_KIND_BOOL)) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "TypeError: branch condition must be bool, got %s",
                 kvlangXvalueKind(&cond));
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    bool taken = kvlangScalarI64(kvlangXvalueScalar(&cond)) != 0;
    return jump_to(f, taken ? 1 : 0, &inst->reads[taken ? 1 : 2], OP_BR);
}

/* 以伪 OP_CALL 进入被调函数：reads[0] 是目标函数，其后为实参。 */
int kvlangKvcpuCallOpcode(kvspace_t *kv, const char *vtid, const char *pc,
                          const char *status_known, const char *opcode,
                          kvlangParam_t *reads, int nr,
                          kvlangParam_t *writes, int nw, char *pc_out,
                          size_t pc_out_cap, bool *pc_feedback,
                          struct kvspaceKeyOff *pc_off,
                          struct kvspaceKeyOff *status_off) {
    kvlangRwirInst_t ci;
    ci.opcode = strdup(OP_CALL);
    ci.opcode_id = 0;
    ci.read_count = nr + 1;
    ci.write_count = nw;
    ci.reads = malloc(sizeof(kvlangParam_t) * (size_t)ci.read_count);
    ci.reads[0] = (kvlangParam_t){0};
    ci.reads[0].name = strdup(opcode);
    for (int i = 0; i < nr; i++)
        ci.reads[i + 1] = reads[i];
    ci.writes = writes;
    ci.write_offs = NULL;
    kvlangFrame_t f = {kv, vtid, pc, &ci, NULL};
    f.status_known = status_known;
    f.pc_out = pc_out;
    f.pc_out_cap = pc_out_cap;
    f.pc_off = pc_off;
    f.status_off = status_off;
    int rc = kvlangControlCall(&f);
    if (f.persist_failed)
        rc = -1;
    *pc_feedback = f.pc_feedback;
    free(ci.opcode);
    free(ci.reads[0].name);
    free(ci.reads);
    return rc;
}

/* 动态调用：以运行时得到的 funckey 在当前 vthread 造一次 OP_CALL（不新开 vid），
 * pc 落在被调入口，帧结束回到本指令 NextPc。供 native vthread·call 用。 */
int kvlangKvcpuDynamicCall(kvlangFrame_t *f, const char *funckey) {
    return kvlangKvcpuCallOpcode(f->kvspace, f->vthread_id, f->pc, NULL, funckey,
                                 NULL, 0, NULL, 0, f->pc_out, f->pc_out_cap,
                                 &f->pc_feedback, f->pc_off, f->status_off);
}


char *kvlangKvcpuBootstrap(kvspace_t *kv, const char *vtid,
                           const char *funcname, const char *const *args,
                           int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = rfind_separator(funcname);
    if (dot) {
        free(pkg);
        pkg = strndup(funcname, (size_t)(dot - funcname));
        free(name);
        name = strdup(dot + MEMBER_SEP_LEN);
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    char func_dir[KV_KEY_MAX];
    kvlangKeyFormat(func_dir, sizeof func_dir, "%s/", func_key);

    char sig_key[KV_KEY_MAX];
    kvlangKeyFormat(sig_key, sizeof sig_key, "%s[0,0]", func_dir);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvspaceGetOne(kv, sig_key, &sig);
    if (kvlangXvalueNone(&sig) ||
        !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        char msg[256];
        snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s",
                 funcname);
        kvlangVthreadSetError(kv, vtid, "", msg);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }
    const uint8_t *sbody = xh_body(sig.bytes);
    int nr = sbody[0] | (sbody[1] << 8);

    char *frame_root = kvlangKeytreeFrameAt(vtid, 1);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    char err[256];
    if (kvlangKvspaceMakeIndex(kv, stack_fr, 0, err, sizeof err) != 0 ||
        kvlangKvspaceExtensionIndex(kv, stack_fr, func_dir, err, sizeof err) != 0) {
        kvlangVthreadSetError(kv, vtid, "", err);
        free(stack_fr);
        free(frame_root);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }

    char *ep = kvlangKeytreeEntryPc(frame_root);
    char callpc[KV_KEY_MAX], seglib[KV_KEY_MAX];
    kvlangKeytreeFrameCallPc(frame_root, callpc, sizeof callpc);
    kvlangKeyFormat(seglib, sizeof seglib, "%s%s", stack_fr, SEG_LIB);
    kvlangXvalue_t v_ep, v_fn;
    kvlangXvalueZero(&v_ep);
    kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[2] = {{callpc, v_ep}, {seglib, v_fn}};
    int sys_rc = kvlangXvalueNone(&v_ep) || kvlangXvalueNone(&v_fn) ? -1 :
                 kvlangKvspaceSet(kv, sys, 2, err, sizeof err);
    kvlangXvalueFree(&v_ep);
    kvlangXvalueFree(&v_fn);
    if (sys_rc != 0)
        snprintf(err, sizeof err, "RuntimeError: cannot persist entry frame");

    int bind_error = sys_rc != 0;
    if (!bind_error && (nargs < 0 || nargs > MAX_PARAMS || nr > MAX_PARAMS)) {
        snprintf(err, sizeof err, "TypeError: too many entry parameters");
        bind_error = 1;
    }
    if (nargs > 0 && !bind_error) {
        kvlangKvPair_t pairs[MAX_BIND_PAIRS];
        bool owned[MAX_BIND_PAIRS] = {0};
        int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            char *value_key = kvlangStringFormat("%s/._arglit%d", frame_root, i);
            kvlangXvalue_t av;
            kvlangXvalueZero(&av);
            kvlangBuiltinResolveReadValue(kv, "", args[i], NULL, &av);
            if (!kvlangXvalueNone(&av))
                pairs[np++] = (kvlangKvPair_t){strdup(value_key), av};
            char lt[256] = {0};
            param_decl_type(kv, func_key, -(i + 1), lt, sizeof lt);
            if (add_param_binding(pairs, owned, &np, frame_root, -(i + 1), lt, value_key) != 0) {
                snprintf(err, sizeof err, "TypeError: cannot bind entry argument %d", i + 1);
                bind_error = 1;
                free(value_key);
                break;
            }
            free(value_key);
        }
        if (!bind_error && np > 0 && kvlangKvspaceSet(kv, pairs, np, err, sizeof err) != 0)
            bind_error = 1;
        for (int i = 0; i < np; i++) {
            free(pairs[i].key);
            if (owned[i])
                kvlangXvalueFree(&pairs[i].value);
        }
    }
    if (bind_error) {
        kvlangVthreadSetError(kv, vtid, "", err);
        kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
        free(ep);
        ep = NULL;
    }

    free(stack_fr);
    free(frame_root);
    free(func_key);
    free(pkg);
    free(name);
    return ep;
}
