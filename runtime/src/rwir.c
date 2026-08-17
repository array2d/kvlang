#include "runtime_internal.h"

int rwir_extract_addr0(const char *coord) {
    const char *p = coord;
    while (*p == '[' || *p == ' ' || *p == '\t') p++;
    char *end;
    long n = strtol(p, &end, 10);
    return end == p ? 0 : (int)n;
}

int rwir_next_pc(const char *pc, sbuf_t *out) {
    sb_clear(out);
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int num = rwir_extract_addr0(slash ? slash + 1 : pc);
    sb_putn(out, pc, plen);
    sb_printf(out, "[%d,0]", num + 1);
    return 0;
}

/* scopePrefixAndBase：从 linkBase 提取 scope 链与 rwfunc 帧 lookupBase。 */
static void scope_prefix_and_base(const char *link_base, sbuf_t *sp, sbuf_t *lb) {
    sb_clear(sp);
    sb_clear(lb);
    sb_puts(sp, "");
    sb_puts(lb, link_base);

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
            sb_clear(lb);
            sb_putn(lb, link_base, rest);
            sb_putc(lb, '/');
            sb_putn(lb, link_base + start, len);
            sb_putc(lb, '/');
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
        if (sp->len > 0) sb_putc(sp, '.');
        sb_puts(sp, scopes[i]);
        free(scopes[i]);
    }
}

void rwir_inst_free(rwir_inst_t *inst) {
    free(inst->opcode);
    for (int i = 0; i < inst->nr; i++) { free(inst->reads[i].name); xv_free(&inst->reads[i].val); }
    for (int i = 0; i < inst->nw; i++) { free(inst->writes[i].name); xv_free(&inst->writes[i].val); }
    free(inst->reads);
    free(inst->writes);
    inst->opcode = NULL; inst->reads = NULL; inst->writes = NULL; inst->nr = inst->nw = 0;
}

int rwir_decode(kv_t *kv, const char *link_base, const char *pc, rwir_inst_t *out,
                char *err, uint32_t err_cap) {
    memset(out, 0, sizeof(*out));
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) { snprintf(err, err_cap, "Decode: invalid pc (no /[coord]): %s", pc); return -1; }
    int addr0 = rwir_extract_addr0(last + 1);

    sbuf_t sp, lb, key;
    sb_init(&sp); sb_init(&lb); sb_init(&key);
    scope_prefix_and_base(link_base, &sp, &lb);

    int nslots = 1 + 2 * MAX_PARAMS;
    char **names = malloc(sizeof(char *) * (size_t)nslots);
    sb_printf(&key, "%s[%d,0]", sp.p, addr0);
    names[0] = sb_detach(&key);
    for (int i = 1; i <= MAX_PARAMS; i++) {
        sb_printf(&key, "%s[%d,-%d]", sp.p, addr0, i);
        names[(i - 1) * 2 + 1] = sb_detach(&key);
        sb_printf(&key, "%s[%d,%d]", sp.p, addr0, i);
        names[(i - 1) * 2 + 2] = sb_detach(&key);
    }
    sb_free(&key);

    xval_t *vals = malloc(sizeof(xval_t) * (size_t)nslots);
    kv_get_batch(kv, lb.p, names, nslots, vals);

    if (!xv_none(&vals[0])) out->opcode = xv_value_string(&vals[0]);

    out->reads = malloc(sizeof(param_t) * MAX_PARAMS);
    out->writes = malloc(sizeof(param_t) * MAX_PARAMS);
    out->nr = out->nw = 0;
    for (int i = 1; i <= MAX_PARAMS; i++) {
        xval_t *rv = &vals[(i - 1) * 2 + 1];
        if (!xv_none(rv)) {
            out->reads[out->nr].name = xv_value_string(rv);
            out->reads[out->nr].val = *rv;
            rv->data = NULL; rv->len = 0;
            out->nr++;
        }
        xval_t *wv = &vals[(i - 1) * 2 + 2];
        if (!xv_none(wv)) {
            out->writes[out->nw].name = xv_value_string(wv);
            out->writes[out->nw].val = *wv;
            wv->data = NULL; wv->len = 0;
            out->nw++;
        }
    }

    for (int i = 0; i < nslots; i++) xv_free(&vals[i]);
    free(vals);
    for (int i = 0; i < nslots; i++) free(names[i]);
    free(names);
    sb_free(&sp); sb_free(&lb);
    return 0;
}
