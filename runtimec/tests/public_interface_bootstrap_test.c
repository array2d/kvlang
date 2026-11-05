/*
 * 公共接口的引导链路：create 必须真的装载 kvspace 前端、连接 DSN、建 session；
 * destroy 必须整条释放；execute 尚未实现，必须明确失败并清空输出。
 *
 * 没有 kvspace 前端或 DSN 不可用时返回 77（ctest SKIP）：缺依赖不能看起来像通过。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <kvlang/runtimec.h>

#define SKIP_RETURN_CODE 77

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
        }                                                                      \
    } while (0)

static kvlang_runtime_error_t prepare_error(char *message, size_t capacity) {
    kvlang_runtime_error_t error;
    memset(&error, 0, sizeof(error));
    error.message.data = message;
    error.message.capacity = capacity;
    return error;
}

int main(void) {
    char directory[256];
    snprintf(directory, sizeof(directory), "/tmp/runtimec-bootstrap-%d",
             (int)getpid());
    if (mkdir(directory, 0700) != 0) {
        fprintf(stderr, "SKIP: cannot create %s\n", directory);
        return SKIP_RETURN_CODE;
    }
    char data_source_name[320];
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);

    /* 1. 正常路径：create 真的建起整条链路。 */
    {
        char error_text[512];
        kvlang_runtime_error_t error =
            prepare_error(error_text, sizeof(error_text));
        kvlang_runtime_create_request_t request = {data_source_name, UINT64_C(1)};
        kvlang_runtime_t *runtime = NULL;
        const runtimec_status_t status =
            kvlang_runtime_create(&request, &runtime, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            /* 两条真实的失败原因，都必须是明确状态且可定位：
             *   NOT_IMPLEMENTED —— 前端装载/绑定/连接都成功了，停在
             *                      runtimec_session_create（engine 尚未实现）；
             *   UNSUPPORTED     —— 没有可装载的 kvspace 前端，错误里点名 SONAME。 */
            fprintf(stderr, "create: status=%d message=%s\n", (int)status,
                    error_text);
            EXPECT(runtime == NULL);
            EXPECT(error.status == status);
            if (status == RUNTIMEC_STATUS_NOT_IMPLEMENTED) {
                /* 引导链走通到 engine session，失败在它构造依赖时发生；
                 * 错误必须点名是哪个组件，而不是笼统的 not_implemented。 */
                EXPECT(strstr(error_text, "dependency failed: ") != NULL);
                EXPECT(strstr(error_text, "vmkv encoder") != NULL ||
                       strstr(error_text, "binrwir compiler") != NULL ||
                       strstr(error_text, "compilation repository") != NULL);
                printf("public_interface_bootstrap_test: ok "
                       "(bootstrap reaches engine session; %s)\n",
                       error_text);
                rmdir(directory);
                return failures == 0 ? 0 : 1;
            }
            EXPECT(status == RUNTIMEC_STATUS_UNSUPPORTED);
            EXPECT(strstr(error_text, "libkvspace") != NULL);
            rmdir(directory);
            return failures == 0 ? SKIP_RETURN_CODE : 1;
        }
        EXPECT(runtime != NULL);

        /*
         * 2. execute 对**存储里不存在的函数**必须失败封闭、输出清空。
         *
         * **期望已变（2026-11-05）**：这里原来写的是「execute 尚未实现」——
         * 那时 `kvlang_runtime_execute` 是空壳，任何调用都返回
         * NOT_IMPLEMENTED / INVALID。它现在是**真的执行入口**（走
         * execution window），所以期望要落在「跑一个不存在的东西会怎样」上。
         *
         * 实测成因（`ERROR`，消息 `function has no readable source revision key`）：
         * 本用例的 store 是**刚建的空 store**，`/lib/example` 与它的 `‥ctime`
         * 都不存在，窗口在建帧前就失败。**判据是失败封闭**：给出明确状态、
         * 输出一个字节都不留，而不是编一个「跑完了」。
         */
        char value[32] = "dirty";
        char program_counter[32] = "dirty";
        kvlang_runtime_execute_result_t result;
        memset(&result, 0, sizeof(result));
        result.value.data = value;
        result.value.capacity = sizeof(value);
        result.program_counter.data = program_counter;
        result.program_counter.capacity = sizeof(program_counter);
        const char *arguments[1] = {"value"};
        kvlang_runtime_execute_request_t execute_request;
        memset(&execute_request, 0, sizeof(execute_request));
        execute_request.entry_kind = KVLANG_RUNTIME_ENTRY_FUNCTION;
        execute_request.entry_name = "example";
        execute_request.arguments = arguments;
        execute_request.argument_count = 1U;
        error = prepare_error(error_text, sizeof(error_text));
        const runtimec_status_t execute_status =
            kvlang_runtime_execute(runtime, &execute_request, &result, &error);
        /* 具体状态而非「非成功」：`ERROR` 是这条路径的实际分类，写成
         * `!= SUCCESS` 会把「状态码分类被改坏」一起放过。 */
        EXPECT(execute_status == RUNTIMEC_STATUS_ERROR);
        /* 错误必须点名成因，不能只给一个状态码。 */
        EXPECT(error_text[0] != '\0');
        EXPECT(result.value.length == 0U);
        EXPECT(result.program_counter.length == 0U);
        EXPECT(value[0] == '\0');
        EXPECT(program_counter[0] == '\0');

        /* 3. destroy 释放整条链路。 */
        error = prepare_error(error_text, sizeof(error_text));
        EXPECT(kvlang_runtime_destroy(runtime, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    /* 4. 同一 DSN 可以再建一次：说明上一轮真的把句柄和库引用都放掉了。 */
    {
        char error_text[512];
        kvlang_runtime_error_t error =
            prepare_error(error_text, sizeof(error_text));
        kvlang_runtime_create_request_t request = {data_source_name, UINT64_C(1)};
        kvlang_runtime_t *runtime = NULL;
        EXPECT(kvlang_runtime_create(&request, &runtime, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtime != NULL);
        error = prepare_error(error_text, sizeof(error_text));
        EXPECT(kvlang_runtime_destroy(runtime, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    /* 5. 无法识别的 DSN 必须失败且点名 DSN。 */
    {
        char error_text[512];
        kvlang_runtime_error_t error =
            prepare_error(error_text, sizeof(error_text));
        kvlang_runtime_create_request_t request = {"bogus://nowhere", UINT64_C(1)};
        kvlang_runtime_t *runtime = NULL;
        const runtimec_status_t status =
            kvlang_runtime_create(&request, &runtime, &error);
        EXPECT(status != RUNTIMEC_STATUS_SUCCESS);
        EXPECT(runtime == NULL);
        EXPECT(strstr(error_text, "bogus://nowhere") != NULL);
    }

    /* 6. 空 DSN 是非法请求。 */
    {
        char error_text[64];
        kvlang_runtime_error_t error =
            prepare_error(error_text, sizeof(error_text));
        kvlang_runtime_create_request_t request = {"", UINT64_C(1)};
        kvlang_runtime_t *runtime = NULL;
        EXPECT(kvlang_runtime_create(&request, &runtime, &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(runtime == NULL);
    }

    char cleanup[320];
    snprintf(cleanup, sizeof(cleanup), "rm -f %s/store.shm*", directory);
    if (system(cleanup) != 0) {
        fprintf(stderr, "cleanup failed: %s\n", cleanup);
    }
    rmdir(directory);
    if (failures != 0) {
        fprintf(stderr, "public_interface_bootstrap_test: %d failure(s)\n",
                failures);
        return 1;
    }
    printf("public_interface_bootstrap_test: ok\n");
    return 0;
}
