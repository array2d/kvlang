/*
 * 组件：vmbin/interpreter 的实现
 * 作用：唯一 vmbin 执行入口的失败桩。
 * 约束：所有解释策略只能位于本执行器内部，不建立第三执行器。
 */

#include "interpreter.h"
#include "interpreter_dispatch.h"

#include <string.h>

runtimec_status_t
vmbin_execute(vmbin_frame_t *frame,
              const vmbin_builtin_registry_t *builtin_registry,
              vmbin_profile_t *profile, vmbin_exit_t *output_exit) {
    (void)profile;

    if (output_exit != NULL) {
        memset(output_exit, 0, sizeof(*output_exit));
        output_exit->kind = VMBIN_EXIT_ERROR;
        output_exit->status = RUNTIMEC_STATUS_NOT_IMPLEMENTED;
        output_exit->logical_program_counter = VMBIN_PROGRAM_COUNTER_NONE;
        output_exit->resume_logical_program_counter =
            VMBIN_PROGRAM_COUNTER_NONE;
    }
    if (frame == NULL || builtin_registry == NULL || output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
