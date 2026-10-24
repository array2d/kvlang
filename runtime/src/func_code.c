#include "func_code.h"
#include "kvcpu_internal.h"

/* 函数级二维指令表：见 func_code.h。
 * 函数层按需加载行；帧层首次执行某行时绑定帧相对操作数的 key 与 64 位地址。 */

static size_t str_hash(const char *s) {
    size_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ull;
    }
    return h;
}

static kvlangFuncCode_t **cache_slot(kvlangFuncCache_t *c,
                                     const char *func_dir) {
    size_t i = str_hash(func_dir) & (c->cap - 1);
    while (c->keys[i] && strcmp(c->keys[i], func_dir) != 0)
        i = (i + 1) & (c->cap - 1);
    return &c->vals[i];
}

static void cache_grow(kvlangFuncCache_t *c) {
    size_t nc = c->cap ? c->cap * 2 : 64;
    char **keys = calloc(nc, sizeof *keys);
    kvlangFuncCode_t **vals = calloc(nc, sizeof *vals);
    for (size_t i = 0; i < c->cap; i++) {
        if (!c->keys[i])
            continue;
        size_t j = str_hash(c->keys[i]) & (nc - 1);
        while (keys[j])
            j = (j + 1) & (nc - 1);
        keys[j] = c->keys[i];
        vals[j] = c->vals[i];
    }
    free(c->keys);
    free(c->vals);
    c->keys = keys;
    c->vals = vals;
    c->cap = nc;
}

void kvlangFuncCacheInit(kvlangFuncCache_t *c) {
    memset(c, 0, sizeof *c);
    cache_grow(c);
}

static void row_free(kvlangCodeRow_t *row) {
    if (!row)
        return;
    free(row->opcode);
    free(row->def_read_sig);
    free(row->def_write_sig);
    for (int i = 0; i < row->nr + row->nw; i++) {
        free(row->cell[i].descriptor);
        free(row->cell[i].type);
        kvlangXvalueFree(&row->cell[i].literal);
    }
    free(row->cell);
    free(row);
}

static void code_free(kvlangFuncCode_t *code) {
    if (!code)
        return;
    for (size_t i = 0; i < code->rows_cap; i++)
        row_free(code->rows[i]);
    free(code->rows);
    free(code->func_dir);
    free(code);
}

void kvlangFuncCacheFree(kvlangFuncCache_t *c) {
    for (size_t i = 0; i < c->cap; i++) {
        if (!c->keys[i])
            continue;
        code_free(c->vals[i]);
        free(c->keys[i]);
    }
    free(c->keys);
    free(c->vals);
    memset(c, 0, sizeof *c);
}

static kvlangFuncCode_t *func_code_get(kvlangFuncCache_t *c,
                                       const char *func_dir) {
    /* ‥lib 存的是 /lib/<pkg>·<name>（无尾 /）；表内统一补尾 / 供 GetMember 拼接。 */
    char *canon = kvlangKeytreeStack(func_dir);
    kvlangFuncCode_t **slot = cache_slot(c, canon);
    if (*slot) {
        free(canon);
        return *slot;
    }
    if (c->count * 4 >= c->cap * 3) {
        cache_grow(c);
        slot = cache_slot(c, canon);
    }
    kvlangFuncCode_t *code = calloc(1, sizeof *code);
    code->func_dir = canon;
    c->keys[slot - c->vals] = strdup(canon);
    *slot = code;
    c->count++;
    return code;
}

static int parse_cell(kvspace_t *kv, const char *func_dir, int s0, int s1,
                      kvlangOperand_t *cell, char *err, uint32_t err_cap) {
    char slot[48];
    snprintf(slot, sizeof slot, "[%d,%d]", s0, s1);
    cell->s1 = s1;
    char *descriptor = NULL, *type = NULL;
    bool is_literal = false, is_absolute_key = false;
    kvlangXvalue_t literal;
    kvlangXvalueZero(&literal);
    int rc =
        kvlangRwirOperandParse(kv, func_dir, slot, &descriptor, &is_literal,
                               &is_absolute_key, &literal, &type, err, err_cap);
    if (rc < 0) {
        return -1;
    }
    if (rc == 0) {
        snprintf(err, err_cap, "Decode: missing operand %s", slot);
        return -1;
    }
    /* 行加载期一次性分配，code_free 统一释放。 */
    cell->type = type;
    if (is_literal) {
        cell->is_literal = true;
        kvlangXvalueCopy(&literal); /* 字面量是 GetMember 的借用视图，拷成行内副本 */
        cell->literal = literal;
    } else {
        cell->descriptor = descriptor;
        cell->key_absolute = is_absolute_key;
    }
    return 0;
}

static kvlangCodeRow_t *row_load(kvspace_t *kv, kvlangFuncCode_t *code, int s0,
                                 char *err, uint32_t err_cap) {
    char slot[48];
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    snprintf(slot, sizeof slot, "[%d,0]", s0);
    if (kvlangKvspaceGetMember(kv, code->func_dir, slot, &v) != 0) {
        snprintf(err, err_cap, "Decode: cannot read opcode at [%d,0]", s0);
        return NULL;
    }
    if (kvlangXvalueNone(&v)) {
        return NULL; /* 无此指令：err 留空 */
    }
    const uint8_t *body = xh_body(v.bytes);
    int32_t len = xh_content_len(v.bytes);
    if (!body || len < 5) {
        snprintf(err, err_cap, "Decode: invalid opcode at [%d,0]", s0);
        return NULL;
    }
    int nr = (int)body[0] | ((int)body[1] << 8);
    int nw = (int)body[2] | ((int)body[3] << 8);
    if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
        snprintf(err, err_cap, "Decode: too many operands at [%d,0]", s0);
        return NULL;
    }
    kvlangCodeRow_t *row = calloc(1, sizeof *row);
    row->opcode = kvlangXvalueValueString(&v);
    row->opcode_id =
        row->opcode ? kvlangOpClassify(row->opcode) : OPID_notinmyrwircaps;
    row->nr = nr;
    row->nw = nw;
    row->load_write_value = kvlangBuiltinLoadWriteValue(row->opcode_id);
    row->cell = calloc((size_t)(nr + nw ? nr + nw : 1), sizeof *row->cell);
    for (int i = 1; i <= nr; i++) {
        if (parse_cell(kv, code->func_dir, s0, -i, &row->cell[i - 1], err,
                       err_cap) != 0)
            goto fail;
    }
    for (int i = 1; i <= nw; i++) {
        if (parse_cell(kv, code->func_dir, s0, i, &row->cell[nr + i - 1], err,
                       err_cap) != 0)
            goto fail;
    }
    /* 路由判定与签名加载只做一次（行加载期固化）。 */
    if (row->opcode_id < 0 && kvlangNotInMyRwirCaps(kv, row->opcode)) {
        row->is_def_rwir = true;
        char *rk = kvlangKeytreeRwir(row->opcode);
        row->def_read_sig =
            load_def_reads(kv, rk, &row->def_read_count, &row->def_dynamic);
        row->def_write_sig = load_def_writes(kv, rk, &row->def_write_count);
        free(rk);
    }
    row->handler_id =
        row->opcode_id >= 0 ? row->opcode_id : kvlangRwirRouteId();
    /* P1b3 typed_core：manifest core 具体项 = 签名列 BIN/CMP/UN（156 项）。
     * 装载期一次固化，执行期不查 manifest。 */
    if (row->opcode_id >= 0) {
        uint32_t nsig = 0;
        const kvlangIsaSig *sg = kvlangIsaSigs(&nsig);
        if ((uint32_t)row->opcode_id < nsig) {
            uint8_t k = sg[row->opcode_id].sig;
            row->typed_core =
                k == KV_ISA_BIN || k == KV_ISA_CMP || k == KV_ISA_UN;
        }
    }
    /* 结果类型只取自同一张能力表；nw>1 不进快路径。 */
    if (row->opcode_id >= 0 && row->nw == 1) {
        const kvlangRwirCap *cap = &kvlangRwirCaps()[row->opcode_id];
        row->result_kind = cap->result_kind;
        row->result_width = cap->result_width;
        if (!row->result_kind) {
            const char *wty = row->cell[row->nr].type;
            if (wty && wty[0] && !strchr(wty, '|') && !strchr(wty, '[')) {
                int tid = kvlangLangtypeId(wty, strlen(wty));
                if (kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) {
                    row->result_kind = kvlangLangtypeKind(tid);
                    row->result_width = (uint32_t)kvlangLangtypeElemSize(tid);
                }
            } else {
                static const char *const bool_ops[] = {
                    "==", "!=", "≠", "<",  ">",  "<=", "≤",
                    ">=", "≥",  "eq", "neq", "lt", "gt", "le", "ge",
                };
                for (size_t k = 0; k < sizeof bool_ops / sizeof bool_ops[0]; k++)
                    if (strcmp(row->opcode, bool_ops[k]) == 0) {
                        row->result_kind = KVSPACE_KIND_BOOL;
                        row->result_width = 1;
                        break;
                    }
                if (!row->result_kind &&
                    (strcmp(row->opcode, "string·ord") == 0 ||
                     strcmp(row->opcode, "string·len") == 0)) {
                    row->result_kind = KVSPACE_KIND_INT64;
                    row->result_width = 8;
                }
                if (!row->result_kind && strcmp(row->opcode, OP_COPY) == 0 &&
                    row->nr == 1) {
                    const char *rty = row->cell[0].type;
                    if (!rty || !rty[0])
                        rty = row->cell[0].is_literal
                                  ? kvlangXvalueKind(&row->cell[0].literal)
                                  : NULL;
                    if (rty && rty[0] && !strchr(rty, '|') &&
                        !strchr(rty, '[')) {
                        int tid = kvlangLangtypeId(rty, strlen(rty));
                        if (kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) {
                            row->result_kind = kvlangLangtypeKind(tid);
                            row->result_width =
                                (uint32_t)kvlangLangtypeElemSize(tid);
                        }
                    }
                }
            }
        }
    }
    return row;

fail:
    row_free(row);
    return NULL;
}

static kvlangCodeRow_t *row_ensure(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                                   char *err, uint32_t err_cap) {
    kvlangFuncCode_t *code = ctx->code;
    /* P1a：函数首次加载行时校验 layout 记录的 ISA hash；rows_cap 非零后
     * 不再重复（每函数一次）。失败经既有 decode 错误路径上报。 */
    if (code->rows_cap == 0 &&
        kvlangIsaCheck(kv, code->func_dir, err, err_cap) != 0)
        return NULL;
    if ((size_t)s0 < code->rows_cap && code->rows[s0])
        return code->rows[s0];
    kvlangCodeRow_t *row = row_load(kv, code, s0, err, err_cap);
    if (!row)
        return NULL;
    if ((size_t)s0 >= code->rows_cap) {
        size_t nc = code->rows_cap ? code->rows_cap * 2 : 32;
        while (nc <= (size_t)s0)
            nc *= 2;
        code->rows = realloc(code->rows, nc * sizeof *code->rows);
        memset(code->rows + code->rows_cap, 0,
               (nc - code->rows_cap) * sizeof *code->rows);
        code->rows_cap = nc;
    }
    code->rows[s0] = row;
    return row;
}

/* ── 定长标量写：body 指针的捕获、失效与全局登记 ────────────────────
 * 只有本 vthread 子树（/vthread/<vid>/）内的 shm 槽可捕获；任何整值写/删除
 * 命中该 key 时精确清掉对应槽的指针（不清别的 key、不动 keyoff 世代）。
 * 状态放在每候选行一个 kvlangBodySlot，不放大每个操作数 cell。 */
#define BODY_BUCKETS 256
static kvlangBodySlot *body_buckets[BODY_BUCKETS];
static uint64_t body_nonempty[4];
static uint32_t body_reg_count;
#ifdef KV_COUNT
unsigned long kvCBodyCells, kvCBodyCellsMax;
unsigned long kvCBodyCapAttempt, kvCBodyCapOk;
#endif

static uint32_t key_hash(const char *k) {
    uint32_t h = 2166136261u;
    while (*k) {
        h ^= (uint8_t)*k++;
        h *= 16777619u;
    }
    return h;
}

static void body_unlink(kvlangBodySlot *slot) {
    if (!slot->reg)
        return;
    uint32_t b = slot->bucket;
    kvlangBodySlot **pp = &body_buckets[b];
    while (*pp && *pp != slot)
        pp = &(*pp)->next;
    if (*pp)
        *pp = slot->next;
    if (!body_buckets[b])
        body_nonempty[b >> 6] &= ~(1ULL << (b & 63));
    slot->reg = false;
    slot->next = NULL;
    body_reg_count--;
#ifdef KV_COUNT
    kvCBodyCells--;
#endif
}

void kvlangBodyInvalidate(const char *key) {
    if (!key || body_reg_count == 0)
        return;
    uint32_t b = key_hash(key) % BODY_BUCKETS;
    for (kvlangBodySlot *c = body_buckets[b]; c; c = c->next)
        if (strcmp(c->key, key) == 0) {
            c->ok = false;
            c->body = NULL;
            c->dead = false;
        }
}

void kvlangBodyInvalidateKind(const char *key, const uint8_t *langtype,
                              uint32_t langtype_len) {
    if (!key || body_reg_count == 0)
        return;
    uint32_t b = key_hash(key) % BODY_BUCKETS;
    for (kvlangBodySlot *c = body_buckets[b]; c; c = c->next)
        if (strcmp(c->key, key) == 0) {
            c->ok = false;
            c->body = NULL;
            size_t kl = strlen(c->kind);
            c->dead = !(langtype && kl == langtype_len &&
                        memcmp(c->kind, langtype, kl) == 0);
        }
}

void kvlangBodyInvalidateTree(const char *prefix) {
    if (!prefix || body_reg_count == 0)
        return;
    size_t n = strlen(prefix);
    for (int w = 0; w < 4; w++) {
        uint64_t mask = body_nonempty[w];
        while (mask) {
            int bit = __builtin_ctzll(mask);
            mask &= mask - 1;
            int b = (w << 6) + bit;
            for (kvlangBodySlot *c = body_buckets[b]; c; c = c->next)
                if (strncmp(c->key, prefix, n) == 0 &&
                    (c->key[n] == 0 || c->key[n] == '/' ||
                     strncmp(c->key + n, RUNTIME_MEMBER_SEP,
                             sizeof RUNTIME_MEMBER_SEP - 1) == 0)) {
                    c->ok = false;
                    c->body = NULL;
                    c->dead = false;
                }
        }
    }
}

/* frame_root 的 /vthread/<vid> 前缀（不含尾部 /）。 */
static bool vthread_root(const char *frame_root, char *out, size_t cap) {
    static const char pfx[] = "/vthread/";
    if (!frame_root || strncmp(frame_root, pfx, sizeof pfx - 1) != 0)
        return false;
    const char *p = frame_root + sizeof pfx - 1;
    const char *q = strchr(p, '/');
    size_t n = q ? (size_t)(q - p) : strlen(p);
    if (n == 0 || sizeof pfx - 1 + n >= cap)
        return false;
    memcpy(out, pfx, sizeof pfx - 1);
    memcpy(out + sizeof pfx - 1, p, n);
    out[sizeof pfx - 1 + n] = 0;
    return true;
}

bool kvlangBodyCapture(kvspace_t *kv, kvlangBodySlot *slot,
                       const char *frame_root) {
    slot->ok = false;
    slot->body = NULL;
    /* 永久不资格（非 shm / 不在本 vthread 子树）：不再重试。 */
    if (!kv || !kv->shm) {
        slot->dead = true;
        return false;
    }
    /* 槽还不存在：不算失败，等写使其 resolved 后再来。 */
    if (!slot->off->resolved)
        return false;
    char root[KV_KEY_MAX];
    bool in_tree = false;
    if (vthread_root(frame_root, root, sizeof root)) {
        size_t rl = strlen(root);
        in_tree = strncmp(slot->key, root, rl) == 0 && slot->key[rl] == '/';
    }
    if (!in_tree) {
        slot->dead = true;
        return false;
    }
    /* 真正的候选槽：失败才算 dead，并登记以便该 key 被写后唤醒重试。 */
    slot->dead = true;
#ifdef KV_COUNT
    kvCBodyCapAttempt++;
#endif
    if (!slot->reg) {
        uint32_t b = key_hash(slot->key) % BODY_BUCKETS;
        slot->bucket = b;
        slot->next = body_buckets[b];
        body_buckets[b] = slot;
        body_nonempty[b >> 6] |= 1ULL << (b & 63);
        slot->reg = true;
        body_reg_count++;
#ifdef KV_COUNT
        if (++kvCBodyCells > kvCBodyCellsMax)
            kvCBodyCellsMax = kvCBodyCells;
#endif
    }
    uint8_t *val = NULL;
    uint32_t len = 0;
    if (kvspaceGetByRef(kv->handle, &slot->off->off, slot->key, &val, &len) !=
            0 ||
        !val || len < XH_PREFIX)
        return false;
    /* 直接解 wire head：所有权守卫已限定本 vthread 自有的 frame-local 槽，
     * ro/vid 恒为 0，无需再去后端取 sidecar。 */
    if (val[0] < 5 || val[0] > 31)
        return false;
    uint32_t headlen = 1u << val[0];
    if ((val[1] & 7u) != 0) /* 必须是 FIXED_SMALL 且非 ptr/ext */
        return false;
    if (headlen < XH_PREFIX || headlen > len)
        return false;
    uint32_t ltlen = 0;
    while (XH_PREFIX + ltlen < headlen && val[XH_PREFIX + ltlen])
        ltlen++;
    if (XH_PREFIX + ltlen >= headlen) /* langtype 必须 NUL 终止 */
        return false;
    size_t kl = strlen(slot->kind);
    if (ltlen != kl || memcmp(val + XH_PREFIX, slot->kind, kl) != 0)
        return false;
    /* FIXED_SMALL：a/b 必须为 0，宽度由 langtype 决定。 */
    if (xh_rd64(val + 2) != 0 || xh_rd64(val + 10) != 0)
        return false;
    if (len != headlen + slot->width)
        return false;
    slot->body = val + headlen;
    slot->len = slot->width;
    slot->ok = true;
    slot->dead = false;
#ifdef KV_COUNT
    kvCBodyCapOk++;
#endif
    return true;
}

/* goto/br：目标 irseq 是 layout 写死的 int64 字面量，本帧内恒定。
 * 绑定期只固化 irseq（不分配字符串）；异常形状保持 0，执行期回退动态路径。 */
static void bind_jump_targets(kvlangCodeRow_t *row, kvlangRowBind_t *b) {
    if (!row->opcode || row->nw != 0)
        return;
    /* br 的读参顺序：cond, true, false；jump_pc[0]=false、[1]=true。 */
    int which[2] = {0, 0};
    int n = 0;
    if (strcmp(row->opcode, OP_GOTO) == 0 && row->nr == 1) {
        which[0] = 0;
        n = 1;
    } else if (strcmp(row->opcode, OP_BR) == 0 && row->nr == 3) {
        which[0] = 2;
        which[1] = 1;
        n = 2;
    } else {
        return;
    }
    for (int k = 0; k < n; k++) {
        kvlangOperand_t *src = &row->cell[which[k]];
        if (!src->is_literal || kvlangXvalueNone(&src->literal) ||
            !kvlangXvalueKindIs(&src->literal, KVSPACE_KIND_INT64))
            continue;
        int64_t ir = kvlangScalarI64(kvlangXvalueScalar(&src->literal));
        if (ir < 1 || ir > 0x7fffffff)
            continue;
        b->jump_irseq[k] = (int)ir;
    }
}

static void bind_free(kvlangRowBind_t *b) {
    if (b->body) {
        if (b->body->reg)
            body_unlink(b->body);
        free(b->body);
    }
    for (int i = 0; i < b->n; i++) {
        free(b->cell[i].key);
        free(b->cell[i].type);
        free(b->cell[i].coerce_buf);
    }
    free(b->cell);
    memset(b, 0, sizeof *b);
}

/* key 是否位于 base 之下（含相等）：写槽会改写 base，其子树的读视图一并失效。 */
static bool key_under(const char *key, const char *base) {
    size_t n = strlen(base);
    if (strncmp(key, base, n) != 0)
        return false;
    if (key[n] == '\0' || key[n] == '/')
        return true;
    return strncmp(key + n, RUNTIME_MEMBER_SEP, sizeof RUNTIME_MEMBER_SEP - 1) ==
           0;
}

static void compute_row_pins(const kvlangCodeRow_t *row, kvlangRowBind_t *b) {
    b->has_pin = false;
    bool xv_set = row->opcode && strcmp(row->opcode, "xv·set") == 0 &&
                  row->nw == 1;
    for (int i = 0; i < row->nr; i++) {
        b->cell[i].pin = false;
        if (b->cell[i].literal || (xv_set && i == 0))
            continue;
        for (int j = 0; j < row->nw; j++)
            if (key_under(b->cell[i].key, b->cell[row->nr + j].key)) {
                b->cell[i].pin = true;
                b->has_pin = true;
                break;
            }
    }
}

/* 帧层绑定：字面量只留行内值（不落 operand 槽、无 key/off）；
 * 描述符按本帧根解析成绝对 key。 */
static int bind_row(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                    kvlangCodeRow_t *row, kvlangRowBind_t *b, char *err,
                    uint32_t err_cap) {
    int n = row->nr + row->nw;
    b->n = n;
    b->cell = calloc((size_t)(n ? n : 1), sizeof *b->cell);
    for (int i = 0; i < n; i++) {
        kvlangOperand_t *src = &row->cell[i];
        kvlangBoundCell_t *cell = &b->cell[i];
        cell->type = src->type ? strdup(src->type) : NULL;
        if (src->is_literal) {
            cell->literal = &src->literal;
            continue;
        }
        if (src->key_absolute)
            cell->key = strdup(src->descriptor);
        else
            cell->key = kvlangRwirOperandTarget(kv, ctx->frame_root,
                                                src->descriptor);
        if (!cell->key) {
            snprintf(err, err_cap,
                     "Runtime: cannot resolve operand %d at [%d,0]", i, s0);
            goto fail;
        }
        /* typed 读视图的 kind id 在绑定期解析一次，热路径不再扫 langtype 表。
         * P1b3：typed_core row 的 kind 由 layout 保证，不建读侧转换视图。 */
        if (!row->typed_core && cell->type && cell->type[0]) {
            int tid = kvlangLangtypeId(cell->type, strlen(cell->type));
            if (kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) {
                cell->coerce_tid = tid;
                cell->coerce_len = (uint16_t)strlen(cell->type);
            }
        }
        /* 变量可能尚未创建：失败留待执行时重试，不阻断加载。 */
        kvspaceKeyOffBind(&cell->off, kv, cell->key);
    }
    /* 候选行：挂一个 body 绑定槽（只此一处分配，不放大每个 cell）。 */
    if (row->result_kind) {
        kvlangBodySlot *slot = calloc(1, sizeof *slot);
        if (!slot) {
            snprintf(err, err_cap, "Runtime: out of memory binding body slot");
            goto fail;
        }
        kvlangBoundCell_t *wc = &b->cell[row->nr];
        slot->key = wc->key;
        slot->kind = row->result_kind;
        slot->width = row->result_width;
        slot->off = &wc->off;
        wc->off.fast = slot;
        b->body = slot;
    }
    compute_row_pins(row, b);
    bind_jump_targets(row, b);
    b->reenter_gen = ctx->reenter_gen;
    b->bound = true;
    return 0;

fail:
    bind_free(b);
    return -1;
}

kvlangFrameCtx_t *kvlangFrameCtxNew(kvlangFuncCache_t *c, const char *func_dir,
                                    const char *frame_root) {
    kvlangFrameCtx_t *ctx = calloc(1, sizeof *ctx);
    ctx->frame_root = strdup(frame_root);
    ctx->code = func_code_get(c, func_dir);
    return ctx;
}

void kvlangFrameCtxSync(kvlangFrameCtx_t *ctx, uint32_t epoch) {
    if (ctx->epoch == epoch)
        return;
    for (size_t i = 0; i < ctx->binds_cap; i++) {
        if (!ctx->binds[i].bound)
            continue;
        for (int j = 0; j < ctx->binds[i].n; j++)
            ctx->binds[i].cell[j].off.resolved = false;
        /* 地址世代前进后槽可能已被删/重建，body 指针同样作废。 */
        if (ctx->binds[i].body) {
            ctx->binds[i].body->ok = false;
            ctx->binds[i].body->body = NULL;
            ctx->binds[i].body->dead = false;
        }
    }
    ctx->epoch = epoch;
}

int kvlangFrameCtxReenter(kvspace_t *kv, kvlangFrameCtx_t *ctx, uint32_t epoch) {
    (void)kv;
    ctx->reenter_gen++;
    ctx->epoch = epoch;
    return 0;
}

__attribute__((noinline)) int
kvlangRowBindReenter(kvspace_t *kv, kvlangFrameCtx_t *ctx,
                     kvlangCodeRow_t *row, kvlangRowBind_t *b) {
    bool has_star = false;
    for (int j = 0; j < b->n; j++) {
        kvlangOperand_t *src = &row->cell[j];
        kvlangBoundCell_t *cell = &b->cell[j];
        if (cell->literal)
            continue;
        if (!src->key_absolute && src->descriptor &&
            src->descriptor[0] == '*') {
            char *nk =
                kvlangRwirOperandTarget(kv, ctx->frame_root, src->descriptor);
            if (!nk)
                return -1;
            if (!cell->key || strcmp(cell->key, nk) != 0) {
                if (b->body && b->body->key == cell->key && b->body->reg)
                    body_unlink(b->body);
                free(cell->key);
                cell->key = nk;
                cell->off.key = nk;
                if (b->body && j == row->nr)
                    b->body->key = nk;
                has_star = true;
            } else {
                free(nk);
            }
        }
        cell->off.resolved =
            kvspaceResolveRef(kv->handle, cell->key, &cell->off.off) == 0;
    }
    if (b->body) {
        b->body->ok = false;
        b->body->body = NULL;
        b->body->dead = false;
    }
    if (has_star)
        compute_row_pins(row, b);
    b->reenter_gen = ctx->reenter_gen;
    return 0;
}

void kvlangFrameCtxFree(kvlangFrameCtx_t *ctx) {
    if (!ctx)
        return;
    for (size_t i = 0; i < ctx->binds_cap; i++) {
        if (ctx->binds[i].bound)
            bind_free(&ctx->binds[i]);
    }
    free(ctx->binds);
    free(ctx->frame_root);
    free(ctx);
}

kvlangRowBind_t *kvlangFrameRow(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                                kvlangCodeRow_t **row_out, char *err,
                                uint32_t err_cap) {
    err[0] = 0;
    if (s0 < 0) {
        snprintf(err, err_cap, "Decode: invalid irseq %d", s0);
        return NULL;
    }
    kvlangCodeRow_t *row = row_ensure(kv, ctx, s0, err, err_cap);
    if (!row)
        return NULL;
    if ((size_t)s0 >= ctx->binds_cap) {
        size_t nc = ctx->binds_cap ? ctx->binds_cap * 2 : 32;
        while (nc <= (size_t)s0)
            nc *= 2;
        ctx->binds = realloc(ctx->binds, nc * sizeof *ctx->binds);
        memset(ctx->binds + ctx->binds_cap, 0,
               (nc - ctx->binds_cap) * sizeof *ctx->binds);
        ctx->binds_cap = nc;
    }
    kvlangRowBind_t *b = &ctx->binds[s0];
    if (!b->bound) {
        if (bind_row(kv, ctx, s0, row, b, err, err_cap) != 0)
            return NULL;
    } else if (b->reenter_gen != ctx->reenter_gen) {
        if (kvlangRowBindReenter(kv, ctx, row, b) != 0) {
            snprintf(err, err_cap, "Runtime: cannot rebind row [%d,0]", s0);
            return NULL;
        }
    }
    *row_out = row;
    return b;
}
