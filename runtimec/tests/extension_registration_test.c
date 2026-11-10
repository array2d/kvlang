/*
 * TASK-035：公共接口的扩展算子注册。
 *
 * 判据分四层，每层都必须能独立失败：
 *   1. **正例** —— 合法扩展表 → create 成功；
 *   2. **拒绝重复 opid** —— 撞内建 opid → create 失败，且错误点名模块；
 *   3. **拒绝空 handler**；
 *   4. **拒绝尺寸不符** —— request 与 entry 各一条。
 *
 * 第 4 层是本设计的关键：handler 是 `void *`，编译期查不出错配，所以对不上
 * 尺寸必须在 create 处失败。没有这两条负例，`structure_size` 就只是文档里的
 * 一句话，没人保证它真被校验。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvlang/runtimec.h>

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

/* 扩展 handler：本测试只验证注册，不执行——返回 SUCCESS 即可。 */
static runtimec_status_t test_extension_handler(void *frame, const void *instruction,
                                                void *output_exit) {
    (void)frame;
    (void)instruction;
    (void)output_exit;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 取一个**空闲** opid：很靠后的值，不与 catalog 的 0x0001–0x0192 冲突。 */
#define TEST_FREE_OPERATION_IDENTIFIER UINT32_C(0x7001)
/*
 * 一个**确定在 registry 里**的 opid：`kvspace·listlen` = 0x0177，由 host 模块
 * 在 session 创建期装载。
 *
 * 踩过两次坑，记下来免得后人再踩：
 *   - `copy`（0x0001）：catalog 里有行，但**没有 handler 条目**——control/copy
 *     一类由解释器直接分派，registry 里查不到；
 *   - `add`（0x0002）：走**标量 union 通道**，同样由解释器分派，不注册 handler。
 *
 * 只有「handler 型」算子（host / container / string / time / scatter / langtype
 * 这些模块的条目）才在 registry 里。拿分派型的 opid 做重复测试会「因为不是重复
 * 而通过」——测试假绿。
 */
#define TEST_TAKEN_OPERATION_IDENTIFIER UINT32_C(0x0177)

static kvlang_runtime_extension_entry_t make_entry(uint32_t identifier,
                                                   uint32_t structure_size,
                                                   kvlang_runtime_extension_handler_t handler) {
    kvlang_runtime_extension_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.structure_size = structure_size;
    entry.operation_identifier = identifier;
    entry.flags = 0U;
    entry.handler = handler;
    return entry;
}

static kvlang_runtime_create_request_t make_request(
    const kvlang_runtime_extension_module_t *modules, size_t module_count,
    size_t structure_size) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = structure_size;
    request.data_source_name = "shm:///tmp/runtimec-extension-test";
    request.instance_identifier = 1U;
    request.extensions = modules;
    request.extension_count = module_count;
    return request;
}

/* 调用 create，返回状态；同时把错误首行写进 `message`。 */
static runtimec_status_t create_with(const kvlang_runtime_create_request_t *request,
                                     char *message, size_t message_capacity) {
    kvlang_runtime_t *runtime = NULL;
    kvlang_runtime_error_t error;
    memset(&error, 0, sizeof(error));
    error.message.data = message;
    error.message.capacity = message_capacity;
    error.message.length = 0U;
    error.message.required_length = 0U;
    if (message_capacity != 0U) {
        message[0] = '\0';
    }
    const runtimec_status_t status = kvlang_runtime_create(request, &runtime, &error);
    if (runtime != NULL) {
        kvlang_runtime_error_t destroy_error;
        memset(&destroy_error, 0, sizeof(destroy_error));
        char destroy_message[128];
        destroy_message[0] = '\0';
        destroy_error.message.data = destroy_message;
        destroy_error.message.capacity = sizeof(destroy_message);
        (void)kvlang_runtime_destroy(runtime, &destroy_error);
    }
    if (message_capacity != 0U && error.message.length < message_capacity) {
        message[error.message.length] = '\0';
    }
    return status;
}

/* 1. 正例：合法扩展 -> create 成功。 */
static void test_valid_extension_is_accepted(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "test-extension",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    const runtimec_status_t status = create_with(&request, message, sizeof(message));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        printf("[FAIL] a valid extension must be accepted, status=%d message=%s\n",
               (int)status, message);
        ++g_failures;
    }
}

/* 2. 负例：撞内建 opid -> 拒绝，且错误点名模块。 */
static void test_duplicate_identifier_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_TAKEN_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "duplicate-module",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    const runtimec_status_t status = create_with(&request, message, sizeof(message));
    check(status != RUNTIMEC_STATUS_SUCCESS,
          "an extension reusing a builtin opid must be rejected");
    check(strstr(message, "duplicate-module") != NULL,
          "the rejection must name the offending module");
}

/* 3. 负例：空 handler。 */
static void test_missing_handler_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        NULL);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "handlerless",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "an extension entry without a handler must be rejected");
}

/* 4a. 负例：request 的 structure_size 不符。 */
static void test_wrong_request_size_is_rejected(void) {
    const kvlang_runtime_create_request_t request = make_request(
        NULL, 0U, sizeof(kvlang_runtime_create_request_t) - 1U);
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "a create request with a wrong structure_size must be rejected");
}

/* 4b. 负例：条目的 structure_size 不符。 */
static void test_wrong_entry_size_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t) - 1U,
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "wrong-size",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "an extension entry with a wrong structure_size must be rejected");
}

int main(void) {
    test_valid_extension_is_accepted();
    test_duplicate_identifier_is_rejected();
    test_missing_handler_is_rejected();
    test_wrong_request_size_is_rejected();
    test_wrong_entry_size_is_rejected();

    if (g_failures != 0) {
        printf("extension_registration: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("extension_registration: OK "
           "(valid accepted; duplicate/missing-handler/wrong-size rejected)\n");
    return 0;
}
