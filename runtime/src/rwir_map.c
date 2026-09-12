#include "rwir_internal.h"

/* ── 容器值 / 成员索引 ──────────────────────────────────────────── */

/* kvlangBuiltinMemindex（p·）：kind=index，body=[4B count LE][name\n...]，成员列表唯一权威。 */
void kvlangBuiltinMemindex(kvlangXvalue_t *out, const char *const *names,
                           int n) {
    kvlangStrbuf_t body;
    kvlangStrbufInit(&body);
    char count[4] = {(char)(n & 0xFF), (char)((n >> 8) & 0xFF),
                     (char)((n >> 16) & 0xFF), (char)((n >> 24) & 0xFF)};
    kvlangStrbufPutn(&body, count, 4);
    for (int i = 0; i < n; i++) {
        if (i)
            kvlangStrbufPutc(&body, '\n');
        kvlangStrbufPuts(&body, names[i]);
    }
    kvlangXvalueNewTlv(out, KVSPACE_KIND_INDEX, (const uint8_t *)body.p,
                       (uint32_t)body.len, 1);
    kvlangStrbufFree(&body);
}

/* map 容器值（p）：body 空、storetype=index，langtype 为 map langtype（见 [[map容器]]）。
 * langtype 恒非空——layout 强制容器字面量写目标带 map langtype（缺则 layout 报错）。 */
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const char *langtype,
                            const int32_t *dims, int ndim) {
    kvlangXvalueNewTlvDims(out, langtype, (const uint8_t *)"", 0, dims, ndim);
}

/* 写槽 `w` 的声明容器类型：layout 的 write_slot_value 把 map langtype 落进写槽的 langtype
 * （见 code.rs）。缺类型即 layout 漏检——直接 fatal，不退化兜底。调用方负责 free。 */
static char *declared_map_langtype(kvlangFrame_t *f, int w) {
    if (w >= f->inst->nw) {
        fprintf(stderr,
                "panic: container literal write slot %d missing (nw=%d)\n", w,
                f->inst->nw);
        abort();
    }
    char buf[256];
    kvlangXvalueLangtype(&f->inst->writes[w].val, buf, sizeof buf);
    if (!strstr(buf, MEMBER_SEP)) {
        fprintf(stderr,
                "panic: container literal target %s has no map langtype "
                "(layout must reject)\n",
                f->inst->writes[w].name);
        abort();
    }
    return strdup(buf);
}

/* ── obj / map ─────────────────────────────────────────────────── */

int kvlangBuiltinObj(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok =
            kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name），容器值随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL;
        int oc = 0;
        kvlangKvList(f->kv, dir, false, false, &old, &oc);
        for (int i = 0; i < oc; i++) {
            char *mk = kvlangKeytreeMember(ok, old[i]);
            kvlangKvDel(f->kv, mk, err, sizeof err);
            free(mk);
            free(old[i]);
        }
        free(old);
        free(dir);
        /* 收集成员名（跳过 None）。 */
        int cnt = 0;
        for (int i = 0; i + 1 < n; i += 2)
            if (!kvlangXvalueNone(&in[i + 1]))
                cnt++;
        char **names = malloc(sizeof(char *) * (size_t)(cnt > 0 ? cnt : 1));
        for (int i = 0, j = 0; i + 1 < n; i += 2) {
            if (kvlangXvalueNone(&in[i + 1]))
                continue;
            names[j++] = kvlangXvalueValueString(&in[i]);
        }
        /* 容器值 p：langtype=声明的 map langtype，dims=[0]（命名字典无形状，成员在 memindex）。 */
        int32_t odims[1] = {0};
        char *wty = declared_map_langtype(f, w);
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, wty, odims, 1);
        free(wty);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        /* kvlangBuiltinMemindex p·：kind=index，body=[4B count][names]。 */
        char *mip = kvlangKeytreeMember(ok, "");
        kvlangXvalue_t mi;
        kvlangBuiltinMemindex(&mi, (const char *const *)names, cnt);
        kvlangKvPair_t p1 = {mip, mi};
        kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
        kvlangXvalueFree(&mi);
        free(mip);
        for (int i = 0, j = 0; i + 1 < n; i += 2) {
            if (kvlangXvalueNone(&in[i + 1]))
                continue;
            char *mk = kvlangKeytreeMember(ok, names[j]);
            kvlangKvPair_t p = {mk, in[i + 1]};
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(mk);
            free(names[j]);
            j++;
        }
        free(names);
        free(ok);
    }
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

int kvlangBuiltinMap(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->inst->nw; w++) {
        char *ok =
            kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name）与旧容器值，随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL;
        int oc = 0;
        kvlangKvList(f->kv, dir, false, false, &old, &oc);
        for (int i = 0; i < oc; i++) {
            char *mk = kvlangKeytreeMember(ok, old[i]);
            kvlangKvDel(f->kv, mk, err, sizeof err);
            free(mk);
            free(old[i]);
        }
        free(old);
        free(dir);
        kvlangKvDel(f->kv, ok, err, sizeof err);

        char **names = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            kvlangStrbuf_t s;
            kvlangStrbufInit(&s);
            kvlangStrbufPrintf(&s, "[%d]", i);
            names[i] = kvlangStrbufDetach(&s);
        }
        /* 容器值 p：langtype=声明的 map langtype，body 空，dims=[n] 落 head。 */
        int32_t dims[1] = {n};
        char *wty = declared_map_langtype(f, w);
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, wty, dims, 1);
        free(wty);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        /* kvlangBuiltinMemindex p·：kind=index，body=[4B count][[0]\n[1]...]。 */
        char *mip = kvlangKeytreeMember(ok, "");
        kvlangXvalue_t mi;
        kvlangBuiltinMemindex(&mi, (const char *const *)names, n);
        kvlangKvPair_t p1 = {mip, mi};
        kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
        kvlangXvalueFree(&mi);
        free(mip);
        for (int i = 0; i < n; i++) {
            int64_t c[1] = {i};
            char *k = kvlangBuiltinScatterKey(ok, c, 1);
            kvlangKvPair_t p = {k, in[i]};
            kvlangKvSet(f->kv, &p, 1, err, sizeof err);
            free(k);
            free(names[i]);
        }
        free(names);
        free(ok);
    }
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}
