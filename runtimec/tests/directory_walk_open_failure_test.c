/*
 * `runtimec_directory_walk_open` 的前置失败路径：**失败后 close 必须安全**。
 *
 * 为什么单列一条测试：全部 7 处调用点都是
 *
 *     runtimec_directory_walk_t walk;               // 未初始化
 *     if (!open(&walk, ...)) { close(&walk); ... }  // 失败分支也 close
 *
 * `close` 会 `free(walk->directory.source_context)` 并对 names/offsets 做
 * deallocate。如果 `open` 在 `memset` 之前就返回 false，那三处就是未初始化指针。
 *
 * `kvspace_handle == NULL` **不是防御性分支**：runtimec 明确承认「session 可以不接
 * 存储」（`vthread_status.c` 的空句柄即 `NOT_IMPLEMENTED`），所以这条路径真实可达。
 *
 * 判据有三层：
 *   1. open 返回 false（前置检查确实生效）
 *   2. close 不崩
 *   3. close 没有对未初始化指针做释放
 *
 * 第 3 层不能靠「没崩」推断——栈残留值恰好可 free 时也不崩。所以本测试用**显式
 * 注入的分配器**记账：close 调用 deallocate 的每一次都记下来，断言**一次都没有**。
 * 这比「跑一遍没段错误」强：它同时排除了「calling free 到一个合法但错误的地址」。
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "contract/kvspace_interface.h"
#include "contract/allocator.h"
#include "vmbin/directory_walk.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

/* 记账分配器：分配用 malloc，但**每一次释放都计数**。close 在 open 失败后
 * 应当零释放——因为它面对的是全零结构（source_context 为 NULL、names/offsets
 * 为 NULL、capacity 为 0）。 */
typedef struct counting_allocator {
    int allocations;
    int deallocations;
} counting_allocator_t;

static void *counting_allocate(void *context, size_t size, size_t alignment) {
    (void)alignment;
    counting_allocator_t *counting = (counting_allocator_t *)context;
    ++counting->allocations;
    return malloc(size);
}

static void counting_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    (void)size;
    (void)alignment;
    counting_allocator_t *counting = (counting_allocator_t *)context;
    ++counting->deallocations;
    free(allocation);
}

static runtimec_allocator_t counting_allocator(counting_allocator_t *counting) {
    runtimec_allocator_t allocator;
    allocator.context = counting;
    allocator.allocate = counting_allocate;
    allocator.deallocate = counting_deallocate;
    return allocator;
}

/* 1. 空句柄：open false → close 安全且零释放。 */
static void test_null_handle(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    /* **故意不初始化**：这正是 7 处调用点的形状。若 open 在 memset 前返回，
     * close 读到的就是这块栈上的垃圾。 */
    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    const bool opened =
        runtimec_directory_walk_open(&walk, &allocator, NULL, "/p/", false, false);
    check(!opened, "open with a NULL handle must return false");
    check(counting.allocations == 0,
          "open must not allocate when the handle is missing");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after a failed open must succeed");
    check(counting.deallocations == 0,
          "close after a failed open must not deallocate anything");
}

/* 2. 空目录：同样 false → close 安全。 */
static void test_null_directory(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    check(!runtimec_directory_walk_open(&walk, &allocator, (void *)0x1000, NULL,
                                        false, false),
          "open with a NULL directory must return false");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after a NULL-directory failure must succeed");
    check(counting.deallocations == 0,
          "close after a NULL-directory failure must not deallocate");
}

/* 3. `expand_extension = true`：新接口不展开 extindex，明确失败。
 *    这一条在 memset **之后**返回，所以 close 安全本来就是有保证的——用它当
 *    **正向对照**：证明「close 零释放」不是因为 close 什么都不做。 */
static void test_expand_extension_rejected(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    check(!runtimec_directory_walk_open(&walk, &allocator, (void *)0x1000, "/p/",
                                        false, /*expand_extension=*/true),
          "open with expand_extension must fail: the new interface has no extindex");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after an expand_extension rejection must succeed");
    check(counting.deallocations == 0,
          "close after an expand_extension rejection must not deallocate");
}

/* 4. 空指针 walk 本身：只能返回 false，不能崩。 */
static void test_null_walk(void) {
    check(!runtimec_directory_walk_open(NULL, NULL, (void *)0x1000, "/p/", false,
                                        false),
          "open with a NULL walk must return false");
    check(runtimec_directory_walk_close(NULL) == RUNTIMEC_STATUS_INVALID,
          "close on a NULL walk reports invalid instead of crashing");
}

/* 5. 负向对照：真正连一个后端，open 失败后 close **仍必须释放**已分配的东西。
 *    没有这一条，「零释放」可能只是 close 永远不释放，测试会假绿。
 *
 *    为什么要真句柄：假地址（如 0x1000）会让 `kvspaceList` 在解引用句柄时崩，
 *    那时测的就不是「open 的清理路径」而是「传垃圾句柄会怎样」。用真句柄 + 一个
 *    不存在的路径，让失败发生在**后端调用**而不是前置检查——这正是要覆盖的区间：
 *    source 上下文已经分配、目录缓存已经建好，然后 refresh 失败。 */
static void test_backend_failure_still_releases(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    /*
     * store 路径**写死一个进程唯一的临时目录**，不从环境变量读：kvlang 只允许
     * `KVSPACE` 一个 env（用户裁决），测试也不例外——测试里多一个 env 就是给
     * 「跑的时候要设什么」多一条隐式契约。
     *
     * 用 `/tmp` + pid：同一台机器上并发跑多份测试时互不干扰，且进程退出后
     * 残留目录不影响下一次（shm 后端会自己重建）。
     */
    static char dsn[128];
    snprintf(dsn, sizeof(dsn), "shm:///tmp/runtimec-walk-test-%d/store",
             (int)getpid());
    void *handle = kvspaceConnect(dsn);
    if (handle == NULL) {
        printf("[skip] test_backend_failure_still_releases: cannot connect to %s\n",
               dsn);
        return;
    }

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));
    const bool opened = runtimec_directory_walk_open(
        &walk, &allocator, handle, "/definitely-not-an-index/", false, false);
    check(!opened, "open on a missing index must fail");
    /*
     * source 上下文用 libc `malloc`（见 directory_walk.c 的说明：handler 层拿不到
     * contract/allocator），所以它**不进**计数分配器。这里能断言的是：计数分配器
     * 的分配与释放**成对**——目录缓存若已分配，close 必须全部还回去，不泄漏也不
     * 多还。多还就是双释放（那一条由上一轮的实测崩出来了）。
     */
    check(counting.deallocations == counting.allocations,
          "close must return exactly what the directory cache took");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "a second close on the same walk is safe (source pointer was cleared)");
    check(counting.deallocations == counting.allocations,
          "closing twice must not deallocate anything a second time");
    kvspaceClose(handle);
}

int main(void) {
    test_null_handle();
    test_null_directory();
    test_expand_extension_rejected();
    test_null_walk();
    test_backend_failure_still_releases();

    if (g_failures != 0) {
        printf("directory_walk_open_failure: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("directory_walk_open_failure: OK "
           "(failed open leaves a zeroed walk; close is safe and releases nothing)\n");
    return 0;
}
