/* P1b4-b1a：shm slot view 的 runtime 薄封装（晚链接 TU，控制热 TU 布局）。 */

#include "slotview.h"
#include "func_code.h"

__attribute__((visibility("hidden"))) int
kvlangSlotViewBind(kvspace_t *kv, kvspaceKeyOff *ko,
                       kvspaceSlotView_t *view) {
    /* ref 未解析（槽未创建）时不取 view：后端零 ref 会落到错误叶子。 */
    if (!kv || !ko || !view || !ko->key || !ko->resolved)
        return -1;
    return kvspaceSlotView(kv->handle, &ko->off, ko->key, view);
}

__attribute__((visibility("hidden"))) bool
kvlangStatusViewRead(const kvspaceSlotView_t *view, char *out,
                          size_t cap) {
    if (!view || !view->head || !out || cap == 0)
        return false;
    /* 规范 status：SLACK、headlen=64（pow=6）；任何不自洽都不暴露。 */
    if (view->headlen < (1u << 6) || view->head[0] != 6 ||
        (view->head[1] & 7u) != 1u)
        return false;
    uint64_t n = xh_a(view->head);
    if (n > view->body_cap || n + 1 > cap)
        return false;
    memcpy(out, view->body, (size_t)n);
    out[n] = 0;
    return true;
}

/* ── b1b：PC owned view ─────────────────────────────────────────────── */

#define PC_FIXED_CAP KV_KEY_MAX /* 1024 */

__attribute__((visibility("hidden"))) void
kvlangPcViewInit(kvlangPcViewState *st) {
    memset(st, 0, sizeof *st);
}

/* langtype 必须是 "[<n>]char/utf8" 基类型。 */
static bool pc_langtype_ok(const uint8_t *head, uint32_t headlen) {
    uint32_t i = XH_PREFIX, n = headlen;
    if (i >= n || head[i] != '[')
        return false;
    i++;
    uint32_t d0 = i;
    while (i < n && head[i] >= '0' && head[i] <= '9')
        i++;
    if (i == d0 || i >= n || head[i] != ']')
        return false;
    i++;
    static const char suf[] = "char/utf8";
    if (n - i < sizeof suf)
        return false;
    return memcmp(head + i, suf, sizeof suf - 1) == 0 && head[i + sizeof suf - 1] == 0;
}

/* 取 view 并校验固定档与规范形状；成功写入 st->view。 */
static bool pc_view_adopt(kvlangPcViewState *st, kvspace_t *kv,
                          kvspaceKeyOff *ko) {
    kvspaceSlotView_t nv;
    if (kvspaceSlotView(kv->handle, &ko->off, ko->key, &nv) != 0)
        return false;
    if (!nv.head || nv.headlen != (1u << 6) || nv.body_cap < PC_FIXED_CAP ||
        nv.head[0] != 6 || (nv.head[1] & 3u) != 1u ||
        (nv.head[1] & 4u) || !pc_langtype_ok(nv.head, nv.headlen))
        return false;
    st->view = nv;
    return true;
}

/* 永久回退：同时作废 part-write 缓存（其可能指向被移动的旧 box）。 */
static int pc_fallback(kvlangPcViewState *st) {
    st->fallback = true;
    kvlangPcCacheInvalidate();
    return 0;
}

/* 首次：WriteNewPlace 一次预留 PC_FIXED_CAP 并写实际值（不得用 SetValue）。 */
static bool pc_fixed_init(kvlangFrame_t *f, kvspaceKeyOff *ko, const char *pc,
                          uint32_t len, kvlangPcViewState *st) {
    if (len > PC_FIXED_CAP)
        return false;
    uint8_t lt[64];
    if (kvlangCharLangtypeFmt(lt, pc, len) != 0)
        return false;
    uint8_t *body = NULL;
    char err[256];
    if (kvspaceWriteNewPlace(f->kvspace->handle, ko->key, KVSPACE_REF_INLINE,
                             KVSPACE_STORETYPE_SLACK, 0, 0, (const char *)lt,
                             len, PC_FIXED_CAP, &body, err, sizeof err) != 0 ||
        !body)
        return false;
    memcpy(body, pc, len);
    kvlangPcCacheInvalidate(); /* box 可能被移动/重建 */
    if (kvspaceResolveRef(f->kvspace->handle, ko->key, &ko->off) != 0)
        return false;
    ko->resolved = true;
    return pc_view_adopt(st, f->kvspace, ko);
}

__attribute__((visibility("hidden"))) int
kvlangPcViewWrite(kvlangFrame_t *f, kvspaceKeyOff *ko, const char *pc,
                      uint32_t len) {
    kvlangPcViewState *st = f->pc_view_state;
    if (!st || st->fallback || !f->kvspace->shm)
        return 0;
    if (!st->ready) {
        /* 已有固定档槽（如前一次运行）直接采用；否则首次建/扩。 */
        if (ko->resolved && pc_view_adopt(st, f->kvspace, ko)) {
            st->ready = true;
        } else {
            if (!pc_fixed_init(f, ko, pc, len, st))
                return pc_fallback(st);
            st->ready = true;
            return 1; /* 本次值已由 WriteNewPlace 写入 */
        }
    }
    if (len > st->view.body_cap)
        return pc_fallback(st);
    if ((++st->recheck & 127u) == 0) {
        /* 删除/重建检测：强制重解析 keyoff 再取 view。 */
        if (kvspaceResolveRef(f->kvspace->handle, ko->key, &ko->off) != 0 ||
            !pc_view_adopt(st, f->kvspace, ko))
            return pc_fallback(st);
    }
    uint8_t *h = st->view.head;
    for (int i = 0; i < 8; i++)
        h[2 + i] = (uint8_t)((uint64_t)len >> (8 * i));
    /* 快路径：PC 几乎全 ASCII，码点数 == 字节数；避免再次扫串/函数调用。 */
    uint8_t acc = 0;
    for (uint32_t i = 0; i < len; i++)
        acc |= (uint8_t)pc[i];
    if (!(acc & 0x80)) {
        char digits[12];
        int d = 0;
        uint32_t v = len;
        do {
            digits[d++] = (char)('0' + (int)(v % 10));
            v /= 10;
        } while (v);
        uint8_t *lt = h + XH_PREFIX;
        *lt++ = '[';
        while (d)
            *lt++ = (uint8_t)digits[--d];
        memcpy(lt, "]char/utf8", 11); /* 含 NUL */
    } else if (kvlangCharLangtypeFmt(h + XH_PREFIX, pc, len) != 0) {
        return pc_fallback(st);
    }
    memcpy(st->view.body, pc, len);
    return 1;
}

/* ── T-120 A：lazy operand 只读 view ────────────────────────────────── */

/* kind/width 由 row 装载期固化（见 kvlangCodeRow_t.view_pack）。
 * pin cell 允许：pin 只在 handler 前把读值 copy 一份，view 在慢写后由既有
 * Invalidate* 失效；iops 的 `a = a + 1` 读参正是 pin。 */

/* 提升：一次 calloc + capture。失败解注册并释放，调用方清零计数重来。 */
#ifdef KV_COUNT
unsigned long kvCReadViewProm, kvCReadViewDemote, kvCReadViewHit;
#endif

static bool rv_promote(kvspace_t *kv, kvlangBoundCell_t *cell) {
    kvlangBodySlot *slot = calloc(1, sizeof *slot);
    if (!slot)
        return false;
    slot->key = cell->key;
    slot->kind = kvlangLangtypeKind(cell->view.kind);
    slot->width = cell->view.width;
    slot->off = &cell->off;
    /* 读 cell 的 key 是帧内绝对路径；显式取 /vthread/<vid> 前缀交给
     * capture（global/绝对 key 取不到前缀 → 拒绝提升，不隐式借用 key）。 */
    char root[KV_KEY_MAX];
    if (!slot->kind || !slot->kind[0] || !slot->width ||
        !kvlangVthreadRootOfKey(cell->key, root, sizeof root) ||
        !kvlangBodyCapture(kv, slot, root)) {
        kvlangBodySlotRelease(slot);
        return false;
    }
    slot->ref = cell->off.off;
    cell->off.fast = slot;
#ifdef KV_COUNT
    kvCReadViewProm++;
#endif
    cell->view.hits = KV_READ_VIEW_RECHECK;
    cell->view_state = 2;
    return true;
}

static void rv_demote(kvlangBoundCell_t *cell) {
#ifdef KV_COUNT
    kvCReadViewDemote++;
#endif
    kvlangBodySlotRelease((kvlangBodySlot *)cell->off.fast);
    cell->off.fast = NULL;
    cell->view.hits = 0;
    cell->view_state = 1;
}

/* 每 RECHECK 次命中重验一次：只重解析 ref 并与提升时的 ref 比对（一次
 * 后端调用）。ref 不变即同一槽 box；外部删除/改尺寸仍是契约外。 */
static bool rv_recheck(kvspace_t *kv, kvlangBoundCell_t *cell) {
    kvspaceRef_t nr;
    if (kvspaceResolveRef(kv->handle, cell->key, &nr) != 0)
        return false;
    kvlangBodySlot *s = (kvlangBodySlot *)cell->off.fast;
    if (memcmp(&nr, &s->ref, sizeof nr) != 0)
        return false;
    cell->off.off = nr;
    cell->off.resolved = true;
    return true;
}

__attribute__((visibility("hidden"))) int
kvlangReadViewTry(kvspace_t *kv, struct kvlangBoundCell *cell, uint32_t pack,
                  kvlangXvalue_t *out) {
    if (!kv || !kv->shm)
        return 0;
    switch (cell->view_state) {
    case 3:
        return 0;
    case 0:
        cell->view.kind = (uint8_t)(pack & 0xffu);
        cell->view.width = (uint16_t)(pack >> 8);
        cell->view_state = 1;
        cell->view.hits = 0;
        /* fall through */
    case 1:
        if (++cell->view.hits < KV_READ_VIEW_N)
            return 0;
        if (!rv_promote(kv, cell)) {
            cell->view.hits = 0;
            return 0;
        }
        break;
    case 2:
        break;
    default:
        cell->view_state = 3;
        return 0;
    }
    kvlangBodySlot *s = (kvlangBodySlot *)cell->off.fast;
    if (!s || !s->ok || !s->head || !s->headlen) {
        if (s)
            rv_demote(cell);
        else
            cell->view_state = 1;
        return 0;
    }
    if (--cell->view.hits == 0) {
        cell->view.hits = KV_READ_VIEW_RECHECK;
        if (!rv_recheck(kv, cell)) {
            rv_demote(cell);
            return 0;
        }
    }
#ifdef KV_COUNT
    kvCReadViewHit++;
#endif
    out->bytes = s->head;
    out->length = s->headlen + s->width;
    return 1;
}
