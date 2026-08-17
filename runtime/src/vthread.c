#include "runtime_internal.h"

void vt_get(kv_t *kv, const char *vtid, char **pc, char **status) {
    *pc = NULL; *status = NULL;
    sbuf_t k; sb_init(&k);
    kt_vthread_pc(vtid, &k);
    xval_t pv; xv_zero(&pv);
    kv_get_one(kv, k.p, &pv);
    if (!xv_none(&pv)) *pc = xv_value_string(&pv);
    xv_free(&pv);
    kt_vthread_status(vtid, &k);
    xval_t sv; xv_zero(&sv);
    kv_get_one(kv, k.p, &sv);
    if (!xv_none(&sv)) *status = xv_value_string(&sv);
    xv_free(&sv);
    sb_free(&k);
}

void vt_set(kv_t *kv, const char *vtid, const char *pc, const char *status) {
    sbuf_t k1, k2; sb_init(&k1); sb_init(&k2);
    kt_vthread_pc(vtid, &k1);
    kt_vthread_status(vtid, &k2);
    xval_t v1, v2; xv_zero(&v1); xv_zero(&v2);
    xv_new_char_utf8(&v1, pc);
    xv_new_char_utf8(&v2, status);
    kv_pair_t pairs[2] = { { k1.p, v1 }, { k2.p, v2 } };
    char err[256];
    kv_set(kv, pairs, 2, err, sizeof err);
    xv_free(&v1); xv_free(&v2); sb_free(&k1); sb_free(&k2);
}

void vt_set_done(kv_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0) ret = "ok";
    sbuf_t k; sb_init(&k);
    kt_vthread_status(vtid, &k);
    xval_t v; xv_zero(&v);
    xv_new_char_utf8(&v, ret);
    kv_pair_t pair = { k.p, v };
    char err[256];
    kv_set(kv, &pair, 1, err, sizeof err);
    xv_free(&v); sb_free(&k);
}

void vt_set_error(kv_t *kv, const char *vtid, const char *pc, const char *msg) {
    sbuf_t msg_path, pc_key, st_key; sb_init(&msg_path); sb_init(&pc_key); sb_init(&st_key);
    kt_vthread_status_msg(vtid, "error", &msg_path);
    kt_vthread_pc(vtid, &pc_key);
    kt_vthread_status(vtid, &st_key);

    /* 确保 .error/ 父目录存在 */
    char *sep = strrchr(msg_path.p, '/');
    if (sep) {
        sbuf_t dir; sb_init(&dir);
        sb_putn(&dir, msg_path.p, (size_t)(sep - msg_path.p) + 1);
        char err[256];
        kv_mkindex(kv, dir.p, err, sizeof err);
        sb_free(&dir);
    }

    xval_t vpc, vmsg, vst; xv_zero(&vpc); xv_zero(&vmsg); xv_zero(&vst);
    xv_new_char_utf8(&vpc, pc);
    xv_new_char_utf8(&vmsg, msg);
    xv_new_char_utf8(&vst, "error");
    kv_pair_t pairs[3] = { { pc_key.p, vpc }, { msg_path.p, vmsg }, { st_key.p, vst } };
    char err[256];
    kv_set(kv, pairs, 3, err, sizeof err);
    xv_free(&vpc); xv_free(&vmsg); xv_free(&vst);
    sb_free(&msg_path); sb_free(&pc_key); sb_free(&st_key);
}
