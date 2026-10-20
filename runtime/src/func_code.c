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
        kvlangOperand_t *c = &row->cell[i];
        free(c->descriptor);
        kvlangXvalueFree(&c->literal);
        free(c->type);
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
        kvlangXvalueFree(&literal);
        return -1;
    }
    if (rc == 0) {
        snprintf(err, err_cap, "Decode: missing operand %s", slot);
        return -1;
    }
    cell->type = type;
    if (is_literal) {
        cell->is_literal = true;
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
        kvlangXvalueFree(&v);
        return NULL; /* 无此指令：err 留空 */
    }
    const uint8_t *body = xh_body(v.bytes);
    int32_t len = xh_content_len(v.bytes);
    if (!body || len < 5) {
        snprintf(err, err_cap, "Decode: invalid opcode at [%d,0]", s0);
        kvlangXvalueFree(&v);
        return NULL;
    }
    int nr = (int)body[0] | ((int)body[1] << 8);
    int nw = (int)body[2] | ((int)body[3] << 8);
    if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
        snprintf(err, err_cap, "Decode: too many operands at [%d,0]", s0);
        kvlangXvalueFree(&v);
        return NULL;
    }
    kvlangCodeRow_t *row = calloc(1, sizeof *row);
    row->opcode = kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
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
    return row;

fail:
    row_free(row);
    return NULL;
}

static kvlangCodeRow_t *row_ensure(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                                   char *err, uint32_t err_cap) {
    kvlangFuncCode_t *code = ctx->code;
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

static void bind_free(kvlangRowBind_t *b) {
    for (int i = 0; i < b->n; i++) {
        free(b->cell[i].key);
        free(b->cell[i].type);
    }
    free(b->cell);
    memset(b, 0, sizeof *b);
}

/* 帧层绑定：字面量落回帧内 operand 槽；描述符按本帧根解析成绝对 key。 */
static int bind_row(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                    kvlangCodeRow_t *row, kvlangRowBind_t *b, char *err,
                    uint32_t err_cap) {
    int n = row->nr + row->nw;
    char *stack = kvlangKeytreeStack(ctx->frame_root);
    b->n = n;
    b->cell = calloc((size_t)(n ? n : 1), sizeof *b->cell);
    for (int i = 0; i < n; i++) {
        kvlangOperand_t *src = &row->cell[i];
        kvlangBoundCell_t *cell = &b->cell[i];
        if (src->is_literal) {
            cell->literal = &src->literal;
            char slot[48];
            snprintf(slot, sizeof slot, "[%d,%d]", s0, src->s1);
            cell->key = kvlangStringFormat("%s%soperand%s", stack,
                                           RUNTIME_MEMBER_SEP, slot);
            kvlangKvPair_t pair = {cell->key, src->literal};
            if (kvlangKvspaceSet(kv, &pair, 1, err, err_cap) != 0)
                goto fail;
            cell->type = src->type ? strdup(src->type) : NULL;
        } else {
            cell->type = src->type ? strdup(src->type) : NULL;
            if (src->key_absolute)
                cell->key = strdup(src->descriptor);
            else
                cell->key = kvlangRwirOperandTarget(
                    kv, ctx->frame_root, src->descriptor, &cell->type);
        }
        if (!cell->key) {
            snprintf(err, err_cap,
                     "Runtime: cannot resolve operand %d at [%d,0]", i, s0);
            goto fail;
        }
        /* 变量可能尚未创建：失败留待执行时重试，不阻断加载。 */
        kvspaceKeyOffBind(&cell->off, kv, cell->key);
    }
    free(stack);
    b->bound = true;
    return 0;

fail:
    free(stack);
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
    }
    ctx->epoch = epoch;
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
    if (!b->bound && bind_row(kv, ctx, s0, row, b, err, err_cap) != 0)
        return NULL;
    *row_out = row;
    return b;
}
