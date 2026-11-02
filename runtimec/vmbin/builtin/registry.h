#ifndef VMBIN_BUILTIN_REGISTRY_H
#define VMBIN_BUILTIN_REGISTRY_H

/*
 * 组件：vmbin/builtin/registry
 * 作用：将 builtin 标识符映射到唯一处理函数。
 * 联系：interpreter 调用；vmjit 可以内联或退出，但不另建语义实现。
 * 约束：Registry 不拥有条目或处理函数，生命周期属于 session。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "vmbin/exit.h"
#include "vmbin/frame.h"
#include "vmbin/instruction.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef runtimec_status_t
(*vmbin_builtin_handler_function_t)(
    vmbin_frame_t *frame,
    const vmbin_instruction_view_t *instruction,
    vmbin_exit_t *output_exit);

typedef enum vmbin_builtin_flags {
    VMBIN_BUILTIN_FLAG_NONE = 0,
    VMBIN_BUILTIN_FLAG_EFFECT = 1U << 0,
    VMBIN_BUILTIN_FLAG_MAY_YIELD = 1U << 1,
    VMBIN_BUILTIN_FLAG_REQUIRES_MATERIALIZATION = 1U << 2
} vmbin_builtin_flags_t;

typedef struct vmbin_builtin_entry {
    uint32_t builtin_identifier;
    uint32_t flags;
    vmbin_builtin_handler_function_t handler;
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
