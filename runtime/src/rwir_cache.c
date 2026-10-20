#include "rwir_cache.h"
#include "kvcpu_internal.h"

/* ── 指令派生数据缓存（消掉执行循环的每步 rwirDecode）────────────────────
 * 只缓存「派生数据」：opcode / opcode_id / read_count / write_count / 各操作数的 name·type·is_address
 * 与 load_write_value（该指令的写参是否要取值）。值（reads[].value / writes[].value）
 * 永不入缓存——真相源恒在 KVSpace，每步现读。故缓存随时可丢：删掉它只是回到每步
 * decode，崩溃恢复语义不变。
 * 键 = PC 绝对路径 `/vthread/<vtid>/[d]/[addr,seq]`，含 vthread 与帧坐标，帧内唯一。
 * 失效：call/return 后同深度帧会被整棵重建，故帧深变化即按帧作废——
 *   变深（call）→ 丢弃 depth >= 新深度（新帧全新）；
 *   变浅（return）→ 丢弃 depth > 新深度（更深的帧已随返回删除）。
 * PC 不可换「函数模板」键：操作数目标依赖「哪条调用路径到达本帧」——同为 [6]/[4,0]，
 * 经 fib(a) 与 fib(b) 到达时目标分别是 [5]/a 与 [5]/b——故跨调用路径复用会误用。
 * 帧深变化即作废正是为此。opcode/read_count/write_count 一类与实参无关的骨架数据另可按 (模板, addr0)
 * 缓存，但实测收益甚微（瓶颈在操作数物化，不在骨架），故未采用。
 * 路由判定 is_def_rwir 与 def rwir 读/写签名同样在 decode 期固化，执行循环不再每步查
 * /lib 或拼签名串；委托判定保持动态（backend 上下线）。 */

static size_t kvlangRwirCacheHash(const char *s) {
    size_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ull;
    }
    return h;
}

/* 开放寻址：返回首个空槽或同键槽。cap 必须非 0。 */
static rwir_cache_entry_t *cache_probe(rwir_cache_t *c, const char *pc) {
    size_t i = kvlangRwirCacheHash(pc) & (c->capacity - 1);
    while (c->slots[i].pc && strcmp(c->slots[i].pc, pc) != 0)
        i = (i + 1) & (c->capacity - 1);
    return &c->slots[i];
}

rwir_cache_entry_t *kvlangRwirCacheGet(rwir_cache_t *c, const char *pc) {
    if (c->capacity == 0)
        return NULL;
    rwir_cache_entry_t *e = cache_probe(c, pc);
    return e->pc ? e : NULL;
}

static void kvlangRwirCacheEntryFree(rwir_cache_entry_t *e) {
    free(e->pc);
    free(e->def_read_sig);
    free(e->def_write_sig);
    kvlangRwirInstructionFree(&e->instruction); /* 缓存 val 恒空，free 安全 */
    *e = (rwir_cache_entry_t){0};
}

static void kvlangRwirCacheRehash(rwir_cache_t *c, size_t cap) {
    rwir_cache_entry_t *old = c->slots;
    size_t old_cap = c->capacity;
    c->slots = calloc(cap, sizeof *c->slots);
    c->capacity = cap;
    c->count = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].pc)
            continue;
        size_t j = kvlangRwirCacheHash(old[i].pc) & (cap - 1);
        while (c->slots[j].pc)
            j = (j + 1) & (cap - 1);
        c->slots[j] = old[i];
        c->count++;
    }
    free(old);
}

static void cache_copy_params(kvlangParam_t *dst, const kvlangParam_t *src,
                              int n) {
    for (int i = 0; i < n; i++) {
        dst[i].name = strdup(src[i].name);
        dst[i].type = src[i].type ? strdup(src[i].type) : NULL;
        dst[i].is_address = src[i].is_address;
        kvlangXvalueZero(&dst[i].value);
    }
}

rwir_cache_entry_t *kvlangRwirCachePut(kvspace_t *kv, rwir_cache_t *c,
                                   const char *pc, int depth,
                                   const kvlangRwirInst_t *inst) {
    if (!inst->opcode || !inst->opcode[0])
        return NULL;
    if (c->capacity == 0)
        kvlangRwirCacheRehash(c, 256);
    else if (c->count * 4 >= c->capacity * 3)
        kvlangRwirCacheRehash(c, c->capacity * 2);
    rwir_cache_entry_t *e = cache_probe(c, pc);
    if (e->pc)
        kvlangDie("rwir cache put on existing key: %s", pc); /* 调用方已先 get */
    c->count++;
    e->pc = strdup(pc);
    e->depth = depth;
    e->load_write_value = kvlangBuiltinLoadWriteValue(inst->opcode_id);
    e->instruction.opcode = strdup(inst->opcode);
    e->instruction.opcode_id = inst->opcode_id;
    e->instruction.read_count = inst->read_count;
    e->instruction.write_count = inst->write_count;
    e->instruction.reads = inst->read_count
                        ? malloc(sizeof(kvlangParam_t) * (size_t)inst->read_count)
                        : NULL;
    e->instruction.writes = inst->write_count
                         ? malloc(sizeof(kvlangParam_t) * (size_t)inst->write_count)
                         : NULL;
    cache_copy_params(e->instruction.reads, inst->reads, inst->read_count);
    cache_copy_params(e->instruction.writes, inst->writes, inst->write_count);
    /* 路由判定与签名加载只做一次：native（opcode_id>=0）走不到 /lib，跳过 KV 查询。 */
    e->is_def_rwir = inst->opcode_id < 0 && kvlangNotInMyRwirCaps(kv, inst->opcode);
    if (e->is_def_rwir) {
        char *rk = kvlangKeytreeRwir(inst->opcode);
        e->def_read_sig = load_def_reads(kv, rk, &e->def_read_count, &e->def_dynamic);
        e->def_write_sig = load_def_writes(kv, rk, &e->def_write_count);
        free(rk);
    }
    return e;
}

void kvlangRwirCacheDrop(rwir_cache_t *c, int floor, bool inclusive) {
    for (size_t i = 0; i < c->capacity; i++) {
        rwir_cache_entry_t *e = &c->slots[i];
        if (e->pc && (inclusive ? e->depth >= floor : e->depth > floor)) {
            kvlangRwirCacheEntryFree(e);
            c->count--;
        }
    }
}

void kvlangRwirCacheFree(rwir_cache_t *c) {
    for (size_t i = 0; i < c->capacity; i++) {
        if (c->slots[i].pc)
            kvlangRwirCacheEntryFree(&c->slots[i]);
    }
    free(c->slots);
}

static int cache_load_param_vals(kvspace_t *kv, kvlangParam_t *p, int n,
                                 char *err, uint32_t err_cap) {
    for (int i = 0; i < n; i++) {
        if (kvlangKvspaceGetOne(kv, p[i].name, &p[i].value) != 0) {
            snprintf(err, err_cap, "Decode: cannot read target for %s",
                     p[i].name);
            return -1;
        }
        kvlangXvalueMaterialize(&p[i].value);
    }
    return 0;
}

/* 命中后按 name 现读值：缓存只存派生数据，值必须每步回 KVSpace 取。
 * 读参恒取值；写参与 decode 一致——仅 obj/map 指令取值。 */
int kvlangRwirCacheLoadValues(kvspace_t *kv, kvlangRwirInst_t *inst,
                         bool load_write_value, char *err, uint32_t err_cap) {
    if (cache_load_param_vals(kv, inst->reads, inst->read_count, err, err_cap) != 0)
        return -1;
    if (load_write_value &&
        cache_load_param_vals(kv, inst->writes, inst->write_count, err, err_cap) != 0)
        return -1;
    return 0;
}

void rwir_instruction_release(kvlangRwirInst_t *inst, bool from_cache) {
    if (!from_cache) {
        kvlangRwirInstructionFree(inst);
        return;
    }
    for (int i = 0; i < inst->read_count; i++)
        kvlangXvalueFree(&inst->reads[i].value);
    for (int i = 0; i < inst->write_count; i++)
        kvlangXvalueFree(&inst->writes[i].value);
}
