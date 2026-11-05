/*
 * `‥status` 的读入口：形状校验、空值与坏槽的分界。
 *
 * 为什么单独测这一层：`‥status` 是**控制信号**，J-5 之后解释器要靠它判「继续还是停」。
 * 它的三种情形处置完全不同：
 *
 *   合法 + 有内容  → 成功，文本是那个状态
 *   合法 + 空内容  → **成功**，空视图（表示「还没写状态」，不是错误）
 *   形状不符      → INVALID + error（坏槽）
 *
 * 第二条最容易被写成错误——「空」看起来像「没有」，而在这条入口里它恰好是合法的。
 * 第三条最容易被漏——不校验形状时，坏槽的字节会变成一个看起来合法的状态。
 *
 * 测试用一个真的 kvspace 句柄（shm 后端），所以它验的是**真链路**，不是替身。
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "contract/error.h"
#include "contract/view.h"
#include "kvspace/kvspace.h"
#include "vmbin/vthread_status.h"

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define TEST_STATUS_KEY "/vthread/1/‥status"

/* 每条用例用独立 store，避免上一条的残留影响下一条。 */
static void *open_store(const char *tag) {
    char dsn[128];

    snprintf(dsn, sizeof(dsn), "shm:///tmp/c5-vstatus-%s-%d", tag, (int)getpid());
    return kvspaceConnect(dsn);
}

static void test_read_valid_status(void) {
    void *handle = open_store("valid");
    runtimec_string_view_t text;
    runtimec_error_t error;

    EXPECT(handle != NULL);
    if (handle == NULL) {
        return;
    }
    runtimec_error_reset(&error);
    EXPECT(runtimec_vthread_status_write(
               handle, TEST_STATUS_KEY,
               runtimec_bytes_view_make((const uint8_t *)"run", 3U)) ==
           RUNTIMEC_STATUS_SUCCESS);
    runtimec_error_reset(&error);
    EXPECT(runtimec_vthread_status_read(handle, TEST_STATUS_KEY, &text,
                                        &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(text.size == 3U);
    EXPECT(text.data != NULL && memcmp(text.data, "run", 3U) == 0);
    kvspaceClose(handle);
}

/*
 * 空内容：**成功**，不是 INVALID。
 *
 * 这条的反向控制就是「把 len==0 当成错误」——那样本用例必须红。
 * 空状态表示「还没写状态」，而坏槽是「槽不符合规范」，两者的处置完全不同
 * （前者等，后者要重建 store），所以不能合并成一个返回值。
 */
static void test_read_empty_status_is_success(void) {
    void *handle = open_store("empty");
    runtimec_string_view_t text;
    runtimec_error_t error;

    EXPECT(handle != NULL);
    if (handle == NULL) {
        return;
    }
    /* 先占位：写空内容，槽形状按规范建好。 */
    runtimec_error_reset(&error);
    EXPECT(runtimec_vthread_status_write(
               handle, TEST_STATUS_KEY,
               runtimec_bytes_view_make(NULL, 0U)) ==
           RUNTIMEC_STATUS_SUCCESS);
    runtimec_error_reset(&error);
    text = runtimec_string_view_make((const char *)0x1, 99U);
    EXPECT(runtimec_vthread_status_read(handle, TEST_STATUS_KEY, &text,
                                        &error) == RUNTIMEC_STATUS_SUCCESS);
    /* 成功时输出被写成空视图——「什么都没读」与「读到空」在这里是同一件事，
     * 都与「坏槽」区分开。 */
    EXPECT(text.size == 0U);
    kvspaceClose(handle);
}

/*
 * 形状不符：INVALID + error。
 *
 * 用一个**普通变长值**冒充 status：它是 SLACK，但物理容量不等于规范下限 80，
 * 且没有按 status 的固定档建。本用例的反向控制是删掉形状校验——那样它会
 * 「成功」读出一个不该被当成状态的东西。
 */
static void test_read_rejects_wrong_shape(void) {
    void *handle = open_store("shape");
    runtimec_string_view_t text;
    runtimec_error_t error;

    EXPECT(handle != NULL);
    if (handle == NULL) {
        return;
    }
    /*
     * 用一个**非法 status 槽**：内容超过 LV=16。
     *
     * 注意不能用 `kvspaceSetValue` 写 SHORT —— 后端对 status 键做形状校验，
     * 会直接拒绝写入（它拒得对）。所以这里先在**非 status 键**上建好槽，
     * 再用 `WriteNewPlace` 把它写成超长内容——后端对首次创建不做 LV 校验，
     * 于是我们能造出「形状不符」的存量槽，正是本用例要的状态。
     */
    {
        static const char oversized[] = "0123456789abcdefghij"; /* 20 > 16 */
        uint8_t *body = NULL;
        char message[256];

        message[0] = '\0';
        EXPECT(kvspaceWriteNewPlace(handle, TEST_STATUS_KEY, KVSPACE_REF_INLINE,
                                    KVSPACE_STORETYPE_SLACK,
                                    (uint32_t)(sizeof(oversized) - 1U),
                                    RUNTIMEC_VTHREAD_STATUS_CAPACITY,
                                    RUNTIMEC_VTHREAD_STATUS_LANGTYPE, &body,
                                    message, sizeof(message)) == 0);
        if (body != NULL) {
            memcpy(body, oversized, sizeof(oversized) - 1U);
        }
    }
    runtimec_error_reset(&error);
    text = runtimec_string_view_make((const char *)0x1, 99U);
    EXPECT(runtimec_vthread_status_read(handle, TEST_STATUS_KEY, &text,
                                        &error) == RUNTIMEC_STATUS_INVALID);
    EXPECT(runtimec_error_is_set(&error));
    EXPECT(text.size == 0U);
    kvspaceClose(handle);
}

/* 键不存在：UNSUPPORTED，与「坏槽」区分。 */
static void test_read_absent_key(void) {
    void *handle = open_store("absent");
    runtimec_string_view_t text;
    runtimec_error_t error;

    EXPECT(handle != NULL);
    if (handle == NULL) {
        return;
    }
    runtimec_error_reset(&error);
    EXPECT(runtimec_vthread_status_read(handle, TEST_STATUS_KEY, &text,
                                        &error) ==
           RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(text.size == 0U);
    kvspaceClose(handle);
}

int main(void) {
    test_read_valid_status();
    test_read_empty_status_is_success();
    test_read_rejects_wrong_shape();
    test_read_absent_key();
    if (failures != 0) {
        fprintf(stderr, "vthread_status_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "vthread_status_test: OK\n");
    return 0;
}
