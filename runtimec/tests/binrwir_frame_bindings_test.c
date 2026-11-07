/* Frame bindings: call-time address binding, generation guard, rebinding. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/binvthread_frame.h"
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

static void test_binvthread_frame_holds_bindings(void) {
    runtimec_allocator_t allocator = test_allocator();
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_binvthread_frame_t frame;
    const binrwir_binfunc_t *program = (const binrwir_binfunc_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(binrwir_binvthread_frame_initialize(&frame, program, NULL,
                                                       5U, 6U, 11U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(frame.bindings == NULL);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);
    RUNTIMEC_EXPECT(frame.flags == BINRWIR_BINVTHREAD_FRAME_FLAG_INITIALIZED);

    /* 调用入口写入绑定表指针；Frame 只借用，不拥有。 */
    frame.bindings = bindings;
    RUNTIMEC_EXPECT(binrwir_binvthread_frame_initialize(&frame, NULL, NULL, 5U,
                                                       6U, 11U) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.bindings == NULL);

    RUNTIMEC_EXPECT(binrwir_binvthread_frame_initialize(&frame, program, NULL,
                                                       5U, 6U, 11U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = bindings;
    RUNTIMEC_EXPECT(binrwir_binvthread_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(frame.bindings == NULL);
    RUNTIMEC_EXPECT(frame.program == NULL);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(bindings) == 2U);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

/*
 * **绑定表自己拥有 key 字节**。
 *
 * 为什么要单列：帧准备与 handler 都是「拼一个 key 缓冲 → 绑上 → 立刻释放缓冲」。
 * 若绑定表存借用视图，那就是 use-after-free，而它今天不会立刻炸（堆还没复用），
 * 只在换台机器或换分配器时变成读错数据。所以判据不是「跑通了」，是三件事：
 *   1. 源缓冲释放之后，绑定的 key 仍然可读且内容正确；
 *   2. 重绑同一槽会释放旧字节（用分配计数证明，不看「跑通了」）；
 *   3. 销毁后活分配归零。
 * 配合 ASan 跑：若某天退回到借用，第 1 步会直接报 heap-use-after-free。
 */
typedef struct counting_allocator {
    size_t allocations;
    size_t deallocations;
    size_t live;
} counting_allocator_t;

static void *counting_allocate(void *context, size_t size, size_t alignment) {
    counting_allocator_t *counter = (counting_allocator_t *)context;
    void *allocation = malloc(size);
    (void)alignment;
    if (allocation != NULL) {
        ++counter->allocations;
        ++counter->live;
    }
    return allocation;
}

static void counting_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    counting_allocator_t *counter = (counting_allocator_t *)context;
    (void)size;
    (void)alignment;
    if (allocation != NULL) {
        ++counter->deallocations;
        --counter->live;
        free(allocation);
    }
}

static void test_keys_are_owned(void) {
    counting_allocator_t counter = {0U, 0U, 0U};
    runtimec_allocator_t allocator = {
        .context = &counter,
        .allocate = counting_allocate,
        .deallocate = counting_deallocate,
    };
    binrwir_frame_bindings_t *bindings = NULL;
    const binrwir_frame_binding_t *binding = NULL;
    static const char slot_text[] = "/vthread/1/[1]/container";
    static const char caller_text[] = "/vthread/1/[0]/[0,-1]";
    char *slot_buffer;
    char *caller_buffer;
    size_t after_bind;

    RUNTIMEC_EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 缓冲故意用堆：释放后若表仍借用，ASan 会当场报 use-after-free。 */
    slot_buffer = (char *)malloc(sizeof(slot_text) - 1U);
    caller_buffer = (char *)malloc(sizeof(caller_text) - 1U);
    memcpy(slot_buffer, slot_text, sizeof(slot_text) - 1U);
    memcpy(caller_buffer, caller_text, sizeof(caller_text) - 1U);

    RUNTIMEC_EXPECT(
        binrwir_frame_bindings_bind(bindings, 1U,
                                    slot_key_of(slot_buffer,
                                                sizeof(slot_text) - 1U),
                                    slot_key_of(caller_buffer,
                                                sizeof(caller_text) - 1U),
                                    UINT64_C(0x2000), 3U) ==
        RUNTIMEC_STATUS_SUCCESS);
    free(slot_buffer);
    free(caller_buffer);

    /* 1. 源缓冲已释放，绑定里的 key 仍必须是原字节。 */
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 1U, 3U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binding != NULL);
    if (binding != NULL) {
        RUNTIMEC_EXPECT(binding->slot_key.size == sizeof(slot_text) - 1U);
        RUNTIMEC_EXPECT(memcmp(binding->slot_key.data, slot_text,
                               sizeof(slot_text) - 1U) == 0);
        RUNTIMEC_EXPECT(binding->caller_key.size == sizeof(caller_text) - 1U);
        RUNTIMEC_EXPECT(memcmp(binding->caller_key.data, caller_text,
                               sizeof(caller_text) - 1U) == 0);
    }
    /*
     * 计数判据：创建占 3 块（表 / 条目 / 槽键指针表），绑定再多 1 块（key 字节）。
     * 不写死 4——先记下现值，再比增量，别的分配不会把判据带歪。
     */
    after_bind = counter.live;

    /* 2. 重绑同一槽：旧 key 块被释放、新块被分配，活分配数不变。 */
    RUNTIMEC_EXPECT(
        binrwir_frame_bindings_bind(bindings, 1U,
                                    slot_key_of("/vthread/1/[1]/other", 20U),
                                    slot_key_of(NULL, 0U), UINT64_C(0x2000),
                                    3U) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counter.live == after_bind);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_get(bindings, 1U, 3U, &binding) ==
                    RUNTIMEC_STATUS_SUCCESS);
    if (binding != NULL) {
        RUNTIMEC_EXPECT(binding->slot_key.size == 20U);
        RUNTIMEC_EXPECT(memcmp(binding->slot_key.data, "/vthread/1/[1]/other",
                               20U) == 0);
    }

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 3. 销毁后不允许有活分配残留。 */
    RUNTIMEC_EXPECT(counter.live == 0U);
}

int main(void) {
    test_create_and_teardown();
    test_bind_and_get();
    test_bind_rejects_invalid_content();
    test_invalidate_and_rebind();
    test_binvthread_frame_holds_bindings();
    test_keys_are_owned();
    if (failures != 0) {
        fprintf(stderr, "binrwir_frame_bindings_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
