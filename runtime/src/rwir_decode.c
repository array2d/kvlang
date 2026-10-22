#include "runtime_internal.h"

int kvlangRwirExtractAddress(const char *coord) {
    const char *p = coord;
    while (*p == '[' || *p == ' ' || *p == '\t')
        p++;
    char *end;
    long n = strtol(p, &end, 10);
    return end == p ? 0 : (int)n;
}

/* 把 pc 末段 [n,0] 换成 [n+1,0] 写入 buf，返回长度；cap 不足返回 0。
 * 供每指令的 PC 推进热路径复用调用方缓冲。 */
size_t kvlangRwirNextPcBuffer(const char *pc, char *buf, size_t cap) {
    const char *slash = strrchr(pc, '/');
    size_t plen = slash ? (size_t)(slash - pc) + 1 : 0;
    int num = kvlangRwirExtractAddress(slash ? slash + 1 : pc);
    char tail[32];
    int tl = snprintf(tail, sizeof tail, "[%d,0]", num + 1);
    if (plen + (size_t)tl + 1 > cap)
        return 0;
    memcpy(buf, pc, plen);
    memcpy(buf + plen, tail, (size_t)tl + 1);
    return plen + (size_t)tl;
}

void kvlangRwirInstructionFree(kvlangRwirInst_t *inst) {
    free(inst->opcode);
    for (int i = 0; i < inst->read_count; i++) {
        free(inst->reads[i].name);
        free(inst->reads[i].type);
    }
    for (int i = 0; i < inst->write_count; i++) {
        free(inst->writes[i].name);
        free(inst->writes[i].type);
    }
    free(inst->reads);
    free(inst->writes);
    inst->opcode = NULL;
    inst->reads = NULL;
    inst->writes = NULL;
    inst->read_count = inst->write_count = 0;
}

static char *operand_type(const kvlangXvalue_t *v) {
    if (kvlangXvalueNone(v))
        return NULL;
    if (!kvlangXvalueKindIs(v, KVSPACE_KIND_RWIR) &&
        !kvlangXvalueKindIs(v, KVSPACE_KIND_RWFUNC))
        return kvlangXvalueLangtypeDup(v);
    const uint8_t *body = xh_body(v->bytes);
    int32_t len = xh_content_len(v->bytes);
    const uint8_t *end = len > 5 ? memchr(body + 5, 0, (size_t)len - 5) : NULL;
    return end && end + 1 < body + len
               ? strndup((const char *)end + 1, (size_t)(body + len - end - 1))
               : NULL;
}

/* 解析一个操作数槽，供单条 decode 与函数级行加载共用。
 * 返回 1=有操作数；0=None；-1=错误。
 *   literal       -> *out_literal（值，owned），*out_descriptor=NULL
 *   Ptr           -> *out_descriptor=Ptr 目标（owned），*out_is_absolute_key=true
 *   rwir/rwfunc   -> *out_descriptor=槽名（owned），*out_is_absolute_key=false
 * *out_type = 声明类型（owned）。 */
int kvlangRwirOperandParse(kvspace_t *kv, const char *link_base,
                           const char *slot, char **out_descriptor,
                           bool *out_is_literal, bool *out_is_absolute_key,
                           kvlangXvalue_t *out_literal, char **out_type,
                           char *err, uint32_t err_cap) {
    *out_descriptor = NULL;
    *out_is_literal = false;
    *out_is_absolute_key = false;
    kvlangXvalueZero(out_literal);
    *out_type = NULL;
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (kvlangKvspaceGetMember(kv, link_base, slot, &v) != 0) {
        snprintf(err, err_cap, "Decode: cannot read %s", slot);
        return -1;
    }
    if (kvlangXvalueNone(&v)) {
        return 0;
    }
    *out_type = operand_type(&v);
    if (kvlangXvalueIsPtr(&v)) {
        *out_descriptor = kvlangXvaluePtrTarget(&v);
        *out_is_absolute_key = true;
    } else if (kvlangXvalueKindIs(&v, KVSPACE_KIND_RWIR) ||
               kvlangXvalueKindIs(&v, KVSPACE_KIND_RWFUNC)) {
        *out_descriptor = kvlangXvalueSlotName(&v);
    } else {
        *out_is_literal = true;
        *out_literal = v;
        kvlangXvalueZero(&v);
    }
    if (!*out_is_literal && (!*out_descriptor || !(*out_descriptor)[0])) {
        snprintf(err, err_cap, "Decode: empty operand %s", slot);
        free(*out_descriptor);
        *out_descriptor = NULL;
        free(*out_type);
        *out_type = NULL;
        return -1;
    }
    return 1;
}

/* 描述符 -> 当前帧的绝对 key。类型不在此细化：槽 langtype 只取声明
 * （[[赋值转换]]），动态槽保持无声明，handler 按实际 kind 走通用回退。 */
char *kvlangRwirOperandTarget(kvspace_t *kv, const char *frame_root,
                              const char *descriptor) {
    return kvlangBuiltinResolveWriteSlot(kv, frame_root, descriptor);
}

static int decode_operand(kvspace_t *kv, const char *link_base,
                          const char *frame_root, const char *slot,
                          kvlangParam_t *out, bool read_target, char *err,
                          uint32_t err_cap) {
    memset(out, 0, sizeof *out);
    char *descriptor = NULL, *type = NULL;
    bool is_literal = false, is_absolute_key = false;
    kvlangXvalue_t literal;
    kvlangXvalueZero(&literal);
    int rc =
        kvlangRwirOperandParse(kv, link_base, slot, &descriptor, &is_literal,
                               &is_absolute_key, &literal, &type, err, err_cap);
    if (rc <= 0)
        return rc;
    char *target;
    if (is_literal) {
        target = kvlangStringFormat("%s%soperand%s", link_base,
                                    RUNTIME_MEMBER_SEP, slot);
        kvlangKvPair_t pair = {target, literal};
        if (kvlangKvspaceSet(kv, &pair, 1, err, err_cap) != 0) {
            free(target);
            free(type);
            return -1;
        }
    } else if (is_absolute_key) {
        target = descriptor;
    } else {
        target = kvlangRwirOperandTarget(kv, frame_root, descriptor);
        free(descriptor);
    }
    if (!target || !target[0]) {
        snprintf(err, err_cap, "Decode: invalid operand %s", slot);
        free(target);
        free(type);
        return -1;
    }
    out->name = target;
    out->type = type;
    out->is_address = 1;
    if (read_target && kvlangKvspaceGetOne(kv, out->name, &out->value) != 0) {
        snprintf(err, err_cap, "Decode: cannot read target for %s", slot);
        free(out->name);
        free(out->type);
        out->name = out->type = NULL;
        return -1;
    }
    return 1;
}

int kvlangRwirDecode(kvspace_t *kv, const char *link_base, const char *pc,
                     kvlangRwirInst_t *out, char *err, uint32_t err_cap) {
    memset(out, 0, sizeof(*out));
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2)
        last = p;
    if (!last) {
        snprintf(err, err_cap, "Decode: invalid pc (no /[coord]): %s", pc);
        return -1;
    }
    int addr0 = kvlangRwirExtractAddress(last + 1);

    char *frame_root = kvlangKeytreeFrameRoot(pc);

    kvlangXvalue_t v;
    char slot[48];
    int nr = 0, nw = 0;

    snprintf(slot, sizeof slot, "[%d,0]", addr0);
    int opcode_rc = kvlangKvspaceGetMember(kv, link_base, slot, &v);
    if (opcode_rc != 0) {
        snprintf(err, err_cap, "Decode: cannot read opcode at %s", pc);
        goto fail;
    }
    if (!kvlangXvalueNone(&v)) {
        const uint8_t *body = xh_body(v.bytes);
        int32_t len = xh_content_len(v.bytes);
        if (!body || len < 5) {
            snprintf(err, err_cap, "Decode: invalid opcode at %s", pc);
            goto fail;
        }
        nr = (int)body[0] | ((int)body[1] << 8);
        nw = (int)body[2] | ((int)body[3] << 8);
        if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
            snprintf(err, err_cap, "Decode: too many operands at %s", pc);
            goto fail;
        }
        out->opcode = kvlangXvalueValueString(&v);
    }
    out->opcode_id =
        out->opcode ? kvlangOpClassify(out->opcode) : OPID_notinmyrwircaps;
    out->reads = malloc(sizeof(*out->reads) * (size_t)(nr ? nr : 1));
    out->writes = malloc(sizeof(*out->writes) * (size_t)(nw ? nw : 1));
    if (!out->reads || !out->writes) {
        snprintf(err, err_cap, "Decode: out of memory");
        goto fail;
    }

    for (int i = 1; i <= nr; i++) {
        snprintf(slot, sizeof slot, "[%d,-%d]", addr0, i);
        int rc =
            decode_operand(kv, link_base, frame_root, slot,
                           &out->reads[out->read_count], true, err, err_cap);
        if (rc < 0)
            goto fail;
        if (rc == 0) {
            snprintf(err, err_cap, "Decode: missing operand %s", slot);
            goto fail;
        }
        out->read_count++;
    }
    bool load_write_value = kvlangBuiltinLoadWriteValue(out->opcode_id);
    for (int i = 1; i <= nw; i++) {
        snprintf(slot, sizeof slot, "[%d,%d]", addr0, i);
        int rc = decode_operand(kv, link_base, frame_root, slot,
                                &out->writes[out->write_count],
                                load_write_value, err, err_cap);
        if (rc < 0)
            goto fail;
        if (rc == 0) {
            snprintf(err, err_cap, "Decode: missing operand %s", slot);
            goto fail;
        }
        out->write_count++;
    }

    free(frame_root);
    return 0;

fail:
    free(frame_root);
    kvlangRwirInstructionFree(out);
    return -1;
}
