#ifndef BINRWIR_EXECUTION_FRAME_H
#define BINRWIR_EXECUTION_FRAME_H

/*
 * 组件：binrwir/execution_frame
 * 作用：保存 BinRWIR 执行所需的控制状态。
 * 联系：engine route 创建 Frame；vmbin 与 BinRWIR JIT 消费。
 * 约束：Frame 不保存参数绑定或 V.body 内容。语言可见值的唯一权威副本
 *       位于 kvspace；每条源指令边界必须可观察。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_execution_frame_flags {
    BINRWIR_EXECUTION_FRAME_FLAG_NONE = 0,
    BINRWIR_EXECUTION_FRAME_FLAG_INITIALIZED = 1U << 0,
    BINRWIR_EXECUTION_FRAME_FLAG_OBSERVABLE = 1U << 1,
    BINRWIR_EXECUTION_FRAME_FLAG_RESUMED = 1U << 2
} binrwir_execution_frame_flags_t;

typedef struct binrwir_execution_frame {
    const binrwir_program_t *program;
    struct binrwir_execution_frame *parent;
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t logical_program_counter;
    uint32_t flags;
} binrwir_execution_frame_t;

#define BINRWIR_EXECUTION_FRAME_ALIGNMENT \
    ((uint32_t)_Alignof(binrwir_execution_frame_t))

runtimec_status_t binrwir_execution_frame_initialize(
    binrwir_execution_frame_t *frame, const binrwir_program_t *program,
    binrwir_execution_frame_t *parent,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter);
runtimec_status_t binrwir_execution_frame_reset(
    binrwir_execution_frame_t *frame);
runtimec_status_t binrwir_execution_frame_destroy(
    binrwir_execution_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
