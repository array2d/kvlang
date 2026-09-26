#include "rwir_internal.h"

static char *struct_field_type(kvlangKv_t *kv, const char *base,
                               const char *fname) {
    kvlangStrbuf_t key;
    kvlangStrbufInit(&key);
    kvlangStrbufPrintf(&key, "%s/%s", base, fname);
    kvlangXvalue_t value;
    kvlangXvalueZero(&value);
    kvlangKvGetOne(kv, key.p, &value);
    kvlangStrbufFree(&key);
    if (kvlangXvalueNone(&value) ||
        strcmp(kvlangXvalueKind(&value), KVSPACE_KIND_DEF_LANGTYPE) != 0) {
        kvlangXvalueFree(&value);
        return NULL;
    }
    kvspaceHead_t head;
    kvlangXvalueHead(&value, &head);
    int32_t len = 0;
    const uint8_t *body = kvlangXvalueBody(&value, &head, &len);
    char *type = strndup((const char *)body, (size_t)len);
    kvlangXvalueFree(&value);
    return type;
}

/* struct·new：克隆 /lib/Name 原型子树到写槽，覆盖给定字段（校验字段存在性+类型）。
 * in[0]=structref（"/lib/Name"），其后成对 (字段名, 值)。实例基值 kind=structref。 */
int kvlangBuiltinStructNew(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    if (n < 1) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: struct.new requires a type");
    }
    char *ref = kvlangXvalueValueString(&in[0]);
    kvlangXvalue_t proto;
    kvlangXvalueZero(&proto);
    kvlangKvGetOne(f->kv, ref, &proto);
    if (kvlangXvalueNone(&proto) ||
        strcmp(kvlangXvalueKind(&proto), KVSPACE_KIND_DEF_STRUCT) != 0) {
        int e =
            kvlangBuiltinSetErr(f, "TypeError: %s is not a struct type", ref);
        kvlangXvalueFree(&proto);
        free(ref);
        kvlangBuiltinFreeInputs(in, n);
        return e;
    }
    kvlangXvalueFree(&proto);

    char err[256];
    int rc = 0;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw && rc == 0; w++) {
        char *ok =
            kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        if (kvlangKvCpList(f->kv, ref, ok, err, sizeof err) != 0) {
            rc = kvlangBuiltinSetErr(f, "%s", err);
            free(ok);
            break;
        }
        kvlangXvalue_t mark;
        kvlangXvalueNewTlv(&mark, ref, (const uint8_t *)"", 0, 1);
        kvlangKvPair_t p0 = {ok, mark};
        if (kvlangKvSet(f->kv, &p0, 1, err, sizeof err) != 0)
            rc = kvlangBuiltinSetErr(f, "%s", err);
        kvlangXvalueFree(&mark);
        for (int i = 1; i + 1 < n && rc == 0; i += 2) {
            char *fname = kvlangXvalueValueString(&in[i]);
            char *ftype = struct_field_type(f->kv, ref, fname);
            if (!ftype) {
                rc = kvlangBuiltinSetErr(
                    f, "TypeError: struct %s has no field %s", ref, fname);
                free(fname);
                break;
            }
            const char *vk = kvlangXvalueKind(&in[i + 1]);
            kvspaceHead_t vh;
            kvlangXvalueHead(&in[i + 1], &vh);
            kvlangLangtype vkx;
            kvlangLangtypeParse(vh.langtype, &vkx);
            // *T 指针字段：字段类型剥离前导 * 后与值的 langtype 比对，并校验值 ref=1；
            // None 是合法空指针（见 [[ptr]]），不算类型不符。
            const char *fx = (ftype[0] == '*') ? ftype + 1 : ftype;
            bool type_ok = ftype[0]
                               ? kvlangLangtypeMatch(fx, vk, vkx.ndim, vkx.dims)
                               : true;
            if (ftype[0] == '*' && !kvlangXvalueNone(&in[i + 1]) &&
                vh.ref != KVSPACE_REF_PTR)
                type_ok = false;
            if (ftype[0] && !type_ok) {
                rc = kvlangBuiltinSetErr(
                    f, "TypeError: field %s: expected %s, got %s", fname, ftype,
                    vk[0] ? vk : "None");
                free(ftype);
                free(fname);
                break;
            }
            char *mk = kvlangKeytreeMember(ok, fname);
            kvlangKvPair_t p = {mk, in[i + 1]};
            if (kvlangKvSet(f->kv, &p, 1, err, sizeof err) != 0)
                rc = kvlangBuiltinSetErr(f, "%s", err);
            free(mk);
            free(ftype);
            free(fname);
        }
        free(ok);
    }
    free(fr);
    free(ref);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return rc;
    kvlangBuiltinNextPc(f);
    return 0;
}
