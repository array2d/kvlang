#include "runtime_internal.h"
#include "kvlang_rwext.h"

struct rwext_conn { kv_t *kv; };

rwext_conn *rwext_connect(const char *dsn) {
    kv_t *kv = kv_connect(dsn);
    if (!kv) return NULL;
    rwext_conn *c = malloc(sizeof(*c));
    c->kv = kv;
    return c;
}

void rwext_disconnect(rwext_conn *c) {
    if (!c) return;
    kv_disconnect(c->kv);
    free(c);
}

int rwext_register(rwext_conn *c, const char *opcode, int32_t nr, int32_t nw, const char *sig) {
    char *key = kt_rwir(opcode);
    xval_t v; xv_new_rwir(&v, nr, nw, sig);
    kv_pair_t p = { key, v };
    char err[256];
    int rc = kv_set(c->kv, &p, 1, err, sizeof err);
    xv_free(&v); free(key);
    return rc;
}

char *rwext_list(rwext_conn *c, const char *prefix) {
    char **names = NULL; int count = 0;
    if (kv_list(c->kv, prefix, false, false, &names, &count) != 0) return strdup("");
    sbuf_t b; sb_init(&b);
    for (int i = 0; i < count; i++) {
        if (i) sb_putc(&b, '\n');
        sb_puts(&b, names[i]);
        free(names[i]);
    }
    free(names);
    return sb_detach(&b);
}

char *rwext_get(rwext_conn *c, const char *key) {
    xval_t v; xv_zero(&v);
    kv_get_one(c->kv, key, &v);
    char *s = xv_none(&v) ? strdup("") : xv_value_string(&v);
    xv_free(&v);
    return s;
}

int rwext_set(rwext_conn *c, const char *key, const char *val) {
    xval_t v; xv_new_char_utf8(&v, val);
    kv_pair_t p = { (char *)key, v };
    char err[256];
    int rc = kv_set(c->kv, &p, 1, err, sizeof err);
    xv_free(&v);
    return rc;
}

int rwext_del(rwext_conn *c, const char *key) {
    char err[256];
    return kv_del(c->kv, key, err, sizeof err);
}

int rwext_get_tlv(rwext_conn *c, const char *key, uint8_t **out, uint32_t *out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    xval_t v; xv_zero(&v);
    kv_get_one(c->kv, key, &v);
    if (xv_none(&v)) { xv_free(&v); return 0; }
    uint8_t *b = malloc(v.len);
    if (!b) { xv_free(&v); return -1; }
    memcpy(b, v.data, v.len);
    *out = b; *out_len = v.len;
    xv_free(&v);
    return 0;
}

int rwext_set_tlv(rwext_conn *c, const char *key, const uint8_t *val, uint32_t val_len) {
    xval_t v = { (uint8_t *)val, val_len };
    kv_pair_t p = { (char *)key, v };
    char err[256];
    return kv_set(c->kv, &p, 1, err, sizeof err);
}

int rwext_mkindex(rwext_conn *c, const char *path) {
    char err[256];
    return kv_mkindex(c->kv, path, err, sizeof err);
}

char *rwext_print_line(rwext_conn *c, const char *pc, int *rawnl, int *cerr) {
    if (rawnl) *rawnl = 0;
    if (cerr) *cerr = 0;
    char *fr = kt_frame_root(pc);
    if (!fr) return NULL;
    char *lb = kt_stack(fr);
    rwir_inst_t inst;
    char err[256];
    if (rwir_decode(c->kv, lb, pc, &inst, err, sizeof err) != 0) {
        free(fr); free(lb); return NULL;
    }
    free(lb);
    if (!inst.opcode ||
        (strcmp(inst.opcode, "print") != 0 && strcmp(inst.opcode, "println") != 0 &&
         strcmp(inst.opcode, "cerr") != 0)) {
        free(fr); rwir_inst_free(&inst); return NULL;
    }
    const char *sep = strcmp(inst.opcode, "print") == 0 ? "" : " ";
    if (rawnl) *rawnl = (strcmp(inst.opcode, "print") == 0);
    if (cerr) *cerr = (strcmp(inst.opcode, "cerr") == 0);

    sbuf_t line; sb_init(&line);
    for (int i = 0; i < inst.nr; i++) {
        if (i > 0) sb_puts(&line, sep);
        xval_t v; xv_zero(&v);
        bi_resolve_read_value(c->kv, fr, inst.reads[i].name, &inst.reads[i].val, &v);
        char *s = NULL; display(&v, &s);
        sb_puts(&line, s); free(s);
        xv_free(&v);
    }
    free(fr);
    rwir_inst_free(&inst);
    return sb_detach(&line);
}

char *rwext_next_pc(const char *pc) {
    sbuf_t b; sb_init(&b);
    rwir_next_pc(pc, &b);
    return sb_detach(&b);
}

char *rwext_params(rwext_conn *c, const char *pc) {
    char *fr = kt_frame_root(pc);
    if (!fr) return strdup("");
    char *lb = kt_stack(fr);
    rwir_inst_t inst;
    char err[256];
    if (rwir_decode(c->kv, lb, pc, &inst, err, sizeof err) != 0) {
        free(fr); free(lb); return strdup("");
    }
    free(lb);
    sbuf_t b; sb_init(&b);
    sb_puts(&b, inst.opcode ? inst.opcode : "");
    for (int i = 0; i < inst.nr; i++) { sb_putc(&b, '\n'); sb_puts(&b, inst.reads[i].name ? inst.reads[i].name : ""); }
    for (int i = 0; i < inst.nw; i++) { sb_putc(&b, '\n'); sb_puts(&b, inst.writes[i].name ? inst.writes[i].name : ""); }
    free(fr);
    rwir_inst_free(&inst);
    return sb_detach(&b);
}

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *rwext_resolve_read(rwext_conn *c, const char *pc, int idx) {
    char *fr = kt_frame_root(pc);
    if (!fr) return strdup("");
    char *lb = kt_stack(fr);
    rwir_inst_t inst;
    char err[256];
    if (rwir_decode(c->kv, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 || idx >= inst.nr) {
        free(fr); free(lb); return strdup("");
    }
    free(lb);
    xval_t v; xv_zero(&v);
    bi_resolve_read_value(c->kv, fr, inst.reads[idx].name, &inst.reads[idx].val, &v);
    char *s = xv_none(&v) ? strdup("") : xv_value_string(&v);
    xv_free(&v);
    free(fr);
    rwir_inst_free(&inst);
    return s;
}

/* 解析读参 idx 为 KV 路径（变量/临时 → 帧槽路径；路径 → 直接返回；内联字面量 → ""）。 */
char *rwext_resolve_read_path(rwext_conn *c, const char *pc, int idx) {
    char *fr = kt_frame_root(pc);
    if (!fr) return strdup("");
    char *lb = kt_stack(fr);
    rwir_inst_t inst;
    char err[256];
    if (rwir_decode(c->kv, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 || idx >= inst.nr) {
        free(fr); free(lb); return strdup("");
    }
    free(lb);
    const char *nm = inst.reads[idx].name;
    char *s;
    if (!nm || !nm[0] || nm[0] == '"' || (nm[0] >= '0' && nm[0] <= '9') ||
        (nm[0] == '-' && nm[1]) || strcmp(nm, "true") == 0 ||
        strcmp(nm, "false") == 0 || strcmp(nm, "null") == 0) {
        s = strdup("");                         /* 内联字面量：无路径 */
    } else {
        s = bi_resolve_write_slot(c->kv, fr, nm);   /* 变量/临时/路径 → 帧槽路径 */
    }
    free(fr);
    rwir_inst_free(&inst);
    return s ? s : strdup("");
}

/* 解析写参 idx 为 KV 路径（路径 → 直接返回；变量 → 帧槽路径）。 */
char *rwext_resolve_write(rwext_conn *c, const char *pc, int idx) {
    char *fr = kt_frame_root(pc);
    if (!fr) return strdup("");
    char *lb = kt_stack(fr);
    rwir_inst_t inst;
    char err[256];
    if (rwir_decode(c->kv, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 || idx >= inst.nw) {
        free(fr); free(lb); return strdup("");
    }
    free(lb);
    char *s = bi_resolve_write_slot(c->kv, fr, inst.writes[idx].name);
    free(fr);
    rwir_inst_free(&inst);
    return s;
}
