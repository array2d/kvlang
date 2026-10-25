#include "kvlang_vthread.h"
#include "runtime_internal.h"

struct kvlangRuntime_t {
  kvspace_t *kvspace;
};

kvlangRuntime_t *kvlangRuntimeConnect(const char *dsn) {
  kvspace_t *kv = kvlangKvspaceConnect(dsn);
  if (!kv)
    return NULL;
  /* P1a：控制面独立 store（redis 同库）。先连控制 store，再发布 core；
   * 任一失败即连接失败，绝不静默回落到数据 store。 */
  char ctl[KV_KEY_MAX];
  if (!kvlangIsaCtlDsn(dsn, ctl, sizeof ctl)) {
    kvlangKvspaceDisconnect(kv);
    return NULL;
  }
  if (strcmp(ctl, dsn) == 0) {
    kv->isa_handle = kv->handle;
    kv->isa_shared = true;
  } else {
    kvspace_t *isa = kvlangKvspaceConnect(ctl);
    if (!isa) {
      kvlangKvspaceDisconnect(kv);
      return NULL;
    }
    kv->isa_handle = isa->handle;
    free(isa);
  }
  if (kvlangIsaPublishCore(kv) != 0) {
    kvlangKvspaceDisconnect(kv);
    return NULL;
  }
  kvlangRuntime_t *rt = malloc(sizeof(*rt));
  if (!rt) {
    kvlangKvspaceDisconnect(kv);
    return NULL;
  }
  rt->kvspace = kv;
  return rt;
}

void kvlangRuntimeDisconnect(kvlangRuntime_t *rt) {
  if (!rt)
    return;
  kvlangKvspaceDisconnect(rt->kvspace);
  free(rt);
}

int kvlangRuntimeExecutePc(kvlangRuntime_t *rt, const char *pc) {
  return kvlangKvcpuExecute(rt->kvspace, pc);
}

/* runtime 与 byteseek 必须共用同一 kvspace 句柄——durable 惰性 flush
 * 仅在同句柄内相干， 跨句柄写不可见（shm 共享 mmap
 * 掩盖了此点）。暴露内部句柄，禁止调用方另开连接。 */
void *kvlangRuntimeKvspaceHandle(kvlangRuntime_t *rt) { return rt->kvspace->handle; }

static char *read_vthread_pc(kvspace_t *kv, const char *vid) {
  char key[KV_KEY_MAX];
  kvlangKeytreeVthreadPc(vid, key, sizeof key);
  kvlangXvalue_t v;
  kvlangXvalueZero(&v);
  kvlangKvspaceGetOne(kv, key, &v);
  char *pc = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
  return pc;
}

int kvlangRuntimeExecuteVthread(kvlangRuntime_t *rt, const char *vid,
                                char **out_pc) {
  char *pc = read_vthread_pc(rt->kvspace, vid);
  int rc = kvlangKvcpuExecuteMode(rt->kvspace, pc, KVMODE_RETURN, out_pc);
  free(pc);
  return rc;
}

static char *alloc_vthread_id(kvspace_t *kv) {
  static const char seq[] = VTHREAD_ROOT "/" RUNTIME_MEMBER_SEP "seq";
  kvlangXvalue_t v;
  kvlangXvalueZero(&v);
  kvlangKvspaceGetOne(kv, seq, &v);
  char *s = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
  int64_t n = atoll(s) + 1;
  free(s);
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", (long long)n);
  kvlangXvalue_t nv;
  kvlangXvalueNewCharUtf8(&nv, buf);
  kvlangKvPair_t p = {seq, nv};
  char err[256];
  int rc = kvlangXvalueNone(&nv) ? -1 :
           kvlangKvspaceSet(kv, &p, 1, err, sizeof err);
  kvlangXvalueFree(&nv);
  return rc == 0 ? strdup(buf) : NULL;
}

/* 创建一个 vthread（分配 vid + 建栈索引 + bootstrap 首指令 + 置 init），返回
 * vid（调用方 free）。 只创建不运行——运行由 kvlangRuntimeRunVthreadId 按 vid
 * 承接。RuntimeBootstrap/ExecuteKv/vthread·create 共用。 */
char *kvlangVthreadSpawn(kvspace_t *kv, const char *funcname,
                         const char *const *args, int nargs) {
  char *vtid = alloc_vthread_id(kv);
  if (!vtid)
    return NULL;
  char vtroot[KV_KEY_MAX];
  kvlangKeytreeVthread(vtid, vtroot, sizeof vtroot);
  char *stack_vt = kvlangKeytreeStack(vtroot);
  char e[256];
  if (kvlangKvspaceMakeIndex(kv, stack_vt, 0, e, sizeof e) != 0) {
    free(stack_vt);
    free(vtid);
    return NULL;
  }
  char *first_pc = kvlangKvcpuBootstrap(kv, vtid, funcname, args, nargs);
  if (!first_pc) {
    free(stack_vt);
    free(vtid);
    return NULL;
  }
  int persisted = kvlangVthreadSet(kv, vtid, first_pc, "init");
  free(first_pc);
  free(stack_vt);
  if (persisted != 0) {
    free(vtid);
    return NULL;
  }
  return vtid;
}

char *kvlangRuntimeBootstrap(kvlangRuntime_t *rt, const char *funcname,
                             const char *const *args, int nargs) {
  return kvlangVthreadSpawn(rt->kvspace, funcname, args, nargs);
}

/* 按 vid 从其持久化 pc 跑到结束（WATCH 模式，阻塞）。读终态：error 时把消息写
 * err、*ret="error"。 */
int kvlangRuntimeRunVthreadId(kvspace_t *kv, const char *vid, char **ret, char *err,
                        uint32_t err_cap) {
  char *pc = read_vthread_pc(kv, vid);
  if (!pc || !pc[0]) {
    free(pc);
    if (err && err_cap)
      snprintf(err, err_cap, "vthread %s has no pc", vid);
    return -1;
  }
  int rc = kvlangKvcpuExecute(kv, pc);
  free(pc);

  /* 读终态 */
  char st[KV_KEY_MAX];
  kvlangKeytreeVthreadStatus(vid, st, sizeof st);
  kvlangXvalue_t sv;
  kvlangXvalueZero(&sv);
  kvlangKvspaceGetOne(kv, st, &sv);
  char *status =
      kvlangXvalueNone(&sv) ? strdup("") : kvlangXvalueValueString(&sv);

  if (ret) {
    if (strcmp(status, "error") == 0) {
      char mk[KV_KEY_MAX];
      kvlangKeytreeVthreadMessage(vid, mk, sizeof mk);
      kvlangXvalue_t mv;
      kvlangXvalueZero(&mv);
      kvlangKvspaceGetOne(kv, mk, &mv);
      char *msg = kvlangXvalueNone(&mv) ? strdup("error")
                                        : kvlangXvalueValueString(&mv);
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
  return rc;
}

int kvlangRuntimeExecuteKvspace(kvspace_t *kv, const char *funcname,
                           const char *const *args, int nargs, char **ret,
                           char *err, uint32_t err_cap) {
  char *vtid = kvlangVthreadSpawn(kv, funcname, args, nargs);
  if (!vtid) {
    if (err && err_cap)
      snprintf(err, err_cap, "Bootstrap %s failed", funcname);
    return -1;
  }
  int rc = kvlangRuntimeRunVthreadId(kv, vtid, ret, err, err_cap);
  free(vtid);
  return rc;
}

int kvlangRuntimeExecute(kvlangRuntime_t *rt, const char *funcname,
                         const char *const *args, int nargs, char **ret,
                         char *err, uint32_t err_cap) {
  return kvlangRuntimeExecuteKvspace(rt->kvspace, funcname, args, nargs, ret, err,
                                err_cap);
}
