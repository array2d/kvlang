/*
 * 组件：runtime/interpreter 的实现
 * 作用：v9 执行大循环的骨架。
 *
 * 现在只有两件事：创建 vthread（一次算完 `‥pc`/`‥status`/`‥msg` 三处地址），
 * 然后按 `‥status` 决定继续还是返回。执行体（BinRWIRGroup）尚未接入。
 */

#include "runtime/interpreter.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 状态是否为「继续」：init / running / wait。 */
static bool runtimec_interpreter_status_continues(
    const runtimec_vthread_t *vthread) {
    static const uint8_t init_text[RUNTIMEC_VTHREAD_STATUS_LENGTH] = "init";
    static const uint8_t running_text[RUNTIMEC_VTHREAD_STATUS_LENGTH] =
        "running";
    static const uint8_t wait_text[RUNTIMEC_VTHREAD_STATUS_LENGTH] = "wait";
    return memcmp(vthread->status_body, init_text,
                  RUNTIMEC_VTHREAD_STATUS_LENGTH) == 0 ||
           memcmp(vthread->status_body, running_text,
                  RUNTIMEC_VTHREAD_STATUS_LENGTH) == 0 ||
           memcmp(vthread->status_body, wait_text,
                  RUNTIMEC_VTHREAD_STATUS_LENGTH) == 0;
}

void runtimec_runtime_create(runtimec_runtime_t *runtime,
                             void *kvspace_handler) {
    if (kvspace_handler == NULL) {
        abort();
    }
    runtime->kvspace_handler = kvspace_handler;
    runtimec_addrbuf_initialize(&runtime->addrbuf);
}

void runtimec_interpreter_run(runtimec_runtime_t *runtime,
                              const char *vthread_identifier) {
    /*
     * 创建 vthread 时一次算完三处地址：`‥pc`、`‥status`、`‥msg`。
     * 三者在线程生命周期内原地写、地址不搬移，循环里只做 memcpy。
     */
    runtimec_vthread_t vthread;
    runtimec_vthread_open(&vthread, runtime->kvspace_handler,
                          vthread_identifier);

    for (;;) {
        if (!runtimec_interpreter_status_continues(&vthread)) {
            return;
        }
    }
}
