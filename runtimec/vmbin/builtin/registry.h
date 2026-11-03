#ifndef VMBIN_BUILTIN_REGISTRY_H
#define VMBIN_BUILTIN_REGISTRY_H

/*
 * 组件：vmbin/builtin/registry
 * 作用：将 builtin 标识符映射到唯一处理函数。
 * 联系：interpreter 调用；处理函数经 vmbin/value_access 按地址读写操作数，
 *       与 core 规则共用同一条数据面。
 * 约束：Registry 不拥有条目或处理函数，生命周期属于 session；处理函数
 *       不得缓存 V.body 内容或建立寄存器文件。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 处理函数经 vmbin/value_access 按地址读写操作数；它不拥有 frame、instruction
 * 与 output_exit，也不得保留它们的指针。
 */
typedef runtimec_status_t
(*vmbin_builtin_rwir_runtimecimpl_function_t)(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit);

typedef enum vmbin_builtin_flags {
    VMBIN_BUILTIN_FLAG_NONE = 0,
    VMBIN_BUILTIN_FLAG_EFFECT = 1U << 0,
    VMBIN_BUILTIN_FLAG_MAY_YIELD = 1U << 1
} vmbin_builtin_flags_t;

typedef struct vmbin_builtin_entry {
    uint32_t builtin_identifier;
    uint32_t flags;
    vmbin_builtin_rwir_runtimecimpl_function_t rwir_runtimecimpl;
} vmbin_builtin_entry_t;

typedef struct vmbin_builtin_registry {
    const vmbin_builtin_entry_t *entries;
    size_t entry_count;
} vmbin_builtin_registry_t;

runtimec_status_t
vmbin_builtin_registry_initialize(vmbin_builtin_registry_t *registry,
                                  const vmbin_builtin_entry_t *entries,
                                  size_t entry_count);
runtimec_status_t vmbin_builtin_registry_reset(
    vmbin_builtin_registry_t *registry);
runtimec_status_t vmbin_builtin_registry_destroy(
    vmbin_builtin_registry_t *registry);
runtimec_status_t
vmbin_builtin_registry_find(const vmbin_builtin_registry_t *registry,
                            uint32_t builtin_identifier,
                            const vmbin_builtin_entry_t **output_entry);

#ifdef __cplusplus
}
#endif

#endif
