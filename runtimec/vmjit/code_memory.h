#ifndef RUNTIMEC_VMJIT_CODE_MEMORY_H
#define RUNTIMEC_VMJIT_CODE_MEMORY_H

/*
 * 组件：vmjit/code_memory
 * 作用：可执行页的生命周期：分配可写页 -> emit/relocate -> 切换可执行
 *       并取消可写 -> 平台 instruction cache 同步 -> 由 code lease 管理回收。
 *       内存不可同时可写与可执行。
 * 联系：backend 写入 writable_view；vmjit compiler 调 publish；完成的
 *       BinRWIR compilation 管理机器码生命周期。
 * 约束：发布失败或 entitlement 不足时返回错误，绝不执行未发布内存。
 */

#include <stddef.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_code_memory vmjit_code_memory_t;
runtimec_status_t vmjit_code_memory_create(
    size_t capacity,
    vmjit_code_memory_t **output_memory,
    runtimec_error_t *error);
runtimec_status_t vmjit_code_memory_writable_view(
    vmjit_code_memory_t *memory,
    runtimec_bytes_buffer_t *output_buffer,
    runtimec_error_t *error);
runtimec_status_t vmjit_code_memory_publish(
    vmjit_code_memory_t *memory,
    size_t code_size,
    runtimec_error_t *error);
runtimec_status_t vmjit_code_memory_destroy(vmjit_code_memory_t *memory);

#ifdef __cplusplus
}
#endif

#endif
