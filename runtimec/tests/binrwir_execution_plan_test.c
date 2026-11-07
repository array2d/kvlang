/*
 * Execution plan 的失败形状。构造属于 binrwir_compiler/optimization，目前仍是
 * NOT_IMPLEMENTED 桩，所以这里只钉住「没有 plan 时三个入口都不伪装成功」。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_plan.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void test_plan_query_failure_shape(void) {
    binrwir_execution_plan_view_t view;

    memset(&view, 0xff, sizeof(view));
    RUNTIMEC_EXPECT(binrwir_execution_plan_get_view(NULL, &view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.program == NULL);
    RUNTIMEC_EXPECT(view.type_specialization_count == 0U);
    RUNTIMEC_EXPECT(view.type_specializations == NULL);
    RUNTIMEC_EXPECT(view.identity.plan_generation == UINT64_MAX);
    RUNTIMEC_EXPECT(view.identity.compiler_revision == UINT32_MAX);

    RUNTIMEC_EXPECT(binrwir_execution_plan_validate(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_execution_plan_destroy(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_plan_query_failure_shape();
    if (failures != 0) {
        fprintf(stderr, "binrwir_execution_plan_test: %d failure(s)\n",
                failures);
        return 1;
    }
    return 0;
}
