#include "runtime_internal.h"
#include "kvlang_runtime.h"

struct kvlang_rt { kv_t *kv; };

kvlang_rt *kvlang_rt_connect(const char *dsn) {
    kv_t *kv = kv_connect(dsn);
    if (!kv) return NULL;
    kvlang_rt *rt = malloc(sizeof(*rt));
    rt->kv = kv;
    return rt;
}

void kvlang_rt_disconnect(kvlang_rt *rt) {
    if (!rt) return;
    kv_disconnect(rt->kv);
    free(rt);
}

void *kvlang_rt_kv(kvlang_rt *rt) {
    return rt ? rt->kv : NULL;
}

int kvlang_rt_execute_pc(kvlang_rt *rt, const char *pc) {
    return kvcpu_execute(rt->kv, pc);
}

static char *alloc_vtid(kv_t *kv) {
    sbuf_t seq; sb_init(&seq);
    sb_puts(&seq, VTHREAD_ROOT "/" RUNTIME_MEMBER_SEP "seq");
    xval_t v; xv_zero(&v);
    kv_get_one(kv, seq.p, &v);
    char *s = xv_none(&v) ? strdup("") : xv_value_string(&v);
    xv_free(&v);
    int64_t n = atoll(s) + 1;
    free(s);
    char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)n);
    xval_t nv; xv_new_char_utf8(&nv, buf);
    kv_pair_t p = { seq.p, nv };
    char err[256];
    kv_set(kv, &p, 1, err, sizeof err);
    xv_free(&nv); sb_free(&seq);
    return strdup(buf);
}

int kvlang_rt_execute(kvlang_rt *rt, const char *funcname,
                      const char *const *args, int nargs,
                      char **ret, char *err, uint32_t err_cap) {
    kv_t *kv = rt->kv;
    char *vtid = alloc_vtid(kv);

    sbuf_t vtroot; sb_init(&vtroot); kt_vthread(vtid, &vtroot);
    char *stack_vt = kt_stack(vtroot.p);
    char e[256];
    kv_mkindex(kv, stack_vt, e, sizeof e);

    char *first_pc = kvcpu_bootstrap(kv, vtid, funcname, args, nargs);
    if (!first_pc) {
        if (err && err_cap) snprintf(err, err_cap, "Bootstrap %s failed", funcname);
        free(stack_vt); free(vtid); sb_free(&vtroot);
        return -1;
    }
    vt_set(kv, vtid, first_pc, "init");
    int rc = kvcpu_execute(kv, first_pc);
    free(first_pc);

    /* 读终态 */
    sbuf_t st; sb_init(&st); kt_vthread_status(vtid, &st);
    xval_t sv; xv_zero(&sv); kv_get_one(kv, st.p, &sv);
    char *status = xv_none(&sv) ? strdup("") : xv_value_string(&sv);
    xv_free(&sv);

    if (ret) {
        if (strcmp(status, "error") == 0) {
            sbuf_t mk; sb_init(&mk); kt_vthread_status_msg(vtid, "error", &mk);
            xval_t mv; xv_zero(&mv); kv_get_one(kv, mk.p, &mv);
            char *msg = xv_none(&mv) ? strdup("error") : xv_value_string(&mv);
            xv_free(&mv); sb_free(&mk);
            if (err && err_cap) snprintf(err, err_cap, "%s", msg);
            *ret = strdup("error");
            free(msg);
            rc = -1;
        } else {
            *ret = status;
        }
    } else {
        free(status);
    }

    sb_free(&st); sb_free(&vtroot); free(stack_vt); free(vtid);
    return rc;
}
