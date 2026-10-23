#include "rwir_internal.h"

/* ── 容器值 / 成员索引 ──────────────────────────────────────────── */

/* map 容器值（p）：body 空、storetype=index，langtype 为 map langtype（见 [[map容器]]）。
 * langtype 恒非空——layout 强制容器字面量写目标带 map langtype（缺则 layout 报错）。 */
void kvlangBuiltinMapMarker(kvlangXvalue_t *out, const char *langtype) {
    kvlangXvalueNew(out, langtype, (const uint8_t *)"", 0, 1);
}

/* 写槽 `w` 的声明容器类型：layout 的 write_slot_value 把 map langtype 落进写槽的 langtype
 * （见 code.rs）。缺类型即 layout 漏检——直接 fatal，不退化兜底。调用方负责 free。 */
static char *declared_map_langtype(kvlangFrame_t *f, int w) {
    if (w >= f->instruction->write_count) {
        fprintf(stderr,
                "panic: container literal write slot %d missing (nw=%d)\n", w,
                f->instruction->write_count);
        abort();
    }
    if (f->instruction->writes[w].type &&
        strstr(f->instruction->writes[w].type, MEMBER_SEP))
        return strdup(f->instruction->writes[w].type);
    if (kvlangXvalueNone(&f->instruction->writes[w].value))
        abort();
    const uint8_t *body = xh_body(f->instruction->writes[w].value.bytes);
    int32_t len = xh_content_len(f->instruction->writes[w].value.bytes);
    const uint8_t *split = len > 5 ? memchr(body + 5, 0, (size_t)len - 5) : NULL;
    char *type = split ? strndup((const char *)split + 1,
                                 (size_t)(body + len - split - 1)) : strdup("");
    if (!strstr(type, MEMBER_SEP)) {
        fprintf(stderr,
                "panic: container literal target %s has no map langtype "
                "(layout must reject)\n",
                f->instruction->writes[w].name);
        abort();
    }
    return type;
}

/* ── obj / map ─────────────────────────────────────────────────── */

/* 清旧成员（map 连同旧容器值）：一次多键 Del 取代逐键往返。成员无子树
 * （DeleteTree 对 `‥` 成员命名空间也只删该键本身）。 */
static void clear_old(kvspace_t *kv, const char *ok, char **old, int oc,
                      bool drop_container, char *err, uint32_t cap) {
    int n = oc + (drop_container ? 1 : 0);
    if (n <= 0)
        return;
    char **keys = malloc(sizeof *keys * (size_t)n);
    for (int i = 0; i < oc; i++)
        keys[i] = kvlangKeytreeMember(ok, old[i]);
    if (drop_container)
        keys[oc] = kvlangStringFormat("%s", ok);
    for (int i = 0; i < n; i++)
        kvlangBodyInvalidate(keys[i]);
    kvspaceDel(kv->handle, (const char *const *)keys, (uint32_t)n, err, cap);
    for (int i = 0; i < n; i++)
        free(keys[i]);
    free(keys);
}

int kvlangBuiltinObj(kvlangFrame_t *f) {
    kvlangXvalue_t in[64];
    int n = kvlangBuiltinReadInputs(f, in, 64);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    for (int w = 0; w < f->instruction->write_count; w++) {
        char *ok =
            kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name），容器值随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL;
        int oc = 0;
        kvlangKvspaceList(f->kvspace, dir, false, false, &old, &oc);
        clear_old(f->kvspace, ok, old, oc, false, err, sizeof err);
        for (int i = 0; i < oc; i++)
            free(old[i]);
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
        char vbuf[64];
        const char *vk = kvlangDeclElemKind(wty, vbuf, sizeof vbuf);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvspaceSet(f->kvspace, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        int rc = 0;
        for (int i = 0, j = 0; i + 1 < n && rc == 0; i += 2) {
            if (kvlangXvalueNone(&in[i + 1]))
                continue;
            uint8_t cbuf[64];
            kvlangXvalue_t cv = in[i + 1];
            if (vk && kvlangCoerceAssign(f, vk, &in[i + 1], cbuf,
                                         sizeof cbuf, &cv) != 0)
                rc = -1;
            else {
                char *mk = kvlangKeytreeMember(ok, names[j]);
                kvlangKvPair_t p = {mk, cv};
                kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
                free(mk);
            }
            free(names[j]);
            j++;
        }
        free(names);
        free(wty);
        free(ok);
        if (rc != 0) {
            free(fr);
            kvlangBuiltinFreeInputs(in, n);
            return rc;
        }
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
    for (int w = 0; w < f->instruction->write_count; w++) {
        char *ok =
            kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[w].name);
        char err[256];
        /* 重建：清旧成员（p·name）与旧容器值，随后重写。 */
        char *dir = kvlangKeytreeMember(ok, "");
        char **old = NULL;
        int oc = 0;
        kvlangKvspaceList(f->kvspace, dir, false, false, &old, &oc);
        clear_old(f->kvspace, ok, old, oc, true, err, sizeof err);
        for (int i = 0; i < oc; i++)
            free(old[i]);
        free(old);
        free(dir);

        /* 容器值 p：langtype=声明的 map langtype，body 空，dims=[n] 落 head。 */
        char *wty = declared_map_langtype(f, w);
        kvlangXvalue_t mark;
        kvlangBuiltinMapMarker(&mark, wty);
        char vbuf[64];
        const char *vk = kvlangDeclElemKind(wty, vbuf, sizeof vbuf);
        kvlangKvPair_t p0 = {ok, mark};
        kvlangKvspaceSet(f->kvspace, &p0, 1, err, sizeof err);
        kvlangXvalueFree(&mark);
        int rc = 0;
        for (int i = 0; i < n && rc == 0; i++) {
            uint8_t cbuf[64];
            kvlangXvalue_t cv = in[i];
            if (vk && kvlangCoerceAssign(f, vk, &in[i], cbuf, sizeof cbuf,
                                         &cv) != 0)
                rc = -1;
            else {
                int64_t c[1] = {i};
                char *k = kvlangBuiltinScatterKey(ok, c, 1);
                kvlangKvPair_t p = {k, cv};
                kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
                free(k);
            }
        }
        free(wty);
        free(ok);
        if (rc != 0) {
            free(fr);
            kvlangBuiltinFreeInputs(in, n);
            return rc;
        }
    }
    free(fr);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}
