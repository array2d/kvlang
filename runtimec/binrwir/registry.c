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
    /* 空表也是一张可用的表：零条目是合法状态（未装 any builtin 的 session
     * 依赖它）。索引立即就绪，find 能回答「表里没有」。 */
    registry->index_ready = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 重建两张索引。装载期在每次 append 末尾调用一次——条目数是个位数到百位数，
 * 重建成本可以忽略；运行期零维护，find 不加锁、不改表。
 *
 * 用「重建」而不是「增量插入」：增量插入要在两处维护同一不变量（存储顺序与
 * 索引一致性），而重建只有一处。装载不是热路径，不值得为它省这一次 O(n)。
 */
static void binrwir_registry_rebuild_index(binrwir_registry_t *registry) {
    size_t entry_ordinal;
    size_t base_count = 0U;

    memset(registry->specialized_index, 0, sizeof(registry->specialized_index));
    registry->base_index_count = 0U;
    registry->specialized_index_count = 0U;

    for (entry_ordinal = 0U; entry_ordinal < registry->entry_count;
         ++entry_ordinal) {
        const binrwir_operation_identifier_t identifier =
            registry->storage[entry_ordinal].operation_identifier;

        if (identifier >= BINRWIR_SPECIALIZED_BASE &&
            identifier < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
            /* 特化区：直接下标。`+1` 让 0 表示「没有条目」。 */
            const size_t offset =
                (size_t)(identifier - BINRWIR_SPECIALIZED_BASE);
            registry->specialized_index[offset] = (uint16_t)(entry_ordinal + 1U);
            registry->specialized_index_count += 1U;
        } else {
            /* 非特化区的**全部**条目都进这张数组——包括保留区（`0xFF00` 以上）。
             *
             * 保留区里确实住着算子（临时 println `0xFF01–0xFF03`，见
             * `vmbin/temp_println.h`），`append` 也特意放行它们。早先把保留区
             * 排除在索引外，于是「注册了却查不到」：任何临时 println 在执行期
             * 都拿 `CORRUPT_REGISTRY`。
             *
             * 判据：**能不能分派由注册表里有没有这条决定，不由 opid 落在哪个
             * 区间决定**。结构标记（FOREIGN/NONE）在 `append` 期就已被拒绝。 */
            registry->base_index[base_count] = (uint16_t)entry_ordinal;
            base_count += 1U;
        }
    }
    /* 基础区要能二分，必须按 opid 升序。插入排序：条目数是个位数到百位数，
     * 且装载期只跑一次——这里选「显然正确」而不是「常数更小」。 */
    for (size_t left = 1U; left < base_count; ++left) {
        const uint16_t carried = registry->base_index[left];
        const binrwir_operation_identifier_t carried_identifier =
            registry->storage[carried].operation_identifier;
        size_t position = left;

        while (position > 0U &&
               registry->storage[registry->base_index[position - 1U]]
                       .operation_identifier > carried_identifier) {
            registry->base_index[position] = registry->base_index[position - 1U];
            position -= 1U;
        }
        registry->base_index[position] = carried;
    }
    registry->base_index_count = base_count;
    registry->index_ready = true;
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
    binrwir_registry_rebuild_index(registry);
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 查找。两段式：特化区直接下标 O(1)，基础区二分 O(log n)。
 *
 * 保留区（>= RESERVED_BASE）与表外 opid 一律 `UNSUPPORTED`——**不是 INVALID**：
 * INVALID 只说「请求本身不成立」，而这里请求是好的，只是本执行器不处理它。
 */
runtimec_status_t
binrwir_registry_find(const binrwir_registry_t *registry,
                            binrwir_operation_identifier_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry) {
    size_t entry_ordinal = SIZE_MAX;

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
    /* 表被绕过 append 构造过（没走过索引重建）：拒绝，不去线性兜底。
     * 兜底会让「表坏了」看起来像「表是好的只是没有这条」。 */
    if (!registry->index_ready) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * 绕过 append 直接写 `storage` 的情形：索引里查不到，但存储里可能有。
     * 那说明表被写坏过（append 是唯一合法入口，它每次都重建索引）。
     *
     * 判据是「索引覆盖的条目数 < 存储条目数」——两者不等即表被绕过过。
     * 此时返回 INVALID 而不是 UNSUPPORTED：`UNSUPPORTED` 是「本执行器不处理
     * 这个能力」，而这里根本没有一张可信的表。
     */
    if (registry->base_index_count + registry->specialized_index_count !=
        registry->entry_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (operation_identifier >= BINRWIR_SPECIALIZED_BASE &&
        operation_identifier < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
        const size_t offset =
            (size_t)(operation_identifier - BINRWIR_SPECIALIZED_BASE);
        const uint16_t slot = registry->specialized_index[offset];

        if (slot == 0U) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        entry_ordinal = (size_t)(slot - 1U);
    } else {
        /* 非特化区（含保留区里已注册的临时算子）走二分。 */
        size_t low = 0U;
        size_t high = registry->base_index_count;

        while (low < high) {
            const size_t middle = low + (high - low) / 2U;
            const binrwir_operation_identifier_t probe =
                registry->storage[registry->base_index[middle]]
                    .operation_identifier;

            if (probe < operation_identifier) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        if (low >= registry->base_index_count ||
            registry->storage[registry->base_index[low]]
                    .operation_identifier != operation_identifier) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        entry_ordinal = registry->base_index[low];
    }
    /* 装载期已拒绝空 handler，走到这里说明表被绕过 append 写坏了。
     * 返回 INVALID 而不是跳过：跳过会让「注册了但没实现」伪装成「没注册」，
     * 两者在调用方看来都是 UNSUPPORTED。 */
    if (entry_ordinal >= registry->entry_count ||
        registry->storage[entry_ordinal].handler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_entry = &registry->storage[entry_ordinal];
    return RUNTIMEC_STATUS_SUCCESS;
}
