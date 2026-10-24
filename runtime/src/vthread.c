#include "runtime_internal.h"
#include "slotview.h"

#define VT_MEMBER_GET(fn, want_status)                                         \
    void fn(kvspace_t *kv, const char *vtid, char **out) {                     \
        *out = NULL;                                                           \
        char k[KV_KEY_MAX];                                                    \
        if (want_status)                                                       \
            kvlangKeytreeVthreadStatus(vtid, k, sizeof k);                     \
        else                                                                   \
            kvlangKeytreeVthreadPc(vtid, k, sizeof k);                         \
        kvlangXvalue_t v;                                                      \
        kvlangXvalueZero(&v);                                                  \
        kvlangKvspaceGetOne(kv, k, &v);                                        \
        if (!kvlangXvalueNone(&v))                                             \
            *out = kvlangXvalueValueString(&v);                                \
    }

/* Read PC and status separately from KVSpace. */
VT_MEMBER_GET(kvlangVthreadPcGet, false)
VT_MEMBER_GET(kvlangVthreadStatusGet, true)

/* 系统变量值 → 调用方缓冲；false = 键缺失或 None，超长直接 abort。 */
static bool value_to_buffer(kvlangXvalue_t *v, char *out, size_t cap) {
    if (kvlangXvalueNone(v)) {
        return false;
    }
    char *tmp = NULL;
    const char *src;
    size_t n;
    if (kvlangXvalueKindIs(v, KVSPACE_KIND_CHAR_UTF8)) {
        src = (const char *)xh_body(v->bytes);
        n = (size_t)xh_content_len(v->bytes);
    } else {
        tmp = kvlangXvalueValueString(v);
        src = tmp;
        n = strlen(tmp);
    }
    if (n + 1 > cap)
        kvlangDie("system var value exceeds %zu bytes", cap);
    memcpy(out, src, n);
    out[n] = 0;
    free(tmp);
    return true;
}

/* 绑定：key 由调用方持有；只缓存 key 的 64 位地址，值仍每次读。 */
void kvspaceKeyOffBind(kvspaceKeyOff *ko, kvspace_t *kvspace, const char *key) {
    ko->kvspace = kvspace;
    ko->key = key;
    ko->fast = NULL;
    ko->resolved = kvspaceResolveRef(kvspace->handle, key, &ko->off) == 0;
}

bool kvspaceKeyOffRead(kvspaceKeyOff *ko, char *out, size_t cap) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (ko->resolved) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGetByRef(ko->kvspace->handle, &ko->off, ko->key, &d, &len) !=
            0)
            return false;
        if (d && len > 0) {
            v.bytes = d;
            v.length = len;
        }
    } else if (kvlangKvspaceGetOne(ko->kvspace, ko->key, &v) != 0) {
        return false;
    }
    return value_to_buffer(&v, out, cap);
}

/* 整值写：已解析走 SetValueByRef；未解析/失败退回按 key 写并补一次绑定。 */
int kvspaceKeyOffSet(kvspaceKeyOff *ko, const uint8_t *val, uint32_t len,
                     char *err, uint32_t err_cap) {
    if (ko->resolved &&
        kvspaceSetValueByRef(ko->kvspace->handle, &ko->off, ko->key, val, len,
                             0, 0, err, err_cap) == 0)
        return 0;
    if (kvspaceSetValue(ko->kvspace->handle, ko->key, val, len, 0, 0, err,
                        err_cap) != 0)
        return -1;
    if (kvspaceResolveRef(ko->kvspace->handle, ko->key, &ko->off) == 0)
        ko->resolved = true;
    return 0;
}

void kvlangVthreadGet(kvspace_t *kv, const char *vtid, char **pc,
                      char **status) {
    kvlangVthreadPcGet(kv, vtid, pc);
    kvlangVthreadStatusGet(kv, vtid, status);
}

int kvlangVthreadSet(kvspace_t *kv, const char *vtid, const char *pc,
                     const char *status) {
    char k1[KV_KEY_MAX], k2[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, k1, sizeof k1);
    kvlangKeytreeVthreadStatus(vtid, k2, sizeof k2);
    kvlangXvalue_t v1, v2;
    kvlangXvalueZero(&v1);
    kvlangXvalueZero(&v2);
    kvlangXvalueNewCharUtf8(&v1, pc);
    kvlangXvalueNewCharUtf8(&v2, status);
    kvlangKvPair_t pairs[2] = {{k1, v1}, {k2, v2}};
    char err[256];
    int rc = kvlangKvspaceSet(kv, pairs, 2, err, sizeof err);
    kvlangXvalueFree(&v1);
    kvlangXvalueFree(&v2);
    return rc;
}

/* 槽容量：pc 常态长度一档、status 更短一档；超长值退回全尺寸，仍不截断。 */
#define VT_PC_SLACK_CAP 256
/* status 固定档：b=16 使 wire 总长 64+16=80，与后端物理容量下限一致；
 * 超过 16 的写入由后端硬错（persist_failed），不再用更大 cap 扩容。 */
#define VT_STATUS_SLACK_CAP 16
#define VT_SLACK_MAX_CAP KV_KEY_MAX

/* PC 槽缓存：shm 下每步只 part-write `[2, 64+len)`（等价值写），不重编码、
 * 不 memset 320B；fs/redis 保持整值写。见 spec [[执行模型-续]]。 */
typedef struct {
    char key[KV_KEY_MAX]; /* 缓存所属槽的 key 副本：跨 vthread 时栈/堆地址可能复用 */
    uint8_t buf[64 + VT_SLACK_MAX_CAP];
    uint32_t cap; /* 槽实际容量档（b 字段） */
    bool ready;
} pc_slot_cache_t;
static __thread pc_slot_cache_t pc_cache;

/* b1b：owned 接管/回退时作废 part-write 缓存，避免指向被 WriteNewPlace 移动的旧 box。 */
__attribute__((visibility("hidden"))) void kvlangPcCacheInvalidate(void) {
    pc_cache.ready = false;
}

static uint32_t utf8_cp_count(const char *s, uint32_t len) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; i++)
        if (((uint8_t)s[i] & 0xC0) != 0x80)
            n++;
    return n;
}

/* PC 路径绝大多数是纯 ASCII：此时码点数 == 字节数，免一次全串扫描。 */
static uint32_t cp_count(const char *s, uint32_t len) {
    uint8_t acc = 0;
    for (uint32_t i = 0; i < len; i++)
        acc |= (uint8_t)s[i];
    return (acc & 0x80) ? utf8_cp_count(s, len) : len;
}

/* 整值写（首次/超长/非 shm/part 失败回退）：编码进 pc 缓存并写整值。 */
static int pc_write_whole(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                          const char *s, uint32_t len) {
    uint32_t cap = len > VT_PC_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_PC_SLACK_CAP;
    uint32_t n = kvlangSlackEncodeChar((const uint8_t *)s, len, cap,
                                       pc_cache.buf, sizeof pc_cache.buf);
    pc_cache.cap = cap;
    pc_cache.ready = true;
    snprintf(pc_cache.key, sizeof pc_cache.key, "%s", ko->key);
    return kvspaceKeyOffSet(ko, pc_cache.buf, n, NULL, 0);
}

/* langtype = "[N]char/utf8"（冷 helper）：码点数统计 + 无 snprintf 整数格式化。 */
__attribute__((noinline, visibility("hidden"))) int
kvlangCharLangtypeFmt(uint8_t *lt, const char *s, uint32_t len) {
    uint32_t v = cp_count(s, len);
    char digits[12];
    int d = 0;
    do {
        digits[d++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    uint32_t p = 0;
    if ((uint32_t)d + 13 > 64 - XH_PREFIX)
        return -1;
    lt[p++] = '[';
    while (d)
        lt[p++] = (uint8_t)digits[--d];
    memcpy(lt + p, "]char/utf8", 11); /* 含结尾 NUL */
    return 0;
}

/* 等价值写：只更新 head 的 a/langtype 与 body 前缀，写 `[2, 64+len)`。 */
static int pc_write_part(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                         const char *s, uint32_t len) {
    uint8_t *b = pc_cache.buf;
    for (int i = 0; i < 4; i++)
        b[2 + i] = (uint8_t)(len >> (8 * i)); /* a */
    if (kvlangCharLangtypeFmt(b + XH_PREFIX, s, len) != 0)
        return -1;
    memcpy(b + 64, s, len);
    uint32_t part = 64 + len - 2;
    return kvspaceSetPartByRef(f->kvspace->handle, &ko->off, ko->key, 2, b + 2,
                               part, NULL, 0);
}

/* 写 pc/status 槽：已绑定的 keyoff 直接写；独立帧没有绑定，按 key 冷写。 */
static void member_write(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                         bool is_status, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    if (is_status) { /* status 只在变化时写，保持整值写 */
        uint32_t cap = len > VT_STATUS_SLACK_CAP ? VT_SLACK_MAX_CAP
                                                 : VT_STATUS_SLACK_CAP;
        uint8_t val[64 + VT_SLACK_MAX_CAP];
        uint32_t n = kvlangSlackEncodeChar((const uint8_t *)s, len, cap, val,
                                           sizeof val);
        if (ko) {
            if (kvspaceKeyOffSet(ko, val, n, NULL, 0) != 0)
                f->persist_failed = true;
            return;
        }
        char k[KV_KEY_MAX];
        kvlangKeytreeVthreadStatus(f->vthread_id, k, sizeof k);
        if (kvspaceSetValue(f->kvspace->handle, k, val, n, 0, 0, NULL, 0) != 0)
            f->persist_failed = true;
        return;
    }
    if (ko && kvlangPcViewWrite(f, ko, s, len) == 1)
        return;
    if (ko && f->kvspace->shm && pc_cache.ready &&
        strcmp(pc_cache.key, ko->key) == 0 && len <= pc_cache.cap) {
        if (pc_write_part(f, ko, s, len) == 0)
            return;
        /* part 失败：回退整值写（会重建缓存）。 */
    }
    if (ko) {
        if (pc_write_whole(f, ko, s, len) != 0)
            f->persist_failed = true;
        return;
    }
    pc_cache.ready = false;
    char k[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(f->vthread_id, k, sizeof k);
    uint8_t val[64 + VT_SLACK_MAX_CAP];
    uint32_t cap = len > VT_PC_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_PC_SLACK_CAP;
    uint32_t n = kvlangSlackEncodeChar((const uint8_t *)s, len, cap, val,
                                       sizeof val);
    if (kvspaceSetValue(f->kvspace->handle, k, val, n, 0, 0, NULL, 0) != 0)
        f->persist_failed = true;
}

/* Persist PC and changed status after each instruction. */
void kvlangVthreadNextPc(kvlangFrame_t *f, const char *pc,
                          const char *status) {
    member_write(f, f->pc_off, false, pc);
    if (!f->persist_failed &&
        (!f->status_known || strcmp(f->status_known, status) != 0))
        member_write(f, f->status_off, true, status);
    /* 回传：值就是刚写进 kvspace 的那条 PC，不引入第二份事实源。
     * 热路径已直接格式化进 pc_out 时省掉这次 memcpy。 */
    if (!f->persist_failed && f->pc_out) {
        if (pc != f->pc_out) {
            size_t n = strlen(pc);
            if (n + 1 > f->pc_out_cap)
                kvlangDie("feedback pc exceeds %zu bytes", f->pc_out_cap);
            memcpy(f->pc_out, pc, n + 1);
        }
        f->pc_feedback = true;
    }
}

int kvlangVthreadSetDone(kvspace_t *kv, const char *vtid, const char *ret) {
    if (ret == NULL || ret[0] == 0)
        ret = "ok";
    char k[KV_KEY_MAX];
    kvlangKeytreeVthreadStatus(vtid, k, sizeof k);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, ret);
    kvlangKvPair_t pair = {k, v};
    char err[256];
    int rc = kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&v);
    return rc;
}

void kvlangVthreadSetError(kvspace_t *kv, const char *vtid, const char *pc,
                           const char *msg) {
    char msg_key[KV_KEY_MAX], pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    kvlangKeytreeVthreadMessage(vtid, msg_key, sizeof msg_key);
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);

    kvlangXvalue_t vpc, vmsg, vst;
    kvlangXvalueZero(&vpc);
    kvlangXvalueZero(&vmsg);
    kvlangXvalueZero(&vst);
    kvlangXvalueNewCharUtf8(&vpc, pc);
    kvlangXvalueNewCharUtf8(&vmsg, msg);
    kvlangXvalueNewCharUtf8(&vst, "error");
    kvlangKvPair_t pairs[3] = {{pc_key, vpc}, {msg_key, vmsg}, {st_key, vst}};
    char err[256];
    kvlangKvspaceSet(kv, pairs, 3, err, sizeof err);
    kvlangXvalueFree(&vpc);
    kvlangXvalueFree(&vmsg);
    kvlangXvalueFree(&vst);
}
