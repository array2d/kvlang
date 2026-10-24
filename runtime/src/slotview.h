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
