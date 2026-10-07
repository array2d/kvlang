#include "runtime_internal.h"

int kvlangRwirExtractAddr0(const char *coord) {
    const char *p = coord;
    while (*p == '[' || *p == ' ' || *p == '\t')
        p++;
    char *end;
    long n = strtol(p, &end, 10);
    return end == p ? 0 : (int)n;
}

int kvlangRwirNextPc(const char *pc, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int num = kvlangRwirExtractAddr0(slash ? slash + 1 : pc);
    kvlangStrbufPutn(out, pc, plen);
    kvlangStrbufPrintf(out, "[%d,0]", num + 1);
    return 0;
}

/* 免分配版：把 pc 末段 [n,0] 换成 [n+1,0] 写入 buf，返回长度；cap 不足返回 0。
 * 供每指令的 PC 推进热路径复用调用方缓冲，免 strbuf 的 realloc + Printf。 */
size_t kvlangRwirNextPcBuf(const char *pc, char *buf, size_t cap) {
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int num = kvlangRwirExtractAddr0(slash ? slash + 1 : pc);
    char tail[32];
    int tl = snprintf(tail, sizeof tail, "[%d,0]", num + 1);
    if (plen + (size_t)tl + 1 > cap) return 0;
    memcpy(buf, pc, plen);
    memcpy(buf + plen, tail, (size_t)tl + 1);
    return plen + (size_t)tl;
}

void kvlangRwirInstFree(kvlangRwirInst_t *inst) {
    free(inst->opcode);
    for (int i = 0; i < inst->nr; i++) {
        free(inst->reads[i].name);
        free(inst->reads[i].type);
        kvlangXvalueFree(&inst->reads[i].val);
    }
    for (int i = 0; i < inst->nw; i++) {
        free(inst->writes[i].name);
        free(inst->writes[i].type);
        kvlangXvalueFree(&inst->writes[i].val);
    }
    free(inst->reads);
    free(inst->writes);
    inst->opcode = NULL;
    inst->reads = NULL;
    inst->writes = NULL;
    inst->nr = inst->nw = 0;
}

static char *operand_type(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return NULL;
    const char *lt = xh_langtype(v->data);
    if (!kvlangXvalueKindIs(v, KVSPACE_KIND_RWIR) &&
        !kvlangXvalueKindIs(v, KVSPACE_KIND_RWFUNC))
        return strdup(lt);
    const uint8_t *body = xh_body(v->data);
    int32_t len = xh_content_len(v->data);
    const uint8_t *end =
        len > 5 ? memchr(body + 5, 0, (size_t)len - 5) : NULL;
    return end && end + 1 < body + len
               ? strndup((const char *)end + 1, (size_t)(body + len - end - 1))
               : NULL;
}

static int materialize_operand(kvlangKv_t *kv, const char *link_base,
                               const char *frame_root, const char *slot,
                               kvlangXvalue_t *v, char *err, uint32_t err_cap) {
    if (kvlangXvalueIsPtr(v))
        return 0;
    kvlangXvalueMaterialize(v);
    int descriptor = kvlangXvalueKindIs(v, KVSPACE_KIND_RWIR) ||
                     kvlangXvalueKindIs(v, KVSPACE_KIND_RWFUNC);
    char *target = NULL;
    char *type = operand_type(v);
    if (descriptor) {
        char *name = kvlangXvalueSlotName(v);
        if (!name || !name[0]) {
            snprintf(err, err_cap, "Decode: empty operand %s", slot);
            free(name);
            free(type);
            return -1;
        }
        target = kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
        free(name);
        if (target && (!type || strcmp(type, "any") != 0)) {
            kvspaceHead_t actual;
            if (kvlangKvGetHead(kv, target, &actual) == 0 &&
                actual.langtype_len > 0 &&
                (size_t)actual.langtype_len <= sizeof actual.langtype) {
                char *actual_type = strndup((const char *)actual.langtype,
                                            (size_t)actual.langtype_len);
                if (!actual_type) {
                    snprintf(err, err_cap, "Decode: out of memory");
                    free(target);
                    free(type);
                    return -1;
                }
                free(type);
                type = actual_type;
            }
        }
    } else {
        kvlangStrbuf_t literal;
        kvlangStrbufInit(&literal);
        kvlangStrbufPrintf(&literal, "%s%soperand%s", link_base,
                           RUNTIME_MEMBER_SEP, slot);
        target = kvlangStrbufDetach(&literal);
        kvlangKvPair_t pair = {target, *v};
        if (kvlangKvSet(kv, &pair, 1, err, err_cap) != 0) {
            free(target);
            free(type);
            return -1;
        }
    }
    if (!target || !target[0]) {
        snprintf(err, err_cap, "Decode: invalid operand %s", slot);
        free(target);
        free(type);
        return -1;
    }
    kvlangXvalue_t ptr;
    kvlangXvalueZero(&ptr);
    kvlangXvalueNewPtr(&ptr, type ? type : "any", target);
    free(type);
    free(target);
    if (kvlangXvalueNone(&ptr)) {
        snprintf(err, err_cap, "Decode: invalid pointer at %s", slot);
        return -1;
    }
    kvlangStrbuf_t physical;
    kvlangStrbufInit(&physical);
    kvlangStrbufPrintf(&physical, "%s%s", link_base, slot);
    kvlangKvPair_t pair = {physical.p, ptr};
    int rc = kvlangKvSet(kv, &pair, 1, err, err_cap);
    kvlangStrbufFree(&physical);
    if (rc != 0) {
        kvlangXvalueFree(&ptr);
        return -1;
    }
    kvlangXvalueFree(v);
    *v = ptr;
    return 0;
}

static int decode_operand(kvlangKv_t *kv, const char *link_base,
                          const char *frame_root, const char *slot,
                          kvlangParam_t *out, bool read_target,
                          char *err, uint32_t err_cap) {
    memset(out, 0, sizeof *out);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (kvlangKvGetMember(kv, link_base, slot, &v) != 0) {
        snprintf(err, err_cap, "Decode: cannot read %s", slot);
        return -1;
    }
    if (kvlangXvalueNone(&v)) {
        kvlangXvalueFree(&v);
        return 0;
    }
    if (materialize_operand(kv, link_base, frame_root, slot,
                            &v, err, err_cap) != 0) {
        kvlangXvalueFree(&v);
        return -1;
    }
    kvlangXvalueMaterialize(&v);
    out->name = kvlangXvaluePtrTarget(&v);
    if (!out->name || kvlangXvalueNone(&v)) {
        snprintf(err, err_cap, "Decode: invalid pointer at %s", slot);
        kvlangXvalueFree(&v);
        free(out->name);
        out->name = NULL;
        return -1;
    }
    out->type = strdup(xh_langtype(v.data));
    out->address = 1;
    if (!out->type ||
        (read_target && kvlangKvGetOne(kv, out->name, &out->val) != 0)) {
        snprintf(err, err_cap, "Decode: cannot read target for %s", slot);
        free(out->name);
        free(out->type);
        out->name = out->type = NULL;
        kvlangXvalueFree(&out->val);
        kvlangXvalueFree(&v);
        return -1;
    }
    if (read_target)
        kvlangXvalueMaterialize(&out->val);
    kvlangXvalueFree(&v);
    return 1;
}

int kvlangRwirDecode(kvlangKv_t *kv, const char *link_base, const char *pc,
                     kvlangRwirInst_t *out, char *err, uint32_t err_cap) {
    memset(out, 0, sizeof(*out));
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2)
        last = p;
    if (!last) {
        snprintf(err, err_cap, "Decode: invalid pc (no /[coord]): %s", pc);
        return -1;
    }
    int addr0 = kvlangRwirExtractAddr0(last + 1);

    char *frame_root = kvlangKeytreeFrameRoot(pc);

    kvlangXvalue_t v;
    char slot[48];
    int nr = 0, nw = 0;

    snprintf(slot, sizeof slot, "[%d,0]", addr0);
    int opcode_rc = kvlangKvGetMember(kv, link_base, slot, &v);
    if (opcode_rc != 0) {
        snprintf(err, err_cap, "Decode: cannot read opcode at %s", pc);
        goto fail;
    }
    if (!kvlangXvalueNone(&v)) {
        const uint8_t *body = xh_body(v.data);
        int32_t len = xh_content_len(v.data);
        if (!body || len < 5) {
            snprintf(err, err_cap, "Decode: invalid opcode at %s", pc);
            kvlangXvalueFree(&v);
            goto fail;
        }
        nr = (int)body[0] | ((int)body[1] << 8);
        nw = (int)body[2] | ((int)body[3] << 8);
        if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
            snprintf(err, err_cap, "Decode: too many operands at %s", pc);
            kvlangXvalueFree(&v);
            goto fail;
        }
        out->opcode = kvlangXvalueValueString(&v);
    }
    kvlangXvalueFree(&v);
    out->op_id =
        out->opcode ? kvlangOpClassify(out->opcode) : OPID_notinmyrwircaps;
    out->reads = malloc(sizeof(*out->reads) * (size_t)(nr ? nr : 1));
    out->writes = malloc(sizeof(*out->writes) * (size_t)(nw ? nw : 1));
    if (!out->reads || !out->writes) {
        snprintf(err, err_cap, "Decode: out of memory");
        goto fail;
    }

    for (int i = 1; i <= nr; i++) {
        snprintf(slot, sizeof slot, "[%d,-%d]", addr0, i);
        int rc = decode_operand(kv, link_base, frame_root, slot,
                                &out->reads[out->nr], true, err, err_cap);
        if (rc < 0)
            goto fail;
        if (rc == 0) {
            snprintf(err, err_cap, "Decode: missing operand %s", slot);
            goto fail;
        }
        out->nr++;
    }
    bool load_write_value = out->opcode &&
        (strcmp(out->opcode, "obj") == 0 || strcmp(out->opcode, "map") == 0);
    for (int i = 1; i <= nw; i++) {
        snprintf(slot, sizeof slot, "[%d,%d]", addr0, i);
        int rc = decode_operand(kv, link_base, frame_root, slot,
                                &out->writes[out->nw], load_write_value,
                                err, err_cap);
        if (rc < 0)
            goto fail;
        if (rc == 0) {
            snprintf(err, err_cap, "Decode: missing operand %s", slot);
            goto fail;
        }
        out->nw++;
    }

    free(frame_root);
    return 0;

fail:
    free(frame_root);
    kvlangRwirInstFree(out);
    return -1;
}
