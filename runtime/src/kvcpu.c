#include "runtime_internal.h"

static int stack_depth(const char *pc) {
    int d = 0;
    for (; *pc; pc++) if (*pc == '[') d++;
    return d;
}

/* ExtKind：有 .lib → rwfunc，否则空 */
static const char *ext_kind(kv_t *kv, const char *frame_root) {
    sbuf_t k; sb_init(&k);
    char *stk = kt_stack(frame_root);
    sb_puts(&k, stk); free(stk);
    sb_puts(&k, SEG_LIB);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, k.p, &v);
    bool has = !xv_none(&v);
    xv_free(&v); sb_free(&k);
    return has ? K_RWFUNC : "";
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
static int check_read_types(kv_t *kv, const char *vtid, const char *pc,
                            const char *opcode, const char *def_sig, int def_nr,
                            param_t *args, int nargs) {
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
    bool var_last = rn > 0 && type_expr_variadic(reads[rn - 1]);
    int min_args = var_last ? rn - 1 : rn;
    char *fr = kt_frame_root(pc);
    int rc = 0;
    if (nargs < min_args) {
        char msg[256];
        snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, min_args, nargs);
        vt_set_error(kv, vtid, pc, msg);
        rc = -1;
    }
    for (int i = 0; rc == 0 && i < nargs; i++) {
        const char *exp = i < rn ? reads[i] : (var_last ? reads[rn - 1] : NULL);
        if (!exp) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, rn, nargs);
            vt_set_error(kv, vtid, pc, msg);
            rc = -1;
            break;
        }
        if (!exp[0] || !type_expr_valid(exp)) continue;   /* 动态/非法 kindexp 跳过 */
        xval_t v; xv_zero(&v);
        bi_resolve_read_value(kv, fr, args[i].name, &args[i].val, &v);
        const char *k = xv_kind(&v);
        kvhead_t h; xv_head(&v, &h);
        bool ok = type_expr_match(exp, k, h.ndim, h.dims);
        char kbuf[40]; snprintf(kbuf, sizeof kbuf, "%s", k[0] ? k : "None");
        xv_free(&v);
        if (!ok) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s arg %d: expected %s, got %s", opcode, i + 1, exp, kbuf);
            vt_set_error(kv, vtid, pc, msg);
            rc = -1;
        }
    }
    free(fr); free(dup);
    return rc;
}

/* 读取 rwir/rwfunc 定义体的 kindexp-list（nr/nw 前缀后的 \n 分隔串）。
 * 返回 malloc 串（调用方 free）并置 *out_nr；无定义返回 NULL。 */
static char *load_def_reads(kv_t *kv, const char *key, int *out_nr) {
    *out_nr = 0;
    xval_t v; xv_zero(&v);
    kv_get_one(kv, key, &v);
    if (xv_none(&v)) { xv_free(&v); return NULL; }
    kvhead_t h; xv_head(&v, &h);
    int32_t bl; const uint8_t *b = xv_body(&v, &h, &bl);
    if (bl < 4) { xv_free(&v); return NULL; }
    *out_nr = b[0] | (b[1] << 8);
    size_t sl = (size_t)(bl - 4);
    char *sig = malloc(sl + 1);
    memcpy(sig, b + 4, sl); sig[sl] = 0;
    xv_free(&v);
    return sig;
}

static char *frame_slot_key(const char *frame_root, const char *slot) {
    if (!slot || !slot[0]) return NULL;
    if (slot[0] == '/') return strdup(slot);
    if (slot[0] == '.') return NULL;
    sbuf_t b; sb_init(&b);
    char *stk = kt_stack(frame_root);
    sb_puts(&b, stk); free(stk);
    sb_puts(&b, slot);
    return sb_detach(&b);
}

static char *resolve_read_path(kv_t *kv, const char *frame_path, const char *name) {
    if (is_literal(name)) return NULL;
    char *func_frame = bi_func_frame_root(kv, frame_path);
    sbuf_t k; sb_init(&k);
    char *stk = kt_stack(func_frame);
    sb_puts(&k, stk); free(stk);
    sb_puts(&k, name);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, k.p, &v);
    char *result = NULL;
    if (xv_none(&v)) {
        result = frame_slot_key(func_frame, name);
    } else if (xv_is_ptr(&v)) {
        char *target = xv_ptr_target(&v);
        sbuf_t path; sb_init(&path);
        char *stk2 = kt_stack(func_frame);
        sb_puts(&path, stk2); free(stk2);
        sb_puts(&path, target);
        free(target);
        for (;;) {
            xval_t nv; xv_zero(&nv);
            kv_get_one(kv, path.p, &nv);
            if (xv_none(&nv) || !xv_is_char_kind(xv_kind(&nv))) { result = sb_detach(&path); xv_free(&nv); break; }
            char *p2 = xv_value_string(&nv);
            xv_free(&nv);
            sb_clear(&path); sb_puts(&path, p2);
            free(p2);
        }
    } else {
        sbuf_t b; sb_init(&b);
        char *stk3 = kt_stack(func_frame);
        sb_puts(&b, stk3); free(stk3);
        sb_puts(&b, name);
        result = sb_detach(&b);
    }
    xv_free(&v); sb_free(&k); free(func_frame);
    return result;
}

static char *handle_scope_return(kv_t *kv, const char *pc) {
    char *fr = kt_frame_root(pc);
    sbuf_t rk; sb_init(&rk);
    kt_frame_returnpc(fr, &rk);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, rk.p, &v);
    char *parent = xv_none(&v) ? NULL : xv_value_string(&v);
    xv_free(&v);
    char *stk = kt_stack(fr);
    char err[256];
    kv_del_ext_index(kv, stk, err, sizeof err);
    kv_del_tree(kv, fr, err, sizeof err);
    free(stk); free(fr); sb_free(&rk);
    return parent;
}

static char *handle_return(kv_t *kv, const char *pc) {
    sbuf_t vtid_b; sb_init(&vtid_b);
    const char *vtid = kt_vtid_from_pc(pc, &vtid_b);
    sbuf_t vtroot; sb_init(&vtroot);
    kt_vthread(vtid, &vtroot);
    char *fr = kt_frame_root(pc);
    if (strcmp(fr, vtroot.p) == 0) { free(fr); sb_free(&vtid_b); sb_free(&vtroot); return NULL; }
    sbuf_t rk; sb_init(&rk);
    kt_frame_returnpc(fr, &rk);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, rk.p, &v);
    char *next = xv_none(&v) ? strdup("") : xv_value_string(&v);
    xv_free(&v);
    char *stk = kt_stack(fr);
    char err[256];
    kv_del_ext_index(kv, stk, err, sizeof err);
    kv_del_tree(kv, fr, err, sizeof err);
    free(stk); free(fr); sb_free(&rk);
    sb_free(&vtid_b); sb_free(&vtroot);
    return next;
}

static char *handle_scope(kv_t *kv, const char *pc, const char *scope_name) {
    char *fr = kt_frame_root(pc);
    char *rw_root = bi_func_frame_root(kv, fr);
    free(fr);
    size_t n = strlen(rw_root);
    while (n > 0 && rw_root[n - 1] == '/') n--;
    sbuf_t scope_frame; sb_init(&scope_frame);
    sb_putn(&scope_frame, rw_root, n);
    sb_putc(&scope_frame, '/');
    sb_puts(&scope_frame, scope_name);
    sb_putc(&scope_frame, '/');
    free(rw_root);

    sbuf_t callpc; sb_init(&callpc);
    kt_frame_callpc(scope_frame.p, &callpc);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, callpc.p, &v);
    bool exists = !xv_none(&v);
    xv_free(&v);

    char err[256];
    if (!exists) {
        kv_mkindex(kv, scope_frame.p, err, sizeof err);
        sbuf_t npc; sb_init(&npc);
        rwir_next_pc(pc, &npc);
        sbuf_t retpc; sb_init(&retpc);
        kt_frame_returnpc(scope_frame.p, &retpc);
        xval_t rv; xv_new_char_utf8(&rv, npc.p);
        kv_pair_t p = { retpc.p, rv };
        kv_set(kv, &p, 1, err, sizeof err);
        xv_free(&rv); sb_free(&npc); sb_free(&retpc);
    }
    char *sep = kt_scope_entry_pc(scope_frame.p);
    sbuf_t callpc2; sb_init(&callpc2);
    kt_frame_callpc(scope_frame.p, &callpc2);
    xval_t cv; xv_new_char_utf8(&cv, sep);
    kv_pair_t p = { callpc2.p, cv };
    kv_set(kv, &p, 1, err, sizeof err);
    xv_free(&cv);

    sb_free(&callpc); sb_free(&callpc2); sb_free(&scope_frame);
    return sep;
}

/* HandleCall：创建子帧。返回 EntryPC(frameRoot)，失败 NULL */
static char *handle_call(kv_t *kv, const char *pc, rwir_inst_t *inst) {
    sbuf_t vtid_b; sb_init(&vtid_b);
    const char *vtid = kt_vtid_from_pc(pc, &vtid_b);
    const char *fn = inst->reads[0].name;
    char *pkg = strdup("");
    char *name = strdup(fn);
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = strrchr(rest, '.');
        if (dot) { free(pkg); pkg = strndup(rest, (size_t)(dot - rest)); free(name); name = strdup(dot + 1); }
        else { free(name); name = strdup(rest); }
    } else {
        const char *dot = strrchr(fn, '.');
        if (dot) { free(pkg); pkg = strndup(fn, (size_t)(dot - fn)); free(name); name = strdup(dot + 1); }
    }
    char *func_key = kt_lib_func(pkg, name);
    sbuf_t func_dir; sb_init(&func_dir);
    sb_puts(&func_dir, func_key); sb_putc(&func_dir, '/');

    sbuf_t sig_key; sb_init(&sig_key);
    sb_printf(&sig_key, "%s[0,0]", func_dir.p);
    xval_t sig; xv_zero(&sig);
    kv_get_one(kv, sig_key.p, &sig);
    if (xv_none(&sig) || !xv_kind_is(&sig, K_RWFUNC)) {
        sbuf_t vtroot; sb_init(&vtroot); kt_vthread(vtid, &vtroot);
        char msg[256]; snprintf(msg, sizeof msg, "NameError: rwir/rwfunc not found: %s", fn);
        vt_set_error(kv, vtid, pc, msg);
        sb_free(&vtroot);
        goto fail;
    }
    kvhead_t h; kvspace_decode_head(sig.data, sig.len, &h);
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

    char *caller_fr = kt_frame_root(pc);
    char *frame_root = strdup(pc);

    char *stack_fr = kt_stack(frame_root);
    char err[256];
    kv_mkindex(kv, stack_fr, err, sizeof err);
    kv_ext_index(kv, stack_fr, func_dir.p, err, sizeof err);

    /* 系统变量 */
    sbuf_t npc; sb_init(&npc); rwir_next_pc(pc, &npc);
    sbuf_t retpc; sb_init(&retpc); kt_frame_returnpc(frame_root, &retpc);
    sbuf_t callpc; sb_init(&callpc); kt_frame_callpc(frame_root, &callpc);
    char *ep = kt_entry_pc(frame_root);
    sbuf_t seglib; sb_init(&seglib); sb_puts(&seglib, stack_fr); sb_puts(&seglib, SEG_LIB);
    xval_t v_npc, v_ep, v_fn; xv_zero(&v_npc); xv_zero(&v_ep); xv_zero(&v_fn);
    xv_new_char_utf8(&v_npc, npc.p);
    xv_new_char_utf8(&v_ep, ep);
    xv_new_char_utf8(&v_fn, func_key);
    kv_pair_t sys[3] = { { retpc.p, v_npc }, { callpc.p, v_ep }, { seglib.p, v_fn } };
    kv_set(kv, sys, 3, err, sizeof err);
    xv_free(&v_npc); xv_free(&v_ep); xv_free(&v_fn);

    /* 读参 + 写参 */
    kv_pair_t pairs[512]; int np = 0;
    int lit_seq = 0;
    for (int i = 0; i < nr; i++) {
        sbuf_t slot; sb_init(&slot);
        sb_printf(&slot, "%s/[0,-%d]", frame_root, i + 1);
        if (i + 1 < inst->nr) {
            param_t *arg = &inst->reads[i + 1];
            char *rk = resolve_read_path(kv, caller_fr, arg->name);
            bool concrete = !xv_none(&arg->val) && !xv_kind_is(&arg->val, K_RWIR) && !xv_kind_is(&arg->val, K_RWFUNC);
            if (concrete) {
                if (!rk) {
                    sbuf_t lk; sb_init(&lk);
                    sb_printf(&lk, "%s/._lit%d", caller_fr, lit_seq++);
                    rk = sb_detach(&lk);
                }
                /* 写字面量到 rk（拷贝，避免 double-free） */
                kvhead_t ah; kvspace_decode_head(arg->val.data, arg->val.len, &ah);
                int32_t abl; const uint8_t *ab = xv_body(&arg->val, &ah, &abl);
                pairs[np].key = strdup(rk);
                kvspace_tlv_encode(xv_kind(&arg->val), ab, (uint32_t)abl, ah.array_len,
                                   &pairs[np].val.data, &pairs[np].val.len);
                np++;
            }
            if (rk) {
                xval_t rv; xv_new_char_utf8(&rv, rk);
                pairs[np].key = sb_detach(&slot);
                pairs[np].val = rv;
                np++;
                free(rk);
            }
        }
        sb_free(&slot);
    }
    for (int i = 0; i < nw; i++) {
        sbuf_t slot; sb_init(&slot);
        sb_printf(&slot, "%s/[0,%d]", frame_root, i + 1);
        if (i < inst->nw) {
            char *wk = resolve_read_path(kv, caller_fr, inst->writes[i].name);
            if (wk) {
                xval_t wv; xv_new_char_utf8(&wv, wk);
                pairs[np].key = sb_detach(&slot);
                pairs[np].val = wv;
                np++;
                free(wk);
            }
        }
        sb_free(&slot);
    }
    if (np > 0) kv_set(kv, pairs, np, err, sizeof err);
    for (int i = 0; i < np; i++) { free(pairs[i].key); xv_free(&pairs[i].val); }

    free(caller_fr); free(stack_fr);
    sb_free(&npc); sb_free(&retpc); sb_free(&callpc); sb_free(&seglib);
    sb_free(&func_dir); sb_free(&sig_key); sb_free(&vtid_b);
    xv_free(&sig); free(func_key); free(pkg); free(name);
    free(frame_root);
    return ep;

fail:
    sb_free(&func_dir); sb_free(&sig_key); sb_free(&vtid_b);
    xv_free(&sig); free(func_key); free(pkg); free(name);
    return NULL;
}

static int handle_control(kv_t *kv, const char *vtid, const char *pc, rwir_inst_t *inst) {
    if (strcmp(inst->opcode, OP_CALL) == 0) {
        char *sub = handle_call(kv, pc, inst);
        if (!sub) return -1;
        vt_set(kv, vtid, sub, "running");
        free(sub);
        return 0;
    }
    if (strcmp(inst->opcode, OP_RETURN) == 0) {
        char *parent = handle_return(kv, pc);
        if (!parent) { vt_set_done(kv, vtid, "ok"); return 0; }
        vt_set(kv, vtid, parent, "running");
        free(parent);
        return 0;
    }
    if (strcmp(inst->opcode, OP_GOTO) == 0) {
        if (inst->nr == 0) return -1;
        char *np = handle_scope(kv, pc, inst->reads[0].name);
        if (!np) { vt_set_error(kv, vtid, pc, "RuntimeError: goto failed"); return -1; }
        vt_set(kv, vtid, np, "running");
        free(np);
        return 0;
    }
    if (strcmp(inst->opcode, OP_BR) == 0) {
        if (inst->nr < 3) return -1;
        char *fr = kt_frame_root(pc);
        xval_t cond; xv_zero(&cond);
        bi_resolve_read_value(kv, fr, inst->reads[0].name, &inst->reads[0].val, &cond);
        free(fr);
        if (xv_none(&cond)) { vt_set_error(kv, vtid, pc, "TypeError: None in branch condition"); xv_free(&cond); return -1; }
        const char *label = inst->reads[2].name;
        if (xv_as_bool(&cond)) label = inst->reads[1].name;
        xv_free(&cond);
        char *np = handle_scope(kv, pc, label);
        if (!np) { vt_set_error(kv, vtid, pc, "RuntimeError: br failed"); return -1; }
        vt_set(kv, vtid, np, "running");
        free(np);
        return 0;
    }
    return -1;
}

static bool is_copy_op(const char *opcode) {
    return strcmp(opcode, "=") == 0;
}

static bool is_ext_rwir(kv_t *kv, const char *opcode) {
    if (opcode[0] == '/') return false;
    char *rk = kt_rwir(opcode);
    xval_t v; xv_zero(&v);
    kv_get_one(kv, rk, &v);
    bool r = !xv_none(&v) && xv_kind_is(&v, K_RWIR);
    xv_free(&v); free(rk);
    return r;
}

static int64_t handoff_seq = 0;

static int handoff_external_rwir(kv_t *kv, const char *vtid, const char *pc, rwir_inst_t *inst) {
    char *base = kt_rwir(inst->opcode);
    sbuf_t todo, done; sb_init(&todo); sb_init(&done);
    sb_printf(&todo, "%s/.todo<%s>", base, vtid);
    sb_printf(&done, "%s/.done<%s>", base, vtid);
    int64_t id = ++handoff_seq;
    sbuf_t payload; sb_init(&payload);
    sb_printf(&payload, "%s|%lld", pc, (long long)id);
    xval_t pv; xv_new_char_utf8(&pv, payload.p);
    kv_pair_t p = { todo.p, pv };
    char err[256];
    kv_set(kv, &p, 1, err, sizeof err);
    xv_free(&pv);

    char id_str[32]; snprintf(id_str, sizeof id_str, "%lld", (long long)id);
    xval_t want; xv_new_char_utf8(&want, id_str);
    xval_t got; xv_zero(&got);
    kv_watch(kv, done.p, &want, 30000000000ULL, &got);
    char *got_s = xv_none(&got) ? strdup("") : xv_value_string(&got);
    bool ok = strcmp(got_s, id_str) == 0;
    free(got_s); xv_free(&got); xv_free(&want);
    sb_free(&todo); sb_free(&done); sb_free(&payload); free(base);
    if (!ok) {
        char msg[256]; snprintf(msg, sizeof msg, "RuntimeError: external rwir %s timeout", inst->opcode);
        vt_set_error(kv, vtid, pc, msg);
        return -1;
    }
    return 0;
}

char *kvcpu_bootstrap(kv_t *kv, const char *vtid, const char *funcname,
                      const char *const *args, int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = strrchr(funcname, '.');
    if (dot) { free(pkg); pkg = strndup(funcname, (size_t)(dot - funcname)); free(name); name = strdup(dot + 1); }
    char *func_key = kt_lib_func(pkg, name);
    sbuf_t func_dir; sb_init(&func_dir);
    sb_puts(&func_dir, func_key); sb_putc(&func_dir, '/');

    sbuf_t sig_key; sb_init(&sig_key);
    sb_printf(&sig_key, "%s[0,0]", func_dir.p);
    xval_t sig; xv_zero(&sig);
    kv_get_one(kv, sig_key.p, &sig);
    if (xv_none(&sig) || !xv_kind_is(&sig, K_RWFUNC)) {
        char msg[256]; snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s", funcname);
        vt_set_error(kv, vtid, "", msg);
        xv_free(&sig); sb_free(&sig_key); sb_free(&func_dir);
        free(func_key); free(pkg); free(name);
        return NULL;
    }
    kvhead_t h; kvspace_decode_head(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);

    sbuf_t vtroot; sb_init(&vtroot); kt_vthread(vtid, &vtroot);
    char *stack_vt = kt_stack(vtroot.p);
    char err[256];
    kv_mkindex(kv, stack_vt, err, sizeof err);
    kv_ext_index(kv, stack_vt, func_dir.p, err, sizeof err);

    char *ep = kt_entry_pc(vtroot.p);
    sbuf_t callpc; sb_init(&callpc); kt_frame_callpc(vtroot.p, &callpc);
    sbuf_t seglib; sb_init(&seglib); sb_puts(&seglib, stack_vt); sb_puts(&seglib, SEG_LIB);
    xval_t v_ep, v_fn; xv_zero(&v_ep); xv_zero(&v_fn);
    xv_new_char_utf8(&v_ep, ep);
    xv_new_char_utf8(&v_fn, func_key);
    kv_pair_t sys[2] = { { callpc.p, v_ep }, { seglib.p, v_fn } };
    kv_set(kv, sys, 2, err, sizeof err);
    xv_free(&v_ep); xv_free(&v_fn);

    if (nargs > 0) {
        kv_pair_t pairs[128]; int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            sbuf_t slot; sb_init(&slot);
            sb_printf(&slot, "%s/[0,-%d]", vtroot.p, i + 1);
            xval_t av; xv_zero(&av);
            bi_resolve_read_value(kv, "", args[i], NULL, &av);
            pairs[np].key = sb_detach(&slot);
            pairs[np].val = av;
            np++;
        }
        if (np > 0) kv_set(kv, pairs, np, err, sizeof err);
        for (int i = 0; i < np; i++) { free(pairs[i].key); xv_free(&pairs[i].val); }
    }

    xv_free(&sig); sb_free(&sig_key); sb_free(&func_dir);
    sb_free(&vtroot); sb_free(&callpc); sb_free(&seglib);
    free(stack_vt); free(func_key); free(pkg); free(name);
    return ep;
}

int kvcpu_execute_mode(kv_t *kv, const char *pc, kvmode_t mode, char **out_pc) {
    if (out_pc) *out_pc = NULL;
    sbuf_t vtid_b; sb_init(&vtid_b);
    const char *vtid = kt_vtid_from_pc(pc, &vtid_b);
    if (vtid[0] == 0) { sb_free(&vtid_b); return -1; }

    char *cur = strdup(pc);
    int rc = 0;
    for (;;) {
        char *pcv = NULL, *status = NULL;
        vt_get(kv, vtid, &pcv, &status);
        if (!status || (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 && strcmp(status, "wait") != 0)) {
            free(pcv); free(status);
            break;
        }
        free(pcv); free(status);

        int depth = stack_depth(cur);
        if (depth > MAX_STACK_DEPTH) {
            char msg[256];
            snprintf(msg, sizeof msg, "RecursionError: stack overflow: depth=%d pc=%s", depth, cur);
            vt_set_error(kv, vtid, cur, msg);
            rc = -1;
            break;
        }

        char *fr = kt_frame_root(cur);
        char *link_base = kt_stack(fr);
        rwir_inst_t inst;
        char err[256];
        if (rwir_decode(kv, link_base, cur, &inst, err, sizeof err) != 0) {
            char msg[256]; snprintf(msg, sizeof msg, "decode: %s", err);
            vt_set_error(kv, vtid, cur, msg);
            free(fr); free(link_base);
            rc = -1;
            break;
        }
        free(link_base);

        log_debug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur, inst.opcode ? inst.opcode : "(end)", inst.nr, inst.nw);

        if (!inst.opcode || !inst.opcode[0]) {
            /* 帧结束 */
            sbuf_t vtroot; sb_init(&vtroot); kt_vthread(vtid, &vtroot);
            if (strcmp(fr, vtroot.p) == 0) {
                vt_set_done(kv, vtid, "ok");
                sb_free(&vtroot); free(fr); rwir_inst_free(&inst);
                break;
            }
            if (strcmp(ext_kind(kv, fr), K_RWFUNC) != 0) {
                char *parent = handle_scope_return(kv, cur);
                if (!parent || !parent[0]) { free(parent); vt_set_done(kv, vtid, "ok"); sb_free(&vtroot); free(fr); rwir_inst_free(&inst); break; }
                vt_set(kv, vtid, parent, "running");
                free(cur); cur = parent;
            } else {
                char *parent = handle_return(kv, cur);
                if (!parent || !parent[0]) { free(parent); vt_set_done(kv, vtid, "ok"); sb_free(&vtroot); free(fr); rwir_inst_free(&inst); break; }
                vt_set(kv, vtid, parent, "running");
                free(cur); cur = parent;
            }
            sb_free(&vtroot); free(fr); rwir_inst_free(&inst);
            continue;
        }

        int exec_err = 0;
        if (op_is_control(inst.opcode)) {
            exec_err = handle_control(kv, vtid, cur, &inst);
        } else if (bi_is_native(inst.opcode)) {
            frame_t f = { kv, vtid, cur, &inst };
            exec_err = bi_native(&f);
        } else if (is_copy_op(inst.opcode)) {
            exec_err = bi_execute_copy(kv, vtid, cur, &inst);
        } else if (is_ext_rwir(kv, inst.opcode)) {
            char *rk = kt_rwir(inst.opcode);
            int def_nr = 0;
            char *def_sig = load_def_reads(kv, rk, &def_nr);
            free(rk);
            if (def_sig) {
                exec_err = check_read_types(kv, vtid, cur, inst.opcode, def_sig, def_nr, inst.reads, inst.nr);
                free(def_sig);
            }
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                if (out_pc) *out_pc = strdup(cur);
                free(fr); rwir_inst_free(&inst);
                free(cur); sb_free(&vtid_b);
                return 1;
            }
            if (exec_err == 0) exec_err = handoff_external_rwir(kv, vtid, cur, &inst);
        } else {
            /* 用户函数 → call */
            rwir_inst_t ci;
            ci.opcode = strdup(OP_CALL);
            ci.nr = inst.nr + 1;
            ci.nw = inst.nw;
            ci.reads = malloc(sizeof(param_t) * (size_t)ci.nr);
            ci.reads[0].name = strdup(inst.opcode);
            ci.reads[0].val.data = NULL; ci.reads[0].val.len = 0;
            for (int i = 0; i < inst.nr; i++) { ci.reads[i + 1] = inst.reads[i]; }
            ci.writes = inst.writes;
            exec_err = handle_control(kv, vtid, cur, &ci);
            free(ci.opcode); free(ci.reads[0].name); free(ci.reads);
        }

        if (exec_err != 0) { free(fr); rwir_inst_free(&inst); rc = -1; break; }

        char *newpc = NULL, *st = NULL;
        vt_get(kv, vtid, &newpc, &st);
        free(st);
        free(fr);
        rwir_inst_free(&inst);
        if (!newpc || !newpc[0]) { free(newpc); break; }
        free(cur);
        cur = newpc;
    }

    free(cur);
    sb_free(&vtid_b);
    return rc;
}

int kvcpu_execute(kv_t *kv, const char *pc) {
    int rc = kvcpu_execute_mode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc;   /* WATCH 模式不返回 1，防御性归一 */
}
