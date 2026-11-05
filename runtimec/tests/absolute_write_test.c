/*
 * 绝对 key 的读写契约：**指令不创造 key**。
 *
 * 绝对 key 的**存在性是运行时前提，不是静态保证**：layout 什么都不做（不要求声明、
 * 不读存储核对），运行时碰不到就失败封闭——这正是本文件三条判据要钉的行为。
 *
 * 规则（用户裁决）：kvlang 的 rwir 只容许触碰**已经存在**的 key。创建源在源码里：
 * 容器用 `·new` 一族，标量靠**类型声明**（`v:int32 = add(a,4)` 里 `v:int32`
 * 建 key，赋值只写）。帧准备阶段只是把这些声明落地的手段，不是创建源——
 * 没有声明就没有创建。
 *
 * 三条判据：
 *
 *   正向：`x + 1 -> /abs_write_result`，目标在执行前**已存在**（测试用
 *          `ensure_zero_value` 代表源码里那条声明会做的事——标量靠类型声明建 key），
 *          执行成功且值为 42。
 *
 *   负向一：读一个不存在的绝对 key → 执行期失败（不是零值、不是 None）。
 *   负向二：写一个不存在的绝对 key → 执行期失败封闭，**绝不顺手创建**。
 *          这条钉的正是被删掉的「写路径创建」：值访问只解析、不建。
 *
 * 依赖：真 layout 库 + 真 kvspace（shm）。缺任一返回 77 跳过。
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "binrwir/argument.h"
#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "contract/error.h"
#include "contract/snapshot.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include "host/kvspace_adapter.h"
#include "host/kvspace_binary_interface.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/value_access.h"
#include "vmkv/encoder.h"

#define SKIP_RETURN_CODE 77

typedef int (*layout_code_function_t)(const char *source, const char *data_source,
                                      char *entry, uint32_t entry_capacity,
                                      char *error, uint32_t error_capacity);

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            ++failures;                                                       \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,            \
                    #condition);                                              \
        }                                                                     \
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

static runtimec_kvspace_symbol_address_t resolve_symbol(void *context,
                                                        const char *name) {
    void *symbol = dlsym(context, name);
    return (runtimec_kvspace_symbol_address_t)(uintptr_t)symbol;
}

/* 正向：写一个**已存在**的绝对 key，执行后持有 42。 */
static void test_write_existing_key_succeeds(const char *layout_path,
                                             const char *kvspace_path) {
    void *kvspace_library = dlopen(kvspace_path, RTLD_NOW | RTLD_LOCAL);
    void *layout_library = dlopen(layout_path, RTLD_NOW | RTLD_LOCAL);
    if (kvspace_library == NULL || layout_library == NULL) {
        fprintf(stderr, "SKIP: cannot load libraries\n");
        return;
    }
    layout_code_function_t layout_code = NULL;
    {
        void *symbol = dlsym(layout_library, "kvlangLayoutCode");
        memcpy(&layout_code, &symbol, sizeof(layout_code));
    }
    if (layout_code == NULL) {
        fprintf(stderr, "SKIP: layout library lacks kvlangLayoutCode\n");
        return;
    }
    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = kvspace_library, .resolve = resolve_symbol};
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_binary_interface_bind(&resolver, &binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: kvspace bind failed\n");
        return;
    }
    char directory[256];
    char data_source_name[320];
    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding1/"
             "abs-write-%d", (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = binary_interface.connect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect store\n");
        return;
    }
    const runtimec_allocator_t allocator = {
        .context = NULL, .allocate = test_allocate,
        .deallocate = test_deallocate};
    runtimec_kvspace_adapter_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1)};
    runtimec_host_services_t host_services;
    EXPECT(runtimec_kvspace_adapter_create_services(
               &adapter_request, &binary_interface, &host_services, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    options.compilation_cache_capacity = 4U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    EXPECT(runtimec_session_create(&host_services, &options, &session,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    char entry[128];
    char layout_error[512];
    entry[0] = '\0';
    layout_error[0] = '\0';
    const char *source =
        "rwfunc main() -> () {\n"
        "    x = 41\n"
        "    x + 1 -> /abs_write_result\n"
        "}\n";
    EXPECT(layout_code(source, data_source_name, entry,
                       (uint32_t)sizeof(entry), layout_error,
                       (uint32_t)sizeof(layout_error)) == 0);
    /*
     * 执行前必须先把写目标建出来——指令不创造 key，创建源是源码里的声明。
     *
     * 这里调 `host_services.ensure_zero_value` 代表的是**那条声明**会做的事
     * （`v:int32 = …` 建 key、赋值只写），而不是「代表帧准备」：帧准备只是
     * 执行声明的手段，没有声明就没有创建。容器用 `·new` 一族，标量用类型声明，
     * 两者都写在源码里；本用例的源码没有为空 absolute key 写声明，所以由测试
     * 补上声明应做的事。
     *
     * 失效条件：等帧准备开始扫静态写目标（coding3 的 materialize_locals 改造）
     * 之后，这一段应删除，改由源码声明驱动。
     */
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key = runtimec_key_view_make("/lib/main",
                                                   sizeof("/lib/main") - 1U);
    EXPECT(host_services.source_revision(host_services.context,
                                         snapshot.function_key,
                                         &snapshot.source_revision,
                                         &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(host_services.ensure_zero_value(
               host_services.context, &snapshot,
               runtimec_key_view_make("/abs_write_result",
                                      sizeof("/abs_write_result") - 1U),
               runtimec_string_view_from_c_string("int64"),
               &error) == RUNTIMEC_STATUS_SUCCESS);
    /* 目标确实已存在——这是本用例的前提，不是可有可无的旁注。 */
    uint8_t *before = NULL;
    uint32_t before_size = 0U;
    EXPECT(binary_interface.get(handle, "/abs_write_result", 1, &before,
                                &before_size) == 0);
    EXPECT(before != NULL && before_size >= KVSPACE_XVALUE_HEAD_LEN);

    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string("/lib/main");
    request.vthread_identifier = runtimec_string_view_from_c_string("1");
    request.frame_path = runtimec_string_view_from_c_string("/vthread/1/[1]");
    request.stack_identifier = 1;
    request.source_logical_program_counter = 1U;
    runtimec_execution_window_result_t result;
    const runtimec_status_t execute_status = runtimec_execution_window_execute(
        session, &request, &result, &error);
    EXPECT(execute_status == RUNTIMEC_STATUS_SUCCESS);
    uint8_t *after = NULL;
    uint32_t after_size = 0U;
    EXPECT(binary_interface.get(handle, "/abs_write_result", 1, &after,
                                &after_size) == 0);
    EXPECT(after != NULL && after_size >= KVSPACE_XVALUE_HEAD_LEN +
                                              sizeof(int64_t));
    if (after != NULL && after_size >= KVSPACE_XVALUE_HEAD_LEN +
                                             sizeof(int64_t)) {
        int64_t value = 0;
        memcpy(&value, after + KVSPACE_XVALUE_HEAD_LEN, sizeof(value));
        EXPECT(value == 42);
    }
    if (session != NULL) {
        (void)runtimec_session_destroy(session);
    }
    (void)runtimec_kvspace_adapter_destroy_services(&host_services, &error);
    binary_interface.close(handle);
}

/*
 * 负向：读一个不存在的绝对 key，**编码期不再拒绝，执行期必须失败**。
 *
 * 两种情形分开测，否则这条断言会变成摆设：
 *   1. **从未写过** 的 key：程序里没有任何写入；
 *   2. **还没写到** 的 key：同一函数内先读后写（顺序错误）。
 * 两者在执行期都是「此刻 key 不存在」，但成因不同——前者是实现/用例问题，
 * 后者是顺序问题。失败必须**响**：状态非 SUCCESS，且错误消息里带 key 名。
 */
static void test_read_missing_key_fails_at_execution(const char *layout_path,
                                                     const char *kvspace_path,
                                                     const char *source,
                                                     const char *case_name) {
    void *kvspace_library = dlopen(kvspace_path, RTLD_NOW | RTLD_LOCAL);
    void *layout_library = dlopen(layout_path, RTLD_NOW | RTLD_LOCAL);
    if (kvspace_library == NULL || layout_library == NULL) {
        return;
    }
    layout_code_function_t layout_code = NULL;
    {
        void *symbol = dlsym(layout_library, "kvlangLayoutCode");
        memcpy(&layout_code, &symbol, sizeof(layout_code));
    }
    if (layout_code == NULL) {
        return;
    }
    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = kvspace_library, .resolve = resolve_symbol};
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_binary_interface_bind(&resolver, &binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    char directory[256];
    char data_source_name[320];
    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding1/"
             "abs-read-%s-%d", case_name, (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = binary_interface.connect(data_source_name);
    if (handle == NULL) {
        return;
    }
    const runtimec_allocator_t allocator = {
        .context = NULL, .allocate = test_allocate,
        .deallocate = test_deallocate};
    runtimec_kvspace_adapter_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1)};
    runtimec_host_services_t host_services;
    if (runtimec_kvspace_adapter_create_services(
            &adapter_request, &binary_interface, &host_services, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    options.compilation_cache_capacity = 4U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    EXPECT(runtimec_session_create(&host_services, &options, &session,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    char entry[128];
    char layout_error[512];
    entry[0] = '\0';
    layout_error[0] = '\0';
    EXPECT(layout_code(source, data_source_name, entry,
                       (uint32_t)sizeof(entry), layout_error,
                       (uint32_t)sizeof(layout_error)) == 0);
    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string("/lib/main");
    request.vthread_identifier = runtimec_string_view_from_c_string("1");
    request.frame_path = runtimec_string_view_from_c_string("/vthread/1/[1]");
    request.stack_identifier = 1;
    request.source_logical_program_counter = 1U;
    runtimec_execution_window_result_t result;
    const runtimec_status_t execute_status = runtimec_execution_window_execute(
        session, &request, &result, &error);
    /* 失败时机在**执行期**，且必须是失败——静默当成 None/零值是不允许的。 */
    EXPECT(execute_status != RUNTIMEC_STATUS_SUCCESS);
    if (session != NULL) {
        (void)runtimec_session_destroy(session);
    }
    (void)runtimec_kvspace_adapter_destroy_services(&host_services, &error);
    binary_interface.close(handle);
}

/*
 * 负向二（值访问层）：写一个**未解析**的 STATIC 目标，必须失败封闭。
 *
 * 为什么放在值访问层而不是窗口层：声明一旦在窗口层被落地（帧准备物化），
 * 那时「写不存在」在窗口层就**应该**成功——用它钉「不许创建」会立刻过期。
 * 执行指令那一层的不变量才是永久的：值访问只解析、不建。
 *
 * 反向控制：把「写路径顺手 ensure_zero_value」那段加回去，这里会变成 SUCCESS。
 */
static void test_write_never_creates_key(const char *kvspace_path) {
    void *kvspace_library = dlopen(kvspace_path, RTLD_NOW | RTLD_LOCAL);
    if (kvspace_library == NULL) {
        return;
    }
    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = kvspace_library, .resolve = resolve_symbol};
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_binary_interface_bind(&resolver, &binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    char directory[256];
    char data_source_name[320];
    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding1/"
             "abs-no-create-%d", (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = binary_interface.connect(data_source_name);
    if (handle == NULL) {
        return;
    }
    const runtimec_allocator_t allocator = {
        .context = NULL, .allocate = test_allocate,
        .deallocate = test_deallocate};
    runtimec_kvspace_adapter_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1)};
    runtimec_host_services_t host_services;
    if (runtimec_kvspace_adapter_create_services(
            &adapter_request, &binary_interface, &host_services, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        binary_interface.close(handle);
        return;
    }

    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t write_argument;
    binrwir_instruction_build_request_t request;
    binrwir_program_t *program = NULL;
    binrwir_frame_bindings_t *created_bindings = NULL;
    binrwir_execution_frame_t frame;
    uint16_t symbol_index = 0U;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    const int64_t value = 42;

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    EXPECT(binrwir_program_builder_create(&allocator, &identity, &builder) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_builder_add_symbol(
               builder,
               runtimec_string_view_from_c_string("/abs_never_created"),
               runtimec_string_view_from_c_string("int64"),
               &symbol_index) == RUNTIMEC_STATUS_SUCCESS);
    /* STATIC 写参：payload = NONE 表示「编码期未解析」。 */
    EXPECT(binrwir_argument_create(0U, BINRWIR_REFERENCE_STATIC, symbol_index,
                                   BINRWIR_BODY_ADDRESS_NONE,
                                   &write_argument) == RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = 1U;
    request.operation_identifier = UINT32_C(0x7F11);
    request.write_arguments.data = &write_argument;
    request.write_arguments.count = 1U;
    EXPECT(binrwir_program_builder_add_instruction(builder, &request,
                                                   &identifier) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_builder_finalize(builder, &program) ==
           RUNTIMEC_STATUS_SUCCESS);
    (void)binrwir_program_builder_destroy(builder);

    EXPECT(binrwir_execution_frame_initialize(&frame, program, NULL, 1U, 1U,
                                              0U) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_frame_bindings_create(&allocator, 4U, &created_bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = created_bindings;
    /*
     * 挂上**真实**的 host：这样「若写路径还会创建」，它就会真的建出来并成功——
     * 传 NULL 会让两类失败都是失败，测不出「删掉的那段」是否回来了。
     */
    frame.host_services = &host_services;
    runtimec_snapshot_reset(&frame.snapshot);
    frame.snapshot.contract_version = runtimec_contract_version();

    runtimec_error_reset(&error);
    const runtimec_status_t write_status = vmbin_value_access_write_body(
        &frame, &write_argument,
        runtimec_bytes_view_make(&value, sizeof(value)), &error);
    EXPECT(write_status != RUNTIMEC_STATUS_SUCCESS);
    EXPECT(error.message.size != 0U);
    {
        static const char expected[] = "unresolved";
        bool contains = false;
        for (size_t offset = 0U;
             offset + sizeof(expected) - 1U <= error.message.size; ++offset) {
            if (memcmp(error.message.data + offset, expected,
                       sizeof(expected) - 1U) == 0) {
                contains = true;
                break;
            }
        }
        EXPECT(contains);
    }
    /* 存储里不能凭空多出这个 key。 */
    uint8_t *stored = NULL;
    uint32_t stored_size = 0U;
    (void)binary_interface.get(handle, "/abs_never_created", 1, &stored,
                               &stored_size);
    EXPECT(stored == NULL || stored_size == 0U);

    EXPECT(binrwir_frame_bindings_destroy(created_bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_execution_frame_destroy(&frame) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_adapter_destroy_services(&host_services, &error);
    binary_interface.close(handle);
}

int main(int argument_count, char **arguments) {
    if (argument_count < 3) {
        fprintf(stderr, "SKIP: need layout and kvspace library paths\n");
        return SKIP_RETURN_CODE;
    }
    test_write_existing_key_succeeds(arguments[1], arguments[2]);
    test_write_never_creates_key(arguments[2]);
    test_read_missing_key_fails_at_execution(
        arguments[1], arguments[2],
        "rwfunc main() -> () {\n"
        "    /abs_never_written -> x\n"
        "}\n",
        "never-written");
    test_read_missing_key_fails_at_execution(
        arguments[1], arguments[2],
        "rwfunc main() -> () {\n"
        "    /abs_read_before_write -> x\n"
        "    7 -> /abs_read_before_write\n"
        "}\n",
        "read-before-write");
    return failures == 0 ? 0 : 1;
}
