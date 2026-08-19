#include "kvlang_rwext.h"
#include "runtime_internal.h"

int kvlang_rwextRegister(void *kvspace, const char *opcode, int32_t nr,
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
  return rc;
}

char *kvlang_rwextPrintLine(void *kvspace, const char *pc, int *rawnl,
                            int *cerr) {
  kvlangKv_t k = {kvspace};
  if (rawnl)
    *rawnl = 0;
  if (cerr)
    *cerr = 0;
  char *fr = kvlangKeytreeFrameRoot(pc);
  if (!fr)
    return NULL;
  char *lb = kvlangKeytreeStack(fr);
  kvlangRwirInst_t inst;
  char err[256];
  if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0) {
    free(fr);
    free(lb);
    return NULL;
  }
  free(lb);
  if (!inst.opcode || (strcmp(inst.opcode, "print") != 0 &&
                       strcmp(inst.opcode, "println") != 0 &&
                       strcmp(inst.opcode, "cerr") != 0)) {
    free(fr);
    kvlangRwirInstFree(&inst);
    return NULL;
  }
  const char *sep = strcmp(inst.opcode, "print") == 0 ? "" : " ";
  if (rawnl)
    *rawnl = (strcmp(inst.opcode, "print") == 0);
  if (cerr)
    *cerr = (strcmp(inst.opcode, "cerr") == 0);

  kvlangStrbuf_t line;
  kvlangStrbufInit(&line);
  for (int i = 0; i < inst.nr; i++) {
    if (i > 0)
      kvlangStrbufPuts(&line, sep);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(&k, fr, inst.reads[i].name,
                                  &inst.reads[i].val, &v);
    char *s = NULL;
    kvlangDisplay(&v, &s);
    kvlangStrbufPuts(&line, s);
    free(s);
    kvlangXvalueFree(&v);
  }
  free(fr);
  kvlangRwirInstFree(&inst);
  return kvlangStrbufDetach(&line);
}

char *kvlang_rwextNextPc(const char *pc) {
  kvlangStrbuf_t b;
  kvlangStrbufInit(&b);
  kvlangRwirNextPc(pc, &b);
  return kvlangStrbufDetach(&b);
}

char *kvlang_rwextParams(void *kvspace, const char *pc) {
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

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *kvlang_rwextResolveRead(void *kvspace, const char *pc, int idx) {
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
  char *s = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
  kvlangXvalueFree(&v);
  free(fr);
  kvlangRwirInstFree(&inst);
  return s;
}

/* 解析读参 idx 为 KV 路径（变量/临时 → 帧槽路径；路径 → 直接返回；内联字面量 →
 * ""）。 */
char *kvlang_rwextResolveReadPath(void *kvspace, const char *pc, int idx) {
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
char *kvlang_rwextResolveWrite(void *kvspace, const char *pc, int idx) {
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
