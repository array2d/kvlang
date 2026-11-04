/*
 * 组件：binrwir/registry 的实现
 * 作用：置空、按模块追加（装载期校验）、查找（区分「表坏」与「没这个 opid」）。
 * 约束：条目拷贝进 registry 自带的定长存储；append 先全验后写入。
 */

#include "registry.h"

#include <string.h>

#include "contract/view.h"

static runtimec_status_t registry_fail(runtimec_error_t *error,
                                       runtimec_status_t status, int64_t code,
                                       const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t binrwir_registry_initialize(
    binrwir_registry_t *registry) {
    if (registry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_registry_reset(
    binrwir_registry_t *registry) {
    return binrwir_registry_initialize(registry);
}

runtimec_status_t binrwir_registry_destroy(
    binrwir_registry_t *registry) {
    return binrwir_registry_reset(registry);
}

runtimec_status_t binrwir_registry_append(
    binrwir_registry_t *registry, const char *module_name,
    const binrwir_registry_entry_t *entries, size_t entry_count,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (registry == NULL || module_name == NULL ||
        (entry_count != 0U && entries == NULL)) {
        return registry_fail(error, RUNTIMEC_STATUS_INVALID, 1,
                             "invalid builtin registry append request");
    }
    if (entry_count == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (entry_count > BINRWIR_REGISTRY_ENTRY_LIMIT - registry->entry_count) {
        return registry_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 2,
            "builtin registry is full; capacity is not extendable");
    }

    /* 先全部校验：任一条不合格就整体拒绝，registry 保持不变。 */
    for (size_t fresh = 0U; fresh < entry_count; ++fresh) {
        const binrwir_registry_entry_t *entry = &entries[fresh];

        if (entry->handler == NULL) {
            return registry_fail(error, RUNTIMEC_STATUS_INVALID, 3,
                                 "builtin module registers an entry with no handler");
        }
        /*
         * FOREIGN 与 NONE 是**结构标记**，不是算子：前者由解释器的 switch 直接
         * 处理（就地交还控制权），后者表示「没有 opid」。两者都轮不到注册表，
         * 注册进来等于给同一个 opid 造出第二条分派路径。
         *
         * 判据刻意**不是** `>= RESERVED_BASE`：保留区里确实住着算子——临时
         * println 三条注册在 0xFF01–0xFF03（见 binrwir/instruction.h 的占用
         * 记账），它们是 in-process builtin，正是本表存在的理由。用band 判据会
         * 在 RUNTIMEC_TEMP_PRINTLN=ON 时把整个 session 建不起来。
         */
        if (entry->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
            entry->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
            return registry_fail(
                error, RUNTIMEC_STATUS_INVALID, 4,
                "builtin module registers a structural marker instead of an "
                "operator identifier");
        }
        for (size_t known = 0U; known < registry->entry_count; ++known) {
            if (registry->storage[known].operation_identifier ==
                entry->operation_identifier) {
                return registry_fail(
                    error, RUNTIMEC_STATUS_INVALID, 5,
                    "builtin identifier is registered twice; later module is "
                    "rejected rather than overwriting the earlier one");
            }
        }
        for (size_t peer = fresh + 1U; peer < entry_count; ++peer) {
            if (entries[peer].operation_identifier == entry->operation_identifier) {
                return registry_fail(
                    error, RUNTIMEC_STATUS_INVALID, 6,
                    "builtin module registers the same identifier twice");
            }
        }
    }

    memcpy(&registry->storage[registry->entry_count], entries,
           entry_count * sizeof(*entries));
    registry->entry_count += entry_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 线性查找：注册表是 session 拥有的只读小表，不引入第二条索引。 */
runtimec_status_t
binrwir_registry_find(const binrwir_registry_t *registry,
                            binrwir_operation_identifier_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry) {
    size_t entry_ordinal;

    if (output_entry != NULL) {
        *output_entry = NULL;
    }
    /* 0 与 NONE 都不是 opid：前者是「结构体没填」的默认值，后者是显式哨兵。
     * 两者都不能靠「查不到 → UNSUPPORTED」蒙混过去——UNSUPPORTED 的语义是
     * 「本执行器不处理这个能力」，而 0 根本不是能力。 */
    if (registry == NULL || operation_identifier == 0U ||
        operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
        output_entry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (entry_ordinal = 0U; entry_ordinal < registry->entry_count;
         ++entry_ordinal) {
        const binrwir_registry_entry_t *entry = &registry->storage[entry_ordinal];

        /* 装载期已拒绝空 handler，走到这里说明表被绕过 append 写坏了。
         * 返回 INVALID 而不是跳过：跳过会让「注册了但没实现」伪装成「没注册」，
         * 两者在调用方看来都是 UNSUPPORTED。 */
        if (entry->handler == NULL) {
            return RUNTIMEC_STATUS_INVALID;
        }
        if (entry->operation_identifier == operation_identifier) {
            *output_entry = entry;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    /* 没有这一条不是错误，是「本执行器不处理」：交 route handoff。 */
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
