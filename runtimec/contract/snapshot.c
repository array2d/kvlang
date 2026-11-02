/*
 * 组件：contract/snapshot 的实现
 * 作用：版本、source revision 与快照的构造/重置/形状校验；纯值操作。
 * 联系：engine 在取 Program 前构造快照；host 提供 source_revision；
 *       vmkv 在编译前后校验同一快照，拒绝事实变化期间产生的 Program；
 *       vmjit cache key 也引用它。
 */

#include "contract/snapshot.h"

runtimec_version_t runtimec_contract_version(void) {
    runtimec_version_t version;
    version.major = (uint16_t)RUNTIMEC_CONTRACT_VERSION_MAJOR;
    version.minor = (uint16_t)RUNTIMEC_CONTRACT_VERSION_MINOR;
    version.patch = (uint16_t)RUNTIMEC_CONTRACT_VERSION_PATCH;
    version.binary_interface =
        (uint16_t)RUNTIMEC_CONTRACT_BINARY_INTERFACE_VERSION;
    return version;
}

void runtimec_source_revision_reset(runtimec_source_revision_t *revision) {
    if (revision != NULL) {
        revision->instance_identifier = 0U;
        revision->generation = 0U;
        revision->schema_version = 0U;
        revision->instruction_set_version = 0U;
    }
}

void runtimec_snapshot_reset(runtimec_snapshot_t *snapshot) {
    if (snapshot != NULL) {
        snapshot->contract_version = runtimec_contract_version();
        runtimec_source_revision_reset(&snapshot->source_revision);
        snapshot->function_key = runtimec_key_view_make(NULL, 0U);
        snapshot->logical_program_counter =
            runtimec_string_view_make(NULL, 0U);
    }
}

bool runtimec_snapshot_is_valid(const runtimec_snapshot_t *snapshot) {
    return snapshot != NULL &&
           snapshot->contract_version.binary_interface ==
               RUNTIMEC_CONTRACT_BINARY_INTERFACE_VERSION &&
           runtimec_key_view_is_valid(snapshot->function_key) &&
           runtimec_string_view_is_valid(
               snapshot->logical_program_counter);
}
