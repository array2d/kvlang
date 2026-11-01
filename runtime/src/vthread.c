#include "runtime_internal.h"
#include "xvaluebodyview.h"

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
    ko->view_ok = false;
    ko->resolved = kvspaceResolveRef(kvspace->handle, key, &ko->off) == 0;
}

static bool is_status_key(const char *key) {
    static const char pfx[] = "/vthread/";
    static const char suf[] = "/\xE2\x80\xA5status";
    if (!key)
        return false;
    size_t n = strlen(key), sl = sizeof suf - 1;
    return n > sl && strncmp(key, pfx, sizeof pfx - 1) == 0 &&
           memcmp(key + n - sl, suf, sl) == 0;
}

bool kvspaceKeyOffRead(kvspaceKeyOff *ko, char *out, size_t cap) {
    if (!ko->view_ok && ko->resolved && ko->kvspace->shm &&
        ko->kvspace->slot_view_sup >= 0 && is_status_key(ko->key)) {
        int vrc = kvspaceXvalueBodyView(ko->kvspace->handle, &ko->off, ko->key,
                                  &ko->view);
        if (vrc == 0) {
            ko->kvspace->slot_view_sup = 1;
            ko->view_ok = true;
        } else if (vrc == -2) {
            ko->kvspace->slot_view_sup = -1;
        }
    }
    if (ko->view_ok) {
        const uint8_t *h = ko->view.head;
        if (ko->view.headlen == KVSPACE_XVALUE_HEAD_LEN &&
            xh_storetype(h) == KVSPACE_XVALUE_ST_SLACK &&
            xh_ptr(h) == KVSPACE_XVALUE_PTR_INLINE) {
            uint64_t a = xh_len(h);
            if (a <= ko->view.body_cap && a + 1 <= cap) {
                memcpy(out, ko->view.body, (size_t)a);
                out[a] = 0;
                return true;
            }
        }
        ko->view_ok = false;
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (ko->resolved) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGetByRef(ko->kvspace->handle, &ko->off, ko->key, &d, &len) !=
            0)
            return false;
        if (d && len > 0)
            kvlangXvalueSetWire(&v, d, len); /* populate langtype from wire attr */
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
                             err, err_cap) == 0)
        return 0;
    if (kvspaceSetValue(ko->kvspace->handle, ko->key, val, len, err,
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

/* Slack capacities for `‥pc` and `‥status`; overlong `‥pc` falls back to `VT_SLACK_MAX_CAP`. */
#define VT_PC_SLACK_CAP 256
/* Fixed 16-byte slack for `‥status` (64B head + 16B = 80B minimum backend box). */
#define VT_STATUS_SLACK_CAP 16
#define VT_SLACK_MAX_CAP KV_KEY_MAX

int kvlangVthreadSet(kvspace_t *kv, const char *vtid, const char *pc,
                     const char *status) {
    char k1[KV_KEY_MAX], k2[KV_KEY_MAX];
    kvlangKeytreeVthreadPc(vtid, k1, sizeof k1);
    kvlangKeytreeVthreadStatus(vtid, k2, sizeof k2);
    uint8_t b1[64 + VT_SLACK_MAX_CAP], b2[64 + VT_SLACK_MAX_CAP];
    uint32_t l1 = (uint32_t)strlen(pc), l2 = (uint32_t)strlen(status);
    uint32_t c1 = l1 > VT_PC_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_PC_SLACK_CAP;
    uint32_t c2 =
        l2 > VT_STATUS_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_STATUS_SLACK_CAP;
    kvlangXvalue_t v1 = {0}, v2 = {0};
    v1.bytes = b1;
    v1.length =
        kvlangSlackCharToWire((const uint8_t *)pc, l1, c1, b1, sizeof b1);
    v2.bytes = b2;
    v2.length =
        kvlangSlackCharToWire((const uint8_t *)status, l2, c2, b2, sizeof b2);
    kvlangKvPair_t pairs[2] = {{k1, v1}, {k2, v2}};
    char err[256];
    return kvlangKvspaceSet(kv, pairs, 2, err, sizeof err);
}

/* PC 槽缓存：shm 下每步只 part-write `[2, 64+len)`（等价值写），不重写 wire、
 * 不 memset 320B；fs/redis 保持整值写。见 spec [[执行模型-续]]。 */
typedef struct {
    char key[KV_KEY_MAX]; /* 缓存所属槽的 key 副本：跨 vthread 时栈/堆地址可能复用 */
    uint8_t buf[64 + VT_SLACK_MAX_CAP];
    uint32_t cap; /* 槽实际容量档（b 字段） */
    uint32_t last_cp;
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

static uint32_t cp_count(const char *s, uint32_t len) {
    uint8_t acc = 0;
    uint32_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w;
        memcpy(&w, s + i, 8);
        if (w & 0x8080808080808080ULL)
            return utf8_cp_count(s, len);
    }
    for (; i < len; i++)
        acc |= (uint8_t)s[i];
    return (acc & 0x80) ? utf8_cp_count(s, len) : len;
}

static void capture_pc_view(kvlangFrame_t *f, struct kvspaceKeyOff *ko) {
    if (!ko->resolved || !f->kvspace->shm || f->kvspace->slot_view_sup < 0)
        return;
    int vrc = kvspaceXvalueBodyView(f->kvspace->handle, &ko->off, ko->key, &ko->view);
    if (vrc == 0 && ko->view.headlen == KVSPACE_XVALUE_HEAD_LEN &&
        KVSPACE_XVALUE_HEAD_LEN + ko->view.body_cap <= ko->view.cap) {
        f->kvspace->slot_view_sup = 1;
        ko->view_ok = true;
    } else if (vrc == -2) {
        f->kvspace->slot_view_sup = -1;
    }
}

static int pc_write_whole(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                          const char *s, uint32_t len) {
    uint32_t cap = len > VT_PC_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_PC_SLACK_CAP;
    uint32_t n = kvlangSlackCharToWire((const uint8_t *)s, len, cap,
                                       pc_cache.buf, sizeof pc_cache.buf);
    pc_cache.cap = cap;
    pc_cache.last_cp = cp_count(s, len);
    pc_cache.ready = true;
    snprintf(pc_cache.key, sizeof pc_cache.key, "%s", ko->key);
    ko->view_ok = false;
    int rc = kvspaceKeyOffSet(ko, pc_cache.buf, n, NULL, 0);
    if (rc == 0)
        capture_pc_view(f, ko);
    return rc;
}

static int pc_write_part(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                         const char *s, uint32_t len) {
    uint8_t *b = pc_cache.buf;
    kvspaceXvalueHead hd = {
        .storetype = KVSPACE_XVALUE_ST_SLACK,
        .ptr = KVSPACE_XVALUE_PTR_INLINE,
    };
    kvspaceXvalueHeadSetLen(&hd, len);
    memcpy(b, &hd, sizeof hd);
    if (!ko->view_ok && ko->resolved)
        capture_pc_view(f, ko);
    if (ko->view_ok) {
        uint8_t *vh = ko->view.head;
        /* `‥pc` carries empty trailing attr `[02 00]` at `8 + len`; update head, body,
         * and trailing `[02 00]` in place when `plen + 2 <= body_cap`. */
        if (ko->view.headlen == KVSPACE_XVALUE_HEAD_LEN &&
            xh_storetype(vh) == KVSPACE_XVALUE_ST_SLACK &&
            xh_ptr(vh) == KVSPACE_XVALUE_PTR_INLINE) {
            uint64_t old_len = xh_len(vh);
            if (old_len + 2 <= ko->view.body_cap &&
                (uint64_t)len + 2 <= ko->view.body_cap &&
                ko->view.body[old_len] == 2 &&
                ko->view.body[old_len + 1] == 0) {
                memcpy(vh, b, KVSPACE_XVALUE_HEAD_LEN);
                memcpy(ko->view.body, s, len);
                ko->view.body[len] = 2;
                ko->view.body[len + 1] = 0;
                return 0;
            }
        }
        ko->view_ok = false;
    }
    return -1;
}

/* 写 pc/status 槽：已绑定的 keyoff 直接写；独立帧没有绑定，按 key 冷写。 */
static void member_write(kvlangFrame_t *f, struct kvspaceKeyOff *ko,
                         bool is_status, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    if (is_status) { /* status 只在变化时写，保持整值写 */
        uint32_t cap = len > VT_STATUS_SLACK_CAP ? VT_SLACK_MAX_CAP
                                                 : VT_STATUS_SLACK_CAP;
        uint8_t val[64 + VT_SLACK_MAX_CAP];
        uint32_t n = kvlangSlackCharToWire((const uint8_t *)s, len, cap, val,
                                           sizeof val);
        if (ko) {
            if (kvspaceKeyOffSet(ko, val, n, NULL, 0) != 0)
                f->persist_failed = true;
            return;
        }
        char k[KV_KEY_MAX];
        kvlangKeytreeVthreadStatus(f->vthread_id, k, sizeof k);
        if (kvspaceSetValue(f->kvspace->handle, k, val, n, NULL, 0) != 0)
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
    uint32_t n = kvlangSlackCharToWire((const uint8_t *)s, len, cap, val,
                                       sizeof val);
    if (kvspaceSetValue(f->kvspace->handle, k, val, n, NULL, 0) != 0)
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
    uint8_t b[64 + VT_SLACK_MAX_CAP];
    uint32_t l = (uint32_t)strlen(ret);
    uint32_t c = l > VT_STATUS_SLACK_CAP ? VT_SLACK_MAX_CAP : VT_STATUS_SLACK_CAP;
    kvlangXvalue_t v = {0};
    v.bytes = b;
    v.length = kvlangSlackCharToWire((const uint8_t *)ret, l, c, b, sizeof b);
    kvlangKvPair_t pair = {k, v};
    char err[256];
    return kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
}

void kvlangVthreadSetError(kvspace_t *kv, const char *vtid, const char *pc,
                           const char *msg) {
    char msg_key[KV_KEY_MAX], pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    kvlangKeytreeVthreadMessage(vtid, msg_key, sizeof msg_key);
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);

    uint8_t bpc[64 + VT_SLACK_MAX_CAP], bmsg[64 + VT_SLACK_MAX_CAP],
        bst[64 + VT_SLACK_MAX_CAP];
    uint32_t lpc = (uint32_t)strlen(pc), lmsg = (uint32_t)strlen(msg);
    if (lpc > VT_SLACK_MAX_CAP)
        lpc = VT_SLACK_MAX_CAP;
    if (lmsg > VT_SLACK_MAX_CAP)
        lmsg = VT_SLACK_MAX_CAP;
    kvlangXvalue_t vpc = {0}, vmsg = {0}, vst = {0};
    vpc.bytes = bpc;
    vpc.length = kvlangSlackCharToWire((const uint8_t *)pc, lpc,
                                       VT_SLACK_MAX_CAP, bpc, sizeof bpc);
    vmsg.bytes = bmsg;
    vmsg.length = kvlangSlackCharToWire((const uint8_t *)msg, lmsg,
                                        VT_SLACK_MAX_CAP, bmsg, sizeof bmsg);
    vst.bytes = bst;
    vst.length = kvlangSlackCharToWire((const uint8_t *)"error", 5,
                                       VT_STATUS_SLACK_CAP, bst, sizeof bst);
    kvlangKvPair_t pairs[3] = {{pc_key, vpc}, {msg_key, vmsg}, {st_key, vst}};
    char err[256];
    kvlangKvspaceSet(kv, pairs, 3, err, sizeof err);
}
