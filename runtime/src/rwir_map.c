#include "rwir_internal.h"

/* ── 容器值 / 成员索引 ──────────────────────────────────────────── */

/* map 容器值（p）：body 空、storetype=index，langtype 为 map langtype（见 [[map容器]]）。
 * langtype 恒非空——layout 强制容器字面量写目标带 map langtype（缺则 layout 报错）。 */
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const char *langtype) {
    kvlangXvalueNewTlv(out, langtype, (const uint8_t *)"", 0, 1);
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
    if (f->inst->writes[w].type &&
        strstr(f->inst->writes[w].type, MEMBER_SEP))
        return strdup(f->inst->writes[w].type);
    kvspaceHead_t head;
    if (kvlangXvalueHead(&f->inst->writes[w].val, &head) != 0)
        abort();
    int32_t len = 0;
    const uint8_t *body = kvlangXvalueBody(&f->inst->writes[w].val,
                                          &head, &len);
    const uint8_t *split = len > 5 ? memchr(body + 5, 0, (size_t)len - 5) : NULL;
    char *type = split ? strndup((const char *)split + 1,
                                 (size_t)(body + len - split - 1)) : strdup("");
    if (!strstr(type, MEMBER_SEP)) {
        fprintf(stderr,
                "panic: container literal target %s has no map langtype "
                "(layout must reject)\n",
                f->inst->writes[w].name);
        abort();
    }
    return type;
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
        char *wty = declared_map_langtype(f, w);
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, wty);
        free(wty);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
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
        char *wty = declared_map_langtype(f, w);
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, wty);
        free(wty);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
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
