#ifndef VMBIN_EXIT_H
#define VMBIN_EXIT_H

/*
 * 组件：vmbin/exit
 * 作用：表达 interpreter 或 vmjit 离开执行窗口的唯一结构化结果。
 * 联系：engine route 消费退出并决定调用、交接、恢复或报错。
 * 约束：不得携带需要持久化的机器地址或伪造成功状态。
 */

#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_PROGRAM_COUNTER_NONE UINT32_MAX

typedef enum vmbin_exit_kind {
    VMBIN_EXIT_NONE = 0,
    VMBIN_EXIT_DONE,
    VMBIN_EXIT_CALL,
    VMBIN_EXIT_BUILTIN,
    VMBIN_EXIT_HANDOFF,
    VMBIN_EXIT_YIELD,
    VMBIN_EXIT_DEOPTIMIZATION,
    VMBIN_EXIT_ERROR
} vmbin_exit_kind_t;

typedef enum vmbin_exit_flags {
    VMBIN_EXIT_FLAG_NONE = 0,
    VMBIN_EXIT_FLAG_FRAME_MATERIALIZED = 1U << 0,
    VMBIN_EXIT_FLAG_RETRY_LOGICAL_PROGRAM_COUNTER = 1U << 1
} vmbin_exit_flags_t;

typedef struct vmbin_exit {
    uint16_t kind;              /* vmbin_exit_kind_t：为什么离开窗口 */
    uint16_t flags;             /* vmbin_exit_flags_t：物化与重试提示 */
    runtimec_status_t status;   /* 组件状态，与结构化退出原因正交 */
    uint32_t logical_program_counter;
    uint32_t resume_logical_program_counter;
    uint32_t detail_code;       /* 结构化细节码（拒绝原因/错误分类） */
    uint32_t target_identifier;
    uint32_t value_base;        /* 退出携带值的起始槽（如需要） */
    uint32_t value_count;       /* 退出携带值的数量 */
    uint64_t payload_low;       /* 有限普通数据负载，低 64 位 */
    uint64_t payload_high;      /* 有限普通数据负载，高 64 位 */
} vmbin_exit_t;

_Static_assert(sizeof(vmbin_exit_t) == 48U,
               "vmbin structured exit must remain 48 bytes");

#ifdef __cplusplus
}
#endif

#endif
