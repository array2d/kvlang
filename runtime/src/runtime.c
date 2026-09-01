#include "kvlang_runtime.h"
#include "runtime_internal.h"

struct kvlangRuntime_t {
  kvlangKv_t *kv;
};

kvlangRuntime_t *kvlangRuntimeConnect(const char *dsn) {
  kvlangKv_t *kv = kvlangKvConnect(dsn);
  if (!kv)
    return NULL;
  kvlangRuntime_t *rt = malloc(sizeof(*rt));
  rt->kv = kv;
  return rt;
}

void kvlangRuntimeDisconnect(kvlangRuntime_t *rt) {
  if (!rt)
    return;
  kvlangKvDisconnect(rt->kv);
  free(rt);
}

int kvlangRuntimeExecutePc(kvlangRuntime_t *rt, const char *pc) {
  return kvlangKvcpuExecute(rt->kv, pc);
}

static char *read_vthread_pc(kvlangKv_t *kv, const char *vid) {
  kvlangStrbuf_t key;
  kvlangStrbufInit(&key);
  kvlangKeytreeVthreadPc(vid, &key);
  kvlangXvalue_t v;
  kvlangXvalueZero(&v);
  kvlangKvGetOne(kv, key.p, &v);
  char *pc = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
  kvlangXvalueFree(&v);
  kvlangStrbufFree(&key);
  return pc;
}

int kvlangRuntimeExecuteVthread(kvlangRuntime_t *rt, const char *vid,
                                char **out_pc) {
  char *pc = read_vthread_pc(rt->kv, vid);
  int rc = kvlangKvcpuExecuteMode(rt->kv, pc, KVMODE_RETURN, out_pc);
  free(pc);
  return rc;
}

static char *alloc_vtid(kvlangKv_t *kv) {
  kvlangStrbuf_t seq;
  kvlangStrbufInit(&seq);
  kvlangStrbufPuts(&seq, VTHREAD_ROOT "/" RUNTIME_MEMBER_SEP "seq");
  kvlangXvalue_t v;
  kvlangXvalueZero(&v);
  kvlangKvGetOne(kv, seq.p, &v);
  char *s = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
  kvlangXvalueFree(&v);
  int64_t n = atoll(s) + 1;
  free(s);
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", (long long)n);
  kvlangXvalue_t nv;
  kvlangXvalueNewCharUtf8(&nv, buf);
  kvlangKvPair_t p = {seq.p, nv};
  char err[256];
  kvlangKvSet(kv, &p, 1, err, sizeof err);
  kvlangXvalueFree(&nv);
  kvlangStrbufFree(&seq);
  return strdup(buf);
}

char *kvlangRuntimeBootstrap(kvlangRuntime_t *rt, const char *funcname,
                             const char *const *args, int nargs) {
  kvlangKv_t *kv = rt->kv;
  char *vtid = alloc_vtid(kv);
  kvlangStrbuf_t vtroot;
  kvlangStrbufInit(&vtroot);
  kvlangKeytreeVthread(vtid, &vtroot);
  char *stack_vt = kvlangKeytreeStack(vtroot.p);
  char e[256];
  kvlangKvMkindex(kv, stack_vt, e, sizeof e);
  char *first_pc = kvlangKvcpuBootstrap(kv, vtid, funcname, args, nargs);
  if (!first_pc) {
    free(stack_vt);
    free(vtid);
    kvlangStrbufFree(&vtroot);
    return NULL;
  }
  kvlangVthreadSet(kv, vtid, first_pc, "init");
  free(first_pc);
  free(stack_vt);
  kvlangStrbufFree(&vtroot);
  return vtid;
}

int kvlangRuntimeExecuteKv(kvlangKv_t *kv, const char *funcname,
                           const char *const *args, int nargs, char **ret,
                           char *err, uint32_t err_cap) {
  char *vtid = alloc_vtid(kv);

  kvlangStrbuf_t vtroot;
  kvlangStrbufInit(&vtroot);
  kvlangKeytreeVthread(vtid, &vtroot);
  char *stack_vt = kvlangKeytreeStack(vtroot.p);
  char e[256];
  kvlangKvMkindex(kv, stack_vt, e, sizeof e);

  char *first_pc = kvlangKvcpuBootstrap(kv, vtid, funcname, args, nargs);
  if (!first_pc) {
    if (err && err_cap)
      snprintf(err, err_cap, "Bootstrap %s failed", funcname);
    free(stack_vt);
    free(vtid);
    kvlangStrbufFree(&vtroot);
    return -1;
  }
  kvlangVthreadSet(kv, vtid, first_pc, "init");
  int rc = kvlangKvcpuExecute(kv, first_pc);
  free(first_pc);

  /* 读终态 */
  kvlangStrbuf_t st;
  kvlangStrbufInit(&st);
  kvlangKeytreeVthreadStatus(vtid, &st);
  kvlangXvalue_t sv;
  kvlangXvalueZero(&sv);
  kvlangKvGetOne(kv, st.p, &sv);
  char *status =
      kvlangXvalueNone(&sv) ? strdup("") : kvlangXvalueValueString(&sv);
  kvlangXvalueFree(&sv);

  if (ret) {
    if (strcmp(status, "error") == 0) {
      kvlangStrbuf_t mk;
      kvlangStrbufInit(&mk);
      kvlangKeytreeVthreadStatusMsg(vtid, "error", &mk);
      kvlangXvalue_t mv;
      kvlangXvalueZero(&mv);
      kvlangKvGetOne(kv, mk.p, &mv);
      char *msg = kvlangXvalueNone(&mv) ? strdup("error")
                                        : kvlangXvalueValueString(&mv);
      kvlangXvalueFree(&mv);
      kvlangStrbufFree(&mk);
      if (err && err_cap)
        snprintf(err, err_cap, "%s", msg);
      *ret = strdup("error");
      free(msg);
      rc = -1;
    } else {
      *ret = status;
    }
  } else {
    free(status);
  }

  kvlangStrbufFree(&st);
  kvlangStrbufFree(&vtroot);
  free(stack_vt);
  free(vtid);
  return rc;
}

int kvlangRuntimeExecute(kvlangRuntime_t *rt, const char *funcname,
                         const char *const *args, int nargs, char **ret,
                         char *err, uint32_t err_cap) {
  return kvlangRuntimeExecuteKv(rt->kv, funcname, args, nargs, ret, err, err_cap);
}
