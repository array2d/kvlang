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
