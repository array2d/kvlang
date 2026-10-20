#include "kvlang_runtime.h"
#include "runtime_internal.h"

char *kvlangRwirextNextPc(const char *pc) {
    char buf[KV_KEY_MAX];
    size_t n = kvlangRwirNextPcBuffer(pc, buf, sizeof buf);
    if (n == 0)
        kvlangDie("next pc exceeds %zu bytes", sizeof buf);
    return kvlangStringCopyN(buf, n);
}

char *kvlangRwirextParams(void *kvspace, const char *pc) {
    kvspace_t k = {kvspace};
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
    const char *op = inst.opcode ? inst.opcode : "";
    size_t total = strlen(op) + 1;
    for (int i = 0; i < inst.read_count; i++)
        total += 1 + strlen(inst.reads[i].name ? inst.reads[i].name : "");
    for (int i = 0; i < inst.write_count; i++)
        total += 1 + strlen(inst.writes[i].name ? inst.writes[i].name : "");
    char *out = malloc(total);
    if (!out)
        kvlangDie("out of memory formatting params");
    size_t off = strlen(op);
    memcpy(out, op, off);
    for (int i = 0; i < inst.read_count; i++) {
        const char *s = inst.reads[i].name ? inst.reads[i].name : "";
        size_t n = strlen(s);
        out[off++] = '\n';
        memcpy(out + off, s, n);
        off += n;
    }
    for (int i = 0; i < inst.write_count; i++) {
        const char *s = inst.writes[i].name ? inst.writes[i].name : "";
        size_t n = strlen(s);
        out[off++] = '\n';
        memcpy(out + off, s, n);
        off += n;
    }
    out[off] = 0;
    free(fr);
    kvlangRwirInstructionFree(&inst);
    return out;
}

/* stringkeymap 容器值：遍历 p· 成员 → "[e0, e1, ...]"（成员为 map 时递归）。 */
static char *display_map(kvspace_t *k, const char *path) {
    char *dir = kvlangKeytreeMember(path, "");
    char **names;
    int cnt;
    if (kvlangKvspaceList(k, dir, false, false, &names, &cnt) != 0 || cnt <= 0) {
        free(dir);
        free(names);
        return strdup("[]");
    }
    char **ms = malloc(sizeof(char *) * (size_t)cnt);
    if (!ms)
        kvlangDie("out of memory formatting map");
    size_t total = 2; /* "[" + "]" */
    for (int i = 0; i < cnt; i++) {
        char *mk = kvlangKeytreeMember(path, names[i]);
        kvlangXvalue_t mv;
        kvlangXvalueZero(&mv);
        kvlangKvspaceGetOne(k, mk, &mv);
        ms[i] = kvlangXvalueNone(&mv) ? strdup("")
                : kvlangKindIsMap(kvlangXvalueKind(&mv))
                    ? display_map(k, mk)
                    : kvlangXvalueValueString(&mv);
        kvlangXvalueFree(&mv);
        free(mk);
        free(names[i]);
        total += strlen(ms[i]) + (i ? 2 : 0);
    }
    free(names);
    free(dir);
    char *out = malloc(total + 1);
    if (!out)
        kvlangDie("out of memory formatting map");
    size_t off = 0;
    out[off++] = '[';
    for (int i = 0; i < cnt; i++) {
        if (i) {
            out[off++] = ',';
            out[off++] = ' ';
        }
        size_t n = strlen(ms[i]);
        memcpy(out + off, ms[i], n);
        off += n;
        free(ms[i]);
    }
    out[off++] = ']';
    out[off] = 0;
    free(ms);
    return out;
}

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *kvlangRwirextResolveRead(void *kvspace, const char *pc, int idx) {
    kvspace_t k = {kvspace};
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr)
        return strdup("");
    char *lb = kvlangKeytreeStack(fr);
    kvlangRwirInst_t inst;
    char err[256];
    if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
        idx >= inst.read_count) {
        free(fr);
        free(lb);
        return strdup("");
    }
    free(lb);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(&k, fr, inst.reads[idx].name,
                                  &inst.reads[idx].value, &v);
    char *s;
    if (kvlangXvalueNone(&v)) {
        s = strdup("");
    } else if (kvlangKindIsMap(kvlangXvalueKind(&v))) {
        char *path =
            kvlangBuiltinResolveWriteSlot(&k, fr, inst.reads[idx].name);
        s = display_map(&k, path);
        free(path);
    } else {
        s = kvlangXvalueValueString(&v);
    }
    kvlangXvalueFree(&v);
    free(fr);
    kvlangRwirInstructionFree(&inst);
    return s;
}

/* 解析读参 idx 为 KV 路径（变量/临时 → 帧槽路径；路径 → 直接返回；内联字面量 →
 * ""）。 */
char *kvlangRwirextResolveReadPath(void *kvspace, const char *pc, int idx) {
    kvspace_t k = {kvspace};
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr)
        return strdup("");
    char *lb = kvlangKeytreeStack(fr);
    kvlangRwirInst_t inst;
    char err[256];
    if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
        idx >= inst.read_count) {
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
    kvlangRwirInstructionFree(&inst);
    return s ? s : strdup("");
}

/* 解析写参 idx 为 KV 路径（路径 → 直接返回；变量 → 帧槽路径）。 */
char *kvlangRwirextResolveWrite(void *kvspace, const char *pc, int idx) {
    kvspace_t k = {kvspace};
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr)
        return strdup("");
    char *lb = kvlangKeytreeStack(fr);
    kvlangRwirInst_t inst;
    char err[256];
    if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0 || idx < 0 ||
        idx >= inst.write_count) {
        free(fr);
        free(lb);
        return strdup("");
    }
    free(lb);
    char *s = kvlangBuiltinResolveWriteSlot(&k, fr, inst.writes[idx].name);
    free(fr);
    kvlangRwirInstructionFree(&inst);
    return s;
}
