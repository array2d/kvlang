/*
 * 临时 println 的端到端验收（仅 RUNTIMEC_TEMP_PRINTLN=ON 时参与构建）。
 *
 * 链路：dlopen kvspace → 绑二进制接口表 → 建 adapter + session → 发布三条
 *       `def rwir` 签名 → layout 一段源码 → execution window 执行 → 断言
 *       stdout 内容与退出状态。
 *
 * **签名发布是测试侧的**：runtimec 目前没有「运行时自发布 native 签名」的能力
 * （与 byteseek 的 `kvlangDefRwir` 对应），这条临时算子用测试侧绕过。这是一处
 * 已登记的缺口，不是已完成的能力。
 *
 * 断言两条必须同时成立：
 *   1. stdout == 期望文本（逐字节，不是「非空」）
 *   2. 退出码 == 0
 * 今天的教训就是这两个信号会脱钩。
 *
 * 失败时按段指认：连接 / 发布 / layout / session / 执行 / handler，不只报「测试失败」。
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "contract/host_services.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include "host/kvspace_adapter.h"
#include "host/kvspace_binary_interface.h"

#define SKIP_RETURN_CODE 77

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

typedef int (*layout_code_function_t)(const char *, const char *, char *,
                                      uint32_t, char *, uint32_t);

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

static runtimec_kvspace_symbol_address_t resolve_symbol(void *context,
                                                        const char *name) {
    /* dlsym 返回对象指针；ISO C 不允许它与函数指针互转，先经 uintptr_t 往返。
     * 与 host_kvspace_backend_test.c 的写法一致。 */
    void *symbol = dlsym(context, name);
    runtimec_kvspace_symbol_address_t address;
    memcpy(&address, &symbol, sizeof(address));
    return address;
}

/*
 * 发布一条 `def rwir` 声明。形状照 runtimec2 的 `kvlangDefRwir`：
 *   路由头 body = [nr:u16][nw:u16][dyn:u8]
 *   签名行 body = 该参数的完整 langtype 串
 * 三个临时输出算子声明为「1 个变参读参」（nr=1、dynamic=1），
 * 与 layout 产出的形状一致；实际个数由编码期按调用点展开。
 */
static runtimec_status_t publish_def_rwir(
    const runtimec_kvspace_binary_interface_t *binary_interface, void *handle,
    const char *opcode) {
    char route_key[128];
    char signature_key[128];
    char error_text[256];
    uint8_t body[5] = {1U, 0U, 0U, 0U, 1U}; /* nr=1, nw=0, dynamic=1 */
    const char *signature = "*...";
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;

    snprintf(route_key, sizeof(route_key), "/lib/%s", opcode);
    snprintf(signature_key, sizeof(signature_key), "/lib/%s/[0,-1]", opcode);
    error_text[0] = '\0';
    if (binary_interface->mkindex(handle, "/lib/", 0U, error_text,
                                  (uint32_t)sizeof(error_text)) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (binary_interface->new_slack(body, sizeof(body), sizeof(body),
                                    "def rwir", &wire, &wire_size) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    error_text[0] = '\0';
    if (binary_interface->set_value(handle, route_key, wire,
                                    (uint32_t)wire_size, error_text,
                                    (uint32_t)sizeof(error_text)) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (binary_interface->new_slack((const uint8_t *)signature,
                                    strlen(signature), strlen(signature),
                                    "def langtype", &wire, &wire_size) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    error_text[0] = '\0';
    if (binary_interface->set_value(handle, signature_key, wire,
                                    (uint32_t)wire_size, error_text,
                                    (uint32_t)sizeof(error_text)) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

int main(int argument_count, char **arguments) {
    const char *kvspace_library = argument_count > 1 ? arguments[1] : NULL;
    const char *layout_library = argument_count > 2 ? arguments[2] : NULL;
    char directory[256];
    char data_source_name[320];
    char source[512];
    char entry[128];
    char layout_error[512];

    if (kvspace_library == NULL || layout_library == NULL) {
        fprintf(stderr, "SKIP: need kvspace and layout library paths\n");
        return SKIP_RETURN_CODE;
    }
    void *library = dlopen(kvspace_library, RTLD_NOW | RTLD_LOCAL);
    void *layout_library_handle =
        dlopen(layout_library, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL || layout_library_handle == NULL) {
        fprintf(stderr, "SKIP: cannot load libraries\n");
        return SKIP_RETURN_CODE;
    }
    layout_code_function_t layout_code_function = NULL;
    {
        void *symbol = dlsym(layout_library_handle, "kvlangLayoutCode");
        memcpy(&layout_code_function, &symbol, sizeof(layout_code_function));
    }
    if (layout_code_function == NULL) {
        fprintf(stderr, "SKIP: layout library lacks kvlangLayoutCode\n");
        return SKIP_RETURN_CODE;
    }

    /* ── 段 1：绑定 kvspace 二进制接口表 ───────────────────────────── */
    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = library,
        .resolve = resolve_symbol,
    };
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_binary_interface_bind(&resolver, &binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: bind failed: %.*s\n", (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        return SKIP_RETURN_CODE;
    }

    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding2/"
             "println-%d", (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = binary_interface.connect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", data_source_name);
        return SKIP_RETURN_CODE;
    }

    /* ── 段 2：发布三条 def rwir 签名 ──────────────────────────────── */
    const char *const opcodes[] = {"println", "print", "cerr"};
    for (size_t ordinal = 0U; ordinal < 3U; ++ordinal) {
        const runtimec_status_t publish_status = publish_def_rwir(
            &binary_interface, handle, opcodes[ordinal]);
        if (publish_status != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "FAIL segment publish: %s\n", opcodes[ordinal]);
            return 1;
        }
    }

    /* ── 段 3：建 adapter + session ────────────────────────────────── */
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    runtimec_kvspace_adapter_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_host_services_t host_services;
    if (runtimec_kvspace_adapter_create_services(
            &adapter_request, &binary_interface, &host_services,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL segment adapter\n");
        return 1;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    options.compilation_cache_capacity = 4U;
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    if (runtimec_session_create(&host_services, &options, &session, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL segment session: %.*s\n", (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        return 1;
    }

    /* ── 段 4：layout 一段只用 println 的源码进同一 store ────────────── */
    snprintf(source, sizeof(source),
             "rwfunc main() -> () {\n    println(\"hi\")\n}\n");
    entry[0] = '\0';
    layout_error[0] = '\0';
    if (layout_code_function(source, data_source_name, entry,
                             (uint32_t)sizeof(entry), layout_error,
                             (uint32_t)sizeof(layout_error)) != 0) {
        fprintf(stderr, "FAIL segment layout: %s\n", layout_error);
        return 1;
    }

    /*
     * ── 段 5：执行 ────────────────────────────────────────────────────
     * stdout 重定向到临时文件后捕获，逐字节比对——不是「非空」就算过。
     */
    char capture_path[320];
    snprintf(capture_path, sizeof(capture_path), "%s/stdout.txt", directory);
    fflush(stdout);
    const int saved_stdout = dup(fileno(stdout));
    FILE *capture = fopen(capture_path, "w");
    if (capture == NULL) {
        fprintf(stderr, "FAIL segment capture: cannot open %s\n",
                capture_path);
        return 1;
    }
    dup2(fileno(capture), fileno(stdout));

    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string("/lib/main");
    request.vthread_identifier = runtimec_string_view_from_c_string("v1");
    request.frame_path = runtimec_string_view_from_c_string("/vthread/v1/[1]");
    request.stack_identifier = 1;
    /* layout 的 source pc 从 1 起（[0,0] 是签名行）。 */
    request.source_logical_program_counter = 1U;
    request.arguments = NULL;
    request.argument_count = 0U;
    runtimec_execution_window_result_t result;
    const runtimec_status_t execute_status = runtimec_execution_window_execute(
        session, &request, &result, &error);

    fflush(stdout);
    dup2(saved_stdout, fileno(stdout));
    fclose(capture);
    close(saved_stdout);

    /* ── 段 6：断言两条同时成立 ────────────────────────────────────── */
    char captured[256];
    size_t captured_size = 0U;
    capture = fopen(capture_path, "r");
    if (capture != NULL) {
        captured_size = fread(captured, 1U, sizeof(captured), capture);
        fclose(capture);
    }
    if (execute_status != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL segment execute: status=%d detail=%u\n",
                (int)execute_status, (unsigned)result.execution_exit.detail_code);
    }
    EXPECT(execute_status == RUNTIMEC_STATUS_SUCCESS);
    const char *expected = "hi\n";
    EXPECT(captured_size == strlen(expected));
    EXPECT(captured_size == strlen(expected) &&
           memcmp(captured, expected, captured_size) == 0);

    runtimec_session_destroy(session);
    runtimec_kvspace_adapter_destroy_services(&host_services, &error);
    return failures == 0 ? 0 : 1;
}
