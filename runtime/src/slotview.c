/* P1b4-b1a：shm slot view 的 runtime 薄封装（晚链接 TU，控制热 TU 布局）。 */

#include "slotview.h"

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
