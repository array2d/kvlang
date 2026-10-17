#include "runtime_internal.h"

#define FAST_PARAMS 4
#define FAST_SLOTS  (1 + 2 * FAST_PARAMS)
#define COORD_MAX_INST 512

static bool coord_inited = false;
static char coord_op_buf[COORD_MAX_INST][12];
static char coord_r_buf[COORD_MAX_INST][FAST_PARAMS + 1][16];
static char coord_w_buf[COORD_MAX_INST][FAST_PARAMS + 1][16];

static void ensure_coords(void) {
    if (coord_inited) return;
    coord_inited = true;
    for (int i = 0; i < COORD_MAX_INST; i++) {
        snprintf(coord_op_buf[i], sizeof(coord_op_buf[i]), "[%d,0]", i);
        for (int j = 1; j <= FAST_PARAMS; j++) {
            snprintf(coord_r_buf[i][j], sizeof(coord_r_buf[i][j]), "[%d,-%d]", i, j);
            snprintf(coord_w_buf[i][j], sizeof(coord_w_buf[i][j]), "[%d,%d]", i, j);
        }
    }
}

int kvlangRwirExtractAddr0(const char *coord) {
    const char *p = coord;
    while (*p == '[' || *p == ' ' || *p == '\t') p++;
    char *end;
    long n = strtol(p, &end, 10);
    return end == p ? 0 : (int)n;
}

int kvlangRwirNextPc(const char *pc, kvlangStrbuf_t *out) {
    ensure_coords();
    kvlangStrbufClear(out);
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int next_num = kvlangRwirExtractAddr0(slash ? slash + 1 : pc) + 1;
    kvlangStrbufPutn(out, pc, plen);
    if (next_num >= 0 && next_num < COORD_MAX_INST) {
        kvlangStrbufPuts(out, coord_op_buf[next_num]);
    } else {
        kvlangStrbufPrintf(out, "[%d,0]", next_num);
    }
    return 0;
}

void kvlangRwirInstFree(kvlangRwirInst_t *inst) {
    free(inst->opcode);
    for (int i = 0; i < inst->nr; i++) { free(inst->reads[i].name); kvlangXvalueFree(&inst->reads[i].val); }
    for (int i = 0; i < inst->nw; i++) { free(inst->writes[i].name); kvlangXvalueFree(&inst->writes[i].val); }
    free(inst->reads);
    free(inst->writes);
    inst->opcode = NULL; inst->reads = NULL; inst->writes = NULL; inst->nr = inst->nw = 0;
}

kvlangFrameMap_t *kvlangFrameLookup(kvlangKv_t *kv, const char *frame_stk) {
    for (int i = kv->nframes - 1; i >= 0; i--) {
        if (strcmp(kv->frames[i].frame_stk, frame_stk) == 0) return &kv->frames[i];
    }
    return NULL;
}

const char *kvlangFrameVarKeyLookup(kvlangFrameMap_t *fm, const char *name) {
    for (int i = 0; i < fm->nvars; i++) {
        if (strcmp(fm->vars[i].name, name) == 0) return fm->vars[i].resolved_key;
    }
    return NULL;
}

const char *kvlangFrameVarKeyPut(kvlangFrameMap_t *fm, const char *name, char *resolved_key) {
    for (int i = 0; i < fm->nvars; i++) {
        if (strcmp(fm->vars[i].name, name) == 0) {
            free(fm->vars[i].resolved_key);
            fm->vars[i].resolved_key = resolved_key;
            return resolved_key;
        }
    }
    if (fm->nvars == fm->vars_cap) {
        fm->vars_cap = fm->vars_cap ? fm->vars_cap * 2 : 8;
        fm->vars = realloc(fm->vars, sizeof(kvlangVarKey_t) * (size_t)fm->vars_cap);
    }
    fm->vars[fm->nvars].name = strdup(name);
    fm->vars[fm->nvars].resolved_key = resolved_key;
    fm->nvars++;
    return resolved_key;
}

kvlangLibFunc_t *kvlangLibGetFunc(kvlangKv_t *kv, const char *func_dir) {
    for (int i = 0; i < kv->nfuncs; i++) {
        if (strcmp(kv->funcs[i].func_dir, func_dir) == 0) return &kv->funcs[i];
    }
    if (kv->nfuncs == kv->funcs_cap) {
        int ncap = kv->funcs_cap ? kv->funcs_cap * 2 : 16;
        kv->funcs = realloc(kv->funcs, sizeof(kvlangLibFunc_t) * (size_t)ncap);
        kv->funcs_cap = ncap;
        for (int i = 0; i < kv->nframes; i++) kv->frames[i].func = NULL;
    }
    kvlangLibFunc_t *fn = &kv->funcs[kv->nfuncs++];
    memset(fn, 0, sizeof(*fn));
    fn->func_dir = strdup(func_dir);
    return fn;
}

int kvlangRwirDecode(kvlangKv_t *kv, const char *link_base, const char *pc, kvlangRwirInst_t *out,
                char *err, uint32_t err_cap) {
    ensure_coords();
    memset(out, 0, sizeof(*out));
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) { fprintf(stderr, "Decode: invalid pc (no /[coord]): %s\n", pc); abort(); }
    int addr0 = kvlangRwirExtractAddr0(last + 1);

    kvlangInstKeys_t *ik = NULL;
    if (addr0 >= 1 && addr0 < 4096) {
        kvlangLibFunc_t *fn = NULL;
        kvlangFrameMap_t *fm = kvlangFrameLookup(kv, link_base);
        if (fm) {
            if (!fm->func) fm->func = kvlangLibGetFunc(kv, fm->func_dir);
            fn = fm->func;
        } else if (strncmp(link_base, "/lib/", 5) == 0) {
            fn = kvlangLibGetFunc(kv, link_base);
        }
        if (fn) {
            if (addr0 >= fn->inst_cap) {
                int ncap = fn->inst_cap ? fn->inst_cap * 2 : 32;
                while (ncap <= addr0) ncap *= 2;
                fn->insts = realloc(fn->insts, sizeof(kvlangInstKeys_t) * (size_t)ncap);
                memset(fn->insts + fn->inst_cap, 0, sizeof(kvlangInstKeys_t) * (size_t)(ncap - fn->inst_cap));
                fn->inst_cap = ncap;
            }
            ik = &fn->insts[addr0];
        }
    }

    if (ik && ik->valid) {
        int total = 1 + ik->nr + ik->nw;
        kvlangXvalue_t svals[16];
        kvlangXvalue_t *vals = total <= 16 ? svals : malloc(sizeof(kvlangXvalue_t) * (size_t)total);
        int grc = kvlangKvGetBatch(kv, link_base, ik->keys, total, vals);
        if (grc != 0) {
            snprintf(err, err_cap, "GetBatch rc=%d pc=%s", grc, pc);
            fprintf(stderr, "Decode: %s\n", err);
            for (int i = 0; i < total; i++) kvlangXvalueFree(&vals[i]);
            if (vals != svals) free(vals);
            abort();
        }
        if (!kvlangXvalueNone(&vals[0])) out->opcode = kvlangXvalueValueString(&vals[0]);
        kvlangXvalueFree(&vals[0]);
        out->reads = ik->nr > 0 ? malloc(sizeof(kvlangParam_t) * (size_t)ik->nr) : NULL;
        out->writes = ik->nw > 0 ? malloc(sizeof(kvlangParam_t) * (size_t)ik->nw) : NULL;
        out->nr = out->nw = 0;
        for (int i = 0; i < ik->nr; i++) {
            kvlangXvalue_t *rv = &vals[1 + i];
            if (!kvlangXvalueNone(rv)) {
                out->reads[out->nr].name = kvlangXvalueValueString(rv);
                out->reads[out->nr].val = *rv;
                rv->data = NULL; rv->len = 0;
                out->nr++;
            }
        }
        for (int i = 0; i < ik->nw; i++) {
            kvlangXvalue_t *wv = &vals[1 + ik->nr + i];
            if (!kvlangXvalueNone(wv)) {
                out->writes[out->nw].name = kvlangXvalueValueString(wv);
                out->writes[out->nw].val = *wv;
                wv->data = NULL; wv->len = 0;
                out->nw++;
            }
        }
        if (vals != svals) free(vals);
        return 0;
    }

    char nbuf[FAST_SLOTS][24];
    char *fnames[FAST_SLOTS];
    kvlangXvalue_t fvals[FAST_SLOTS];
    if (addr0 >= 0 && addr0 < COORD_MAX_INST) {
        fnames[0] = coord_op_buf[addr0];
        for (int i = 1; i <= FAST_PARAMS; i++) {
            fnames[(i - 1) * 2 + 1] = coord_r_buf[addr0][i];
            fnames[(i - 1) * 2 + 2] = coord_w_buf[addr0][i];
        }
    } else {
        snprintf(nbuf[0], sizeof(nbuf[0]), "[%d,0]", addr0);
        fnames[0] = nbuf[0];
        for (int i = 1; i <= FAST_PARAMS; i++) {
            snprintf(nbuf[(i - 1) * 2 + 1], sizeof(nbuf[0]), "[%d,-%d]", addr0, i);
            fnames[(i - 1) * 2 + 1] = nbuf[(i - 1) * 2 + 1];
            snprintf(nbuf[(i - 1) * 2 + 2], sizeof(nbuf[0]), "[%d,%d]", addr0, i);
            fnames[(i - 1) * 2 + 2] = nbuf[(i - 1) * 2 + 2];
        }
    }

    int grc = kvlangKvGetBatch(kv, link_base, fnames, FAST_SLOTS, fvals);
    if (grc != 0) {
        snprintf(err, err_cap, "GetBatch rc=%d pc=%s", grc, pc);
        fprintf(stderr, "Decode: %s\n", err);
        for (int i = 0; i < FAST_SLOTS; i++) kvlangXvalueFree(&fvals[i]);
        abort();
    }

    bool need_more = !kvlangXvalueNone(&fvals[(FAST_PARAMS - 1) * 2 + 1]) ||
                     !kvlangXvalueNone(&fvals[(FAST_PARAMS - 1) * 2 + 2]);
    kvlangXvalue_t *more_vals = NULL;
    char **mnames = NULL;
    int more_slots = 2 * (MAX_PARAMS - FAST_PARAMS);
    if (need_more) {
        mnames = malloc(sizeof(char *) * (size_t)more_slots);
        for (int i = FAST_PARAMS + 1; i <= MAX_PARAMS; i++) {
            int idx = (i - FAST_PARAMS - 1) * 2;
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "[%d,-%d]", addr0, i);
            mnames[idx] = strdup(tmp);
            snprintf(tmp, sizeof(tmp), "[%d,%d]", addr0, i);
            mnames[idx + 1] = strdup(tmp);
        }
        more_vals = malloc(sizeof(kvlangXvalue_t) * (size_t)more_slots);
        grc = kvlangKvGetBatch(kv, link_base, mnames, more_slots, more_vals);
        if (grc != 0) {
            snprintf(err, err_cap, "GetBatch rc=%d pc=%s", grc, pc);
            fprintf(stderr, "Decode: %s\n", err);
            for (int i = 0; i < FAST_SLOTS; i++) kvlangXvalueFree(&fvals[i]);
            for (int i = 0; i < more_slots; i++) { free(mnames[i]); kvlangXvalueFree(&more_vals[i]); }
            free(mnames); free(more_vals);
            abort();
        }
    }

    if (!kvlangXvalueNone(&fvals[0])) out->opcode = kvlangXvalueValueString(&fvals[0]);
    kvlangXvalueFree(&fvals[0]);

    int nr_cap = 0, nw_cap = 0;
    int max_p = need_more ? MAX_PARAMS : FAST_PARAMS;
    for (int i = 1; i <= max_p; i++) {
        kvlangXvalue_t *rv = i <= FAST_PARAMS ? &fvals[(i - 1) * 2 + 1] : &more_vals[(i - FAST_PARAMS - 1) * 2];
        kvlangXvalue_t *wv = i <= FAST_PARAMS ? &fvals[(i - 1) * 2 + 2] : &more_vals[(i - FAST_PARAMS - 1) * 2 + 1];
        if (!kvlangXvalueNone(rv)) nr_cap++;
        if (!kvlangXvalueNone(wv)) nw_cap++;
    }

    out->reads = nr_cap > 0 ? malloc(sizeof(kvlangParam_t) * (size_t)nr_cap) : NULL;
    out->writes = nw_cap > 0 ? malloc(sizeof(kvlangParam_t) * (size_t)nw_cap) : NULL;
    out->nr = out->nw = 0;

    if (ik) {
        ik->nr = nr_cap;
        ik->nw = nw_cap;
        ik->keys = malloc(sizeof(char *) * (size_t)(1 + nr_cap + nw_cap));
        ik->keys[0] = strdup(fnames[0]);
        ik->valid = true;
    }

    for (int i = 1; i <= max_p; i++) {
        kvlangXvalue_t *rv = i <= FAST_PARAMS ? &fvals[(i - 1) * 2 + 1] : &more_vals[(i - FAST_PARAMS - 1) * 2];
        const char *rk_name = i <= FAST_PARAMS ? fnames[(i - 1) * 2 + 1] : mnames[(i - FAST_PARAMS - 1) * 2];
        if (!kvlangXvalueNone(rv)) {
            if (ik) ik->keys[1 + out->nr] = strdup(rk_name);
            out->reads[out->nr].name = kvlangXvalueValueString(rv);
            out->reads[out->nr].val = *rv;
            rv->data = NULL; rv->len = 0;
            out->nr++;
        }
        kvlangXvalue_t *wv = i <= FAST_PARAMS ? &fvals[(i - 1) * 2 + 2] : &more_vals[(i - FAST_PARAMS - 1) * 2 + 1];
        const char *wk_name = i <= FAST_PARAMS ? fnames[(i - 1) * 2 + 2] : mnames[(i - FAST_PARAMS - 1) * 2 + 1];
        if (!kvlangXvalueNone(wv)) {
            if (ik) ik->keys[1 + nr_cap + out->nw] = strdup(wk_name);
            out->writes[out->nw].name = kvlangXvalueValueString(wv);
            out->writes[out->nw].val = *wv;
            wv->data = NULL; wv->len = 0;
            out->nw++;
        }
    }

    if (mnames) {
        for (int i = 0; i < more_slots; i++) free(mnames[i]);
        free(mnames);
    }
    free(more_vals);
    return 0;
}

void kvlangCacheClearFuncs(kvlangKv_t *kv) {
    for (int i = 0; i < kv->nfuncs; i++) {
        kvlangLibFunc_t *fn = &kv->funcs[i];
        free(fn->func_dir);
        for (int j = 0; j < fn->inst_cap; j++) {
            if (fn->insts[j].valid) {
                int total = 1 + fn->insts[j].nr + fn->insts[j].nw;
                for (int m = 0; m < total; m++) free(fn->insts[j].keys[m]);
                free(fn->insts[j].keys);
            }
        }
        free(fn->insts);
    }
    free(kv->funcs);
    kv->funcs = NULL;
    kv->nfuncs = kv->funcs_cap = 0;
    for (int i = 0; i < kv->nframes; i++) kv->frames[i].func = NULL;
}

void kvlangCacheClearFrames(kvlangKv_t *kv) {
    for (int i = 0; i < kv->nframes; i++) {
        free(kv->frames[i].frame_stk);
        free(kv->frames[i].func_dir);
        for (int j = 0; j < kv->frames[i].nvars; j++) {
            free(kv->frames[i].vars[j].name);
            free(kv->frames[i].vars[j].resolved_key);
        }
        free(kv->frames[i].vars);
    }
    free(kv->frames);
    kv->frames = NULL;
    kv->nframes = kv->frames_cap = 0;
}
