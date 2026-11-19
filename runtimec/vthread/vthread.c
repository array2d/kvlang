/*
 * 组件：vthread 的实现
 * 作用：解析 `‥pc` / `‥status` 的进程内地址，并提供两个 8 字节写入。
 * 约束：键只在这里拼；解析失败 abort；不分配、不缓存值。
 */

#include "vthread/vthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>

/* key → 值区偏移 → body 地址。两个系统键共用这一条。 */
static uint8_t *runtimec_vthread_resolve_body(void *kvspace_handle,
                                              const char *key) {
    uint64_t value_offset = 0U;
    char error[RUNTIMEC_VTHREAD_STATUS_LENGTH];
    error[0] = '\0';
    if (kvspaceResolveOffset(kvspace_handle, key, 0, &value_offset, error,
                             (uint32_t)sizeof(error)) != 0) {
        abort();
    }
    uint8_t *body = kvspaceXvalueInProcessAddress(kvspace_handle) +
                    value_offset + KVSPACE_XVALUE_HEAD_LEN;
    /* 原子写与 8 字节格都要求 body 8 字节对齐；不满足就是存储违契，终止。 */
    if (((uintptr_t)body & (RUNTIMEC_VTHREAD_PROGRAM_COUNTER_LENGTH - 1U)) !=
        0U) {
        abort();
    }
    return body;
}

void runtimec_vthread_open(runtimec_vthread_t *vthread, void *kvspace_handle,
                           const char *identifier) {
    snprintf(vthread->identifier, sizeof(vthread->identifier), "%s",
             identifier);
    /* "/vthread/" + vid + "/‥status" + NUL 的最长形态。 */
    char key[RUNTIMEC_VTHREAD_IDENTIFIER_CAPACITY + 24U];
    snprintf(key, sizeof(key), "/vthread/%s/‥status", vthread->identifier);
    vthread->status_body = runtimec_vthread_resolve_body(kvspace_handle, key);
    snprintf(key, sizeof(key), "/vthread/%s/‥pc", vthread->identifier);
    vthread->pc_body =
        runtimec_vthread_resolve_body(kvspace_handle, key);
    snprintf(key, sizeof(key), "/vthread/%s/‥msg", vthread->identifier);
    vthread->message_body = runtimec_vthread_resolve_body(kvspace_handle, key);
}

RUNTIMEC_STATUS_TEXT_LENGTH_OK("init");
RUNTIMEC_STATUS_TEXT_LENGTH_OK("running");
RUNTIMEC_STATUS_TEXT_LENGTH_OK("wait");
RUNTIMEC_STATUS_TEXT_LENGTH_OK("paused");
RUNTIMEC_STATUS_TEXT_LENGTH_OK("error");
RUNTIMEC_STATUS_TEXT_LENGTH_OK("done");

/*
 * 状态字符串数组：下标即状态编号，一项一格 8 字节。
 * 字符串初始化器由编译器零补齐，`memcpy(status_body, TABLE[id], 8)` 直接可用。
 *
 *   idx 0 INIT     "init"     4 + 4 零
 *   idx 1 RUNNING  "running"  7 + 1 零
 *   idx 2 WAIT     "wait"     4 + 4 零
 *   idx 3 PAUSED   "paused"   6 + 2 零
 *   idx 4 ERROR    "error"    5 + 3 零
 *   idx 5 DONE     "done"     4 + 4 零
 */
const char RUNTIMEC_VTHREAD_STATUS_TEXT[RUNTIMEC_VTHREAD_STATUS_COUNT]
                                       [RUNTIMEC_VTHREAD_STATUS_LENGTH] = {
    [RUNTIMEC_VTHREAD_STATUS_INIT] = "init",
    [RUNTIMEC_VTHREAD_STATUS_RUNNING] = "running",
    [RUNTIMEC_VTHREAD_STATUS_WAIT] = "wait",
    [RUNTIMEC_VTHREAD_STATUS_PAUSED] = "paused",
    [RUNTIMEC_VTHREAD_STATUS_ERROR] = "error",
    [RUNTIMEC_VTHREAD_STATUS_DONE] = "done",
};

_Static_assert(sizeof(RUNTIMEC_VTHREAD_STATUS_TEXT) /
                       sizeof(RUNTIMEC_VTHREAD_STATUS_TEXT[0]) ==
                   RUNTIMEC_VTHREAD_STATUS_COUNT,
               "status text table and identifier enum must have equal size");

void runtimec_vthread_write_status(
    runtimec_vthread_t *vthread,
    runtimec_vthread_status_identifier_t status_identifier) {
    memcpy(vthread->status_body,
           RUNTIMEC_VTHREAD_STATUS_TEXT[status_identifier],
           RUNTIMEC_VTHREAD_STATUS_LENGTH);
}

void runtimec_vthread_write_message(runtimec_vthread_t *vthread,
                                    const char *message) {
    memset(vthread->message_body, 0, RUNTIMEC_VTHREAD_MESSAGE_LENGTH);
    memcpy(vthread->message_body, message, strlen(message));
}

void runtimec_vthread_write_pc(runtimec_vthread_t *vthread,
                               int32_t stack_identifier,
                               int32_t source_instruction_ordinal) {
    const int32_t pair[2] = {stack_identifier, source_instruction_ordinal};
    memcpy(vthread->pc_body, pair,
           RUNTIMEC_VTHREAD_PROGRAM_COUNTER_LENGTH);
}
