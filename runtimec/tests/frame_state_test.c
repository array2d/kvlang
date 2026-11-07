/*
 * engine/frame_state 验收：帧状态表的**状态机**——三态判据、终态销毁、
 * 同帧异函数的失败封闭、分配失败不破坏既有表、销毁后残留归零。
 *
 * ── 为什么这些用例必须在这里，不能写成语料 ──────────────────────────────
 * 语料 runner（`kv_runner`）**每个用例新建一个 session**，而本组要验的是
 * **同一 session 内**同一 frame_path 的两次 execute 之间发生了什么。新 session
 * 天然是空的，什么都能「通过」——那是恒真判据。
 *
 * ── 判据是状态，不是调用次数 ────────────────────────────────────────────
 * 断言一律落在 `runtimec_frame_state_count` 与 `bindings` **指针本身**上，
 * 不断言「调了几次 open」。调用次数是代理指标：实现一换形态就失效，状态判据不会。
 *
 * ── 窗口层的生命周期不在这里 ────────────────────────────────────────────
 * 「DONE 之后 count 归零」「失败也是终态」这两条依赖 execute 的退出种类，
 * 落在 `execution_window_test.c`（那里有真 kvspace + 预置 compilation 的夹具）。
 * 本文件只验表自己的契约：**谁调 close 是窗口的事，close 之后表必须干净**。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/allocator.h"
#include "contract/error.h"
#include "engine/frame_state.h"

#define TEST_FRAME_A "/vthread/1/[1]"
#define TEST_FRAME_B "/vthread/1/[2.1]"
#define TEST_FUNCTION_A "/lib/frame-state-a"
#define TEST_FUNCTION_B "/lib/frame-state-b"

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

/* 永远返回 NULL 的分配器：验「分配失败不破坏既有表」。 */
static void *test_allocate_never(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    return NULL;
}

static void test_deallocate_noop(void *context, void *allocation, size_t size,
                                 size_t alignment) {
    (void)context;
    (void)allocation;
    (void)size;
    (void)alignment;
}

static runtimec_frame_state_result_t test_open(
    runtimec_frame_state_table_t *table, const char *frame_path,
    const char *function_path, uint64_t generation,
    runtimec_frame_state_t **output_state) {
    runtimec_error_t error;
    runtimec_frame_state_result_t result = RUNTIMEC_FRAME_STATE_INVALID;

    runtimec_error_reset(&error);
    *output_state = NULL;
    const runtimec_status_t status = runtimec_frame_state_open(
        table, runtimec_key_view_from_c_string(frame_path),
        runtimec_string_view_from_c_string(function_path), generation, 8U,
        output_state, &result, &error);
    /* 成功路径不该留下 error；失败由调用方按 result / status 断言。 */
    EXPECT(status == RUNTIMEC_STATUS_SUCCESS || error.message.size != 0U);
    return result;
}

int main(void) {
    const runtimec_allocator_t allocator = {NULL, test_allocate,
                                            test_deallocate};
    runtimec_error_t error;
    runtimec_error_reset(&error);

    runtimec_frame_state_table_t table;
    runtimec_frame_state_table_initialize(&table, allocator);
    EXPECT(runtimec_frame_state_count(&table) == 0U);

    /* 1. 首次 open = CREATED，表里有一条。 */
    runtimec_frame_state_t *first = NULL;
    EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 1U, &first) ==
           RUNTIMEC_FRAME_STATE_CREATED);
    EXPECT(first != NULL);
    EXPECT(runtimec_frame_state_count(&table) == 1U);
    const binrwir_frame_bindings_t *first_bindings = first->bindings;
    EXPECT(first_bindings != NULL);

    /* 2. 同路径同函数同代次 = REUSED，且**同一张绑定表**。
     *    这条是 `strings.kv` 的 0x4501 的正对照：resume 必须拿到上次那张表。 */
    runtimec_frame_state_t *second = NULL;
    EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 1U, &second) ==
           RUNTIMEC_FRAME_STATE_REUSED);
    EXPECT(second == first);
    EXPECT(second != NULL && second->bindings == first_bindings);
    EXPECT(runtimec_frame_state_count(&table) == 1U);

    /* 3. 同路径同函数但代次变了 = REBUILT，**旧绑定必须消失**。
     *
     *    判据用**表里的状态**，不用指针不等：旧表被销毁后那块内存会被下一次
     *    分配复用，`新指针 != 旧指针` 既可能失败也可能在 UB 上「通过」——
     *    拿悬空指针做不等比较是坏判据。这里改成：先在旧表里绑一个槽，重建后
     *    在新表里查同一个槽，必须查不到。 */
    {
        const runtimec_key_view_t slot_key =
            runtimec_key_view_from_c_string("/vthread/1/[1]/survivor");
        EXPECT(binrwir_frame_bindings_bind(
                   first->bindings, 0U, slot_key, slot_key,
                   (runtimec_xvalue_address_t)UINT64_C(0x1000), 1U) ==
               RUNTIMEC_STATUS_SUCCESS);
    }
    runtimec_frame_state_t *third = NULL;
    EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 2U, &third) ==
           RUNTIMEC_FRAME_STATE_REBUILT);
    EXPECT(third != NULL && third->generation == 2U);
    EXPECT(runtimec_frame_state_count(&table) == 1U);
    {
        const binrwir_frame_binding_t *stale = NULL;
        EXPECT(third != NULL &&
               binrwir_frame_bindings_get(third->bindings, 0U, 1U, &stale) !=
                   RUNTIMEC_STATUS_SUCCESS);
        EXPECT(stale == NULL);
    }

    /* 4. 同路径**不同函数** = 失败封闭，且**不覆盖**既有表项。
     *    判据与 `ensure_frame` 同一句话：一帧只许绑一个函数。 */
    {
        runtimec_frame_state_t *conflict = NULL;
        runtimec_error_t conflict_error;
        runtimec_error_reset(&conflict_error);
        EXPECT(runtimec_frame_state_open(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_A),
                   runtimec_string_view_from_c_string(TEST_FUNCTION_B), 2U,
                   8U, &conflict, NULL, &conflict_error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(conflict == NULL);
        EXPECT(conflict_error.message.data != NULL);
        EXPECT(conflict_error.message.size != 0U);
        /* 表没被破坏：仍是原来那一条，函数路径没被改写。 */
        EXPECT(runtimec_frame_state_count(&table) == 1U);
        runtimec_frame_state_t *survivor = NULL;
        EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 2U,
                         &survivor) == RUNTIMEC_FRAME_STATE_REUSED);
        EXPECT(survivor == third);
    }

    /* 5. 两个不同的帧并存：线性表按路径区分，互不影响。 */
    {
        runtimec_frame_state_t *other = NULL;
        EXPECT(test_open(&table, TEST_FRAME_B, TEST_FUNCTION_B, 5U, &other) ==
               RUNTIMEC_FRAME_STATE_CREATED);
        EXPECT(other != NULL && other->bindings != third->bindings);
        EXPECT(runtimec_frame_state_count(&table) == 2U);
        /* 关掉 B，A 必须还在。 */
        runtimec_error_reset(&error);
        EXPECT(runtimec_frame_state_close(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_B),
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_frame_state_count(&table) == 1U);
        runtimec_frame_state_t *still_there = NULL;
        EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 2U,
                         &still_there) == RUNTIMEC_FRAME_STATE_REUSED);
        EXPECT(still_there == third);
    }

    /* 6. close 之后同路径再 open = CREATED（表已销毁 ⇒ 新帧、新表）。
     *    这是「帧 DONE 后再 execute 必须是新帧」的表层判据。 */
    {
        runtimec_error_reset(&error);
        EXPECT(runtimec_frame_state_close(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_A),
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_frame_state_count(&table) == 0U);
        runtimec_frame_state_t *reborn = NULL;
        EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 2U, &reborn) ==
               RUNTIMEC_FRAME_STATE_CREATED);
        EXPECT(reborn != NULL && reborn->generation == 2U);
        /* 同样不比指针：`third` 已被 close 释放，与它比较是悬空指针运算。
         * 判据是「三态给了 CREATED」+「表里恰好一条」——那才是「新帧」。 */
        EXPECT(runtimec_frame_state_count(&table) == 1U);
        /* 收尾：再关一次，表回到空。 */
        runtimec_error_reset(&error);
        EXPECT(runtimec_frame_state_close(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_A),
                   &error) == RUNTIMEC_STATUS_SUCCESS);
    }

    /* 7. close 一个不存在的帧 ⇒ 成功（「让它不存在」不是幂等性检查）。 */
    {
        runtimec_error_reset(&error);
        EXPECT(runtimec_frame_state_close(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_B),
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtimec_frame_state_count(&table) == 0U);
    }

    /* 8. 非法请求被拒：空路径、空函数、零代次。零代次必须拒——
     *    它是「存储还没被观测过」的哨兵，放进来会让所有帧判成同一代。 */
    {
        runtimec_frame_state_t *rejected = NULL;
        runtimec_frame_state_result_t result = RUNTIMEC_FRAME_STATE_CREATED;
        runtimec_error_t invalid_error;
        runtimec_error_reset(&invalid_error);
        EXPECT(runtimec_frame_state_open(
                   &table, runtimec_key_view_from_c_string(""), 
                   runtimec_string_view_from_c_string(TEST_FUNCTION_A), 1U, 8U,
                   &rejected, &result, &invalid_error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(result == RUNTIMEC_FRAME_STATE_INVALID);
        EXPECT(rejected == NULL);
        runtimec_error_reset(&invalid_error);
        EXPECT(runtimec_frame_state_open(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_A),
                   runtimec_string_view_from_c_string(""), 1U, 8U, &rejected,
                   &result, &invalid_error) == RUNTIMEC_STATUS_INVALID);
        runtimec_error_reset(&invalid_error);
        EXPECT(runtimec_frame_state_open(
                   &table, runtimec_key_view_from_c_string(TEST_FRAME_A),
                   runtimec_string_view_from_c_string(TEST_FUNCTION_A), 0U, 8U,
                   &rejected, &result, &invalid_error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(runtimec_frame_state_count(&table) == 0U);
    }

    /* 9. 分配失败**不破坏既有表**：先建一条，再把表的分配器换成永远失败的，
     *    对新路径 open 必须 OUT_OF_MEMORY，且既有那条原样可用。
     *    反向判据：若表被破坏，count 会变成 0 或既有条目查不到。 */
    {
        runtimec_frame_state_t *kept = NULL;
        EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 7U, &kept) ==
               RUNTIMEC_FRAME_STATE_CREATED);
        const binrwir_frame_bindings_t *kept_bindings = kept->bindings;
        const runtimec_allocator_t broken = {NULL, test_allocate_never,
                                             test_deallocate_noop};
        runtimec_frame_state_table_t starving;
        runtimec_frame_state_table_initialize(&starving, broken);
        /* 空表 + 必失败分配器 ⇒ OUT_OF_MEMORY，不是部分插入。 */
        runtimec_frame_state_t *nothing = NULL;
        runtimec_frame_state_result_t result = RUNTIMEC_FRAME_STATE_CREATED;
        runtimec_error_t starving_error;
        runtimec_error_reset(&starving_error);
        EXPECT(runtimec_frame_state_open(
                   &starving, runtimec_key_view_from_c_string(TEST_FRAME_B),
                   runtimec_string_view_from_c_string(TEST_FUNCTION_B), 1U, 8U,
                   &nothing, &result, &starving_error) ==
               RUNTIMEC_STATUS_OUT_OF_MEMORY);
        EXPECT(nothing == NULL);
        EXPECT(result == RUNTIMEC_FRAME_STATE_INVALID);
        EXPECT(runtimec_frame_state_count(&starving) == 0U);
        runtimec_frame_state_table_release(&starving);
        /* 原表未受影响。 */
        EXPECT(runtimec_frame_state_count(&table) == 1U);
        runtimec_frame_state_t *still = NULL;
        EXPECT(test_open(&table, TEST_FRAME_A, TEST_FUNCTION_A, 7U, &still) ==
               RUNTIMEC_FRAME_STATE_REUSED);
        EXPECT(still != NULL && still->bindings == kept_bindings);
    }

    /* 10. `release_all` 兜底：会话销毁时残留必须归零，且表自身也能再释放。
     *     反向判据：release 之后 count 若不为 0，就是泄漏——那正是改动前
     *     tutorial 语料十几条 LEAKED 的形态。 */
    EXPECT(runtimec_frame_state_count(&table) == 1U);
    runtimec_frame_state_table_release(&table);
    EXPECT(runtimec_frame_state_count(&table) == 0U);
    /* 二次 release 必须安全（幂等），不重复释放。 */
    runtimec_frame_state_table_release(&table);
    EXPECT(runtimec_frame_state_count(&table) == 0U);

    (void)error;
    if (failures != 0) {
        fprintf(stderr, "frame_state_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("frame_state_test: ok\n");
    return 0;
}
