/*
 * 组件：vmjit 的 scaffold 实现
 * 作用：所有未实现阶段显式返回 NOT_IMPLEMENTED；唯一可选执行入口在
 *       无机器码能力时显式返回 FALLBACK，绝不伪造已执行结果。
 * 联系：engine/route 是唯一策略调用者；其余函数只构成 vmjit 内部流水线。
 * 约束：只接受 verifier 产生的 verified Program，不解释 RWIR 或 BinRWIR。
 */

#include "vmjit/vmjit.h"

#include <stddef.h>

#include "vmjit/code_memory.h"
#include "vmjit/lower.h"
#include "vmjit/plan.h"

static void vmjit_refusal_reset(
    vmjit_refusal_t *refusal,
    vmjit_refusal_reason_t reason) {
    if (refusal != NULL) {
        refusal->reason = reason;
        refusal->logical_program_counter = 0U;
        refusal->detail_code = 0U;
    }
}

runtimec_status_t vmjit_execution_interface(
    vmjit_execution_interface_t *output_interface) {
    if (output_interface != NULL) {
        *output_interface = (vmjit_execution_interface_t){0};
    }
    if (output_interface == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_compiler_create(
    runtimec_allocator_t allocator,
    const vmjit_config_t *config,
    vmjit_compiler_t **output_compiler,
    runtimec_error_t *error) {
    (void)allocator;
    (void)error;
    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (config == NULL || output_compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_compiler_destroy(vmjit_compiler_t *compiler) {
    if (compiler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_verify_program(
    const vmbin_program_t *program,
    const vmjit_target_t *target,
    vmjit_verified_program_t *output_verified_program,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_verified_program != NULL) {
        *output_verified_program = (vmjit_verified_program_t){0};
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_INVALID_PROGRAM);
    if (program == NULL || target == NULL || output_verified_program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_DISABLED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_compile(
    vmjit_compiler_t *compiler,
    const vmjit_verified_program_t *verified_program,
    vmjit_code_t **output_code,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_code != NULL) {
        *output_code = NULL;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_INVALID_PROGRAM);
    if (compiler == NULL || verified_program == NULL ||
        verified_program->program == NULL || output_code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_DISABLED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_retain(vmjit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_release(vmjit_code_t *code) {
    if (code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_get_machine_code_function(
    const vmjit_code_t *code,
    vmjit_machine_code_function_t *output_function) {
    if (output_function != NULL) {
        *output_function = NULL;
    }
    if (code == NULL || output_function == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_plan_create(
    const vmjit_verified_program_t *verified_program,
    const vmjit_target_t *target,
    const vmjit_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    vmjit_plan_t **output_plan,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_plan != NULL) {
        *output_plan = NULL;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_INVALID_PROGRAM);
    if (verified_program == NULL || verified_program->program == NULL ||
        target == NULL || options == NULL || allocator == NULL ||
        output_plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_DISABLED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_plan_destroy(vmjit_plan_t *plan) {
    if (plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_lower(
    const vmjit_plan_t *plan,
    const runtimec_allocator_t *allocator,
    vmjit_lowered_program_t **output_program,
    runtimec_error_t *error) {
    (void)error;
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (plan == NULL || allocator == NULL || output_program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_lowered_program_destroy(
    vmjit_lowered_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_memory_create(
    size_t capacity,
    vmjit_code_memory_t **output_memory,
    runtimec_error_t *error) {
    (void)capacity;
    (void)error;
    if (output_memory != NULL) {
        *output_memory = NULL;
    }
    if (output_memory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_memory_writable_view(
    vmjit_code_memory_t *memory,
    runtimec_bytes_buffer_t *output_buffer,
    runtimec_error_t *error) {
    (void)error;
    if (output_buffer != NULL) {
        runtimec_bytes_buffer_reset(output_buffer);
    }
    if (memory == NULL || output_buffer == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_memory_publish(
    vmjit_code_memory_t *memory,
    size_t code_size,
    runtimec_error_t *error) {
    (void)code_size;
    (void)error;
    if (memory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_memory_destroy(vmjit_code_memory_t *memory) {
    if (memory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_cache_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    vmjit_code_cache_t **output_cache,
    runtimec_error_t *error) {
    (void)allocator;
    (void)capacity;
    (void)error;
    if (output_cache != NULL) {
        *output_cache = NULL;
    }
    if (output_cache == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_cache_destroy(vmjit_code_cache_t *cache) {
    if (cache == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_cache_clear(vmjit_code_cache_t *cache) {
    if (cache == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_cache_invalidate(
    vmjit_code_cache_t *cache,
    const runtimec_snapshot_t *snapshot) {
    if (cache == NULL || snapshot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmjit_code_cache_get_or_compile(
    vmjit_code_cache_t *cache,
    vmjit_compiler_t *compiler,
    const vmjit_cache_key_t *key,
    const vmjit_verified_program_t *verified_program,
    vmjit_code_t **output_code,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_code != NULL) {
        *output_code = NULL;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_INVALID_PROGRAM);
    if (cache == NULL || compiler == NULL || key == NULL ||
        verified_program == NULL || verified_program->program == NULL ||
        output_code == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmjit_refusal_reset(refusal, VMJIT_REFUSAL_DISABLED);
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

vmjit_execution_result_t vmjit_try_execute(
    vmjit_compiler_t *compiler,
    vmjit_code_cache_t *cache,
    const vmjit_verified_program_t *verified_program,
    vmbin_frame_t *frame,
    runtimec_error_t *error) {
    vmjit_execution_result_t result = {0};
    (void)error;
    result.outcome = VMJIT_EXECUTION_OUTCOME_ERROR;
    result.status = RUNTIMEC_STATUS_INVALID;
    result.exit.kind = VMBIN_EXIT_ERROR;
    result.exit.status = RUNTIMEC_STATUS_INVALID;
    if (compiler == NULL || cache == NULL || verified_program == NULL ||
        verified_program->program == NULL || frame == NULL) {
        return result;
    }
    result.outcome = VMJIT_EXECUTION_OUTCOME_FALLBACK;
    result.status = RUNTIMEC_STATUS_FALLBACK;
    result.exit.kind = VMBIN_EXIT_NONE;
    result.exit.status = RUNTIMEC_STATUS_FALLBACK;
    return result;
}
