#pragma once

#include "runtime_internal.h"

/* 绑定期取 shm slot view（status 必须传完整 key_fallback）。
 * 0=成功；-1=槽不存在/不可视；-2=后端不支持（调用方每句柄永久 fallback）。 */
int kvlangSlotViewBind(kvspace_t *kv, kvspaceKeyOff *ko,
                       kvspaceSlotView_t *view);

/* status 只读 view：完整取 head content_len 与全部 body 字节并 NUL 结尾。
 * 形状/长度/容量不自洽 → false（调用方丢弃 view 并回退）。 */
bool kvlangStatusViewRead(const kvspaceSlotView_t *view, char *out,
                          size_t cap);

/* b1b：PC owned view 状态（每 ExecuteMode 一份）。 */
typedef struct {
    kvspaceSlotView_t view;
    bool ready;     /* 固定档（body_cap>=1024）已就绪 */
    bool fallback;  /* 永久回退 SetPartByRef */
    unsigned recheck; /* 每 128 次写重验 keyoff/view 地址 */
} kvlangPcViewState;

void kvlangPcViewInit(kvlangPcViewState *st);

/* 返回 1=已写（调用方不要再走 part/whole）；0=未处理（走原路径）；
 * -1 当前也返回 0，并把 state->fallback 置位。 */
int kvlangPcViewWrite(kvlangFrame_t *f, kvspaceKeyOff *ko, const char *pc,
                      uint32_t len);
