#ifndef RUNTIMEC_VMJIT_BACKEND_H
#define RUNTIMEC_VMJIT_BACKEND_H

/*
 * 组件：vmjit/backend
 * 作用：单一 backend 契约：目标架构与 feature 判定及机器码生成。x86_64 与
 *       AArch64 是并列 peer，谁也不是另一个的默认模板。
 *       backend 只拥有指令编码、调用约定实现、寄存器/spill、常量池/
 *       branch fixup、可执行页切换与 instruction cache 同步；不拥有 Program 语义、
 *       eligibility 或 cache identity。
 * 联系：vmjit/compiler 选择 backend；emit 请求来自 lower；输出由
 *       code_memory 发布。backend 禁止读 RWIR、kvspace 或 route policy。
 * 约束：未知 processor 或缺 feature 必须返回 UNSUPPORTED，不得默认 x86_64。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmjit_architecture_t {
    VMJIT_ARCHITECTURE_NONE = 0,
    VMJIT_ARCHITECTURE_X86_64 = 1,
    VMJIT_ARCHITECTURE_AARCH64 = 2
} vmjit_architecture_t;
typedef struct vmjit_target_t {
    vmjit_architecture_t architecture;
    runtimec_string_view_t triple;
    uint64_t feature_mask;
} vmjit_target_t;

struct vmjit_lowered_program;

typedef struct vmjit_emit_request_t {
    const struct vmjit_lowered_program *lowered_program;
    runtimec_bytes_buffer_t writable_code;
} vmjit_emit_request_t;

typedef struct vmjit_emit_result_t {
    size_t machine_code_size;
    size_t entry_byte_offset;
} vmjit_emit_result_t;
typedef struct vmjit_backend_t {
    vmjit_architecture_t architecture;
    runtimec_string_view_t name;
    bool (*supports)(const vmjit_target_t *target);
    runtimec_status_t (*emit)(
        const vmjit_emit_request_t *request,
        vmjit_emit_result_t *output_result,
        runtimec_error_t *error);
} vmjit_backend_t;
runtimec_status_t vmjit_backend_for_target(
    const vmjit_target_t *target,
    const vmjit_backend_t **output_backend);
runtimec_status_t vmjit_x86_64_backend(
    const vmjit_backend_t **output_backend);
runtimec_status_t vmjit_aarch64_backend(
    const vmjit_backend_t **output_backend);

#ifdef __cplusplus
}
#endif

#endif
