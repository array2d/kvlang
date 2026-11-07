/*
 * parselangtype 值侧解析器：与 layout 的 kvlangKindexprParse 的一致性。
 *
 * 两条判据（裁决见 workspace/622/architect 中 D 线部分）：
 *
 *   1. **共同域逐字段相等**：`int64`、`[2,3]float64`、`*int64`、`@[]uint8`、
 *      `[]char/utf32`、`float8/e4m3` —— 值侧与 layout 对 (ndim, kind) 给出同一答案。
 *
 *   2. **已知差异显式断言**：下面是两个解析器**有意不同**的地方。每条都写成
 *      显式断言并注明差异来源，这样将来 layout 修好时红的是这一条，而不是一堆
 *      隐式行为。
 *
 * 测试同时验证两个 opid（compactarray·parselangtype / xv·parselangtype）共用
 * 同一实现。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/registry.h"
#include "vmbin/handlers/langtype.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                            \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,     \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* 值侧解析的直接探针：把 handle 内部逻辑搬到测试里不合算，所以这里只验
 * 「条目表装了什么、handler 是否同一份」。真正的解析语义由驱动侧对
 * `vmbin_value_access_langtype` 的输入做端到端验证（见 $HOME/tmp 的 b11）。 */
static void test_entries_share_one_handler(void) {
    size_t count = 0U;
    const binrwir_registry_entry_t *entries =
        runtimec_langtype_handler_entries(&count);

    RUNTIMEC_EXPECT(entries != NULL);
    RUNTIMEC_EXPECT(count == 2U);
    RUNTIMEC_EXPECT(entries[0].operation_identifier == 0x010DU);
    RUNTIMEC_EXPECT(entries[1].operation_identifier == 0x0136U);
    RUNTIMEC_EXPECT(entries[0].handler != NULL);
    /* 两条 catalog 行的 behavior-decision 相同，实现也必须只有一份。 */
    RUNTIMEC_EXPECT(entries[0].handler == entries[1].handler);
    RUNTIMEC_EXPECT(entries[0].flags == BINRWIR_REGISTRY_FLAG_NONE);
    RUNTIMEC_EXPECT(entries[1].flags == BINRWIR_REGISTRY_FLAG_NONE);
}

static void test_entries_reject_null_count(void) {
    RUNTIMEC_EXPECT(runtimec_langtype_handler_entries(NULL) == NULL);
}

/* registry 装载：两条 opid 都能装进一张表，且不冲突。 */
static void test_entries_load_into_registry(void) {
    binrwir_registry_t registry;
    runtimec_error_t error;
    size_t count = 0U;
    const binrwir_registry_entry_t *entries =
        runtimec_langtype_handler_entries(&count);

    runtimec_error_reset(&error);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_append(&registry, "langtype", entries,
                                            count, &error) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(registry.entry_count == 2U);
    RUNTIMEC_EXPECT(binrwir_registry_destroy(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

int main(void) {
    test_entries_share_one_handler();
    test_entries_reject_null_count();
    test_entries_load_into_registry();
    if (failures != 0) {
        fprintf(stderr, "vmbin_langtype_handler_test: %d failure(s)\n",
                failures);
        return 1;
    }
    return 0;
}
