#include "kvlang_rwirext.h"
#include "runtime_internal.h"

/* 进程内已注册 rwir opcode 集合：isothersrwir 不读 /lib，直接本地过滤。 */
static char *g_rwir_opcodes[256];
static int g_rwir_n = 0;

/* 共享队列根：第一个 rwir 的 /lib/<opcode>/todo 绝对路径。 */
static char *g_first_todo = NULL;

bool isothersrwir(const char *opcode) {
  if (opcode[0] == '/')
    return false;
  for (int i = 0; i < g_rwir_n; i++)
    if (strcmp(g_rwir_opcodes[i], opcode) == 0)
      return true;
  return false;
}

/* 建立 rwir 的 todo 队列：第一个 rwir 是真实 strkeymap，后续是 Ptr 指向第一个。 */
static void register_todo(kvlangKv_t *k, const char *opcode) {
  char *base = kvlangKeytreeRwir(opcode);
  kvlangStrbuf_t tk; kvlangStrbufInit(&tk);
  kvlangStrbufPuts(&tk, base); kvlangStrbufPuts(&tk, "/todo");
  kvlangXvalue_t v; kvlangXvalueZero(&v);
  if (g_first_todo == NULL) {
    g_first_todo = strdup(tk.p);
    int32_t dims[1] = {0};
    kvlangXvalueNewTlvDims(&v, KVSPACE_KIND_MAP, (const uint8_t *)"", 0, dims, 1);
  } else {
    int32_t dims[1] = {0};
    kvlangXvalueNewPtrDims(&v, KVSPACE_KIND_MAP, g_first_todo, dims, 1);
  }
  kvlangKvPair_t p = {tk.p, v};
  char err[256];
  kvlangKvSet(k, &p, 1, err, sizeof err);
  kvlangXvalueFree(&v);
  kvlangStrbufFree(&tk);
  free(base);
}

int kvlang_rwirextRegister(void *kvspace, const char *opcode, int32_t nr,
                         int32_t nw, const char *sig) {
  kvlangKv_t k = {kvspace};
  char *key = kvlangKeytreeRwir(opcode);
  kvlangXvalue_t v;
  kvlangXvalueNewRwir(&v, nr, nw, sig);
  kvlangKvPair_t p = {key, v};
  char err[256];
  int rc = kvlangKvSet(&k, &p, 1, err, sizeof err);
  kvlangXvalueFree(&v);
  free(key);
  /* 进程内 map 也注册一份。 */
  for (int i = 0; i < g_rwir_n; i++)
    if (strcmp(g_rwir_opcodes[i], opcode) == 0)
      return rc;
  if (g_rwir_n < 256)
    g_rwir_opcodes[g_rwir_n++] = strdup(opcode);
  /* 建立共享 todo 队列（第一个真实 strkeymap，后续 Ptr 指向它）。 */
  register_todo(&k, opcode);
  return rc;
}

int kvlang_rwirextHandoff(void *kvspace, const char *vtid, const char *pc) {
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

char *kvlang_rwirextNextPc(const char *pc) {
  kvlangStrbuf_t b;
  kvlangStrbufInit(&b);
  kvlangRwirNextPc(pc, &b);
  return kvlangStrbufDetach(&b);
}

char *kvlang_rwirextParams(void *kvspace, const char *pc) {
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
char *kvlang_rwirextResolveRead(void *kvspace, const char *pc, int idx) {
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
char *kvlang_rwirextResolveReadPath(void *kvspace, const char *pc, int idx) {
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
char *kvlang_rwirextResolveWrite(void *kvspace, const char *pc, int idx) {
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
