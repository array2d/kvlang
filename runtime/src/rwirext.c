#include "kvlang_rwirext.h"
#include "runtime_internal.h"

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

int kvlang_rwirextIsExt(void *kvspace, const char *opcode) {
  kvlangKv_t k = {kvspace};
  return is_ext_rwir(&k, opcode) ? 1 : 0;
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
  char *s = kvlangXvalueNone(&v) ? strdup("") : kvlangXvalueValueString(&v);
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
