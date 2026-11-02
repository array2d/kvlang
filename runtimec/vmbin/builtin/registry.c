/*
 * 组件：vmbin/builtin/registry 的实现
 * 作用：初始化只读条目表；查找业务保持失败关闭。
 */

#include "registry.h"

#include <string.h>

runtimec_status_t
vmbin_builtin_registry_initialize(vmbin_builtin_registry_t *registry,
                                  const vmbin_builtin_entry_t *entries,
                                  size_t entry_count) {
    if (registry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    if (entry_count != 0U && entries == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    registry->entries = entries;
    registry->entry_count = entry_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_builtin_registry_reset(
    vmbin_builtin_registry_t *registry) {
    if (registry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_builtin_registry_destroy(
    vmbin_builtin_registry_t *registry) {
    return vmbin_builtin_registry_reset(registry);
}

runtimec_status_t
vmbin_builtin_registry_find(const vmbin_builtin_registry_t *registry,
                            uint32_t builtin_identifier,
                            const vmbin_builtin_entry_t **output_entry) {
    (void)registry;
    (void)builtin_identifier;
    if (output_entry != NULL) {
        *output_entry = NULL;
    }
    if (registry == NULL || builtin_identifier == VMBIN_IDENTIFIER_NONE ||
        output_entry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
