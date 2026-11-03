/* Frame bindings: call-time address binding, generation guard, rebinding. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/* 键视图在调用处只能由指针与长度构造（字面量宏只能用于初始化）。 */
static runtimec_key_view_t slot_key_of(const char *text, size_t size) {
    return runtimec_key_view_make(text, size);
}

static runtimec_allocator_t test_allocator(void) {
    runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    return allocator;
}

static void test_create_and_teardown(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = (binrwir_frame_bindings_t *)(uintptr_t)1U;
    binrwir_frame_bindings_t *storage =
        (binrwir_frame_bindings_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(NULL, 4U, &bindings) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(bindings == NULL);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 0U, &storage) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(storage == NULL);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 4U, NULL) ==
                    RUNTIMEC_STATUS_INVALID);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 4U, &storage) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(storage != NULL);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(storage) == 4U);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(NULL) == 0U);

    {
        const binrwir_frame_binding_t *binding = NULL;
        /* 槽在容量内但没绑定：UNSUPPORTED；越界才是 INVALID（见下面的用例）。 */
        RUNTIMEC_EXPECT(binrwir_frame_bindings_get(storage, 0U, 1U, &binding) ==
                        RUNTIMEC_STATUS_UNSUPPORTED);
        RUNTIMEC_EXPECT(binding == NULL);
    }

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(storage) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_bind_and_get(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = NULL;
    const binrwir_frame_binding_t *binding = NULL;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 4U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* 未绑定槽：取绑定必须失败，输出保持空。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 0U, 5U, &binding) ==
                    RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(binding == NULL);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 1U,
                        slot_key_of("/vthread/7/[1]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000), 5U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 1U, 5U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding->xvalue_address == UINT64_C(0x1000));
    RUNTIMEC_EXPECT(binding->generation == 5U);
    RUNTIMEC_EXPECT(binding->kind == (uint8_t)BINRWIR_BINDING_ADDRESS);

    /* generation 不匹配：拒绝，且不交出旧绑定。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 1U, 6U, &binding) ==
                    RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(binding == NULL);

    /* 同一槽重新绑定覆盖旧值。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 1U,
                        slot_key_of("/vthread/42/[3]/[0,-1]", 22U),
                        slot_key_of("/caller/b", 9U),
                        UINT64_C(0x2000), 6U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 1U, 6U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding->xvalue_address == UINT64_C(0x2000));
    RUNTIMEC_EXPECT(binding->generation == 6U);

    /* 两个不同槽可以绑定到同一个基址：别名由签名策略裁定，绑定表如实记录。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 2U,
                        slot_key_of("/vthread/42/[3]/[0,+1]", 22U),
                        slot_key_of("/caller/b", 9U),
                        UINT64_C(0x2000), 6U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 2U, 6U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding->xvalue_address == UINT64_C(0x2000));

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

static void test_bind_rejects_invalid_content(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = NULL;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* 槽号越界。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 2U,
                        slot_key_of("/vthread/1/[0]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000), 1U) == RUNTIMEC_STATUS_INVALID);
    /* 零地址不得进表。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        slot_key_of("/vthread/1/[0]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        RUNTIMEC_XVALUE_ADDRESS_NONE, 1U) ==
                    RUNTIMEC_STATUS_INVALID);
    /* slot_key 必填。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U, slot_key_of(NULL, 0U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000), 1U) == RUNTIMEC_STATUS_INVALID);
    /* generation 必填：零表示未绑定。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        slot_key_of("/vthread/1/[0]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000),
                        BINRWIR_FRAME_BINDING_GENERATION_NONE) ==
                    RUNTIMEC_STATUS_INVALID);
    /* caller_key 可以为空：本地槽没有调用方实参。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        slot_key_of("/vthread/1/[0]/local/0", 22U),
                        slot_key_of(NULL, 0U), UINT64_C(0x1000), 1U) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        NULL, 0U,
                        slot_key_of("/vthread/1/[0]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000), 1U) == RUNTIMEC_STATUS_INVALID);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

static void test_invalidate_and_rebind(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = NULL;
    const binrwir_frame_binding_t *binding = NULL;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        slot_key_of("/vthread/7/[1]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x1000), 3U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 0U, 3U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding->valid);
    RUNTIMEC_EXPECT(binding->xvalue_address == UINT64_C(0x1000));
    RUNTIMEC_EXPECT(binding->generation == 3U);
    RUNTIMEC_EXPECT(binding->slot_key.size == 21U);
    RUNTIMEC_EXPECT(binding->caller_key.size == 9U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_invalidate(bindings, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 失效后：不得交出旧绑定。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 0U, 3U, &binding) ==
                    RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(binding == NULL);
    /* 槽号越界是另一个错误类别。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 2U, 3U, &binding) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binding == NULL);

    /* 重绑后恢复。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        slot_key_of("/vthread/7/[1]/[0,-1]", 21U),
                        slot_key_of("/caller/a", 9U),
                        UINT64_C(0x3000), 4U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 0U, 4U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding->xvalue_address == UINT64_C(0x3000));
    RUNTIMEC_EXPECT(binding->generation == 4U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_invalidate(bindings, 2U) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_invalidate(NULL, 0U) ==
                    RUNTIMEC_STATUS_INVALID);

    /* reset 清空全部条目但保留容量。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_reset(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(bindings) == 2U);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 0U, 4U, &binding) ==
                    RUNTIMEC_STATUS_UNSUPPORTED);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

static void test_execution_frame_holds_bindings(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_frame_t frame;
    const binrwir_program_t *program = (const binrwir_program_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(binrwir_execution_frame_initialize(&frame, program, NULL,
                                                       5U, 6U, 11U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(frame.bindings == NULL);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);
    RUNTIMEC_EXPECT(frame.flags == BINRWIR_EXECUTION_FRAME_FLAG_INITIALIZED);

    /* 调用入口写入绑定表指针；Frame 只借用，不拥有。 */
    frame.bindings = bindings;
    RUNTIMEC_EXPECT(binrwir_execution_frame_initialize(&frame, NULL, NULL, 5U,
                                                       6U, 11U) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.bindings == NULL);

    RUNTIMEC_EXPECT(binrwir_execution_frame_initialize(&frame, program, NULL,
                                                       5U, 6U, 11U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = bindings;
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(frame.bindings == NULL);
    RUNTIMEC_EXPECT(frame.program == NULL);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(bindings) == 2U);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

int main(void) {
    test_create_and_teardown();
    test_bind_and_get();
    test_bind_rejects_invalid_content();
    test_invalidate_and_rebind();
    test_execution_frame_holds_bindings();
    if (failures != 0) {
        fprintf(stderr, "binrwir_frame_bindings_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
