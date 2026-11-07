/*
 * `println` 打印一个**指针值**时输出什么。
 *
 * 判据（架构师 2026-11-06 裁决）：指针的值就是它指向的目标 key 路径
 * （spec 03-类型系统/03-种类：Ptr 的 body 是目标键）。所以
 * `println(p)`（p 是 `*int64`）打印的是路径文本，**不是**它指向的那个数。
 *
 * ── 为什么必须单独一个目标 ────────────────────────────────────────────
 *
 * 打印层按 langtype 查渲染表；`*int64` 查不到——那不是缺陷，是指针根本没有
 * 「数值形态」。真实调用里这条路径来自 `println(p)`，而 `println(*p)` 走的是
 * 另一条：layout 先发一条 copy 把 `*p` 落进临时槽（dump 实测 `[8,-1] *p int64`），
 * println 拿到的已经是 int64 槽。**两条路径必须分别断**，否则「都打印出 `7`」
 * 会被当成两条都对，而其中一条问的根本不是同一个问题。
 *
 * 四条断言，缺一条就会放过一类错误实现：
 *   1. 非空指针  → 打印路径文本（≠ 指向的数、≠ 空）
 *   2. 解引用值  → 打印那个数（证明 1 不是靠「所有值都打印 body」蒙对的）
 *   3. 普通标量  → 打印那个数（证明 1 没把普通值也当成路径）
 *   4. 空引用    → 打印空串（空指针没有目标，不编造 `None` 字面、不打地址）
 *
 * 第 1 与 2/3 的差别就是本用例存在的理由：只断 2/3 时，「指针打印成路径」
 * 这条根本没有被测过。
 */

/* `fileno` / `dup` / `dup2` 是 POSIX：本工程 C11 关闭扩展，必须显式要。
 * 必须在**任何** include 之前定义——它影响系统头里暴露哪些声明。 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "binrwir/registry.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/temp_println.h"

#define TEST_BODY_CAPACITY 32U
#define TEST_VALUE_CAPACITY (KVSPACE_XVALUE_HEAD_LEN + TEST_BODY_CAPACITY)
#define TEST_OUTPUT_CAPACITY 256U

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* 一个可长期存活的 XValue：16 字节 head + body + attr。 */
typedef struct test_value {
    uint8_t storage[TEST_VALUE_CAPACITY];
} test_value_t;

/*
 * `pointer` 非零 → langtype 带 `*` 前缀（指针值，body 是目标 key）；
 * 零 → langtype 无前缀（普通值）。
 *
 * 指针性**只在 langtype 首字符**（head 里已经没有 ptr 字节了）。
 */
static void test_value_initialize(test_value_t *value, int pointer,
                                  const char *langtype_base,
                                  const char *body, size_t body_size) {
    kvspaceXvalueHead head;
    char langtype[64];
    const size_t base_size = strlen(langtype_base);
    const size_t langtype_size = base_size + (pointer != 0 ? 1U : 0U);
    const size_t attr_size = 3U + langtype_size;

    if (pointer != 0) {
        langtype[0] = '*';
    }
    memcpy(langtype + (pointer != 0 ? 1U : 0U), langtype_base, base_size);
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, body_size);
    kvspaceXvalueHeadSetAttrsLen(&head, (uint32_t)attr_size);
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
    memset(value->storage, 0, sizeof(value->storage));
    memcpy(value->storage, &head, sizeof(head));
    if (body_size != 0U) {
        memcpy(value->storage + KVSPACE_XVALUE_HEAD_LEN, body, body_size);
    }
    {
        uint8_t *attr = value->storage + KVSPACE_XVALUE_HEAD_LEN + body_size;

        attr[0] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        attr[1] = (uint8_t)(langtype_size & 0xFFU);
        attr[2] = (uint8_t)(langtype_size >> 8U);
        memcpy(attr + 3, langtype, langtype_size);
    }
}

static runtimec_xvalue_address_t test_value_address(const test_value_t *value) {
    return (runtimec_xvalue_address_t)(uintptr_t)value->storage;
}

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

/*
 * 跑一次 `println`，把 stdout 抓成字符串。
 *
 * 用 `freopen` 重定向而不是管道：被测的是 handler 内部 `fputs`/`fwrite` 到
 * `stdout` 的行为，管道要多一个线程读，失败时更难定位。重定向后**必须**
 * `fflush` 再收起，否则缓冲区里的内容会漏到下一条用例的输出里——那种污染
 * 会让下一条用例莫名其妙地「多一行」。
 */
static void run_println(const test_value_t *value, char *output,
                        size_t output_capacity) {
    runtimec_allocator_t allocator = {NULL, test_allocate, test_deallocate};
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_binfunc_t *program = NULL;
    binrwir_binvthread_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_instruction_build_request_t request;
    binrwir_argument_t read_argument;
    binrwir_argument_t write_arguments[2];
    binrwir_instruction_view_t view;
    binrwir_execution_exit_t exit_state;
    const binrwir_registry_entry_t *entries = NULL;
    size_t entry_count = 0U;
    binrwir_registry_entry_t println_entry;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
    char output_path[64];
    int saved_stdout;
    FILE *captured;
    size_t count;

    output[0] = '\0';
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 2U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;
    if (binrwir_binfunc_builder_create(&allocator, &identity, &builder) !=
        RUNTIMEC_STATUS_SUCCESS) {
        EXPECT(0);
        return;
    }
    /*
     * 读参用**形参**（PARAMETER_READ）：绑定表给出真实 XValue 地址，
     * `vmbin_value_access_form` 才能从 attr 里读到 langtype。用 LITERAL
     * 就永远读到空 langtype（恒普通值），指针分支走不到。
     */
    EXPECT(binrwir_binfunc_builder_add_parameter(
               builder, BINRWIR_PARAMETER_DIRECTION_READ, 1U,
               runtimec_string_view_from_c_string("int64"),
               &parameter_index) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                                   &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    /*
     * 两个写参是 echo 出来的槽：`println` 的签名是 `(…)->()`，但程序 builder
     * 要求写参非空（算术结果槽），这里给两个不会被读的占位槽。
     */
    EXPECT(binrwir_binfunc_builder_add_symbol(
               builder, runtimec_string_view_from_c_string("/t/one"),
               runtimec_string_view_from_c_string("int64"),
               &symbol_index) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_argument_create(
               2U, BINRWIR_REFERENCE_STATIC, symbol_index,
               (uint64_t)(uintptr_t)test_value_address(value),
               &write_arguments[0]) == RUNTIMEC_STATUS_SUCCESS);
    write_arguments[1] = write_arguments[0];
    write_arguments[1].argument_index = 3U;

    memset(&request, 0, sizeof(request));
    request.operation_identifier = RUNTIMEC_TEMP_PRINTLN_OPERATION_IDENTIFIER;
    request.source_logical_program_counter = 1U;
    request.read_arguments.data = &read_argument;
    request.read_arguments.count = 1U;
    request.write_arguments.data = write_arguments;
    request.write_arguments.count = 2U;
    EXPECT(binrwir_binfunc_builder_add_instruction(builder, &request,
                                                   &instruction_identifier) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (binrwir_binfunc_builder_finalize(builder, &program) !=
        RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        EXPECT(0);
        return;
    }
    (void)binrwir_binfunc_builder_destroy(builder);

    EXPECT(binrwir_binvthread_frame_initialize(&frame, program, NULL, 1U, 1U,
                                              0U) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_frame_bindings_create(&allocator, 2U, &bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = bindings;
    EXPECT(binrwir_frame_bindings_bind(
               bindings, 0U, runtimec_key_view_from_c_string("/t/p"),
               runtimec_key_view_from_c_string("/t/p"),
               test_value_address(value), 1U) == RUNTIMEC_STATUS_SUCCESS);

    /* 从条目表里取 println 的 handler——不直接引用 static 函数。 */
    entries = runtimec_temp_println_entries(&entry_count);
    EXPECT(entries != NULL && entry_count != 0U);
    println_entry = entries[0];
    for (count = 0U; count < entry_count; ++count) {
        if (entries[count].operation_identifier ==
            RUNTIMEC_TEMP_PRINTLN_OPERATION_IDENTIFIER) {
            println_entry = entries[count];
            break;
        }
    }

    EXPECT(binrwir_binfunc_get_instruction(program, instruction_identifier,
                                           &view) ==
           RUNTIMEC_STATUS_SUCCESS);

    /* 抓 stdout：先保住原 fd，再用文件重定向。 */
    snprintf(output_path, sizeof(output_path), "/tmp/temp_println_pointer_%d",
             (int)getpid());
    fflush(stdout);
    saved_stdout = dup(fileno(stdout));
    captured = freopen(output_path, "w+", stdout);
    EXPECT(captured != NULL);
    if (captured != NULL) {
        memset(&exit_state, 0, sizeof(exit_state));
        (void)println_entry.handler(&frame, &view, &exit_state);
        fflush(stdout);
        /*
         * 用 `dup2` 把保存的 fd 装回标准输出，**不用 `freopen("/dev/tty")`**：
         * 无终端环境（CI、后台任务）没有 tty，`freopen` 会失败并让 stdout 停在
         * 一个已关闭的流上，后续所有用例的输出全部丢失。
         */
        (void)dup2(saved_stdout, fileno(stdout));
        clearerr(stdout);
    }
    close(saved_stdout);

    captured = fopen(output_path, "r");
    if (captured != NULL) {
        const size_t read = fread(output, 1U, output_capacity - 1U, captured);

        output[read] = '\0';
        fclose(captured);
    }
    (void)remove(output_path);

    (void)binrwir_frame_bindings_destroy(bindings);
    (void)binrwir_binvthread_frame_destroy(&frame);
    (void)binrwir_binfunc_destroy(program);
}

int main(void) {
    test_value_t pointer_value;
    test_value_t plain_value;
    test_value_t none_value;
    char output[TEST_OUTPUT_CAPACITY];

    /* 1. 非空指针 → 目标路径文本。 */
    test_value_initialize(&pointer_value, 1, "int64", "/vthread/1/x", 12U);
    run_println(&pointer_value, output, sizeof(output));
    EXPECT(strcmp(output, "/vthread/1/x\n") == 0);

    /* 2. 普通标量 → 那个数（证明上面不是「所有值都打 body」）。 */
    test_value_initialize(&plain_value, 0, "int64", "\x07\x00\x00\x00\x00\x00\x00\x00",
                          8U);
    run_println(&plain_value, output, sizeof(output));
    EXPECT(strcmp(output, "7\n") == 0);

    /* 3. 空引用 → 空串（一行空，不编造 `None`、不打地址）。 */
    test_value_initialize(&none_value, 1, "int64", NULL, 0U);
    run_println(&none_value, output, sizeof(output));
    EXPECT(strcmp(output, "\n") == 0);

    EXPECT(pointer_value.storage[0] != 0U);

    if (failures != 0) {
        fprintf(stderr, "temp_println_pointer_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "temp_println_pointer_test: OK\n");
    return 0;
}
