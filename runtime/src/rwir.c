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

/* scopePrefixAndBase：从 linkBase 提取 scope 链与 rwfunc 帧 lookupBase。 */
static void scope_prefix_and_base(const char *link_base, kvlangStrbuf_t *sp, kvlangStrbuf_t *lb) {
    kvlangStrbufClear(sp);
    kvlangStrbufClear(lb);
    kvlangStrbufPuts(sp, "");
    kvlangStrbufPuts(lb, link_base);

    size_t n = strlen(link_base);
    while (n > 0 && link_base[n - 1] == '/') n--;
    if (n == 0) return;
    bool has_bracket = false;
    for (size_t i = 0; i < n; i++) if (link_base[i] == '[') { has_bracket = true; break; }
    if (!has_bracket) return;

    char *scopes[MAX_STACK_DEPTH];
    int nscopes = 0;
    size_t rest = n;
    while (rest > 0) {
        size_t sep = (size_t)-1;
        for (size_t i = 0; i < rest; i++) if (link_base[i] == '/') sep = i;
        if (sep == (size_t)-1) break;
        size_t start = sep + 1, len = rest - start;
        rest = sep;
        if (len > 0 && link_base[start] == '[') {
            kvlangStrbufClear(lb);
            kvlangStrbufPutn(lb, link_base, rest);
            kvlangStrbufPutc(lb, '/');
            kvlangStrbufPutn(lb, link_base + start, len);
            kvlangStrbufPutc(lb, '/');
            break;
        }
        if (nscopes < MAX_STACK_DEPTH) {
            scopes[nscopes] = malloc(len + 1);
            memcpy(scopes[nscopes], link_base + start, len);
            scopes[nscopes][len] = 0;
            nscopes++;
        }
    }
    for (int i = nscopes - 1; i >= 0; i--) {
        if (sp->len > 0) kvlangStrbufPutc(sp, '.');
        kvlangStrbufPuts(sp, scopes[i]);
        free(scopes[i]);
    }
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

    kvlangStrbuf_t sp, lb, key;
    kvlangStrbufInit(&sp); kvlangStrbufInit(&lb); kvlangStrbufInit(&key);
    scope_prefix_and_base(link_base, &sp, &lb);

    int nslots = 1 + 2 * MAX_PARAMS;
    char **names = malloc(sizeof(char *) * (size_t)nslots);
    kvlangStrbufPrintf(&key, "%s[%d,0]", sp.p, addr0);
    names[0] = kvlangStrbufDetach(&key);
    for (int i = 1; i <= MAX_PARAMS; i++) {
        kvlangStrbufPrintf(&key, "%s[%d,-%d]", sp.p, addr0, i);
        names[(i - 1) * 2 + 1] = kvlangStrbufDetach(&key);
        kvlangStrbufPrintf(&key, "%s[%d,%d]", sp.p, addr0, i);
        names[(i - 1) * 2 + 2] = kvlangStrbufDetach(&key);
    }
    kvlangStrbufFree(&key);

    kvlangXvalue_t *vals = malloc(sizeof(kvlangXvalue_t) * (size_t)nslots);
    kvlangKvGetBatch(kv, lb.p, names, nslots, vals);

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
    kvlangStrbufFree(&sp); kvlangStrbufFree(&lb);
    return 0;
}
