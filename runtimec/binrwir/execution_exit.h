#ifndef BINRWIR_EXECUTION_EXIT_H
#define BINRWIR_EXECUTION_EXIT_H

/*
 * 组件：binrwir/execution_exit
 * 作用：表达 vmbin 或 BinRWIR JIT 离开执行窗口的唯一结构化结果。
 * 联系：engine route 消费退出并决定调用、交接、恢复或报错。
 * 约束：只携带控制信息。语言可见值必须已写回 V.body；不得携带值、机器
 *       地址、重试或反优化请求，所选执行目标失败即失败。
 */

#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE UINT32_MAX

typedef enum binrwir_execution_exit_kind {
    BINRWIR_EXECUTION_EXIT_NONE = 0,
    BINRWIR_EXECUTION_EXIT_DONE,
    BINRWIR_EXECUTION_EXIT_CALL,
    BINRWIR_EXECUTION_EXIT_BUILTIN,
    BINRWIR_EXECUTION_EXIT_HANDOFF,
    BINRWIR_EXECUTION_EXIT_YIELD,
    BINRWIR_EXECUTION_EXIT_ERROR
} binrwir_execution_exit_kind_t;

typedef enum binrwir_execution_exit_flags {
    BINRWIR_EXECUTION_EXIT_FLAG_NONE = 0,
    BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED = 1U << 0
} binrwir_execution_exit_flags_t;

typedef struct binrwir_execution_exit {
    uint16_t kind; /* binrwir_execution_exit_kind_t */
    uint16_t flags; /* binrwir_execution_exit_flags_t */
    runtimec_status_t status; /* 组件状态，与结构化退出原因正交 */
    uint32_t logical_program_counter;
    uint32_t resume_logical_program_counter;
    uint32_t detail_code;       /* 结构化细节码（拒绝原因/错误分类） */
    uint32_t target_identifier;
} binrwir_execution_exit_t;

_Static_assert(sizeof(binrwir_execution_exit_t) == 24U,
               "binrwir structured exits must contain only control state");

#ifdef __cplusplus
}
#endif

#endif
