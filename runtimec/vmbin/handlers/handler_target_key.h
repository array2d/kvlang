#ifndef VMBIN_HANDLERS_HANDLER_TARGET_KEY_H
#define VMBIN_HANDLERS_HANDLER_TARGET_KEY_H

/*
 * 组件：vmbin/handlers/handler_target_key
 * 作用：把写参解析成它在存储里的**绝对 key**（`<frame_path>/<符号名>`）。
 * 联系：创建型 handler（`container` / `string` / `scatter`）写完 wire 要按这个 key
 *       落到存储；三处原本各有一份同样六行的实现，第三次出现时抽到这里。
 * 约束：
 *   - **只回答 key，不回答地址**：目标槽通常是**未物化**的（容器局部不建零值槽），
 *     所以它不在绑定表里——绑定表只回答「已存在的东西在哪」。创建型算子的语义
 *     正是「写入即创建」，它要的是 key，不是地址。
 *   - 帧路径来自 Frame，由调用入口**原样**写入，本层不重拼、不补斜杠、不剥斜杠；
 *     符号名来自 Program 符号池。两者都不猜。
 *   - 只接受 `LOCAL` 引用：静态/字面量参数没有「这一帧里的槽」这回事。
 */

#include <stddef.h>

#include "binrwir/execution_frame.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 成功时把 `\0` 结尾的 key 写进 `buffer` 并返回 SUCCESS；缓冲区不够返回
 * INSUFFICIENT_CAPACITY，引用种类不对或帧路径/符号名缺失返回 INVALID。
 */
runtimec_status_t runtimec_handler_target_key(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
