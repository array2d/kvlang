#ifndef BINRWIR_BINVTHREAD_FRAME_H
#define BINRWIR_BINVTHREAD_FRAME_H

/*
 * 组件：binrwir/binvthread_frame
 * 作用：保存 BinRWIR 执行所需的控制状态，并持有本次调用的帧绑定表指针。
 * 联系：engine route 创建 Frame；vmbin 在调用入口写入绑定表，vmbin 与 JIT 消费。
 * 约束：Frame 持有的是可丢弃、可重建的地址绑定（槽 → XValue 基址 + generation），
 *       不是值缓存：语言可见值的唯一权威副本位于 kvspace，Frame 不保存 V.body
 *       内容；每条源指令边界必须可观察。
 */

#include <stdint.h>

#include "contract/kvspace_access.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/binfunc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_binvthread_frame_flags {
    BINRWIR_BINVTHREAD_FRAME_FLAG_NONE = 0,
    BINRWIR_BINVTHREAD_FRAME_FLAG_INITIALIZED = 1U << 0,
    BINRWIR_BINVTHREAD_FRAME_FLAG_OBSERVABLE = 1U << 1,
    BINRWIR_BINVTHREAD_FRAME_FLAG_RESUMED = 1U << 2
} binrwir_binvthread_frame_flags_t;

typedef struct binrwir_binvthread_frame {
    const binrwir_binfunc_t *program;
    struct binrwir_binvthread_frame *parent;
    /* 本次调用的地址绑定表；由调用入口建立并拥有，Frame 只借用该指针。 */
    binrwir_frame_bindings_t *bindings;
    /*
     * kvspace 句柄（`kvspaceConnect` 的原样返回值），session 拥有，Frame 借用。
     * 类型刻意是 `void *` 而不是任何一个包装类型：需要存储的 handler 直接
     * include `kvspace/kvspace.h` 调 ABI，本层不提供窄接口、不做转发。
     */
    void *kvspace_handle;
    /*
     * 本帧的路径（`/vthread/{vid}/[d]`），由调用入口用 `request->frame_path`
     * **原样**写入——不重新拼接。拼接就是造第二份地址，一有出入就分不清哪份对。
     *
     * 借用视图：字节归调用方，Frame 不拷贝、不释放，只在本次调用内有效。
     * 尾斜杠只出现一次（由调用方给的路径决定），本层不再补也不再剥。
     */
    runtimec_key_view_t frame_path;
    /*
     * host services（借用，可为 NULL）。值访问写路径需要两件只有 host 才能做的事：
     *   - 按 langtype **创建**尚不存在的写目标（`ensure_zero_value`，零值规则的
     *     唯一实现）；
     *   - 对 STATIC 且 payload 为 NONE 的写目标做**执行期**地址解析。
     * 由执行入口填入；其它构造点保持 NULL，此时未解析的写目标失败封闭。
     */
    runtimec_kvspace_access_t *kvspace_access;
    /* 与 host services 配套的快照（源世代）；借用的拷贝。 */
    runtimec_snapshot_t snapshot;
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t logical_program_counter;
    uint32_t flags;
} binrwir_binvthread_frame_t;

#define BINRWIR_BINVTHREAD_FRAME_ALIGNMENT \
    ((uint32_t)_Alignof(binrwir_binvthread_frame_t))

runtimec_status_t binrwir_binvthread_frame_initialize(
    binrwir_binvthread_frame_t *frame, const binrwir_binfunc_t *program,
    binrwir_binvthread_frame_t *parent,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter);
runtimec_status_t binrwir_binvthread_frame_reset(
    binrwir_binvthread_frame_t *frame);
/* 只清空借用指针与标志，不销毁绑定表（所有权归调用入口）。 */
runtimec_status_t binrwir_binvthread_frame_destroy(
    binrwir_binvthread_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
