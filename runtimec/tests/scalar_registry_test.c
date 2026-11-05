/*
 * `scalar` 模块的条目表：每一条都能被 `registry_find` 找到，且 finding 到的
 * handler 就是它自己的那一条。
 *
 * 为什么需要这一层：`scalar_entries` 是文件内 `static`，谁也看不见它；模块对外
 * 只暴露 `runtimec_scalar_handler_entries`。所以「某个 opid 到底注册没有」这件事
 * **只能**经这两条真实路径验证——看源码里的表不算证明，表可能是死的。
 *
 * 本测试盯的是一类具体的漏：**结构上成对、实现里只写了一半**。
 * `ptreq`(0x0021) 有而 `ptreq·not`(0x002A) 没有，就是这种——两侧邻居都在、
 * 它不在。整族缺失会被当成进度，单点漏会被当成已完成，所以更危险。
 */

#include <stdio.h>
#include <string.h>

#include "binrwir/registry.h"
#include "vmbin/handlers/scalar.h"

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* 把模块条目装进一个真 registry——`find` 只在 registry 上工作。 */
static void load_registry(binrwir_registry_t *registry) {
    runtimec_error_t error;
    size_t count = 0U;
    const binrwir_registry_entry_t *entries =
        runtimec_scalar_handler_entries(&count);

    EXPECT(entries != NULL);
    EXPECT(count != 0U);
    runtimec_error_reset(&error);
    EXPECT(binrwir_registry_initialize(registry) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_registry_append(registry, "scalar", entries, count,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
}

static void test_every_entry_is_findable(void) {
    binrwir_registry_t registry;
    size_t count = 0U;
    const binrwir_registry_entry_t *entries =
        runtimec_scalar_handler_entries(&count);

    load_registry(&registry);

    /* 逐条：它自己能被找到，且找到的 handler 不是别人的。
     *
     * 只验「能找到」不够——如果两条 opid 都指向同一个 handler，`find` 对两个
     * opid 都成功，但其中一条的实现是错的。所以还要比对 handler 指针。 */
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const binrwir_registry_entry_t *found = NULL;

        EXPECT(binrwir_registry_find(&registry,
                                     entries[ordinal].operation_identifier,
                                     &found) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(found != NULL);
        if (found != NULL) {
            EXPECT(found->handler == entries[ordinal].handler);
        }
    }
}

/*
 * 成对结构：三对「正向 / 取反」都必须在表里，且**各自有自己的 handler**。
 *
 * 判据是成对存在，不是逐个点名——点名会在下次加第四对时又漏。但配对本身要写死
 * （谁与谁是一对是语义，不是可推导的），所以这里列三对。
 */
static void test_counterpart_pairs_are_complete(void) {
    static const struct {
        binrwir_operation_identifier_t positive;
        binrwir_operation_identifier_t negative;
        const char *description;
    } pairs[] = {
        {BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
         BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
         "ptreq / ptreq·not"},
        {BINRWIR_OPERATION_IDENTIFIER_CHARACTER_EQUAL,
         BINRWIR_OPERATION_IDENTIFIER_CHARACTER_NOT_EQUAL,
         "streq / streq·not"},
        {BINRWIR_OPERATION_IDENTIFIER_BOOLEAN_EQUAL,
         BINRWIR_OPERATION_IDENTIFIER_BOOLEAN_NOT_EQUAL,
         "bool·eq / bool·neq"},
    };
    binrwir_registry_t registry;

    load_registry(&registry);

    for (size_t ordinal = 0U;
         ordinal < sizeof(pairs) / sizeof(pairs[0]); ++ordinal) {
        const binrwir_registry_entry_t *positive = NULL;
        const binrwir_registry_entry_t *negative = NULL;

        EXPECT(binrwir_registry_find(&registry, pairs[ordinal].positive,
                                     &positive) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_registry_find(&registry, pairs[ordinal].negative,
                                     &negative) ==
               RUNTIMEC_STATUS_SUCCESS);
        /* 两侧必须是**不同的** handler：共用一个就等于「取反」没有实现。 */
        if (positive != NULL && negative != NULL) {
            EXPECT(positive->handler != negative->handler);
        }
    }
}

int main(void) {
    test_every_entry_is_findable();
    test_counterpart_pairs_are_complete();
    if (failures != 0) {
        fprintf(stderr, "scalar_registry_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "scalar_registry_test: OK\n");
    return 0;
}
