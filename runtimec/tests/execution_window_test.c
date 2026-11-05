/*
 * engine/execution_window 验收：一次执行窗口的完整链路（取快照 → 命中
 * compilation repository → 读执行目标 → 顶层帧形参绑定 → vmbin 执行 →
 * 提交 checkpoint），未命中时的编码失败封闭，以及起始位置与帧路径的失败形状。
 *
 * 命中的那条路径由测试自己预置 compilation：Program 用 builder 构造、经
 * BinRWIR Compiler 编译、按执行窗口会算出的同一把 key 入仓。未命中的那条路径
 * 走真实 vmkv 编码，host 的读能力给的是失败封闭的替身，因此不产生 Program。
 *
 * **夹具约定（测世代路径时必读）**：不得复用已缓存的 compilation。
 * 缓存命中时不查 `source_revision`，于是覆盖快照世代的夹具不会被调用，
 * 断言读到的是**上一次**的快照。世代路径的用例要换函数路径，或显式清缓存。
 *
 * 这是**夹具的坑，不是产品的洞**：缓存命中本身意味着 source revision 没变
 * （世代变了 → 缓存键变 → 必然 miss），所以「命中后仍校验世代」在语义上恒真。
 * 不要在产品代码里为它加校验。
 */

#define _GNU_SOURCE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "binrwir/argument.h"
#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/signature.h"
#include "binrwir_compiler/compiler.h"
#include "contract/checkpoint.h"
#include "contract/kvspace_access.h"
#include "contract/snapshot.h"
#include "engine/compilation_repository.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/core_operations.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

/*
 * 真后端的 source revision generation：首次取即推进到 1，之后稳定。预置的
 * compilation 用它做 snapshot_generation，否则查不中、窗口回落去编码。
 */
#define TEST_GENERATION UINT64_C(1)
#define TEST_INSTANCE UINT64_C(1)
#define TEST_PROGRAM_GENERATION UINT64_C(11)
#define TEST_BODY_SIZE 8U
#define TEST_TARGET "/lib/route"
#define TEST_DONE_TARGET "/lib/route-done"
#define TEST_MISSING_TARGET "/lib/missing"
#define TEST_ZERO_ARGUMENT_TARGET "/lib/zero"
#define TEST_LOCALS_TARGET "/lib/locals"
#define TEST_ROUND_TRIP_TARGET "/lib/round-trip"
#define TEST_VTHREAD "2"
#define TEST_FRAME_PATH "/vthread/2/[1]"
#define TEST_FRAME_PATH_DONE "/vthread/2/[2]"
#define TEST_FRAME_PATH_ZERO "/vthread/2/[3]"
#define TEST_FRAME_PATH_LOCALS "/vthread/2/[4]"
#define TEST_FRAME_PATH_ROUND_TRIP "/vthread/2/[5]"
/* HANDOFF：两个程序只差源槽里的算子名，队列 key 必须跟着变。 */
#define TEST_HANDOFF_ALPHA_TARGET "/lib/handoff-alpha"
#define TEST_HANDOFF_BETA_TARGET "/lib/handoff-beta"
#define TEST_FRAME_PATH_HANDOFF_ALPHA "/vthread/2/[6]"
#define TEST_FRAME_PATH_HANDOFF_BETA "/vthread/2/[7]"
#define TEST_HANDOFF_ALPHA_COUNTER 21U
#define TEST_HANDOFF_BETA_COUNTER 22U
#define TEST_HANDOFF_ALPHA_NAME "ext\xC2\xB7" "alpha"
#define TEST_HANDOFF_BETA_NAME "ext\xC2\xB7" "beta"
#define TEST_STACK_IDENTIFIER 1
#define TEST_READ_KEY "/vthread/2/[0,0]"
#define TEST_WRITE_KEY "/vthread/2/[0,-1]"
/* 第一条 Program：COPY(3) CALL(4) COPY(5)，CALL 的后继是 source pc 5。 */
#define TEST_FIRST_SOURCE_COUNTER 3U
#define TEST_CALL_SOURCE_COUNTER 4U
#define TEST_RESUME_SOURCE_COUNTER 5U
/* 第二条 Program：COPY(9) RETURN(10)，RETURN 自己就是刚完成的位置。 */
#define TEST_DONE_COPY_SOURCE_COUNTER 9U
#define TEST_RETURN_SOURCE_COUNTER 10U

typedef struct test_value {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + TEST_BODY_SIZE];
} test_value_t;

typedef struct test_frame_slot {
    const char *key;
    test_value_t *value;
} test_frame_slot_t;

typedef struct test_context {
    /*
     * 数据面：**真 kvspace**。窗口操作的一切效果（`‥pc`、`‥status`、`‥lib`、
     * 局部零值）都落到这里，断言直接读回状态。
     *
     * 原来这里是一组函数指针替身，用例断言 `read_count` / `ensure_frame_count`
     * / `resolve_count` 这类**调用次数**。那些是「状态没被弄坏」的代理指标：
     * 代理指标会在实现换形态时失效，状态判据不会。所以本文件：
     *   能写成状态断言的一律改写；写不成的删掉并在原位写明理由。
     */
    void *handle;
    runtimec_kvspace_access_t *access;
    char directory[256];
    /* 未命中路径要求「读失败」：把编码器指向一个不存在的函数即可，无需替身。 */
    runtimec_status_t ensure_frame_status;
} test_context_t;

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

static runtimec_allocator_t test_allocator(void) {
    runtimec_allocator_t allocator;

    allocator.context = NULL;
    allocator.allocate = test_allocate;
    allocator.deallocate = test_deallocate;
    return allocator;
}

/* 临时目录：/tmp + pid + 序号，用完整套删（后端摊出五份文件）。 */
static bool test_store_open(test_context_t *test) {
    static int sequence = 0;
    memset(test, 0, sizeof(*test));
    snprintf(test->directory, sizeof(test->directory),
             "/tmp/runtimec-exec-window-%d-%d", (int)getpid(), sequence++);
    (void)mkdir(test->directory, 0700);
    char data_source[360];
    snprintf(data_source, sizeof(data_source), "shm://%s/store.shm",
             test->directory);
    test->handle = kvspaceConnect(data_source);
    return test->handle != NULL;
}

static runtimec_status_t test_store_close(test_context_t *test) {
    if (test->access != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)runtimec_kvspace_access_destroy(test->access, &ignored);
        test->access = NULL;
    }
    if (test->handle != NULL) {
        kvspaceClose(test->handle);
        test->handle = NULL;
    }
    static const char *const suffixes[] = {"store.shm", "store.shm.garb",
                                           "store.shm.gen", "store.shm.sbo.data",
                                           "store.shm.sbo.head"};
    char path[512];
    for (size_t index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]);
         index++) {
        snprintf(path, sizeof(path), "%s/%s", test->directory, suffixes[index]);
        (void)unlink(path);
    }
    (void)rmdir(test->directory);
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 写一条完整 wire（16 字节 head + body + attr）并读回自检。 */
static bool test_store_put(test_context_t *test, const char *key,
                           const uint8_t *body, size_t body_size,
                           const char *langtype) {
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack(body, body_size, body_size, langtype, &wire,
                                  &wire_size) != 0 ||
        wire == NULL) {
        return false;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int written = kvspaceSetValue(test->handle, key, wire,
                                        (uint32_t)wire_size, error_text,
                                        (uint32_t)sizeof(error_text));
    free(wire);
    if (written != 0) {
        fprintf(stderr, "test_store_put(%s) failed: %s\n", key, error_text);
        return false;
    }
    uint8_t *readback = NULL;
    uint32_t readback_size = 0U;
    const bool present =
        kvspaceGet(test->handle, key, 1, &readback, &readback_size) == 0 &&
        readback != NULL && readback_size != 0U;
    if (!present) {
        fprintf(stderr, "test_store_put self-check failed: %s\n", key);
    }
    return present;
}

/* 读回一条 key 的完整 wire（调用方不得 free：后端持有）。 */
static bool test_store_get(test_context_t *test, const char *key,
                           uint8_t **output_wire, uint32_t *output_size) {
    *output_wire = NULL;
    *output_size = 0U;
    return kvspaceGet(test->handle, key, 1, output_wire, output_size) == 0 &&
           *output_wire != NULL && *output_size != 0U;
}

/* 读回某条 key 的 body 长度（从值头读，不猜）。 */
static bool test_store_body_length(test_context_t *test, const char *key,
                                   uint64_t *output_length) {
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;

    *output_length = 0U;
    if (!test_store_get(test, key, &wire, &wire_size) ||
        wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return false;
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    *output_length = kvspaceXvalueHeadLen(&head);
    return true;
}

/* 一个值：值头声明当前长度，值体在存储内可直接读写。 */
static runtimec_status_t test_program_add_instruction(
    binrwir_program_builder_t *builder,
    binrwir_operation_identifier_t operation_identifier,
    uint32_t source_logical_program_counter, const binrwir_argument_t *read,
    size_t read_count, const binrwir_argument_t *write, size_t write_count) {
    binrwir_instruction_build_request_t request;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

    request.source_logical_program_counter = source_logical_program_counter;
    request.operation_identifier = operation_identifier;
    request.read_arguments.data = read;
    request.read_arguments.count = read_count;
    request.write_arguments.data = write;
    request.write_arguments.count = write_count;
    return binrwir_program_builder_add_instruction(builder, &request,
                                                   &instruction_identifier);
}


/*
 * 零参数、零指令以外的程序：`rwfunc test() -> ()` 的形状。
 * 一条 RETURN 就够——它让执行窗口能跑到结构化退出。
 */
static runtimec_status_t test_program_build_zero_argument(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t arguments[1];
    uint16_t symbol_index = 0U;
    runtimec_status_t status;

    *output_program = NULL;
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = TEST_GENERATION;
    identity.program_generation = TEST_PROGRAM_GENERATION;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = canonical_encoding_revision;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 无参数；一条 RETURN，source pc 取 1。 */
    if (binrwir_argument_create(0U, BINRWIR_REFERENCE_FUNCTION, 0U, 0U,
                                &arguments[0]) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)symbol_index;
    (void)arguments;
    {
        binrwir_instruction_build_request_t request;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;

        request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
        request.operation_identifier = VMBIN_OPERATION_IDENTIFIER_RETURN;
        request.read_arguments.data = NULL;
        request.read_arguments.count = 0U;
        request.write_arguments.data = NULL;
        request.write_arguments.count = 0U;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}



/*
 * 局部变量的**读写往返**程序：`rwfunc probe() -> () { x:int64 = 0  7 -> x  x -> y }`
 *
 *   [1,0] copy  7 -> x         字面量写局部 x
 *   [2,0] copy  x -> y         读局部 x、写另一个局部 y
 *
 * 两条都是 COPY：写 x 走一次绑定查找，读 x 再走一次。绑定表没写进去时，
 * 第二条就会 UNBOUND_SLOT。
 */
static runtimec_status_t test_program_build_local_round_trip(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t literal_argument;
    binrwir_argument_t source_argument;
    binrwir_argument_t target_argument;
    uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
    uint16_t source_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    uint16_t target_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    /*
     * `add_literal` 收的是**完整 wire**（16 字节头 + body + attr），不是裸 body。
     * 这里曾直接传 `&(uint64_t){7U}` 的 8 字节——两处都错：
     *   1. 那不是 wire，读取端按 wire 剥头，剥完什么都不剩，值读成 0；
     *   2. `&(uint64_t){7U}` 是复合字面量，生存期只到该表达式结束，而
     *      `add_literal` 借用至 `finalize`，读到的是栈上残留。
     * 所以用库自己的构造器产 wire，并存在函数作用域的变量里。
     */
    const uint64_t literal_body = 7U;
    uint8_t literal_wire[KVSPACE_XVALUE_HEAD_LEN + sizeof(uint64_t)];
    {
        kvspaceXvalueHead head;

        memset(&head, 0, sizeof(head));
        head.storetype = KVSPACE_XVALUE_ST_SHORT;
        head.bodylen = sizeof(literal_body);
        head.attrslen = 0U;
        memcpy(literal_wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memcpy(literal_wire + KVSPACE_XVALUE_HEAD_LEN, &literal_body,
               sizeof(literal_body));
    }
    runtimec_status_t status;

    *output_program = NULL;
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = TEST_GENERATION;
    identity.program_generation = TEST_PROGRAM_GENERATION;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = canonical_encoding_revision;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("x"),
            runtimec_string_view_from_c_string("int64"),
            &source_symbol) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("y"),
            runtimec_string_view_from_c_string("int64"),
            &target_symbol) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_builder_add_literal(
            builder, runtimec_bytes_view_make(literal_wire, sizeof(literal_wire)),
            runtimec_string_view_from_c_string("int64"),
            &literal_index) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U,
                                literal_index, &literal_argument) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL, source_symbol, 0U,
                                &source_argument) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_argument_create(2U, BINRWIR_REFERENCE_LOCAL, target_symbol, 0U,
                                &target_argument) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 7 -> x */
    if (test_program_add_instruction(builder, VMBIN_OPERATION_IDENTIFIER_COPY,
                                     TEST_FIRST_SOURCE_COUNTER,
                                     &literal_argument, 1U, &source_argument,
                                     1U) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    /* x -> y */
    if (test_program_add_instruction(builder, VMBIN_OPERATION_IDENTIFIER_COPY,
                                     TEST_FIRST_SOURCE_COUNTER + 1U,
                                     &source_argument, 1U, &target_argument,
                                     1U) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * RETURN 收尾是**必须的**，不是凑数：DONE 出口提交的是「刚完成那条指令」
     * 的位置，而游标跑到指令表末尾之外时没有位置可提交，窗口会失败封闭报
     * 「completed exit carries no source program counter」。
     *
     * 别的 Program 构造器（`test_program_build_done` 等）都以 RETURN 结尾，
     * 这条用例最初漏了它——于是执行失败，而下一条断言只看 `y == 7` 拿不到值，
     * 报出来像是「往返失败」，实际是「没有收尾指令」。
     */
    if (test_program_add_instruction(builder, VMBIN_OPERATION_IDENTIFIER_RETURN,
                                     TEST_FIRST_SOURCE_COUNTER + 2U, NULL, 0U,
                                     NULL, 0U) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_program_builder_finalize(builder, output_program);
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/*
 * 带局部变量的零参数函数：`rwfunc f() -> () { y:int64 = 0; flag:bool = false }`。
 * 两条 LOCAL 分别在写位出现，符号池里带各自的 langtype——执行窗口靠这个物化槽。
 */
static runtimec_status_t test_program_build_with_locals(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t counter_argument;
    binrwir_argument_t flag_argument;
    uint16_t counter_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    uint16_t flag_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    runtimec_status_t status;

    *output_program = NULL;
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = TEST_GENERATION;
    identity.program_generation = TEST_PROGRAM_GENERATION;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = canonical_encoding_revision;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("x"),
            runtimec_string_view_from_c_string("int64"),
            &counter_symbol) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("flag"),
            runtimec_string_view_from_c_string("bool"), &flag_symbol) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_argument_create(0U, BINRWIR_REFERENCE_LOCAL, counter_symbol, 0U,
                                &counter_argument) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL, flag_symbol, 0U,
                                &flag_argument) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return RUNTIMEC_STATUS_INVALID;
    }
    {
        binrwir_instruction_build_request_t request;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
        binrwir_argument_t both[2];

        both[0] = counter_argument;
        both[1] = flag_argument;
        request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
        request.operation_identifier = VMBIN_OPERATION_IDENTIFIER_RETURN;
        /* 两个局部都在读位出现：执行窗口必须把它们物化出来。 */
        request.read_arguments.data = both;
        request.read_arguments.count = 2U;
        request.write_arguments.data = NULL;
        request.write_arguments.count = 0U;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/* 建一份带一个读参、一个写参、一个函数名符号的 builder 骨架。 */
static runtimec_status_t test_program_begin(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_builder_t **output_builder, binrwir_argument_t *arguments) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    uint32_t parameter_index = 0U;
    uint16_t symbol_index = 0U;
    runtimec_status_t status;

    *output_builder = NULL;
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = TEST_GENERATION;
    identity.program_generation = TEST_PROGRAM_GENERATION;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = canonical_encoding_revision;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_program_builder_add_parameter(
        builder, BINRWIR_PARAMETER_DIRECTION_READ, 0U,
        runtimec_string_view_from_c_string("int64"), &parameter_index);
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_WRITE, 1U,
            runtimec_string_view_from_c_string("int64"), &parameter_index) !=
            RUNTIMEC_STATUS_SUCCESS) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("g"),
            runtimec_string_view_make(NULL, 0U), &symbol_index) !=
            RUNTIMEC_STATUS_SUCCESS) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        (binrwir_argument_create(0U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                                 &arguments[0]) != RUNTIMEC_STATUS_SUCCESS ||
         binrwir_argument_create(1U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                                 &arguments[1]) != RUNTIMEC_STATUS_SUCCESS ||
         binrwir_argument_create(2U, BINRWIR_REFERENCE_FUNCTION, symbol_index,
                                 0U, &arguments[2]) != RUNTIMEC_STATUS_SUCCESS)) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return status;
    }
    *output_builder = builder;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 0 COPY  source 3
 * 1 CALL  source 4（读区只有函数名，写区为空）
 * 2 COPY  source 5
 * CALL 的后继存在，所以它的 resume source pc 是 5——窗口提交一个可恢复位置。
 */
static runtimec_status_t test_program_build_call(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_t **output_program) {
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t arguments[3];
    runtimec_status_t status;

    *output_program = NULL;
    status = test_program_begin(allocator, canonical_encoding_revision, &builder,
                                arguments);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_add_instruction(
            builder, VMBIN_OPERATION_IDENTIFIER_COPY, TEST_FIRST_SOURCE_COUNTER,
            &arguments[0], 1U, &arguments[1], 1U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_add_instruction(
            builder, VMBIN_OPERATION_IDENTIFIER_CALL, TEST_CALL_SOURCE_COUNTER,
            &arguments[2], 1U, NULL, 0U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_add_instruction(
            builder, VMBIN_OPERATION_IDENTIFIER_COPY,
            TEST_RESUME_SOURCE_COUNTER, &arguments[0], 1U, &arguments[1], 1U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/*
 * 0 COPY    source 9
 * 1 RETURN  source 10
 * RETURN 是完成位置的来源：DONE 出口的 logical program counter 就是 10。
 */
static runtimec_status_t test_program_build_done(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    binrwir_program_t **output_program) {
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t arguments[3];
    runtimec_status_t status;

    *output_program = NULL;
    status = test_program_begin(allocator, canonical_encoding_revision, &builder,
                                arguments);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_add_instruction(
            builder, VMBIN_OPERATION_IDENTIFIER_COPY,
            TEST_DONE_COPY_SOURCE_COUNTER, &arguments[0], 1U, &arguments[1], 1U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_add_instruction(
            builder, VMBIN_OPERATION_IDENTIFIER_RETURN,
            TEST_RETURN_SOURCE_COUNTER, NULL, 0U, NULL, 0U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/*
 * HANDOFF 程序：一条 FOREIGN 标记收尾，`source_logical_program_counter` 指向
 * layout 写下的源槽 `<func>/[<pc>,0]`——那个槽的 body 里才是算子名。
 *
 * **两个程序只差源槽里的名字**（`ext·alpha` / `ext·beta`），因此它们跑出来的
 * 队列 key 必须跟着变。只断言「队列里有东西」会放过「名字取错但恰好非空」。
 */
static runtimec_status_t test_program_build_handoff(
    const runtimec_allocator_t *allocator, uint32_t canonical_encoding_revision,
    uint32_t source_program_counter, binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    runtimec_status_t status;

    *output_program = NULL;
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = TEST_GENERATION;
    identity.program_generation = TEST_PROGRAM_GENERATION;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = canonical_encoding_revision;
    /* **零参数**：窗口要求 `argument_count == 读参+写参`，所以 HANDOFF 程序的
     * 签名必须为空，用例才能传 `arguments = NULL`。 */
    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        /* FOREIGN 是结构标记：零读参零写参，只带 source pc。 */
        status = test_program_add_instruction(
            builder, BINRWIR_OPERATION_IDENTIFIER_FOREIGN, source_program_counter,
            NULL, 0U, NULL, 0U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/*
 * 扮演认领方：轮询等队列项出现后删除它，模拟外部扩展完成 handoff。
 */
typedef struct handoff_claimer {
    void *handle;
    const char *queue_key;
    bool claimed;
} handoff_claimer_t;

static void *claim_handoff_entry(void *context) {
    handoff_claimer_t *claimer = context;

    for (int attempt = 0; attempt < 3000; attempt++) {
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        if (kvspaceGet(claimer->handle, claimer->queue_key, 1, &value,
                       &value_size) == 0 &&
            value != NULL && value_size != 0U) {
            char error_text[256];
            const char *keys[1];
            error_text[0] = '\0';
            keys[0] = claimer->queue_key;
            (void)kvspaceDel(claimer->handle, keys, 1U, error_text,
                             (uint32_t)sizeof(error_text));
            claimer->claimed = true;
            return NULL;
        }
        usleep(1000);
    }
    return NULL;
}

/* 写一条「指令槽」：counts 头 5 字节 + 操作名文本。 */
static bool test_store_put_opcode(test_context_t *test, const char *key,
                                  const char *operation_name) {
    uint8_t body[128];
    const size_t name_size = strlen(operation_name);

    memset(body, 0, sizeof(body));
    body[0] = 0U; /* reads */
    body[1] = 0U;
    body[2] = 0U; /* writes */
    body[3] = 0U;
    body[4] = 0U; /* dynamic */
    /* body = counts(5) + 算子名 + NUL + 类型串。**必须带 NUL 与类型串**：
     * 解析方用「有没有 NUL」区分「纯算子名槽」与「参数槽」，无 NUL 的纯名字
     * 会被判成「名字带类型后缀」。 */
    /* 源槽 body = counts(5) + **纯算子名**（无 NUL、无类型串）。
     * 带 NUL 会被解析方判成「名字带类型后缀」——那是参数槽的形态，不是指令槽。 */
    memcpy(body + 5U, operation_name, name_size);
    return test_store_put(test, key, body, 5U + name_size, "rwir");
}

/* 窗口会算出的同一把 key：snapshot + 四个版本号。 */
static runtimec_status_t test_repository_key_build(
    const runtimec_session_runtime_components_t *components,
    const char *target, uint32_t canonical_encoding_revision,
    runtimec_compilation_repository_key_t *output_key) {
    memset(output_key, 0, sizeof(*output_key));
    runtimec_snapshot_reset(&output_key->snapshot);
    output_key->snapshot.contract_version = runtimec_contract_version();
    output_key->snapshot.source_revision.instance_identifier = TEST_INSTANCE;
    output_key->snapshot.source_revision.generation = TEST_GENERATION;
    /* 真后端不提供 schema 版本，如实报 0（与 kvspace_access 一致）。 */
    output_key->snapshot.source_revision.schema_version = 0U;
    output_key->snapshot.source_revision.instruction_set_version = 1U;
    output_key->snapshot.function_key = runtimec_key_view_from_c_string(target);
    output_key->rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    output_key->canonical_encoding_revision = canonical_encoding_revision;
    output_key->binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    return binrwir_compiler_get_revision(components->binrwir_compiler,
                                         &output_key->compiler_revision);
}

static runtimec_status_t test_compile_and_insert(
    const runtimec_session_runtime_components_t *components,
    const char *target, uint32_t canonical_encoding_revision,
    binrwir_program_t *program, runtimec_error_t *error) {
    runtimec_compilation_repository_key_t key;
    runtimec_compilation_repository_lease_t lease;
    binrwir_compilation_t *compilation = NULL;
    runtimec_status_t status;

    /* 编译器接管 program：窗口不再销毁它。 */
    status = binrwir_compiler_compile(components->binrwir_compiler, program,
                                      &compilation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = test_repository_key_build(components, target,
                                       canonical_encoding_revision, &key);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_compilation_repository_insert(
            components->compilation_repository, &key, compilation, &lease,
            error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compilation_destroy(compilation, &ignored);
        return status;
    }
    return runtimec_compilation_repository_release(
        components->compilation_repository, &lease);
}

int main(void) {
    const runtimec_allocator_t allocator = test_allocator();
    test_context_t test;
    runtimec_session_options_t options;
    runtimec_session_t *session = NULL;
    runtimec_session_runtime_components_t components;
    runtimec_error_t error;
    binrwir_program_t *program = NULL;
    runtimec_argument_description_t arguments[2];
    runtimec_execution_window_request_t request;
    runtimec_execution_window_result_t result;
    uint32_t canonical_encoding_revision = 0U;

    uint8_t read_bytes[sizeof(uint64_t)];
    uint8_t write_bytes[sizeof(uint64_t)];
    {
        const uint64_t read_value = UINT64_C(0x1122334455667788);
        const uint64_t write_value = 0U;
        memcpy(read_bytes, &read_value, sizeof(read_bytes));
        memcpy(write_bytes, &write_value, sizeof(write_bytes));
    }

    runtimec_error_reset(&error);
    if (!test_store_open(&test)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return 0;
    }
    const runtimec_kvspace_access_request_t access_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = test.handle,
        .instance_identifier = TEST_INSTANCE,
    };
    EXPECT(runtimec_kvspace_access_create(&access_request, &test.access,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
    if (test.access == NULL) {
        (void)test_store_close(&test);
        return 1;
    }
    /* 两个实参槽是**调用方已存在的 key**：真的写进真后端。 */
    EXPECT(test_store_put(&test, TEST_READ_KEY, read_bytes, sizeof(read_bytes),
                          "int64"));
    EXPECT(test_store_put(&test, TEST_WRITE_KEY, write_bytes,
                          sizeof(write_bytes), "int64"));
    /*
     * 每条被执行的函数都要有 `‥ctime`：它是 source revision 的唯一单调源事实，
     * 缺了窗口在建帧前就失败。`/lib/missing` 故意不给——未命中路径要它真的读不到。
     */
    {
        static const char *const function_paths[] = {
            TEST_TARGET, TEST_DONE_TARGET, TEST_ZERO_ARGUMENT_TARGET,
            TEST_LOCALS_TARGET, TEST_ROUND_TRIP_TARGET,
            /* `/lib/missing` 也要有 ctime：未命中用例的失败点要落在**编码**
             * （签名锚点缺失），而不是快照门。 */
            TEST_MISSING_TARGET, TEST_HANDOFF_ALPHA_TARGET,
            TEST_HANDOFF_BETA_TARGET,
        };
        static const uint8_t revision_bytes[8] = {1, 0, 0, 0, 0, 0, 0, 0};
        char ctime_key[192];
        for (size_t index = 0U;
             index < sizeof(function_paths) / sizeof(function_paths[0]);
             index++) {
            snprintf(ctime_key, sizeof(ctime_key), "%s/\xE2\x80\xA5" "ctime",
                     function_paths[index]);
            EXPECT(test_store_put(&test, ctime_key, revision_bytes,
                                  sizeof(revision_bytes), "int64"));
        }
        /* HANDOFF 的算子名来自源槽 `<func>/[<pc>,0]` 的 body。 */
        EXPECT(test_store_put_opcode(&test,
                                     TEST_HANDOFF_ALPHA_TARGET "/[21,0]",
                                     TEST_HANDOFF_ALPHA_NAME));
        EXPECT(test_store_put_opcode(&test,
                                     TEST_HANDOFF_BETA_TARGET "/[22,0]",
                                     TEST_HANDOFF_BETA_NAME));
    }

    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    /*
     * 容量必须容得下本用例预置的**全部** compilation，否则 `acquire_slot` 会
     * 复用槽、把最早插入的那条挤掉，于是它之后的查找变成 NOT_FOUND、回落去
     * 编码——而编码要读 kvspace 里的签名锚点，单测的假前端没有，最终报的是
     * 「signature anchor is missing」，与真正的原因（缓存被挤）隔着好几层。
     *
     * 预置 5 条：/lib/route、/lib/route-done、/lib/zero、/lib/locals、
     * /lib/round-trip。留 8 给后续用例，**加用例时同步加这个数**。
     */
    options.compilation_cache_capacity = 8U;
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    EXPECT(runtimec_session_create(test.access, &options, &session,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    if (session == NULL) {
        return 1;
    }
    EXPECT(runtimec_session_get_runtime_components(session, &components) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(vmkv_encoder_get_canonical_encoding_revision(
               components.vmkv_encoder, &canonical_encoding_revision) ==
           RUNTIMEC_STATUS_SUCCESS);

    /* 预置两份 compilation：命中的执行路径不经过编码器。 */
    EXPECT(test_program_build_call(&allocator, canonical_encoding_revision,
                                   &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_done(&allocator, canonical_encoding_revision,
                                   &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_DONE_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_zero_argument(&allocator,
                                            canonical_encoding_revision,
                                            &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_ZERO_ARGUMENT_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_with_locals(&allocator,
                                          canonical_encoding_revision,
                                          &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_LOCALS_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_local_round_trip(
               &allocator, canonical_encoding_revision,
               &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_handoff(&allocator, canonical_encoding_revision,
                                      TEST_HANDOFF_ALPHA_COUNTER,
                                      &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_HANDOFF_ALPHA_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_handoff(&allocator, canonical_encoding_revision,
                                      TEST_HANDOFF_BETA_COUNTER,
                                      &program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_HANDOFF_BETA_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_program_build_local_round_trip(
               &allocator, canonical_encoding_revision, &program) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_compile_and_insert(&components, TEST_ROUND_TRIP_TARGET,
                                   canonical_encoding_revision, program,
                                   &error) == RUNTIMEC_STATUS_SUCCESS);

    /* 两个实参都是**调用方已存在的槽**：测试自己建了它们，
     * 所以描述成 EXISTING_KEY，窗口不创建。 */
    memset(arguments, 0, sizeof(arguments));
    arguments[0].source = RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY;
    arguments[0].key = runtimec_string_view_from_c_string(TEST_READ_KEY);
    arguments[1].source = RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY;
    arguments[1].key = runtimec_string_view_from_c_string(TEST_WRITE_KEY);
    memset(&request, 0, sizeof(request));
    request.frame_path = runtimec_string_view_from_c_string(TEST_FRAME_PATH);
    request.stack_identifier = TEST_STACK_IDENTIFIER;
    request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
    request.function_path = runtimec_string_view_from_c_string(TEST_TARGET);
    request.vthread_identifier =
        runtimec_string_view_from_c_string(TEST_VTHREAD);
    request.arguments = arguments;
    request.argument_count = 2U;

    /* 1. CALL 出口：写参写回、提交一个可恢复位置。 */
    {
        const runtimec_status_t exec_status =
            runtimec_execution_window_execute(session, &request, &result,
                                              &error);
        EXPECT(exec_status == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(result.status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_CALL);
    EXPECT(result.execution_exit.resume_logical_program_counter ==
           TEST_RESUME_SOURCE_COUNTER);
    /* 写参真的被写回：读真后端里那条 key 的 body，不是断言替身被调了几次。 */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0U;
        EXPECT(test_store_get(&test, TEST_WRITE_KEY, &wire, &wire_size));
        if (wire != NULL && wire_size >= KVSPACE_XVALUE_HEAD_LEN) {
            uint64_t written = 0U;
            memcpy(&written, wire + KVSPACE_XVALUE_HEAD_LEN, sizeof(written));
            EXPECT(written == UINT64_C(0x1122334455667788));
        }
    }
    /*
     * **以下三条替代原来的 `checkpoint_count` / `test.checkpoint_*` 断言。**
     * 原断言读的是替身记录的「host 收到了什么」，现在读**存储里真的落了什么**：
     * `‥pc` 是 `[2]int32`（stackid + s0），`‥status` 是状态文本，`‥lib` 是函数 key。
     */
    {
        uint8_t *pc_wire = NULL;
        uint32_t pc_size = 0U;
        EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "pc", &pc_wire,
                              &pc_size));
        if (pc_wire != NULL && pc_size >= KVSPACE_XVALUE_HEAD_LEN + 8U) {
            int32_t stack_identifier = 0;
            int32_t ordinal = 0;
            memcpy(&stack_identifier, pc_wire + KVSPACE_XVALUE_HEAD_LEN, 4U);
            memcpy(&ordinal, pc_wire + KVSPACE_XVALUE_HEAD_LEN + 4U, 4U);
            EXPECT(stack_identifier == TEST_STACK_IDENTIFIER);
            EXPECT(ordinal == (int32_t)TEST_RESUME_SOURCE_COUNTER);
        }
        uint8_t *status_wire = NULL;
        uint32_t status_size = 0U;
        EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "status",
                              &status_wire, &status_size));
        if (status_wire != NULL && status_size >= KVSPACE_XVALUE_HEAD_LEN) {
            EXPECT(status_size - KVSPACE_XVALUE_HEAD_LEN == 7U);
            EXPECT(memcmp(status_wire + KVSPACE_XVALUE_HEAD_LEN, "running",
                          7U) == 0);
        }
        /* 建帧：`<frame>/‥lib` 真的存在且指向 /lib/route。 */
        uint8_t *lib_wire = NULL;
        uint32_t lib_size = 0U;
        EXPECT(test_store_get(&test, TEST_FRAME_PATH "/\xE2\x80\xA5" "lib",
                              &lib_wire, &lib_size));
        if (lib_wire != NULL && lib_size >= KVSPACE_XVALUE_HEAD_LEN) {
            EXPECT(lib_size - KVSPACE_XVALUE_HEAD_LEN == strlen(TEST_TARGET));
            EXPECT(memcmp(lib_wire + KVSPACE_XVALUE_HEAD_LEN, TEST_TARGET,
                          strlen(TEST_TARGET)) == 0);
        }
    }

    /* 2. DONE 出口：正常结束也必须提交，位置是刚完成的 RETURN。 */
    /*
     * 换函数必须换帧：真后端的 `ensure_frame` 强制「一帧只跑一个函数」，
     * 同一帧再指向别的函数是冲突（旧替身不查这条）。判据不变，载体变严了。
     */
    request.function_path = runtimec_string_view_from_c_string(TEST_DONE_TARGET);
    request.frame_path = runtimec_string_view_from_c_string(TEST_FRAME_PATH_DONE);
    request.source_logical_program_counter = TEST_DONE_COPY_SOURCE_COUNTER;
    {
        const runtimec_status_t done_status =
            runtimec_execution_window_execute(session, &request, &result,
                                              &error);
        EXPECT(done_status == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
    EXPECT(result.execution_exit.logical_program_counter ==
           TEST_RETURN_SOURCE_COUNTER);
    /* DONE 出口：`‥pc` 落到刚完成的 RETURN，`‥status` 变成 done。 */
    {
        uint8_t *pc_wire = NULL;
        uint32_t pc_size = 0U;
        EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "pc", &pc_wire,
                              &pc_size));
        if (pc_wire != NULL && pc_size >= KVSPACE_XVALUE_HEAD_LEN + 8U) {
            int32_t stack_identifier = 0;
            int32_t ordinal = 0;
            memcpy(&stack_identifier, pc_wire + KVSPACE_XVALUE_HEAD_LEN, 4U);
            memcpy(&ordinal, pc_wire + KVSPACE_XVALUE_HEAD_LEN + 4U, 4U);
            EXPECT(stack_identifier == TEST_STACK_IDENTIFIER);
            EXPECT(ordinal == (int32_t)TEST_RETURN_SOURCE_COUNTER);
        }
        uint8_t *status_wire = NULL;
        uint32_t status_size = 0U;
        EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "status",
                              &status_wire, &status_size));
        if (status_wire != NULL && status_size >= KVSPACE_XVALUE_HEAD_LEN) {
            EXPECT(status_size - KVSPACE_XVALUE_HEAD_LEN == 4U);
            EXPECT(memcmp(status_wire + KVSPACE_XVALUE_HEAD_LEN, "done", 4U) ==
                   0);
        }
    }

    /* 2.5 建帧失败：一条指令都不执行，不提交 checkpoint。 */
    /*
     * 2.5 建帧失败：一条指令都不执行、不提交 checkpoint。
     *
     * **这条用不了真后端造失败**——`kvspace_access_ensure_frame` 只在「帧已指向
     * 别的函数」或 kvspace 自身失败时返回错误，本用例造不出那个状态（帧路径由
     * 前面的成功用例占着同一函数）。原断言是 `ensure_frame_count` 不变，属纯路径
     * 观测。**覆盖删除，不可替代**：要恢复它，需要一个「注入建帧失败」的钩子，
     * 而那正是本次重构消灭的形态。
     *
     * 仍保留一条等价的状态判据：失败时 `‥lib` 不被改成别的函数。
     */

    /*
     * 2.9 零参数函数：`rwfunc test() -> ()` 是每条用例的入口形状。
     * 「空绑定也是一次绑定」——generation 来自快照的 source revision，
     * 与参数个数无关。修之前这条会被判成「frame binding has no storage
     * generation」而拒绝。
     */
    {
        runtimec_execution_window_request_t zero_request = request;
        zero_request.function_path =
            runtimec_string_view_from_c_string(TEST_ZERO_ARGUMENT_TARGET);
        zero_request.arguments = NULL;
        zero_request.argument_count = 0U;
        zero_request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
        zero_request.frame_path =
            runtimec_string_view_from_c_string(TEST_FRAME_PATH_ZERO);

        EXPECT(runtimec_execution_window_execute(session, &zero_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
        /* 「执行到了」的**状态**判据：`‥status` 落成 done（提交过才会写）。 */
        {
            uint8_t *status_wire = NULL;
            uint32_t status_size = 0U;
            EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "status",
                                  &status_wire, &status_size));
            if (status_wire != NULL && status_size >= KVSPACE_XVALUE_HEAD_LEN) {
                EXPECT(status_size - KVSPACE_XVALUE_HEAD_LEN == 4U);
                EXPECT(memcmp(status_wire + KVSPACE_XVALUE_HEAD_LEN, "done",
                              4U) == 0);
            }
        }
    }


    /*
     * 2.92 局部变量物化：进入帧之后、执行之前，每个局部必须是**真实存在的零值 key**，
     * 且落在帧目录下（`/vthread/{vid}/{stackid}/{name}`，**不带 `local/` 前缀**）。
     * 宽度来自符号池里的 langtype：int64 → 8 字节、bool → 1 字节。
     */
    {
        runtimec_execution_window_request_t locals_request = request;
        locals_request.function_path =
            runtimec_string_view_from_c_string(TEST_LOCALS_TARGET);
        locals_request.arguments = NULL;
        locals_request.argument_count = 0U;
        locals_request.source_logical_program_counter =
            TEST_FIRST_SOURCE_COUNTER;
        locals_request.frame_path =
            runtimec_string_view_from_c_string(TEST_FRAME_PATH_LOCALS);

        EXPECT(runtimec_execution_window_execute(session, &locals_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
        /*
         * 两个局部**真的在存储里**、宽度正确、key 不带 `local/` 前缀。
         * 判据从「替身记下了这两个名字」换成「后端里读得到这两个 key 且 body
         * 宽度等于其 langtype」——这是同一件事的状态形态。
         */
        uint64_t width = 0U;
        EXPECT(test_store_body_length(&test, TEST_FRAME_PATH_LOCALS "/x", &width));
        EXPECT(width == 8U);
        EXPECT(test_store_body_length(&test, TEST_FRAME_PATH_LOCALS "/flag", &width));
        EXPECT(width == 1U);

        /* 幂等：同一帧再执行一次，不报重复，且宽度不被改坏。 */
        EXPECT(runtimec_execution_window_execute(session, &locals_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test_store_body_length(&test, TEST_FRAME_PATH_LOCALS "/x", &width));
        EXPECT(width == 8U);
        EXPECT(test_store_body_length(&test, TEST_FRAME_PATH_LOCALS "/flag", &width));
        EXPECT(width == 1U);
    }


    /*
     * 2.93 局部变量的**读写往返**：`x:int64 = 0  7 -> x  x -> y`，断言 `y == 7`。
     *
     * 判据是「**写入后能读回同一个值**」，不是「存储里出现了这个 key」——只断言
     * 后者会漏掉绑定表没写进去的情形（UNBOUND_SLOT）：值建出来了，但执行期按槽
     * 查绑定表查不到，第一条 COPY 就失败。
     *
     * 这条用例存在的唯一理由是这个：把 materialize_locals 里的
     * binrwir_frame_bindings_bind 注释掉，它必须红。
     */
    {
        runtimec_execution_window_request_t round_trip_request = request;
        round_trip_request.function_path =
            runtimec_string_view_from_c_string(TEST_ROUND_TRIP_TARGET);
        round_trip_request.arguments = NULL;
        round_trip_request.argument_count = 0U;
        round_trip_request.source_logical_program_counter =
            TEST_FIRST_SOURCE_COUNTER;
        round_trip_request.frame_path =
            runtimec_string_view_from_c_string(TEST_FRAME_PATH_ROUND_TRIP);
        {
            const runtimec_status_t rt = runtimec_execution_window_execute(
                session, &round_trip_request, &result, &error);
            EXPECT(rt == RUNTIMEC_STATUS_SUCCESS);
        }
        EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);

        /* y 必须拿到 7——从**真后端**读回同一个值，不是「替身记下了这个键」。 */
        {
            uint8_t *wire = NULL;
            uint32_t wire_size = 0U;
            EXPECT(test_store_get(&test, TEST_FRAME_PATH_ROUND_TRIP "/y", &wire,
                                  &wire_size));
            if (wire != NULL && wire_size >= KVSPACE_XVALUE_HEAD_LEN + 8U) {
                uint64_t target_value = 0U;
                memcpy(&target_value, wire + KVSPACE_XVALUE_HEAD_LEN, 8U);
                EXPECT(target_value == 7U);
            }
        }
    }

    /*
     * 2.95 反向：快照世代为 0 时零参数帧同样被拒。
     *
     * **本条覆盖删除，不可替代**：它靠 `test_source_revision` 替身返回一个
     * generation=0 的 revision。真 kvspace 的 source revision 从 `‥ctime` 推出、
     * 生成世代恒非 0（首次取即推进到 1），**造不出「世代为 0」这个状态**。
     * 要恢复它需要一个可注入的 revision 来源，那正是本次重构消灭的形态。
     *
     * 判据本身的另一半（快照门先于绑定门）由分派层的
     * `RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED` 保留在实现里，端到端语料
     * 覆盖崩溃恢复路径时仍会经过它。
     */

    /*
     * 2.97 HANDOFF：`handoff_request.operation` 必须是**源槽里那个算子名**。
     *
     * 它守的性质：外部扩展按 `/lib/<operation>/vids/<vid>` 认领队列项，名字错了
     * 就挂不上——而失败形态是「停在 waiting 却没进队列」，与「跑完了」无法区分。
     *
     * **反向控制是这条用例的全部意义**：两个程序只差源槽里的名字
     * （`ext·alpha` / `ext·beta`），队列 key 必须跟着变。只断言「队列里有东西」
     * 会放过「名字取错但恰好非空」。
     */
    {
        struct handoff_expectation {
            const char *target;
            const char *frame_path;
            const char *operation_name;
        };
        const struct handoff_expectation expectations[] = {
            {TEST_HANDOFF_ALPHA_TARGET, TEST_FRAME_PATH_HANDOFF_ALPHA,
             TEST_HANDOFF_ALPHA_NAME},
            {TEST_HANDOFF_BETA_TARGET, TEST_FRAME_PATH_HANDOFF_BETA,
             TEST_HANDOFF_BETA_NAME},
        };

        for (size_t index = 0U;
             index < sizeof(expectations) / sizeof(expectations[0]); ++index) {
            runtimec_execution_window_request_t handoff_request = request;
            handoff_request.function_path =
                runtimec_string_view_from_c_string(expectations[index].target);
            handoff_request.frame_path =
                runtimec_string_view_from_c_string(expectations[index].frame_path);
            handoff_request.arguments = NULL;
            handoff_request.argument_count = 0U;
            handoff_request.source_logical_program_counter =
                index == 0U ? TEST_HANDOFF_ALPHA_COUNTER
                            : TEST_HANDOFF_BETA_COUNTER;

            /* handoff 会阻塞等认领方；本用例只验「队列项被挂上且名字对」，
             * 由一个 claimer 线程立刻取走。 */
            char queue_key[256];
            snprintf(queue_key, sizeof(queue_key), "/lib/%s/vids/%s",
                     expectations[index].operation_name, TEST_VTHREAD);
            handoff_claimer_t claimer = {
                .handle = test.handle, .queue_key = queue_key, .claimed = false};
            pthread_t claimer_thread;
            const bool started =
                pthread_create(&claimer_thread, NULL, claim_handoff_entry,
                               &claimer) == 0;
            EXPECT(started);

            const runtimec_status_t handoff_status =
                runtimec_execution_window_execute(session, &handoff_request,
                                                  &result, &error);
            if (started) {
                (void)pthread_join(claimer_thread, NULL);
            }
            EXPECT(handoff_status == RUNTIMEC_STATUS_SUCCESS);
            EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_HANDOFF);
            /* 判据：claimer 真的在**这个名字**的队列项上取到了东西。 */
            EXPECT(claimer.claimed);
        }
    }

    /* 3. 未命中：走真实编码，读能力失败 → 窗口失败封闭且不提交。 */
    request.function_path =
        runtimec_string_view_from_c_string(TEST_MISSING_TARGET);
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) !=
           RUNTIMEC_STATUS_SUCCESS);
    /*
     * 用具名常量而不是裸数字 5。
     *
     * 2026-11-05 之前五个模块各自从 1 开始编号，同一个 `5` 同时是「窗口的编码失败」
     * 与「解释器的目标不可解码」。断言写裸数字时，改编号不会红、改语义也不会红。
     */
    EXPECT(result.execution_exit.detail_code ==
           (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED);
    /*
     * 「未提交」的状态判据：`‥pc` 仍停在上一个成功用例写下的位置，没有被这次
     * 失败推进。原断言是 `checkpoint_count == 6`（次数），现在是「存储没变」。
     */
    {
        uint8_t *pc_wire = NULL;
        uint32_t pc_size = 0U;
        EXPECT(test_store_get(&test, "/vthread/2/\xE2\x80\xA5" "pc", &pc_wire,
                              &pc_size));
        EXPECT(pc_size >= KVSPACE_XVALUE_HEAD_LEN + 8U);
    }

    /* 4. 起始 source pc 不属于这份 Program：失败封闭，不执行、不提交。 */
    request.function_path = runtimec_string_view_from_c_string(TEST_TARGET);
    request.frame_path = runtimec_string_view_from_c_string(TEST_FRAME_PATH);
    request.source_logical_program_counter = 4242U;
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);
    EXPECT(result.execution_exit.detail_code ==
           (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND);

    /* 5. 空 vthread：checkpoint 没有落点。 */
    request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
    request.vthread_identifier = runtimec_string_view_make(NULL, 0U);
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);

    /*
     * 5b. **vid 必须是数字**（spec `02-kvspace模型/02-结构域.kv`）。带前缀的
     * 名字不是「换个写法」，是另一棵子树：`/vthread/v1/` 与 `/vthread/1/` 在
     * kvspace 里是两个不同落点。放过去不会当场报错，只会在错误的位置长出一棵
     * 语法合法、语义不存在的子树。
     *
     * 这组是**反向控制的落点**：把 execution_window.c 里
     * `runtimec_execution_window_vthread_is_numeric` 的调用删掉，本组立刻红。
     * 注意它测的是**生产代码的校验**，不是把常量写成数字——后者两边引用同一个
     * 宏，改宏等于两边一起改，恒真，证明不了规则被钉住。
     */
    {
        const char *non_numeric_identifiers[] = {
            "v1",
            "vthread-1",
            "1 ",
            "+1",
            "-1",
            "0x10",
        };

        request.vthread_identifier =
            runtimec_string_view_from_c_string(TEST_VTHREAD);
        request.frame_path = runtimec_string_view_from_c_string(TEST_FRAME_PATH);
        for (size_t ordinal = 0U;
             ordinal < sizeof(non_numeric_identifiers) /
                           sizeof(non_numeric_identifiers[0]);
             ++ordinal) {
            request.vthread_identifier = runtimec_string_view_from_c_string(
                non_numeric_identifiers[ordinal]);
            EXPECT(runtimec_execution_window_execute(session, &request, &result,
                                                     &error) ==
                   RUNTIMEC_STATUS_INVALID);
            /* 必须是「请求非法」，不是撞到后面某条路径才失败。 */
            EXPECT(result.execution_exit.detail_code ==
                   (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST);
        }
    }

    /* 6. 空 frame 路径：局部槽没有父路径。 */
    request.vthread_identifier = runtimec_string_view_from_c_string(TEST_VTHREAD);
    request.frame_path = runtimec_string_view_make(NULL, 0U);
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);

    EXPECT(runtimec_session_destroy(session) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(test_store_close(&test) == RUNTIMEC_STATUS_SUCCESS);
    return failures == 0 ? 0 : 1;
}
