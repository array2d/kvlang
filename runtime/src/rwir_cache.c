#include "rwir_cache.h"
#include "kvcpu_internal.h"

/* ── 指令派生数据缓存（消掉执行循环的每步 rwirDecode）────────────────────
 * 只缓存「派生数据」：opcode / op_id / nr / nw / 各操作数的 name·type·address
 * 与 load_write_value（该指令的写参是否要取值）。值（reads[].val / writes[].val）
 * 永不入缓存——真相源恒在 KVSpace，每步现读。故缓存随时可丢：删掉它只是回到每步
 * decode，崩溃恢复语义不变。
 * 键 = PC 绝对路径 `/vthread/<vtid>/[d]/[addr,seq]`，含 vthread 与帧坐标，帧内唯一。
 * 失效：call/return 后同深度帧会被整棵重建，故帧深变化即按帧作废——
 *   变深（call）→ 丢弃 depth >= 新深度（新帧全新）；
 *   变浅（return）→ 丢弃 depth > 新深度（更深的帧已随返回删除）。
 * PC 不可换「函数模板」键：操作数目标依赖「哪条调用路径到达本帧」——同为 [6]/[4,0]，
 * 经 fib(a) 与 fib(b) 到达时目标分别是 [5]/a 与 [5]/b——故跨调用路径复用会误用。
 * 帧深变化即作废正是为此。opcode/nr/nw 一类与实参无关的骨架数据另可按 (模板, addr0)
 * 缓存，但实测收益甚微（瓶颈在操作数物化，不在骨架），故未采用。
 * 路由判定 is_def_rwir 与 def rwir 读/写签名同样在 decode 期固化，执行循环不再每步查
 * /lib 或拼签名串；委托判定保持动态（backend 上下线）。 */

static size_t rwir_cache_hash(const char *s) {
    size_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ull;
    }
    return h;
}

/* 开放寻址：返回首个空槽或同键槽。cap 必须非 0。 */
static rwir_cache_entry_t *cache_probe(rwir_cache_t *c, const char *pc) {
    size_t i = rwir_cache_hash(pc) & (c->cap - 1);
    while (c->slot[i].pc && strcmp(c->slot[i].pc, pc) != 0)
        i = (i + 1) & (c->cap - 1);
    return &c->slot[i];
}

rwir_cache_entry_t *rwir_cache_get(rwir_cache_t *c, const char *pc) {
    if (c->cap == 0)
        return NULL;
    rwir_cache_entry_t *e = cache_probe(c, pc);
    return e->pc ? e : NULL;
}

static void rwir_cache_entry_free(rwir_cache_entry_t *e) {
    free(e->pc);
    free(e->def_sig);
    free(e->def_wsig);
    kvlangRwirInstFree(&e->inst); /* 缓存 val 恒空，free 安全 */
    *e = (rwir_cache_entry_t){0};
}

static void rwir_cache_rehash(rwir_cache_t *c, size_t cap) {
    rwir_cache_entry_t *old = c->slot;
    size_t old_cap = c->cap;
    c->slot = calloc(cap, sizeof *c->slot);
    c->cap = cap;
    c->n = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].pc)
            continue;
        size_t j = rwir_cache_hash(old[i].pc) & (cap - 1);
        while (c->slot[j].pc)
            j = (j + 1) & (cap - 1);
        c->slot[j] = old[i];
        c->n++;
    }
    free(old);
}

static void cache_copy_params(kvlangParam_t *dst, const kvlangParam_t *src,
                              int n) {
    for (int i = 0; i < n; i++) {
        dst[i].name = strdup(src[i].name);
        dst[i].type = src[i].type ? strdup(src[i].type) : NULL;
        dst[i].address = src[i].address;
        kvlangXvalueZero(&dst[i].val);
    }
}

rwir_cache_entry_t *rwir_cache_put(kvlangKv_t *kv, rwir_cache_t *c,
                                   const char *pc, int depth,
                                   const kvlangRwirInst_t *inst) {
    if (!inst->opcode || !inst->opcode[0])
        return NULL;
    if (c->cap == 0)
        rwir_cache_rehash(c, 256);
    else if (c->n * 4 >= c->cap * 3)
        rwir_cache_rehash(c, c->cap * 2);
    rwir_cache_entry_t *e = cache_probe(c, pc);
    if (e->pc)
        kvlangDie("rwir cache put on existing key: %s", pc); /* 调用方已先 get */
    c->n++;
    e->pc = strdup(pc);
    e->depth = depth;
    e->load_write_value = kvlangBuiltinLoadWriteValue(inst->op_id);
    e->inst.opcode = strdup(inst->opcode);
    e->inst.op_id = inst->op_id;
    e->inst.nr = inst->nr;
    e->inst.nw = inst->nw;
    e->inst.reads = inst->nr
                        ? malloc(sizeof(kvlangParam_t) * (size_t)inst->nr)
                        : NULL;
    e->inst.writes = inst->nw
                         ? malloc(sizeof(kvlangParam_t) * (size_t)inst->nw)
                         : NULL;
    cache_copy_params(e->inst.reads, inst->reads, inst->nr);
    cache_copy_params(e->inst.writes, inst->writes, inst->nw);
    /* 路由判定与签名加载只做一次：native（op_id>=0）走不到 /lib，跳过 KV 查询。 */
    e->is_def_rwir = inst->op_id < 0 && notinmyrwircaps(kv, inst->opcode);
    if (e->is_def_rwir) {
        char *rk = kvlangKeytreeRwir(inst->opcode);
        e->def_sig = load_def_reads(kv, rk, &e->def_nr, &e->def_dyn);
        e->def_wsig = load_def_writes(kv, rk, &e->def_nw);
        free(rk);
    }
    return e;
}

void rwir_cache_drop(rwir_cache_t *c, int floor, bool inclusive) {
    for (size_t i = 0; i < c->cap; i++) {
        rwir_cache_entry_t *e = &c->slot[i];
        if (e->pc && (inclusive ? e->depth >= floor : e->depth > floor)) {
            rwir_cache_entry_free(e);
            c->n--;
        }
    }
}

void rwir_cache_free(rwir_cache_t *c) {
    for (size_t i = 0; i < c->cap; i++) {
        if (c->slot[i].pc)
            rwir_cache_entry_free(&c->slot[i]);
    }
    free(c->slot);
}

static int cache_load_param_vals(kvlangKv_t *kv, kvlangParam_t *p, int n,
                                 char *err, uint32_t err_cap) {
    for (int i = 0; i < n; i++) {
        if (kvlangKvGetOne(kv, p[i].name, &p[i].val) != 0) {
            snprintf(err, err_cap, "Decode: cannot read target for %s",
                     p[i].name);
            return -1;
        }
        kvlangXvalueMaterialize(&p[i].val);
    }
    return 0;
}

/* 命中后按 name 现读值：缓存只存派生数据，值必须每步回 KVSpace 取。
 * 读参恒取值；写参与 decode 一致——仅 obj/map 指令取值。 */
int rwir_cache_load_vals(kvlangKv_t *kv, kvlangRwirInst_t *inst,
                         bool load_write_value, char *err, uint32_t err_cap) {
    if (cache_load_param_vals(kv, inst->reads, inst->nr, err, err_cap) != 0)
        return -1;
    if (load_write_value &&
        cache_load_param_vals(kv, inst->writes, inst->nw, err, err_cap) != 0)
        return -1;
    return 0;
}

void rwir_inst_release(kvlangRwirInst_t *inst, bool from_cache) {
    if (!from_cache) {
        kvlangRwirInstFree(inst);
        return;
    }
    for (int i = 0; i < inst->nr; i++)
        kvlangXvalueFree(&inst->reads[i].val);
    for (int i = 0; i < inst->nw; i++)
        kvlangXvalueFree(&inst->writes[i].val);
}
