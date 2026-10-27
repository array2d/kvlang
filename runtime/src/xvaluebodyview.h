#pragma once

#include "runtime_internal.h"

/* 绑定期取 shm xvalue body view（status 必须传完整 key_fallback）。
 * 0=成功；-1=槽不存在/不可视；-2=后端不支持（调用方每句柄永久 fallback）。 */
int kvlangXvalueBodyViewBind(kvspace_t *kv, kvspaceKeyOff *ko,
                       kvspaceXvalueBodyView_t *view);

/* status 只读 view：完整取 head content_len 与全部 body 字节并 NUL 结尾。
 * 形状/长度/容量不自洽 → false（调用方丢弃 view 并回退）。 */
bool kvlangStatusXvalueBodyViewRead(const kvspaceXvalueBodyView_t *view, char *out,
                          size_t cap);

/* T-120 A：lazy operand 只读 view。同一 bind 内第 N 次读才分配+capture；
 * 递归帧单帧命中 <N 时零分配。提升后每 RECHECK 次命中重验 head。 */
#ifndef KV_READ_VIEW_N
#define KV_READ_VIEW_N 8
#endif
#ifndef KV_READ_VIEW_RECHECK
#define KV_READ_VIEW_RECHECK 512
#endif

/* Q1：op 边界静默点（epoch 变化时作废全部 body slot 并 ack）。 */
__attribute__((visibility("hidden"))) void kvlangViewQuiesce(kvspace_t *kv);
__attribute__((visibility("hidden"))) void kvlangViewAckNow(kvspace_t *kv);

/* 返回 1=命中（out 为借用视图），0=回退 GetByRef。pack=0 时调用方不应调用。 */
struct kvlangBoundCell;
__attribute__((visibility("hidden"))) int
kvlangReadViewTry(kvspace_t *kv, struct kvlangBoundCell *cell, uint32_t pack,
                  kvlangXvalue_t *out);

/* b1b：PC owned view 状态（每 ExecuteMode 一份）。 */
typedef struct {
    kvspaceXvalueBodyView_t view;
    bool ready;     /* 固定档（body_cap>=1024）已就绪 */
    bool fallback;  /* 永久回退 SetPartByRef */
    unsigned recheck; /* 每 128 次写重验 keyoff/view 地址 */
} kvlangPcViewState;

void kvlangPcViewInit(kvlangPcViewState *st);

/* 返回 1=已写（调用方不要再走 part/whole）；0=未处理（走原路径）；
 * -1 当前也返回 0，并把 state->fallback 置位。 */
int kvlangPcViewWrite(kvlangFrame_t *f, kvspaceKeyOff *ko, const char *pc,
                      uint32_t len);
