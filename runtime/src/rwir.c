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

    out->reads = malloc(sizeof(kvlangParam_t) * MAX_PARAMS);
    out->writes = malloc(sizeof(kvlangParam_t) * MAX_PARAMS);
    out->nr = out->nw = 0;

    /* 指令槽是稠密数组：opcode 在 [addr0,0]，读参 [addr0,-1..]、写参 [addr0,1..] 各自从 1 连续，
     * 首个缺失槽即终止。逐槽读、遇空即停，替代每步固定读满 1+2*MAX_PARAMS 个槽——durable 后端上
     * 那些缺失槽会各触发一次祖先 ext-index 解析，放大成 syscall 风暴（prime_sieve fs/redis 超时根因）。 */
    kvlangXvalue_t v;
    char *nm;

    kvlangStrbufPrintf(&key, "[%d,0]", addr0);
    nm = kvlangStrbufDetach(&key);
    kvlangKvGetBatch(kv, link_base, &nm, 1, &v);
    free(nm);
    if (!kvlangXvalueNone(&v)) out->opcode = kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);

    for (int i = 1; i <= MAX_PARAMS; i++) {
        kvlangStrbufPrintf(&key, "[%d,-%d]", addr0, i);
        nm = kvlangStrbufDetach(&key);
        kvlangKvGetBatch(kv, link_base, &nm, 1, &v);
        free(nm);
        if (kvlangXvalueNone(&v)) { kvlangXvalueFree(&v); break; }
        out->reads[out->nr].name = kvlangXvalueValueString(&v);
        out->reads[out->nr].val = v;
        out->nr++;
    }
    for (int i = 1; i <= MAX_PARAMS; i++) {
        kvlangStrbufPrintf(&key, "[%d,%d]", addr0, i);
        nm = kvlangStrbufDetach(&key);
        kvlangKvGetBatch(kv, link_base, &nm, 1, &v);
        free(nm);
        if (kvlangXvalueNone(&v)) { kvlangXvalueFree(&v); break; }
        out->writes[out->nw].name = kvlangXvalueValueString(&v);
        out->writes[out->nw].val = v;
        out->nw++;
    }

    kvlangStrbufFree(&key);
    return 0;
}
