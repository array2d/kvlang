/*
 * engine/compilation_repository 验收：key 匹配语义（snapshot 四字段 + 四个
 * 版本号）、租约计数、失效不打断在租约、容量淘汰、以及失败封闭。
 *
 * 这里用替身 compilation（一个非空指针）验证所有权与租约计数，不触发真实
 * 编译器；compilation 的 destroy 目前是桩，因此本用例只验证到「entry 被回收
 * 时不再持有它」这一层。
 *
 * **删除记录**：原第 11 条断言「依赖构造失败时错误消息以 `dependency failed: `
 * 开头并点名组件」。`host_services` 删除后依赖不再是可注入的函数表，
 * 该失败形态**不可达**，无法改写成状态断言；改成「传空 kvspace access ⇒ 失败、
 * session 保持 NULL、消息非空」，保留「不留半初始化 + 可定位」这两条性质。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/compilation_repository.h"
#include "engine/session.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
        }                                                                      \
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

/* 构造一份形状合法的 key；generation 变化即代表 source 变了。 */
static runtimec_compilation_repository_key_t make_key(uint64_t generation,
                                                      uint32_t compiler_revision) {
    runtimec_compilation_repository_key_t key;
    memset(&key, 0, sizeof(key));
    runtimec_snapshot_reset(&key.snapshot);
    key.snapshot.source_revision.instance_identifier = 7U;
    key.snapshot.source_revision.generation = generation;
    key.snapshot.source_revision.schema_version = 1U;
    key.snapshot.source_revision.instruction_set_version = 2U;
    key.snapshot.function_key = runtimec_key_view_from_c_string("/lib/sum");
    key.snapshot.source_position_text =
        runtimec_string_view_from_c_string("/lib/sum/[1,0]");
    key.rwir_manifest_version = 3U;
    key.canonical_encoding_revision = 4U;
    key.binrwir_binary_interface_generation = 5U;
    key.compiler_revision = compiler_revision;
    return key;
}

int main(void) {
    const runtimec_allocator_t allocator = {NULL, test_allocate,
                                            test_deallocate};
    runtimec_error_t error;
    runtimec_error_reset(&error);

    /* 1. 非法创建参数。 */
    {
        runtimec_compilation_repository_t *repository = NULL;
        runtimec_allocator_t empty_allocator = {NULL, NULL, NULL};
        EXPECT(runtimec_compilation_repository_create(
                   empty_allocator, 4U, &repository, &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(repository == NULL);
        EXPECT(runtimec_compilation_repository_create(
                   allocator, 0U, &repository, &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(repository == NULL);
    }

    runtimec_compilation_repository_t *repository = NULL;
    EXPECT(runtimec_compilation_repository_create(allocator, 2U, &repository,
                                                  &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(repository != NULL);

    /* 2. 空仓库 find = NOT_FOUND（不是错误）。 */
    {
        const runtimec_compilation_repository_key_t key = make_key(1U, 1U);
        runtimec_compilation_repository_lookup_result_t lookup_result =
            RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
        runtimec_compilation_repository_lease_t lease;
        memset(&lease, 0, sizeof(lease));
        EXPECT(runtimec_compilation_repository_find(
                   repository, &key, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND);
        EXPECT(lease.compilation == NULL);
    }

    /* 3. 非法 key（instance_identifier 为 0）被拒。 */
    {
        runtimec_compilation_repository_key_t key = make_key(1U, 1U);
        key.snapshot.source_revision.instance_identifier = 0U;
        runtimec_compilation_repository_lookup_result_t lookup_result;
        runtimec_compilation_repository_lease_t lease;
        EXPECT(runtimec_compilation_repository_find(
                   repository, &key, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(lookup_result ==
               RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID);
    }

    /* 4. insert + find 命中；lease.snapshot 指向仓库自己的副本。 */
    int compilation_storage[2];
    {
        const runtimec_compilation_repository_key_t key = make_key(1U, 1U);
        runtimec_compilation_repository_lease_t inserted_lease;
        memset(&inserted_lease, 0, sizeof(inserted_lease));
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &key, (binrwir_compilation_t *)&compilation_storage,
                   &inserted_lease, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(inserted_lease.compilation ==
               (const binrwir_compilation_t *)&compilation_storage);
        EXPECT(inserted_lease.token != NULL);
        /* snapshot 的文本视图必须指向仓库副本，而不是调用方字符串。 */
        EXPECT(inserted_lease.snapshot.function_key.data !=
               key.snapshot.function_key.data);
        EXPECT(inserted_lease.snapshot.function_key.size ==
               key.snapshot.function_key.size);
        EXPECT(memcmp(inserted_lease.snapshot.function_key.data,
                      key.snapshot.function_key.data,
                      key.snapshot.function_key.size) == 0);

        runtimec_compilation_repository_lookup_result_t lookup_result =
            RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
        runtimec_compilation_repository_lease_t lease;
        memset(&lease, 0, sizeof(lease));
        EXPECT(runtimec_compilation_repository_find(
                   repository, &key, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND);
        EXPECT(lease.compilation ==
               (const binrwir_compilation_t *)&compilation_storage);
        EXPECT(runtimec_compilation_repository_release(repository, &lease) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lease.token == NULL);

        /* 5. 同一 key 重复 insert 被拒（调用方应先 find）。 */
        runtimec_compilation_repository_lease_t duplicate_lease;
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &key, (binrwir_compilation_t *)&compilation_storage,
                   &duplicate_lease, &error) == RUNTIMEC_STATUS_INVALID);

        /* 6. 只有版本号之一不同 = 不同条目，不命中。 */
        runtimec_compilation_repository_key_t other = key;
        other.compiler_revision = 2U;
        runtimec_compilation_repository_lookup_result_t other_result;
        runtimec_compilation_repository_lease_t other_lease;
        EXPECT(runtimec_compilation_repository_find(
                   repository, &other, &other_result, &other_lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(other_result ==
               RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND);

        /* 7. 失效后不再发放租约（NOT_FOUND），但仍被租约持有的条目不被释放。 */
        EXPECT(runtimec_compilation_repository_invalidate(
                   repository, &key.snapshot) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_compilation_repository_lookup_result_t after_result;
        runtimec_compilation_repository_lease_t after_lease;
        EXPECT(runtimec_compilation_repository_find(
                   repository, &key, &after_result, &after_lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(after_result ==
               RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND);
        /* 还有 inserted_lease 在身：compilation 指针此刻必须仍然有效。 */
        EXPECT(inserted_lease.compilation ==
               (const binrwir_compilation_t *)&compilation_storage);
        EXPECT(runtimec_compilation_repository_release(repository,
                                                       &inserted_lease) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    /* 8. clear 之后所有条目不可命中。 */
    {
        const runtimec_compilation_repository_key_t key = make_key(2U, 1U);
        runtimec_compilation_repository_lease_t inserted_lease;
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &key, (binrwir_compilation_t *)&compilation_storage,
                   &inserted_lease, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository,
                                                       &inserted_lease) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_clear(repository) ==
               RUNTIMEC_STATUS_SUCCESS);
        runtimec_compilation_repository_lookup_result_t lookup_result;
        runtimec_compilation_repository_lease_t lease;
        EXPECT(runtimec_compilation_repository_find(
                   repository, &key, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND);
    }

    /* 9. 容量 2：插入第 3 条淘汰最旧的无租约条目（不返回错误）。 */
    {
        const runtimec_compilation_repository_key_t first = make_key(10U, 1U);
        const runtimec_compilation_repository_key_t second = make_key(11U, 1U);
        const runtimec_compilation_repository_key_t third = make_key(12U, 1U);
        runtimec_compilation_repository_lease_t lease_first;
        runtimec_compilation_repository_lease_t lease_second;
        runtimec_compilation_repository_lease_t lease_third;
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &first, (binrwir_compilation_t *)&compilation_storage,
                   &lease_first, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository, &lease_first) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &second, (binrwir_compilation_t *)&compilation_storage,
                   &lease_second, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository, &lease_second) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &third, (binrwir_compilation_t *)&compilation_storage,
                   &lease_third, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository, &lease_third) ==
               RUNTIMEC_STATUS_SUCCESS);
        runtimec_compilation_repository_lookup_result_t lookup_result;
        runtimec_compilation_repository_lease_t lease;
        EXPECT(runtimec_compilation_repository_find(
                   repository, &first, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lookup_result ==
               RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND);
        EXPECT(runtimec_compilation_repository_find(
                   repository, &third, &lookup_result, &lease, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND);
        EXPECT(runtimec_compilation_repository_release(repository, &lease) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    /* 10. 双重释放同一租约被拒。 */
    {
        const runtimec_compilation_repository_key_t key = make_key(20U, 1U);
        runtimec_compilation_repository_lease_t lease;
        EXPECT(runtimec_compilation_repository_insert(
                   repository, &key, (binrwir_compilation_t *)&compilation_storage,
                   &lease, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository, &lease) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_compilation_repository_release(repository, &lease) ==
               RUNTIMEC_STATUS_INVALID);
    }

    EXPECT(runtimec_compilation_repository_destroy(repository) ==
           RUNTIMEC_STATUS_SUCCESS);

    /*
     * 11. 创建失败时**不留下半初始化 session**，且错误消息非空。
     *
     * 原用例是「依赖缺失时点名组件」（错误消息以 `dependency failed: ` 开头）。
     * `host_services` 删除后**这条性质不再可达**：依赖不再是可注入的函数表，
     * 全是编译进二进制的模块，`session_create` 只剩「请求非法」与「分配失败」
     * 两种失败。所以本条改成它在新形态下**仍然成立**的部分：传空 kvspace access
     * ⇒ 返回失败、`*output_session` 保持 NULL、错误消息非空（可定位，不是哑失败）。
     */
    {
        runtimec_session_options_t options;
        memset(&options, 0, sizeof(options));
        options.structure_size = sizeof(options);
        options.allocator = allocator;
        options.compilation_cache_capacity = 8U;
        options.machine_code_capacity = 4096U;
        options.enable_jit = true;
        runtimec_session_t *session = NULL;
        runtimec_error_t session_error;
        runtimec_error_reset(&session_error);
        const runtimec_status_t status = runtimec_session_create(
            NULL, &options, &session, &session_error);
        EXPECT(session == NULL);
        EXPECT(status != RUNTIMEC_STATUS_SUCCESS);
        EXPECT(session_error.message.data != NULL);
        EXPECT(session_error.message.size != 0U);
    }

    if (failures != 0) {
        fprintf(stderr, "engine_repository_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("engine_repository_test: ok\n");
    return 0;
}
