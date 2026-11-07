#ifndef VMBIN_FRAME_TARGET_KEY_H
#define VMBIN_FRAME_TARGET_KEY_H

/*
 * 组件：vmbin/frame_target_key
 * 作用：把写参解析成它在存储里的**绝对 key**（`<frame_path>/<符号名>`）。
 * 联系：创建型 handler（container / string / scatter）写完 wire 要按这个 key 落到
 *       存储；三个模块原本各有一份同样六行的实现，第三次出现时抽到这里。
 * 约束：
 *   - **只回答 key，不回答地址**：目标槽通常是**未物化**的（容器局部不建零值槽），
 *     所以它不在绑定表里——绑定表只回答「已存在的东西在哪」。创建型算子的语义
 *     正是「写入即创建」，它要的是 key，不是地址。
 *   - 帧路径来自 Frame，由调用入口**原样**写入，本层不重拼、不补斜杠、不剥斜杠；
 *     符号名来自 Program 符号池。两者都不猜。
 *   - 只接受 `LOCAL` 引用：静态/字面量参数没有「这一帧里的槽」这回事。
 *   - 放在 `vmbin` 而不是 `contract`：它依赖 `frame_path` 的语义（帧是执行期概念），
 *     contract 层不认识帧。
 */

#include <stddef.h>

#include "binrwir/execution_frame.h"
#include "contract/error.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 成功时把 `\0` 结尾的 key 写进 `buffer` 并返回 SUCCESS；缓冲区不够返回
 * INSUFFICIENT_CAPACITY，引用种类不对或帧路径/符号名缺失返回 INVALID。
 */
runtimec_status_t vmbin_frame_target_key(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity);

/*
 * **写目标的 key 文本**——两种引用种类各走各的派生：
 *   - `LOCAL`            ⇒ `<frame_path>/<符号名>`（`vmbin_frame_target_key` 那条）；
 *   - `STATIC`（绝对路径）⇒ 符号名**本身就是完整 key**（`/tmp/adk` 这类），
 *                          不需要、也不能拼 `frame_path`；
 *   - 其余（读参引用 / 字面量 / 函数）⇒ INVALID。
 *
 * 为什么要有它：容器创建（`map·new` / `struct·new` / `compactarray·new`）的写目标
 * 可以是绝对路径。曾经 `container_target_key` 直接调只接 LOCAL 的
 * `vmbin_frame_target_key`，于是写槽是 `static[N]` 时建 key 失败——**而且报成了
 * `UNPARSEABLE_LANGTYPE`**，判据指向完全错的地方（四例：`062`/`056`/`038`/
 * `abs_dynamic_key`，全部 pc=1）。
 *
 * 错误经 `error` 传出（status + 具名诊断），调用方据以翻译成自己的 `exit`。
 */
runtimec_status_t vmbin_value_access_argument_key(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
