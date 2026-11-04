/*
 * `kvspace·set` 写出去的必须是**完整 wire**，不是裸 body。
 *
 * 为什么单列一条：这条路径曾经 `locate` 之后直接把 `location.read_bytes` 交给
 * `kvspaceSetValue`。对**字面量**读参那是对的（`locate` 对字面量给整条 wire），
 * 但对**普通槽**（STATIC / LOCAL / 形参）它给的是 body——于是写进存储的是一条
 * 没有 16 字节头的裸缓冲。那种错误不会崩，只会让读回来的值缺 head 缺 attr。
 *
 * 断言必须是「**比对整条 wire**」，不能是「值相等」：
 *
 *     值相等       ← bug 存在时照样绿（body 的字节可能一字不差）
 *     wire 逐字节  ← 才抓得住
 *
 * 本用例真的驱动 handler：建 Program（两个 STATIC 读参，payload 填真实值体
 * 地址 → 走非字面量分支）、建帧与绑定、按 opid 从 handler 表取 set 条目调用。
 * 不驱动 handler 就只是测了一遍 kvspace ABI，与待测的 bug 无关。
 *
 * 反向控制：把 host.c 里 set 的 `is_whole_wire` 分流去掉、改回直接传
 * `location.read_bytes`，本用例必须红。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "binrwir/registry.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "kvspace/kvspace.h"
#include "vmbin/handlers/host.h"

#define SKIP_RETURN_CODE 77

#define TEST_KEY_PATH "/probe/target"
#define TEST_VALUE_PATH "/probe/source"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            failures += 1;                                                     \
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

static const binrwir_registry_entry_t *test_find_entry(
    binrwir_operation_identifier_t operation_identifier) {
    size_t count = 0U;
    const binrwir_registry_entry_t *entries =
        runtimec_host_handler_entries(&count);

    if (entries == NULL) {
        return NULL;
    }
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (entries[ordinal].operation_identifier == operation_identifier) {
            return &entries[ordinal];
        }
    }
    return NULL;
}

int main(void) {
    runtimec_allocator_t allocator = {NULL, test_allocate, test_deallocate};
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_frame_t frame;
    binrwir_execution_exit_t exit_state;
    binrwir_instruction_view_t instruction_view;
    const binrwir_registry_entry_t *set_entry;
    void *handle = NULL;
    char directory[] = "/tmp/runtimec-host-set-wire-XXXXXX";
    char dsn[1024];
    char store_path[768];
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    uint16_t key_symbol = 0U;
    uint16_t value_symbol = 0U;
    const uint8_t *source_wire = NULL;
    uint32_t source_wire_size = 0U;
    runtimec_status_t status;

    if (mkdtemp(directory) == NULL) {
        fprintf(stderr, "SKIP: cannot create a temporary directory\n");
        return SKIP_RETURN_CODE;
    }
    snprintf(store_path, sizeof(store_path), "%s/store.shm", directory);
    snprintf(dsn, sizeof(dsn), "shm://%s", store_path);
    handle = kvspaceConnect(dsn);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect kvspace at %s\n", dsn);
        return SKIP_RETURN_CODE;
    }

    /* 源值：先经 ABI 写进存储，set 的 value 读参指向它。 */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0;
        char message[256] = {0};

        EXPECT(kvspaceNewInt64(INT64_C(4242), "int64", &wire, &wire_size) == 0);
        EXPECT(kvspaceSetValue(handle, TEST_VALUE_PATH, wire, wire_size, message,
                               sizeof(message)) == 0);
        /* 记住这条 wire 的原样字节，稍后逐字节比对。 */
        EXPECT(kvspaceGet(handle, TEST_VALUE_PATH, 1,
                          (uint8_t **)&source_wire, &source_wire_size) == 0);
        EXPECT(source_wire != NULL);
        EXPECT(source_wire_size > KVSPACE_XVALUE_HEAD_LEN);
    }

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    EXPECT(binrwir_program_builder_create(&allocator, &identity, &builder) ==
           RUNTIMEC_STATUS_SUCCESS);

    EXPECT(binrwir_program_builder_add_symbol(
               builder, runtimec_string_view_from_c_string(TEST_KEY_PATH),
               runtimec_string_view_from_c_string("[]char/utf8"),
               &key_symbol) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_builder_add_symbol(
               builder, runtimec_string_view_from_c_string(TEST_VALUE_PATH),
               runtimec_string_view_from_c_string("int64"), &value_symbol) ==
           RUNTIMEC_STATUS_SUCCESS);

    /*
     * 两个 STATIC 读参：slot 是符号下标，payload 是**值体地址**。
     * 填值体地址（不是 wire 基址）——`locate` 对 STATIC 就是这么读的，
     * 也正是「普通槽给 body」这条分支的入口。
     */
    {
        binrwir_argument_t arguments[2];
        binrwir_instruction_build_request_t request;
        uint8_t *key_wire = NULL;
        uint32_t key_wire_size = 0;

        EXPECT(kvspaceGet(handle, TEST_KEY_PATH, 1, &key_wire,
                          &key_wire_size) == 0);
        /* key 槽也要真实存在，否则 handler 取 key 会失败。先写一个占位。 */
        if (key_wire == NULL) {
            uint8_t *placeholder = NULL;
            uint32_t placeholder_size = 0;
            char message[256] = {0};

            EXPECT(kvspaceNewChar((const uint8_t *)TEST_KEY_PATH,
                                  (uint32_t)strlen(TEST_KEY_PATH),
                                  "[]char/utf8", &placeholder,
                                  &placeholder_size) == 0);
            EXPECT(kvspaceSetValue(handle, TEST_KEY_PATH, placeholder,
                                   placeholder_size, message,
                                   sizeof(message)) == 0);
            EXPECT(kvspaceGet(handle, TEST_KEY_PATH, 1, &key_wire,
                              &key_wire_size) == 0);
        }

        EXPECT(binrwir_argument_create(
                   0U, BINRWIR_REFERENCE_STATIC, key_symbol,
                   (uint64_t)(uintptr_t)
                       (key_wire + KVSPACE_XVALUE_HEAD_LEN),
                   &arguments[0]) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_argument_create(
                   1U, BINRWIR_REFERENCE_STATIC, value_symbol,
                   (uint64_t)(uintptr_t)
                       (source_wire + KVSPACE_XVALUE_HEAD_LEN),
                   &arguments[1]) == RUNTIMEC_STATUS_SUCCESS);

        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = 1U;
        request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_SET;
        request.read_arguments.data = arguments;
        request.read_arguments.count = 2U;
        request.write_arguments.data = NULL;
        request.write_arguments.count = 0U;
        EXPECT(binrwir_program_builder_add_instruction(
                   builder, &request, &instruction_identifier) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    EXPECT(binrwir_program_builder_finalize(builder, &program) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_frame_bindings_create(&allocator, 8U, &bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_execution_frame_initialize(
               &frame, program, NULL, 1U, 1U, instruction_identifier) ==
           RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = bindings;
    frame.kvspace_handle = handle;
    frame.frame_path = runtimec_key_view_from_c_string("/vthread/1/[1]");

    set_entry = test_find_entry(BINRWIR_OPERATION_IDENTIFIER_KVSPACE_SET);
    EXPECT(set_entry != NULL);
    EXPECT(binrwir_program_get_instruction(program, instruction_identifier,
                                           &instruction_view) ==
           RUNTIMEC_STATUS_SUCCESS);

    if (set_entry != NULL) {
        memset(&exit_state, 0, sizeof(exit_state));
        status = set_entry->handler(&frame, &instruction_view, &exit_state);
        EXPECT(status == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_NONE);
    }

    /* 核心断言：存储里的目标值必须是**整条 wire**，逐字节等于源。 */
    {
        uint8_t *written = NULL;
        uint32_t written_size = 0;

        EXPECT(kvspaceGet(handle, TEST_KEY_PATH, 1, &written, &written_size) ==
               0);
        EXPECT(written != NULL);
        /*
         * 这条是规格本体：长度与字节都要对上。
         * 「值相等」的断言在这里没有意义——bug 存在时 body 那 8 字节照样对。
         */
        EXPECT(written_size == source_wire_size);
        if (written != NULL && written_size == source_wire_size) {
            EXPECT(memcmp(written, source_wire, source_wire_size) == 0);
        }
    }

    EXPECT(binrwir_frame_bindings_destroy(bindings) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_execution_frame_destroy(&frame) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_builder_destroy(builder) == RUNTIMEC_STATUS_SUCCESS);

    kvspaceClose(handle);
    /* 清理用 unlink/rmdir 而不是 system("rm ...")：不经过 shell，也不忽略返回值。
     * 打不开就跳过——用例的结论不该被清理失败改写。 */
    if (unlink(store_path) != 0) {
        /* shm 后端可能不带这个后缀，下面的 rmdir 会兜底报出来。 */
    }
    if (rmdir(directory) != 0) {
        fprintf(stderr, "warning: cannot remove %s\n", directory);
    }

    if (failures != 0) {
        printf("host_kvspace_set_wire_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("host_kvspace_set_wire_test: OK\n");
    return 0;
}
