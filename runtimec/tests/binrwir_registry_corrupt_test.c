/*
 * builtin 注册表契约（workspace/622/coding1/builtin-registry-contract.md §7/§8）的
 * 判据验证。
 *
 * 它存在的理由：**「两种失败可区分」必须由测试钉住，不能只写在注释里**。
 *   - 坏表（条目 handler 为空）→ `find` 返回 INVALID，`interpreter` 报
 *     `DIAG_CORRUPT_REGISTRY`；
 *   - 好表但没有这个 opid → `find` 返回 UNSUPPORTED，走 FOREIGN/handoff。
 * 若有人把前者改回「跳过并返回 UNSUPPORTED」，两种失败又共用一个信号，
 * 下游再也分不清「注册了没实现」与「没注册」——这里立刻红。
 *
 * 覆盖十二组：坏表 find、好表未命中、空 handler 拒装载、重复 opid 拒装载且不覆盖、
 * 模块内自重复、超限不截断、刚好装满、两个诊断码互不相等、结构标记拒装载、
 * opid 0 在 find 上拒为 INVALID、两层「没有 opid」同值、保留区真算子可装载。
 *
 * 时钟与外部依赖：无。纯内存，不连 kvspace，不建 session。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir/registry.h"
#include "vmbin/interpreter.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            failures += 1;                                                     \
        }                                                                      \
    } while (0)

/* 一个不会被调用的 handler：坏表里它的槽位是 NULL。 */
static runtimec_status_t unused_handler(binrwir_execution_frame_t *frame,
                                        const binrwir_instruction_view_t *view,
                                        binrwir_execution_exit_t *exit) {
    (void)frame;
    (void)view;
    (void)exit;
    return RUNTIMEC_STATUS_ERROR;
}

int main(void) {
    /* ① find 本身：坏条目的 opid 必须返回 INVALID，而不是跳过再返回 UNSUPPORTED。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);

        const uint32_t corrupt_identifier = 0x1234U;
        registry.storage[0].operation_identifier = corrupt_identifier;
        registry.storage[0].flags = BINRWIR_REGISTRY_FLAG_NONE;
        registry.storage[0].handler = NULL; /* 坏表 */
        registry.entry_count = 1U;

        const binrwir_registry_entry_t *entry = NULL;
        runtimec_status_t status = binrwir_registry_find(
            &registry, corrupt_identifier, &entry);
        EXPECT(status == RUNTIMEC_STATUS_INVALID);
        EXPECT(status != RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(entry == NULL);
    }

    /* ② 好表里没有的 opid：必须是 UNSUPPORTED，不是 INVALID。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        const binrwir_registry_entry_t *entry = NULL;
        EXPECT(binrwir_registry_find(&registry, 0x5678U, &entry) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(entry == NULL);
    }

    /* ③ append 的装载期校验：空 handler 必须被拒，且 registry 不被改写。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        binrwir_registry_entry_t bad = {0x9U, BINRWIR_REGISTRY_FLAG_NONE, NULL};
        runtimec_error_t error;
        EXPECT(binrwir_registry_append(&registry, "bad", &bad, 1U,
                                             &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(registry.entry_count == 0U); /* 失败不改写 */
        EXPECT(error.status == RUNTIMEC_STATUS_INVALID);
    }

    /* ④ append 的装载期校验：重复 opid 必须被拒，且**不覆盖**已有条目。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        binrwir_registry_entry_t first = {0x1U, BINRWIR_REGISTRY_FLAG_NONE,
                                       unused_handler};
        EXPECT(binrwir_registry_append(&registry, "first", &first, 1U,
                                             NULL) ==
               RUNTIMEC_STATUS_SUCCESS);
        binrwir_registry_entry_t second = {0x1U, BINRWIR_REGISTRY_FLAG_EFFECT,
                                        unused_handler};
        runtimec_error_t error;
        EXPECT(binrwir_registry_append(&registry, "second", &second, 1U,
                                             &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(registry.entry_count == 1U);
        EXPECT(registry.storage[0].flags == BINRWIR_REGISTRY_FLAG_NONE);
    }

    /* ⑤ append 的装载期校验：模块内自重复也要拒。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        binrwir_registry_entry_t twins[2] = {
            {0x2U, BINRWIR_REGISTRY_FLAG_NONE, unused_handler},
            {0x2U, BINRWIR_REGISTRY_FLAG_NONE, unused_handler},
        };
        runtimec_error_t error;
        EXPECT(binrwir_registry_append(&registry, "twins", twins, 2U,
                                             &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(registry.entry_count == 0U);
    }

    /* ⑥ 超限：硬失败、不截断。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        static binrwir_registry_entry_t many[BINRWIR_REGISTRY_ENTRY_LIMIT + 1U];
        for (size_t index = 0U; index < BINRWIR_REGISTRY_ENTRY_LIMIT + 1U;
             ++index) {
            many[index].operation_identifier = (uint32_t)(0x1000U + index);
            many[index].flags = BINRWIR_REGISTRY_FLAG_NONE;
            many[index].handler = unused_handler;
        }
        runtimec_error_t error;
        EXPECT(binrwir_registry_append(
                   &registry, "many", many, BINRWIR_REGISTRY_ENTRY_LIMIT + 1U,
                   &error) == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
        EXPECT(registry.entry_count == 0U);
    }

    /* ⑦ 容量刚好装满：必须成功。 */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        static binrwir_registry_entry_t exact[BINRWIR_REGISTRY_ENTRY_LIMIT];
        for (size_t index = 0U; index < BINRWIR_REGISTRY_ENTRY_LIMIT; ++index) {
            exact[index].operation_identifier = (uint32_t)(0x2000U + index);
            exact[index].flags = BINRWIR_REGISTRY_FLAG_NONE;
            exact[index].handler = unused_handler;
        }
        EXPECT(binrwir_registry_append(
                   &registry, "exact", exact, BINRWIR_REGISTRY_ENTRY_LIMIT,
                   NULL) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(registry.entry_count == BINRWIR_REGISTRY_ENTRY_LIMIT);
    }

    /* ⑧ 诊断码必须互不相等：这是「两种失败可区分」的机器判据。 */
    EXPECT(VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY !=
           VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION);
    EXPECT(VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY == 10);

    /*
     * ⑨ 结构标记不得注册。FOREIGN 由解释器 switch 直接分派，NONE 表示「没有
     * opid」；任一注册进来就等于给同一个 opid 造出第二条分派路径。
     *
     * 这条是**反向控制的落点**：把 registry.c 里那个守卫删掉，本组立刻红。
     * 之前的守卫写的是 `== BINRWIR_IDENTIFIER_NONE`（UINT32_MAX），opid 收窄到
     * 16 位后它恒为假，守卫虽在却从不触发——没有本组就无法发现「守卫死了」。
     */
    {
        const binrwir_operation_identifier_t markers[] = {
            BINRWIR_OPERATION_IDENTIFIER_NONE,
            BINRWIR_OPERATION_IDENTIFIER_FOREIGN,
        };

        for (size_t marker = 0U;
             marker < sizeof(markers) / sizeof(markers[0]); ++marker) {
            binrwir_registry_t registry;
            EXPECT(binrwir_registry_initialize(&registry) ==
                   RUNTIMEC_STATUS_SUCCESS);
            binrwir_registry_entry_t entry = {markers[marker],
                                              BINRWIR_REGISTRY_FLAG_NONE,
                                              unused_handler};
            runtimec_error_t error;
            EXPECT(binrwir_registry_append(&registry, "markers", &entry, 1U,
                                           &error) == RUNTIMEC_STATUS_INVALID);
            EXPECT(registry.entry_count == 0U); /* 失败不改写 */
        }
    }

    /*
     * ⑨b opid 0 在 `find` 上也必须是 INVALID，不能退化成 UNSUPPORTED。
     *
     * `UNSUPPORTED` 的语义是「本执行器不处理这个能力，交 route handoff」；而 0
     * 根本不是能力，它是 `memset(0)` 的默认值。若这里返回 UNSUPPORTED，执行期
     * 就会把「编码期没填 opid」当成「外部能力」交给下游——错误换了个地方出现。
     */
    {
        binrwir_registry_t registry;
        const binrwir_registry_entry_t *entry = NULL;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_registry_find(&registry, 0U, &entry) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(entry == NULL);
    }

    /*
     * ⑨c 两层的「没有 opid」必须是同一个值。
     *
     * 这里曾是两个数：`binrwir` 侧 0xFFFF、`vmbin` 侧 0，于是写侧校验放行 0、
     * 读侧执行到才拦。判据在编译期就能比，不必跑到运行期。
     */
    EXPECT(VMBIN_OPERATION_IDENTIFIER_NONE ==
           BINRWIR_OPERATION_IDENTIFIER_NONE);

    /*
     * ⑩ 保留区里的**真算子**必须能注册：临时 println 就住在 0xFF01–0xFF03。
     * 判据若写成 `>= RESERVED_BASE`，本组红——它保证「拦结构标记」没有被写成
     * 「拦整个保留区」。
     */
    {
        binrwir_registry_t registry;
        EXPECT(binrwir_registry_initialize(&registry) ==
               RUNTIMEC_STATUS_SUCCESS);
        binrwir_registry_entry_t reserved_operator = {
            BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE,
            BINRWIR_REGISTRY_FLAG_NONE, unused_handler};
        EXPECT(binrwir_registry_append(&registry, "reserved", &reserved_operator,
                                       1U, NULL) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(registry.entry_count == 1U);
    }

    if (failures != 0) {
        printf("binrwir_registry_corrupt_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("binrwir_registry_corrupt_test: OK (12 groups)\n");
    return 0;
}
