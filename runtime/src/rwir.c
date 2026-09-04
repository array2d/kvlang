#include "runtime_internal.h"

int kvlangRwirExtractAddr0(const char *coord) {
    const char *p = coord;
    while (*p == '[' || *p == ' ' || *p == '\t') p++;
    char *end;
    long n = strtol(p, &end, 10);
    return end == p ? 0 : (int)n;
}

int kvlangRwirNextPc(const char *pc, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int num = kvlangRwirExtractAddr0(slash ? slash + 1 : pc);
    kvlangStrbufPutn(out, pc, plen);
    kvlangStrbufPrintf(out, "[%d,0]", num + 1);
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

int kvlangRwirDecode(kvlangKv_t *kv, const char *link_base, const char *pc, kvlangRwirInst_t *out,
                char *err, uint32_t err_cap) {
    memset(out, 0, sizeof(*out));
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) { snprintf(err, err_cap, "Decode: invalid pc (no /[coord]): %s", pc); return -1; }
    int addr0 = kvlangRwirExtractAddr0(last + 1);

    kvlangStrbuf_t key;
    kvlangStrbufInit(&key);

    int nslots = 1 + 2 * MAX_PARAMS;
    char **names = malloc(sizeof(char *) * (size_t)nslots);
    kvlangStrbufPrintf(&key, "[%d,0]", addr0);
    names[0] = kvlangStrbufDetach(&key);
    for (int i = 1; i <= MAX_PARAMS; i++) {
        kvlangStrbufPrintf(&key, "[%d,-%d]", addr0, i);
        names[(i - 1) * 2 + 1] = kvlangStrbufDetach(&key);
        kvlangStrbufPrintf(&key, "[%d,%d]", addr0, i);
        names[(i - 1) * 2 + 2] = kvlangStrbufDetach(&key);
    }

    kvlangXvalue_t *vals = malloc(sizeof(kvlangXvalue_t) * (size_t)nslots);
    if (kvlangKvGetBatch(kv, link_base, names, nslots, vals) != 0) {
        snprintf(err, err_cap, "Decode: GetBatch failed at %s", pc);
        for (int i = 0; i < nslots; i++) free(names[i]);
        free(names); free(vals); kvlangStrbufFree(&key);
        return -1;
    }

    if (!kvlangXvalueNone(&vals[0])) out->opcode = kvlangXvalueValueString(&vals[0]);

    out->reads = malloc(sizeof(kvlangParam_t) * MAX_PARAMS);
    out->writes = malloc(sizeof(kvlangParam_t) * MAX_PARAMS);
    out->nr = out->nw = 0;
    for (int i = 1; i <= MAX_PARAMS; i++) {
        kvlangXvalue_t *rv = &vals[(i - 1) * 2 + 1];
        if (!kvlangXvalueNone(rv)) {
            out->reads[out->nr].name = kvlangXvalueValueString(rv);
            out->reads[out->nr].val = *rv;
            rv->data = NULL; rv->len = 0;
            out->nr++;
        }
        kvlangXvalue_t *wv = &vals[(i - 1) * 2 + 2];
        if (!kvlangXvalueNone(wv)) {
            out->writes[out->nw].name = kvlangXvalueValueString(wv);
            out->writes[out->nw].val = *wv;
            wv->data = NULL; wv->len = 0;
            out->nw++;
        }
    }

    for (int i = 0; i < nslots; i++) kvlangXvalueFree(&vals[i]);
    free(vals);
    for (int i = 0; i < nslots; i++) free(names[i]);
    free(names);
    kvlangStrbufFree(&key);
    return 0;
}
