/*
 * TASK-071：**状态码为主，错误文本为辅**。
 *
 * 缺陷：错误缓冲写不下时，`kvlang_runtime_fail` 把返回值改成
 * INSUFFICIENT_CAPACITY——于是**调用方给多大缓冲决定了操作报什么错**。
 * 同一个失败，err 传 NULL、cap=0、cap=1、cap 充足，得到四个不同的答案。
 *
 * 判据（本测试逐条钉住）：
 *   1. `error == NULL`、`cap == 0`、`cap == 1`、cap 充足——**状态码完全相同**；
 *   2. 状态码就是**操作本身**的结论，不是缓冲的结论：两个不同的失败给出两个
 *      不同的码（INVALID 与 ERROR 各一例），证明码没有被抹平成同一个值；
 *   3. 缓冲写不下时**不写半条**：length == 0，required_length 是完整消息长度
 *      （调用方据此扩容重试）；
 *   4. 缓冲够大时消息完整落地——这一条是第 3 条的正向对照，否则「永远不写」
 *      也能让第 3 条通过。
 */

#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

/*
 * 三种失败源，各自的期望码写死在调用处——**不从被测代码里读**，否则码改坏了
 * 测试跟着改坏，等于没测。
 */

/* take 1：请求非法 ⇒ INVALID。 */
static kvlang_runtime_create_request_t invalid_request(void) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.data_source_name = NULL; /* 非法 */
    request.instance_identifier = 1U;
    return request;
}

/* take 2：数据源连不上 ⇒ ERROR（不是 INVALID——请求本身是合法的）。 */
static kvlang_runtime_create_request_t unconnectable_request(void) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.data_source_name = "no-such-scheme://runtimec-error-status-test";
    request.instance_identifier = 1U;
    return request;
}

/*
 * 用给定缓冲跑一次失败调用，回报状态码与缓冲里发生了什么。
 * `data == NULL` 即「调用方的错误对象本身是 NULL」。
 */
typedef struct observation {
    runtimec_status_t returned;
    runtimec_status_t error_status;
    size_t length;
    size_t required_length;
} observation_t;

static observation_t observe(const kvlang_runtime_create_request_t *request,
                             char *data, size_t capacity) {
    observation_t seen;
    kvlang_runtime_t *runtime = NULL;

    memset(&seen, 0, sizeof(seen));
    if (data == NULL) {
        seen.returned = kvlang_runtime_create(request, &runtime, NULL);
        return seen;
    }
    kvlang_runtime_error_t error;
    memset(&error, 0, sizeof(error));
    error.message.data = data;
    error.message.capacity = capacity;
    if (capacity != 0U) {
        memset(data, 0, capacity);
    }
    seen.returned = kvlang_runtime_create(request, &runtime, &error);
    seen.error_status = error.status;
    seen.length = error.message.length;
    seen.required_length = error.message.required_length;
    if (runtime != NULL) {
        char destroy_message[128];
        kvlang_runtime_error_t destroy_error;
        memset(&destroy_error, 0, sizeof(destroy_error));
        destroy_message[0] = '\0';
        destroy_error.message.data = destroy_message;
        destroy_error.message.capacity = sizeof(destroy_message);
        (void)kvlang_runtime_destroy(runtime, &destroy_error);
    }
    return seen;
}

static void check_status_is_independent_of_the_message_buffer(
    const kvlang_runtime_create_request_t *request,
    runtimec_status_t expected, const char *label) {
    char ample_text[512];
    char one_byte[1];
    char message[256];

    const observation_t without_error = observe(request, NULL, 0U);
    const observation_t with_zero_capacity = observe(request, ample_text, 0U);
    const observation_t with_one_byte = observe(request, one_byte, 1U);
    const observation_t with_ample_capacity =
        observe(request, ample_text, sizeof(ample_text));

    snprintf(message, sizeof(message),
             "%s: err=NULL returned %d (expected %d)", label,
             (int)without_error.returned, (int)expected);
    check(without_error.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: cap=0 returned %d (expected %d)", label,
             (int)with_zero_capacity.returned, (int)expected);
    check(with_zero_capacity.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: cap=1 returned %d (expected %d)", label,
             (int)with_one_byte.returned, (int)expected);
    check(with_one_byte.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: ample capacity returned %d (expected %d)", label,
             (int)with_ample_capacity.returned, (int)expected);
    check(with_ample_capacity.returned == expected, message);

    /* 错误对象里的 status 与返回码是同一个结论，不是第二个结论。 */
    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status", label,
             (int)with_zero_capacity.error_status);
    check(with_zero_capacity.error_status == with_zero_capacity.returned,
          message);
    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status (cap=1)",
             label, (int)with_one_byte.error_status);
    check(with_one_byte.error_status == with_one_byte.returned, message);
    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status (ample)",
             label, (int)with_ample_capacity.error_status);
    check(with_ample_capacity.error_status == with_ample_capacity.returned,
          message);

    /* 写不下 ⇒ 不写半条，length 必须是 0。 */
    snprintf(message, sizeof(message),
             "%s: cap=0 wrote %zu byte(s); a partial message is not allowed",
             label, with_zero_capacity.length);
    check(with_zero_capacity.length == 0U, message);
    snprintf(message, sizeof(message),
             "%s: cap=1 wrote %zu byte(s); a partial message is not allowed",
             label, with_one_byte.length);
    check(with_one_byte.length == 0U, message);

    /* 调用方要靠 required_length 扩容，所以两条不足路径都必须报出**同一个**
     * 完整长度，且与充足容量时的实际长度一致。 */
    snprintf(message, sizeof(message),
             "%s: required_length differs between cap=0 (%zu) and cap=1 (%zu)",
             label, with_zero_capacity.required_length,
             with_one_byte.required_length);
    check(with_zero_capacity.required_length ==
              with_one_byte.required_length,
          message);
    snprintf(message, sizeof(message),
             "%s: required_length=%zu but the ample run wrote %zu byte(s)",
             label, with_ample_capacity.required_length,
             with_ample_capacity.length);
    check(with_ample_capacity.required_length == with_ample_capacity.length,
          message);
    /* 正向对照：容量充足时消息真的落进了缓冲。 */
    snprintf(message, sizeof(message),
             "%s: ample capacity wrote nothing; the message is lost", label);
    check(with_ample_capacity.length != 0U, message);

    /*
     * 第三条旁路：**错误缓冲自身畸形**（capacity 非零而 data 为空）。
     *
     * 那不是操作的错，是调用方把结构填坏了。旧代码在这里也返回 INVALID，于是
     * 「缓冲填坏」与「请求非法」两个完全不同的原因给出同一个码。判据相同：
     * 状态码只说操作的结论，缓冲的事一概不参与。
     */
    {
        kvlang_runtime_t *runtime = NULL;
        kvlang_runtime_error_t error;
        memset(&error, 0, sizeof(error));
        error.message.data = NULL;
        error.message.capacity = 64U; /* 畸形：有容量没有存储 */
        const runtimec_status_t returned =
            kvlang_runtime_create(request, &runtime, &error);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer returned %d (expected %d)", label,
                 (int)returned, (int)expected);
        check(returned == expected, message);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer changed error.status to %d", label,
                 (int)error.status);
        check(error.status == expected, message);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer wrote %zu byte(s)", label,
                 error.message.length);
        check(error.message.length == 0U, message);
        if (runtime != NULL) {
            (void)kvlang_runtime_destroy(runtime, NULL);
        }
    }
}

int main(void) {
    /* 两个不同的失败，期望两个不同且非容量语义的码。 */
    const kvlang_runtime_create_request_t invalid = invalid_request();
    const kvlang_runtime_create_request_t unconnectable = unconnectable_request();

    check_status_is_independent_of_the_message_buffer(
        &invalid, RUNTIMEC_STATUS_INVALID, "invalid request");
    check_status_is_independent_of_the_message_buffer(
        &unconnectable, RUNTIMEC_STATUS_ERROR, "unconnectable data source");

    /* 两条路径的码必须**不同**：若都被抹成同一个值，「码来自操作」就不成立。 */
    check(observe(&invalid, NULL, 0U).returned !=
              observe(&unconnectable, NULL, 0U).returned,
          "two different failures collapsed to the same status");

    if (g_failures == 0) {
        printf("error_status_priority_test: OK\n");
        return 0;
    }
    printf("error_status_priority_test: %d failure(s)\n", g_failures);
    return 1;
}
