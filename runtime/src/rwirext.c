#include "kvlang_rwirext.h"
#include "runtime_internal.h"

/* 共享队列根：第一个 rwir 的 /lib/<opcode>/vids 绝对路径。 */
static char *g_first_vids = NULL;

/* notinmyrwircaps：opcode 是一条不在本 runtime myrwircaps 内、须经 def rwir 路由给
 * 能兑现它的其它 runtime 的 rwir。判据是 /lib/<opcode> 存在 def rwir 路由头
 * （langtype=def rwir，storetype=index）。kvspace 是能力唯一事实源；本判定在独立
 * kvlang 进程内发生，进程内 myrwircaps 恒不含它，故只有 /lib 路由头可信。 */
bool notinmyrwircaps(kvlangKv_t *k, const char *opcode) {
  if (opcode[0] == '/')
    return false;
  char *key = kvlangKeytreeRwir(opcode);
  kvlangXvalue_t v; kvlangXvalueZero(&v);
  kvlangKvGetOne(k, key, &v);
  bool yes = !kvlangXvalueNone(&v) && kvlangXvalueKindIs(&v, KVSPACE_KIND_DEF_RWIR);
  kvlangXvalueFree(&v);
  free(key);
  return yes;
}

/* 建立 rwir 的 vids 队列：第一个 rwir 是真实 strkeymap，后续是 Ptr 指向第一个。
 * 幂等：vids 已存在则跳过——否则脏 kvspace 上重复注册时，Set 经路径穿透会把首队列
 * 改成自指 Ptr，令 resolve_path 死循环（父子 kvlang 共享同一 redis 的挂起根因）。 */
static void register_vids(kvlangKv_t *k, const char *opcode) {
  char *base = kvlangKeytreeRwir(opcode);
  kvlangStrbuf_t tk; kvlangStrbufInit(&tk);
  kvlangStrbufPuts(&tk, base); kvlangStrbufPuts(&tk, "/vids");
  bool first = (g_first_vids == NULL);
  if (first) g_first_vids = strdup(tk.p);
  kvlangXvalue_t cur; kvlangXvalueZero(&cur);
  bool exists = (kvlangKvGetOne(k, tk.p, &cur) == 0 && !kvlangXvalueNone(&cur));
  kvlangXvalueFree(&cur);
  if (!exists) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    int32_t dims[1] = {0};
    if (first)
      kvlangXvalueNewTlvDims(&v, KVSPACE_KIND_MAP, (const uint8_t *)"", 0, dims, 1);
    else
      kvlangXvalueNewPtr(&v, KVSPACE_KIND_MAP, g_first_vids);
    kvlangKvPair_t p = {tk.p, v};
    char err[256];
    kvlangKvSet(k, &p, 1, err, sizeof err);
    kvlangXvalueFree(&v);
  }
  kvlangStrbufFree(&tk);
  free(base);
}

/* 写 [0,x] 签名行槽 = def langtype（body 为该参数 langtype 串）。 */
static void write_sig_slot(kvlangKv_t *k, const char *base, int x, const char *lt, size_t lt_len) {
  kvlangStrbuf_t sk; kvlangStrbufInit(&sk);
  kvlangStrbufPrintf(&sk, "%s/[0,%d]", base, x);
  char *clean = strndup(lt, lt_len);
  kvlangXvalue_t sv; kvlangXvalueNewDefLangtype(&sv, clean);
  free(clean);
  kvlangKvPair_t sp = {sk.p, sv};
  char err[256];
  kvlangKvSet(k, &sp, 1, err, sizeof err);
  kvlangXvalueFree(&sv);
  kvlangStrbufFree(&sk);
}

/* 注册一条 rwir：/lib/<op> 路由头（仅计数头）+ 各参数落 [0,x] 签名行槽（def langtype）。
 * 参数类型逐条传入（读参 rp[0..nr]、写参 wp[0..nw]），不再拼签名串——避免其它 runtime
 * 把「拼接 sig 串」误当注册标准。末读参尾缀 "..." → 变参 arity（落 dynamic 字节）。 */
int kvlangRwirextRegister(void *kvspace, const char *opcode,
                         const char *const *rp, int32_t nr,
                         const char *const *wp, int32_t nw) {
  kvlangKv_t k = {kvspace};
  char *base = kvlangKeytreeRwir(opcode);
  char err[256];

  int dynamic = 0;
  size_t last_len = (nr > 0 && rp[nr - 1]) ? strlen(rp[nr - 1]) : 0;
  if (nr > 0 && last_len >= 3 && memcmp(rp[nr - 1] + last_len - 3, "...", 3) == 0) {
    dynamic = 1;
    last_len -= 3;
  }

  kvlangXvalue_t hv;
  kvlangXvalueNewDefRwir(&hv, nr, nw, dynamic);
  kvlangKvPair_t hp = {base, hv};
  int rc = kvlangKvSet(&k, &hp, 1, err, sizeof err);
  kvlangXvalueFree(&hv);

  for (int32_t i = 0; i < nr; i++)
    write_sig_slot(&k, base, -(i + 1), rp[i], (i == nr - 1) ? last_len : strlen(rp[i]));
  for (int32_t i = 0; i < nw; i++)
    write_sig_slot(&k, base, i + 1, wp[i], strlen(wp[i]));

  free(base);
  /* 建立共享 vids 队列（第一个真实 strkeymap，后续 Ptr 指向它；幂等查 kvspace）。 */
  register_vids(&k, opcode);
  return rc;
}

int kvlangRwirextHandoff(void *kvspace, const char *vtid, const char *pc) {
  kvlangKv_t k = {kvspace};
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return -1;
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0) {
    free(fr);
    free(lb);
    return -1;
  }
  free(lb);
  int rc = handoff_external_rwir(&k, vtid, pc, &inst);
  free(fr);
  kvlangRwirInstFree(&inst);
  return rc;
}

char *kvlangRwirextNextPc(const char *pc) {
  kvlangStrbuf_t b;
  kvlangStrbufInit(&b);
  kvlangRwirNextPc(pc, &b);
  return kvlangStrbufDetach(&b);
}

char *kvlangRwirextParams(void *kvspace, const char *pc) {
  kvlangKv_t k = {kvspace};
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return strdup("");
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0) {
    free(fr);
    free(lb);
    return strdup("");
  }
  free(lb);
  kvlangStrbuf_t b;
  kvlangStrbufInit(&b);
  kvlangStrbufPuts(&b, inst.opcode ? inst.opcode : "");
  for (int i = 0; i < inst.nr; i++) {
    kvlangStrbufPutc(&b, '\n');
    kvlangStrbufPuts(&b, inst.reads[i].name ? inst.reads[i].name : "");
  }
  for (int i = 0; i < inst.nw; i++) {
    kvlangStrbufPutc(&b, '\n');
    kvlangStrbufPuts(&b, inst.writes[i].name ? inst.writes[i].name : "");
  }
  free(fr);
  kvlangRwirInstFree(&inst);
  return kvlangStrbufDetach(&b);
}

/* stringkeymap 容器值：遍历 p· 成员 → "[e0, e1, ...]"（成员为 map 时递归）。 */
static char *display_map(kvlangKv_t *k, const char *path) {
  char *dir = kvlangKeytreeMember(path, "");
  char **names;
  int cnt;
  if (kvlangKvList(k, dir, false, false, &names, &cnt) != 0 || cnt <= 0) {
    free(dir);
    free(names);
    return strdup("[]");
  }
  kvlangStrbuf_t b;
  kvlangStrbufInit(&b);
  kvlangStrbufPutc(&b, '[');
  for (int i = 0; i < cnt; i++) {
    if (i) kvlangStrbufPuts(&b, ", ");
    char *mk = kvlangKeytreeMember(path, names[i]);
    kvlangXvalue_t mv;
    kvlangXvalueZero(&mv);
    kvlangKvGetOne(k, mk, &mv);
    char *ms = kvlangXvalueNone(&mv)
                   ? strdup("")
                   : strcmp(kvlangXvalueKind(&mv), KVSPACE_KIND_MAP) == 0
                         ? display_map(k, mk)
                         : kvlangXvalueValueString(&mv);
    kvlangStrbufPuts(&b, ms);
    free(ms);
    kvlangXvalueFree(&mv);
    free(mk);
    free(names[i]);
  }
  kvlangStrbufPutc(&b, ']');
  free(names);
  free(dir);
  return kvlangStrbufDetach(&b);
}

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *kvlangRwirextResolveRead(void *kvspace, const char *pc, int idx) {
  kvlangKv_t k = {kvspace};
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return strdup("");
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
      idx >= inst.nr) {
    free(fr);
    free(lb);
    return strdup("");
  }
  free(lb);
  kvlangXvalue_t v;
  kvlangXvalueZero(&v);
  kvlangBuiltinResolveReadValue(&k, fr, inst.reads[idx].name,
                                &inst.reads[idx].val, &v);
  char *s;
  if (kvlangXvalueNone(&v)) {
    s = strdup("");
  } else if (strcmp(kvlangXvalueKind(&v), KVSPACE_KIND_MAP) == 0) {
    char *path = kvlangBuiltinResolveWriteSlot(&k, fr, inst.reads[idx].name);
    s = display_map(&k, path);
    free(path);
  } else {
    s = kvlangXvalueValueString(&v);
  }
  kvlangXvalueFree(&v);
  free(fr);
  kvlangRwirInstFree(&inst);
  return s;
}

/* 解析读参 idx 为 KV 路径（变量/临时 → 帧槽路径；路径 → 直接返回；内联字面量 →
 * ""）。 */
char *kvlangRwirextResolveReadPath(void *kvspace, const char *pc, int idx) {
  kvlangKv_t k = {kvspace};
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return strdup("");
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
      idx >= inst.nr) {
    free(fr);
    free(lb);
    return strdup("");
  }
  free(lb);
  const char *nm = inst.reads[idx].name;
  char *s;
  if (!nm || !nm[0] || nm[0] == '"' || (nm[0] >= '0' && nm[0] <= '9') ||
      (nm[0] == '-' && nm[1]) || strcmp(nm, "true") == 0 ||
      strcmp(nm, "false") == 0 || strcmp(nm, "null") == 0) {
    s = strdup(""); /* 内联字面量：无路径 */
  } else {
    s = kvlangBuiltinResolveWriteSlot(&k, fr,
                                      nm); /* 变量/临时/路径 → 帧槽路径 */
  }
  free(fr);
  kvlangRwirInstFree(&inst);
  return s ? s : strdup("");
}

/* 解析写参 idx 为 KV 路径（路径 → 直接返回；变量 → 帧槽路径）。 */
char *kvlangRwirextResolveWrite(void *kvspace, const char *pc, int idx) {
  kvlangKv_t k = {kvspace};
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return strdup("");
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
      idx >= inst.nw) {
    free(fr);
    free(lb);
    return strdup("");
  }
  free(lb);
  char *s = kvlangBuiltinResolveWriteSlot(&k, fr, inst.writes[idx].name);
  free(fr);
  kvlangRwirInstFree(&inst);
  return s;
}
