#include "runtime_internal.h"

/* 找末个成员分隔符（·，多字节），对齐 strrchr('.') 的单字节旧语义。 */
static const char *rfind_sep(const char *s) {
    const char *found = NULL, *p = s;
    while ((p = strstr(p, MEMBER_SEP)) != NULL) {
        found = p;
        p += MEMBER_SEP_LEN;
    }
    return found;
}

static void die(const char *fmt, ...) {
    fputs("panic: ", stderr);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}

static int irseq_of(const kvlangParam_t *p) {
    if (kvlangXvalueNone(&p->val) || !kvlangXvalueKindIs(&p->val, KVSPACE_KIND_INT64)) {
        die("goto/br target is not int64 irseq: name=%s kind=%s",
            p->name ? p->name : "", kvlangXvalueKind(&p->val));
    }
    int64_t n = kvlangXvalueAsInt64(&p->val);
    if (n < 1 || n > 0x7fffffff) die("irseq out of range: %lld", (long long)n);
    return (int)n;
}

static char *jump_irseq(const char *pc, int irseq) {
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr) die("jump: no frame root: %s", pc);
    char *np = kvlangKeytreeIrseqPc(fr, irseq);
    free(fr);
    return np;
}

static int stack_depth(const char *pc) {
    int d = 0;
    for (; *pc; pc++) if (*pc == '[') d++;
    return d;
}

static bool is_literal(const char *s) {
    if (!s || !s[0]) return false;
    return s[0] == '"' || s[0] == '/' || strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
           strcmp(s, "null") == 0 || (s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && s[1]);
}

/* 派发期读参类型校验（runtime篇-07 第八节）：把每个实参的 kind 逐一匹配
 * rwir/rwfunc 定义的读参 kindexp。def_sig 为读参 kindexp 在前的 \n 分隔列表，
 * def_nr 为定义读参数。空 kindexp / any 跳过；末读参 "..." 变参吸收其后全部实参。
 * XValue 头只携带 array_len 不含多维 shape，故仅校验 kind 层。
 * 不匹配 → 置 TypeError，返回 -1；通过返回 0。 */
static int check_read_types(kvlangKv_t *kv, const char *vtid, const char *pc,
                            const char *opcode, const char *def_sig, int def_nr,
                            kvlangParam_t *args, int nargs) {
    if (def_nr <= 0 || !def_sig || !*def_sig) return 0;
    char *dup = strdup(def_sig);
    char *reads[128];
    int rn = 0;
    for (char *s = dup; rn < def_nr && rn < 128; ) {
        reads[rn++] = s;
        char *nl = strchr(s, '\n');
        if (!nl) break;
        *nl = 0; s = nl + 1;
    }
    bool var_last = rn > 0 && kvlang_rwirextKindexprVariadic(reads[rn - 1]);
    int min_args = var_last ? rn - 1 : rn;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int rc = 0;
    if (nargs < min_args) {
        char msg[256];
        snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, min_args, nargs);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        rc = -1;
    }
    for (int i = 0; rc == 0 && i < nargs; i++) {
        const char *exp = i < rn ? reads[i] : (var_last ? reads[rn - 1] : NULL);
        if (!exp) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, rn, nargs);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
            break;
        }
        if (!exp[0] || !kvlang_rwirextKindexprValid(exp)) continue;   /* 动态/非法 kindexp 跳过 */
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangBuiltinResolveReadValue(kv, fr, args[i].name, &args[i].val, &v);
        const char *k = kvlangXvalueKind(&v);
        kvspaceHead_t h; kvlangXvalueHead(&v, &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        bool ok = kvlang_rwirextKindexprMatch(exp, k, kx.ndim, kx.dims);
        char kbuf[40]; snprintf(kbuf, sizeof kbuf, "%s", k[0] ? k : "None");
        kvlangXvalueFree(&v);
        if (!ok) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s arg %d: expected %s, got %s", opcode, i + 1, exp, kbuf);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
        }
    }
    free(fr); free(dup);
    return rc;
}

/* 读取 rwir/rwfunc 定义体的 kindexp-list（nr/nw 前缀后的 \n 分隔串）。
 * 返回 malloc 串（调用方 free）并置 *out_nr；无定义返回 NULL。 */
static char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr) {
    *out_nr = 0;
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    if (kvlangXvalueNone(&v)) { kvlangXvalueFree(&v); return NULL; }
    kvspaceHead_t h; kvlangXvalueHead(&v, &h);
    int32_t bl; const uint8_t *b = kvlangXvalueBody(&v, &h, &bl);
    if (bl < 4) { kvlangXvalueFree(&v); return NULL; }
    *out_nr = b[0] | (b[1] << 8);
    size_t sl = (size_t)(bl - 4);
    char *sig = malloc(sl + 1);
    memcpy(sig, b + 4, sl); sig[sl] = 0;
    kvlangXvalueFree(&v);
    return sig;
}

static char *frame_slot_key(const char *frame_root, const char *slot) {
    if (!slot || !slot[0]) return NULL;
    if (slot[0] == '/') return strdup(slot);
    if (strncmp(slot, MEMBER_SEP, MEMBER_SEP_LEN) == 0) return NULL;
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangStrbufPuts(&b, stk); free(stk);
    kvlangStrbufPuts(&b, slot);
    return kvlangStrbufDetach(&b);
}

static char *resolve_read_path(kvlangKv_t *kv, const char *frame_path, const char *name) {
    if (is_literal(name)) return NULL;
    char *func_frame = kvlangBuiltinFuncFrameRoot(kv, frame_path);
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    char *stk = kvlangKeytreeStack(func_frame);
    kvlangStrbufPuts(&k, stk); free(stk);
    kvlangStrbufPuts(&k, name);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, k.p, &v);
    char *result = NULL;
    if (kvlangXvalueNone(&v)) {
        result = frame_slot_key(func_frame, name);
    } else if (kvlangXvalueIsPtr(&v)) {
        char *target = kvlangXvaluePtrTarget(&v);
        kvlangStrbuf_t path; kvlangStrbufInit(&path);
        char *stk2 = kvlangKeytreeStack(func_frame);
        kvlangStrbufPuts(&path, stk2); free(stk2);
        kvlangStrbufPuts(&path, target);
        free(target);
        for (;;) {
            kvlangXvalue_t nv; kvlangXvalueZero(&nv);
            kvlangKvGetOne(kv, path.p, &nv);
            if (kvlangXvalueNone(&nv) || !kvlangXvalueIsCharKind(kvlangXvalueKind(&nv))) { result = kvlangStrbufDetach(&path); kvlangXvalueFree(&nv); break; }
            char *p2 = kvlangXvalueValueString(&nv);
            kvlangXvalueFree(&nv);
            kvlangStrbufClear(&path); kvlangStrbufPuts(&path, p2);
            free(p2);
        }
    } else {
        kvlangStrbuf_t b; kvlangStrbufInit(&b);
        char *stk3 = kvlangKeytreeStack(func_frame);
        kvlangStrbufPuts(&b, stk3); free(stk3);
        kvlangStrbufPuts(&b, name);
        result = kvlangStrbufDetach(&b);
    }
    kvlangXvalueFree(&v); kvlangStrbufFree(&k); free(func_frame);
    return result;
}

static char *handle_return(kvlangKv_t *kv, const char *pc) {
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    kvlangStrbuf_t vtroot; kvlangStrbufInit(&vtroot);
    kvlangKeytreeVthread(vtid, &vtroot);
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (strcmp(fr, vtroot.p) == 0) { free(fr); kvlangStrbufFree(&vtid_b); kvlangStrbufFree(&vtroot); return NULL; }
    kvlangStrbuf_t rk; kvlangStrbufInit(&rk);
    kvlangKeytreeFrameReturnpc(fr, &rk);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, rk.p, &v);
    char *next = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    char *stk = kvlangKeytreeStack(fr);
    char err[256];
    kvlangKvDelExtIndex(kv, stk, err, sizeof err);
    kvlangKvDelTree(kv, fr, err, sizeof err);
    free(stk); free(fr); kvlangStrbufFree(&rk);
    kvlangStrbufFree(&vtid_b); kvlangStrbufFree(&vtroot);
    return next;
}

/* HandleCall：创建子帧。返回 EntryPC(frameRoot)，失败 NULL */
static char *handle_call(kvlangKv_t *kv, const char *pc, kvlangRwirInst_t *inst) {
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    const char *fn = inst->reads[0].name;
    char *pkg = strdup("");
    char *name = strdup(fn);
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = rfind_sep(rest);
        if (dot) { free(pkg); pkg = strndup(rest, (size_t)(dot - rest)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
        else { free(name); name = strdup(rest); }
    } else {
        const char *dot = rfind_sep(fn);
        if (dot) { free(pkg); pkg = strndup(fn, (size_t)(dot - fn)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
        else {
            /* 裸名调用（无 /lib/ 无 ·）：同 pkg 优先——当前函数所在 lib 下有同名 rwfunc 就用之
             * （lib aaa/bbb/math 内 sum(A,A) → /lib/aaa/bbb/math·sum），否则退回根 /lib/<fn>。 */
            char *fr = kvlangKeytreeFrameRoot(pc);
            char *ff = fr ? kvlangBuiltinFuncFrameRoot(kv, fr) : NULL;
            free(fr);
            if (ff) {
                /* 函数目录在帧的 ‥lib 槽（/lib/aaa/bbb/math·double/）：由此取调用者 pkg。 */
                kvlangStrbuf_t lk; kvlangStrbufInit(&lk);
                char *stk = kvlangKeytreeStack(ff);
                kvlangStrbufPuts(&lk, stk); free(stk);
                kvlangStrbufPuts(&lk, SEG_LIB);
                kvlangXvalue_t lv; kvlangXvalueZero(&lv);
                kvlangKvGetOne(kv, lk.p, &lv);
                kvlangStrbufFree(&lk);
                char *funcdir = kvlangXvalueNone(&lv) ? NULL : kvlangXvalueValueString(&lv);
                kvlangXvalueFree(&lv);
                if (funcdir) {
                    char *rel = funcdir + 5; // 剥 /lib/
                    size_t rl = strlen(rel);
                    if (rl > 0 && rel[rl - 1] == '/') rel[rl - 1] = '\0'; // 剥尾 /
                    const char *sep = rfind_sep(rel);
                    if (sep) {
                        char *cand_pkg = strndup(rel, (size_t)(sep - rel));
                        char *cand = kvlangKeytreeLibFunc(cand_pkg, fn);
                        kvlangStrbuf_t sk; kvlangStrbufInit(&sk);
                        kvlangStrbufPrintf(&sk, "%s/[0,0]", cand);
                        kvlangXvalue_t sv; kvlangXvalueZero(&sv);
                        kvlangKvGetOne(kv, sk.p, &sv);
                        bool ok = !kvlangXvalueNone(&sv) && kvlangXvalueKindIs(&sv, KVSPACE_KIND_DEF_RWFUNC);
                        kvlangXvalueFree(&sv); kvlangStrbufFree(&sk);
                        if (ok) { free(pkg); pkg = cand_pkg; }
                        else free(cand_pkg);
                        free(cand);
                    }
                    free(funcdir);
                }
                free(ff);
            }
        }
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir; kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key); kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key; kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig; kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) || !kvlangXvalueKindIs(&sig, KVSPACE_KIND_DEF_RWFUNC)) {
        /* 按 xvalue 的 kind 精确区分缺 rwir 还是缺 rwfunc：
         * 到这里说明 opcode 已被 isothersrwir 判否（/lib/<op> 非 rwir）。 */
        char *rk = kvlangKeytreeRwir(fn);
        kvlangXvalue_t rv; kvlangXvalueZero(&rv);
        kvlangKvGetOne(kv, rk, &rv);
        char msg[256];
        if (!kvlangXvalueNone(&rv) && kvlangXvalueKindIs(&rv, KVSPACE_KIND_DEF_RWIR))
            snprintf(msg, sizeof msg, "NameError: rwir 未注册/签名不匹配: %s", fn);
        else if (!kvlangXvalueNone(&sig))
            snprintf(msg, sizeof msg, "NameError: %s 不是 rwfunc (kind=%s)", fn, kvlangXvalueKind(&sig));
        else
            snprintf(msg, sizeof msg, "NameError: rwfunc not found: %s", fn);
        kvlangXvalueFree(&rv); free(rk);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        goto fail;
    }
    kvspaceHead_t h; kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);
    int nw = sbody[2] | (sbody[3] << 8);

    {   /* 读参类型校验：reads[0]=函数名，实参从 reads[1] 起 */
        size_t sl = h.body_len >= 4 ? (size_t)(h.body_len - 4) : 0;
        char *ds = malloc(sl + 1);
        memcpy(ds, sbody + 4, sl); ds[sl] = 0;
        int crc = check_read_types(kv, vtid, pc, fn, ds, nr, inst->reads + 1, inst->nr - 1);
        free(ds);
        if (crc != 0) goto fail;
    }

    char *caller_fr = kvlangKeytreeFrameRoot(pc);
    char *frame_root = strdup(pc);

    char *stack_fr = kvlangKeytreeStack(frame_root);
    char err[256];
    kvlangKvMkindex(kv, stack_fr, err, sizeof err);
    kvlangKvExtIndex(kv, stack_fr, func_dir.p, err, sizeof err);

    /* 系统变量 */
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc); kvlangRwirNextPc(pc, &npc);
    kvlangStrbuf_t retpc; kvlangStrbufInit(&retpc); kvlangKeytreeFrameReturnpc(frame_root, &retpc);
    kvlangStrbuf_t callpc; kvlangStrbufInit(&callpc); kvlangKeytreeFrameCallpc(frame_root, &callpc);
    char *ep = kvlangKeytreeEntryPc(frame_root);
    kvlangStrbuf_t seglib; kvlangStrbufInit(&seglib); kvlangStrbufPuts(&seglib, stack_fr); kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_npc, v_ep, v_fn; kvlangXvalueZero(&v_npc); kvlangXvalueZero(&v_ep); kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_npc, npc.p);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[3] = { { retpc.p, v_npc }, { callpc.p, v_ep }, { seglib.p, v_fn } };
    kvlangKvSet(kv, sys, 3, err, sizeof err);
    kvlangXvalueFree(&v_npc); kvlangXvalueFree(&v_ep); kvlangXvalueFree(&v_fn);

    /* 读参 + 写参 */
    kvlangKvPair_t pairs[512]; int np = 0;
    int lit_seq = 0;
    for (int i = 0; i < nr; i++) {
        kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
        kvlangStrbufPrintf(&slot, "%s/[0,-%d]", frame_root, i + 1);
        if (i + 1 < inst->nr) {
            kvlangParam_t *arg = &inst->reads[i + 1];
            char *rk = resolve_read_path(kv, caller_fr, arg->name);
            bool concrete = !kvlangXvalueNone(&arg->val) && !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWFUNC);
            if (concrete) {
                /* 字面量无变量槽，一律写 ._litN；勿沿用 resolve_read_path 的返回值——
                 * 否则字面量内容（如 "https://x" 里的 //）会被当路径段，二次读回即丢。 */
                if (rk) free(rk);
                kvlangStrbuf_t lk; kvlangStrbufInit(&lk);
                kvlangStrbufPrintf(&lk, "%s/._lit%d", caller_fr, lit_seq++);
                rk = kvlangStrbufDetach(&lk);
                /* 写字面量到 rk（拷贝，避免 double-free） */
                kvspaceHead_t ah; kvspaceDecodeHead(arg->val.data, arg->val.len, &ah);
                int32_t abl; const uint8_t *ab = kvlangXvalueBody(&arg->val, &ah, &abl);
                kvlang_kindexpr_t akx; kvlang_kindexpr_parse(ah.kindexpr, &akx);
                pairs[np].key = strdup(rk);
                kvspaceTlvEncode(kvlangXvalueKind(&arg->val), ab, (uint32_t)abl, akx.dims, akx.ndim,
                                   &pairs[np].val.data, &pairs[np].val.len);
                np++;
            }
            if (rk) {
                kvlangXvalue_t rv; kvlangXvalueNewCharUtf8(&rv, rk);
                pairs[np].key = kvlangStrbufDetach(&slot);
                pairs[np].val = rv;
                np++;
                free(rk);
            }
        }
        kvlangStrbufFree(&slot);
    }
    for (int i = 0; i < nw; i++) {
        kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
        kvlangStrbufPrintf(&slot, "%s/[0,%d]", frame_root, i + 1);
        if (i < inst->nw) {
            char *wk = resolve_read_path(kv, caller_fr, inst->writes[i].name);
            if (wk) {
                kvlangXvalue_t wv; kvlangXvalueNewCharUtf8(&wv, wk);
                pairs[np].key = kvlangStrbufDetach(&slot);
                pairs[np].val = wv;
                np++;
                free(wk);
            }
        }
        kvlangStrbufFree(&slot);
    }
    if (np > 0) kvlangKvSet(kv, pairs, np, err, sizeof err);
    for (int i = 0; i < np; i++) { free(pairs[i].key); kvlangXvalueFree(&pairs[i].val); }

    free(caller_fr); free(stack_fr);
    kvlangStrbufFree(&npc); kvlangStrbufFree(&retpc); kvlangStrbufFree(&callpc); kvlangStrbufFree(&seglib);
    kvlangStrbufFree(&func_dir); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig); free(func_key); free(pkg); free(name);
    free(frame_root);
    return ep;

fail:
    kvlangStrbufFree(&func_dir); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig); free(func_key); free(pkg); free(name);
    return NULL;
}

static int handle_control(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    if (strcmp(inst->opcode, OP_CALL) == 0) {
        char *sub = handle_call(kv, pc, inst);
        if (!sub) return -1;
        kvlangVthreadSet(kv, vtid, sub, "running");
        free(sub);
        return 0;
    }
    if (strcmp(inst->opcode, OP_RETURN) == 0) {
        char *parent = handle_return(kv, pc);
        if (!parent) { kvlangVthreadSetDone(kv, vtid, "ok"); return 0; }
        kvlangVthreadSet(kv, vtid, parent, "running");
        free(parent);
        return 0;
    }
    if (strcmp(inst->opcode, OP_GOTO) == 0) {
        if (inst->nr != 1) die("goto requires 1 irseq, got %d", inst->nr);
        char *np = jump_irseq(pc, irseq_of(&inst->reads[0]));
        kvlangVthreadSet(kv, vtid, np, "running");
        kvlangLogDebug("[%s] GOTO → %s", vtid, np);
        free(np);
        return 0;
    }
    if (strcmp(inst->opcode, OP_BR) == 0) {
        if (inst->nr != 3) die("br requires 3 args: cond trueIrseq falseIrseq, got %d", inst->nr);
        char *fr = kvlangKeytreeFrameRoot(pc);
        kvlangXvalue_t cond; kvlangXvalueZero(&cond);
        kvlangBuiltinResolveReadValue(kv, fr, inst->reads[0].name, &inst->reads[0].val, &cond);
        free(fr);
        if (kvlangXvalueNone(&cond)) {
            kvlangVthreadSetError(kv, vtid, pc, "TypeError: None in branch condition");
            kvlangXvalueFree(&cond);
            return -1;
        }
        int irseq = irseq_of(kvlangXvalueAsBool(&cond) ? &inst->reads[1] : &inst->reads[2]);
        kvlangXvalueFree(&cond);
        char *np = jump_irseq(pc, irseq);
        kvlangVthreadSet(kv, vtid, np, "running");
        free(np);
        return 0;
    }
    return -1;
}

/* 动态调用：以运行时得到的 funckey 在当前 vthread 造一次 OP_CALL（不新开 vid），
 * pc 落在被调入口，帧结束回到本指令 NextPc。供 native vthread·call 用。 */
int kvlangKvcpuDynCall(kvlangKv_t *kv, const char *vtid, const char *pc, const char *funckey) {
    kvlangRwirInst_t ci;
    ci.opcode = strdup(OP_CALL);
    ci.reads = malloc(sizeof(kvlangParam_t));
    ci.reads[0].name = strdup(funckey);
    kvlangXvalueZero(&ci.reads[0].val);
    ci.nr = 1;
    ci.writes = NULL;
    ci.nw = 0;
    int rc = handle_control(kv, vtid, pc, &ci);
    free(ci.opcode); free(ci.reads[0].name); free(ci.reads);
    return rc;
}

static bool is_copy_op(const char *opcode) {
    return strcmp(opcode, "=") == 0;
}

int handoff_external_rwir(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    /* handoff：把 pc 挂到共享队列 /lib/<opcode>/vids/<vtid>（各 rwir 的 vids 已 Ptr 统一到
     * 第一个 rwir 的 vids 下，Set 经路径穿透落到同一 strkeymap）。外部执行器认领并驱动该 vthread，
     * 完成后删除该条目。本端 watch 同一 key 直至变 None（== 认领方已完成），单键交接、无 id。 */
    char *base = kvlangKeytreeRwir(inst->opcode);
    kvlangStrbuf_t vids; kvlangStrbufInit(&vids);
    kvlangStrbufPrintf(&vids, "%s/vids/%s", base, vtid);
    kvlangXvalue_t pv; kvlangXvalueNewCharUtf8(&pv, pc);
    kvlangKvPair_t p = { vids.p, pv };
    char err[256];
    kvlangKvSet(kv, &p, 1, err, sizeof err);
    kvlangXvalueFree(&pv);

    kvlangXvalue_t none; kvlangXvalueZero(&none);   /* 目标 None：等条目被删除 */
    kvlangXvalue_t got; kvlangXvalueZero(&got);
    int rc = kvlangKvWatch(kv, vids.p, &none, 30000000000ULL, &got);
    kvlangXvalueFree(&got);
    kvlangStrbufFree(&vids); free(base);
    if (rc != 0) {
        char msg[256]; snprintf(msg, sizeof msg, "RuntimeError: external rwir %s handoff failed", inst->opcode);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        return -1;
    }
    return 0;
}

char *kvlangKvcpuBootstrap(kvlangKv_t *kv, const char *vtid, const char *funcname,
                      const char *const *args, int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = rfind_sep(funcname);
    if (dot) { free(pkg); pkg = strndup(funcname, (size_t)(dot - funcname)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir; kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key); kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key; kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig; kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) || !kvlangXvalueKindIs(&sig, KVSPACE_KIND_DEF_RWFUNC)) {
        char msg[256]; snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s", funcname);
        kvlangVthreadSetError(kv, vtid, "", msg);
        kvlangXvalueFree(&sig); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&func_dir);
        free(func_key); free(pkg); free(name);
        return NULL;
    }
    kvspaceHead_t h; kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);

    kvlangStrbuf_t vtroot; kvlangStrbufInit(&vtroot); kvlangKeytreeVthread(vtid, &vtroot);
    char *stack_vt = kvlangKeytreeStack(vtroot.p);
    char err[256];
    kvlangKvMkindex(kv, stack_vt, err, sizeof err);
    kvlangKvExtIndex(kv, stack_vt, func_dir.p, err, sizeof err);

    char *ep = kvlangKeytreeEntryPc(vtroot.p);
    kvlangStrbuf_t callpc; kvlangStrbufInit(&callpc); kvlangKeytreeFrameCallpc(vtroot.p, &callpc);
    kvlangStrbuf_t seglib; kvlangStrbufInit(&seglib); kvlangStrbufPuts(&seglib, stack_vt); kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_ep, v_fn; kvlangXvalueZero(&v_ep); kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[2] = { { callpc.p, v_ep }, { seglib.p, v_fn } };
    kvlangKvSet(kv, sys, 2, err, sizeof err);
    kvlangXvalueFree(&v_ep); kvlangXvalueFree(&v_fn);

    if (nargs > 0) {
        kvlangKvPair_t pairs[128]; int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
            kvlangStrbufPrintf(&slot, "%s/[0,-%d]", vtroot.p, i + 1);
            kvlangXvalue_t av; kvlangXvalueZero(&av);
            kvlangBuiltinResolveReadValue(kv, "", args[i], NULL, &av);
            pairs[np].key = kvlangStrbufDetach(&slot);
            pairs[np].val = av;
            np++;
        }
        if (np > 0) kvlangKvSet(kv, pairs, np, err, sizeof err);
        for (int i = 0; i < np; i++) { free(pairs[i].key); kvlangXvalueFree(&pairs[i].val); }
    }

    kvlangXvalueFree(&sig); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&func_dir);
    kvlangStrbufFree(&vtroot); kvlangStrbufFree(&callpc); kvlangStrbufFree(&seglib);
    free(stack_vt); free(func_key); free(pkg); free(name);
    return ep;
}

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode, char **out_pc) {
    if (out_pc) *out_pc = NULL;
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    if (vtid[0] == 0) { kvlangStrbufFree(&vtid_b); return -1; }

    char *cur = strdup(pc);
    int rc = 0;
    for (;;) {
        char *pcv = NULL, *status = NULL;
        kvlangVthreadGet(kv, vtid, &pcv, &status);
        if (!status || (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 && strcmp(status, "wait") != 0)) {
            free(pcv); free(status);
            break;
        }
        free(pcv); free(status);

        int depth = stack_depth(cur);
        if (depth > MAX_STACK_DEPTH) {
            char msg[256];
            snprintf(msg, sizeof msg, "RecursionError: stack overflow: depth=%d pc=%s", depth, cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rc = -1;
            break;
        }

        char *fr = kvlangKeytreeFrameRoot(cur);
        char *link_base = kvlangKeytreeStack(fr);
        kvlangRwirInst_t inst;
        char err[256];
        if (kvlangRwirDecode(kv, link_base, cur, &inst, err, sizeof err) != 0) {
            char msg[256]; snprintf(msg, sizeof msg, "decode: %s", err);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            free(fr); free(link_base);
            rc = -1;
            break;
        }
        free(link_base);

        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur, inst.opcode ? inst.opcode : "(empty)", inst.nr, inst.nw);

        if (!inst.opcode || !inst.opcode[0]) {
            die("Execute: empty instruction at %s", cur);
        }

        int exec_err = 0;
        if (op_is_control(inst.opcode)) {
            exec_err = handle_control(kv, vtid, cur, &inst);
        } else if (kvlangBuiltinIsNative(inst.opcode)) {
            char *yield = NULL;
            kvlangFrame_t f = { kv, vtid, cur, &inst, &yield };
            exec_err = kvlangBuiltinNative(&f);
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                if (out_pc) *out_pc = yield; else free(yield);
                free(fr); kvlangRwirInstFree(&inst);
                free(cur); kvlangStrbufFree(&vtid_b);
                return 1;
            }
        } else if (is_copy_op(inst.opcode)) {
            exec_err = kvlangBuiltinExecuteCopy(kv, vtid, cur, &inst);
        } else if (isothersrwir(kv, inst.opcode)) {
            char *rk = kvlangKeytreeRwir(inst.opcode);
            int def_nr = 0;
            char *def_sig = load_def_reads(kv, rk, &def_nr);
            free(rk);
            if (def_sig) {
                exec_err = check_read_types(kv, vtid, cur, inst.opcode, def_sig, def_nr, inst.reads, inst.nr);
                free(def_sig);
            }
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                if (out_pc) *out_pc = strdup(cur);
                free(fr); kvlangRwirInstFree(&inst);
                free(cur); kvlangStrbufFree(&vtid_b);
                return 1;
            }
            if (exec_err == 0) exec_err = handoff_external_rwir(kv, vtid, cur, &inst);
        } else {
            /* 用户函数 → call */
            kvlangRwirInst_t ci;
            ci.opcode = strdup(OP_CALL);
            ci.nr = inst.nr + 1;
            ci.nw = inst.nw;
            ci.reads = malloc(sizeof(kvlangParam_t) * (size_t)ci.nr);
            ci.reads[0].name = strdup(inst.opcode);
            ci.reads[0].val.data = NULL; ci.reads[0].val.len = 0;
            for (int i = 0; i < inst.nr; i++) { ci.reads[i + 1] = inst.reads[i]; }
            ci.writes = inst.writes;
            exec_err = handle_control(kv, vtid, cur, &ci);
            free(ci.opcode); free(ci.reads[0].name); free(ci.reads);
        }

        if (exec_err != 0) { free(fr); kvlangRwirInstFree(&inst); rc = -1; break; }

        char *newpc = NULL, *st = NULL;
        kvlangVthreadGet(kv, vtid, &newpc, &st);
        free(st);
        free(fr);
        kvlangRwirInstFree(&inst);
        if (!newpc || !newpc[0]) { free(newpc); break; }
        free(cur);
        cur = newpc;
    }

    free(cur);
    kvlangStrbufFree(&vtid_b);
    return rc;
}

int kvlangKvcpuExecute(kvlangKv_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc;   /* WATCH 模式不返回 1，防御性归一 */
}
