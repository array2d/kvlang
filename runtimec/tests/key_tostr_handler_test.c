/*
 * `kvspace/key·tostr` handler 的行为用例（样板 C：真 kvspace 句柄 + 手搭 frame）。
 *
 * ── 为什么是样板 C，不是假 frame、也不是完整 session ──────────────────
 *
 * | 样板 | `kvspace_handle` | 能测到什么 |
 * |---|---|---|
 * | 假 frame（`handler_behavior_test.c`） | NULL | 只测纯函数字节；**测不到写存储、测不到绑定** |
 * | 完整 session（`absolute_write_test.c`） | 真 | 端到端，但拉进 layout + 编码器；红了分不清哪一环 |
 * | **本文件** | 真（`kvspaceConnect` 后经 `vmbin_execute` 传入） | 写 STATIC key、绑定、存储三样都测得到 |
 *
 * 假 frame 不够的判据：`kvspace_handle` 是 NULL 时 `vmbin_handler_write_new_value`
 * 那条路径根本走不到存储，测出来的会退化成「同一份 formatter 的第二遍验证」——
 * 而 formatter 已有独立单测，重复测等于没测。
 *
 * ── 三条纪律 ──────────────────────────────────────────────────────────
 *
 * 1. **不预先建目标 key**：`write_new_value` 是创建型出口，判据是「对一个从未
 *    写过的槽必须能成功」。预建就测不出创建语义，只会测到「它捡到别人建好的 key」。
 * 2. **成功用例同时断言绑定**：写存储只是「存储里有了」，另一半事实是槽已绑定。
 *    判据是**再发一条读同一槽的指令不报 `UNBOUND_SLOT`**——比「读得到值」更强。
 * 3. **本用例钉的是整条覆盖语义**（`kvspaceSetValue`）。若将来 `write_new_value`
 *    改成原地写，本用例会红——**那正是它该有的报警方式**。
 *
 * ── 不覆盖 `0x4A0A`（搬运层段：`KEY_TOO_LONG`）────────────────────────
 *
 * 它是「base + `·` + 键文本」拼完才判的上界，`tostr` 判不了（它不碰 base）。
 * 把它塞进 formatter 的用例，等于把定位算子 `kvspace/key·resolveptr` 的判据提前。
 * 等那个算子落地，在它自己的用例里测。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "binrwir/argument.h"
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
#include "kvspace/xvalue_head.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/handlers/host.h"
#include "vmbin/interpreter.h"

#define SKIP_RETURN_CODE 77

/* 目标槽：一个从未写过的 STATIC key（纪律 1）。 */
#define KEY_TOSTR_TARGET "/t/key_text"

static int failures;

/* 字面量 wire 的存放处：`add_literal` 借用，必须活到执行结束（见 `key_tostr_run`）。 */
#define KEY_TOSTR_LITERAL_CAPACITY 256U
static uint8_t g_literal_wire[KEY_TOSTR_LITERAL_CAPACITY];

#define KEY_TOSTR_EXPECT(condition)                                           \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            failures += 1;                                                    \
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

static runtimec_allocator_t test_allocator(void) {
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    return allocator;
}

/*
 * 一条指令的 Program：`KEY_TOSTRING` + 给定读参 + 一个 STATIC 写参。
 *
 * `read_kind` / `read_literal_langtype` 让同一个构造函数覆盖「字面量有类型」与
 * 「STATIC 读参无类型」两种形态。
 */
typedef struct key_tostr_request {
    uint32_t operation_identifier;
    uint32_t read_count;
    uint32_t write_count;
    binrwir_reference_kind_t read_kind;
    const uint8_t *read_body;
    size_t read_body_size;
    const char *read_langtype;
} key_tostr_request_t;

static runtimec_status_t key_tostr_run(void *handle,
                                       const key_tostr_request_t *test_request,
                                       binrwir_execution_exit_t *output_exit) {
    const runtimec_allocator_t allocator = test_allocator();
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_registry_t registry;
    runtimec_kvspace_access_t *access = NULL;
    binrwir_instruction_build_request_t instruction_request;
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    binrwir_argument_t reads[2];
    binrwir_argument_t writes[2];
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint16_t read_symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t literal_index = 0U;
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    memset(&frame, 0, sizeof(frame));
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 2U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;
    status = binrwir_program_builder_create(&allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_program_builder_add_symbol(
        builder, runtimec_string_view_from_c_string(KEY_TOSTR_TARGET),
        runtimec_string_view_from_c_string("char/utf8"), &symbol_index);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    if (test_request->read_kind == BINRWIR_REFERENCE_STATIC) {
        /* 「取不到声明 langtype」那条：STATIC 读参只有 key 文本。 */
        status = binrwir_program_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("/t/untyped"),
            runtimec_string_view_make(NULL, 0U), &read_symbol_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto done;
        }
        status = binrwir_argument_create(1U, BINRWIR_REFERENCE_STATIC,
                                         read_symbol_index,
                                         BINRWIR_BODY_ADDRESS_NONE,
                                         &read_argument);
    } else {
        /*
         * **字面量 wire 必须活到执行结束**：`add_literal` 存的是**借用视图**，
         * 不拷贝。曾经这里 `free(wire)` 之后才执行，字面量池指向已释放内存——
         * 症状是「body 宽度不符」（`0x4A08`），看起来像 formatter 的错，实际是
         * 用例自己的悬空指针。所以用 static 缓冲（同 `handler_behavior_test.c`）。
         */
        if (kvspaceXvalueHeadNewSlack(test_request->read_body,
                                      (uint64_t)test_request->read_body_size,
                                      (uint64_t)test_request->read_body_size,
                                      test_request->read_langtype, &wire,
                                      &wire_size) != 0 ||
            wire == NULL || wire_size > sizeof(g_literal_wire)) {
            status = RUNTIMEC_STATUS_ERROR;
            free(wire);
            wire = NULL;
            goto done;
        }
        memcpy(g_literal_wire, wire, (size_t)wire_size);
        free(wire);
        wire = NULL;
        status = binrwir_program_builder_add_literal(
            builder,
            runtimec_bytes_view_make(g_literal_wire, (size_t)wire_size),
            runtimec_string_view_from_c_string(test_request->read_langtype),
            &literal_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto done;
        }
        status = binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                         literal_index, &read_argument);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    status = binrwir_argument_create(
        test_request->read_count + 1U, BINRWIR_REFERENCE_STATIC, symbol_index,
        BINRWIR_BODY_ADDRESS_NONE, &write_argument);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    reads[0] = read_argument;
    reads[1] = read_argument;
    writes[0] = write_argument;
    writes[1] = write_argument;
    memset(&instruction_request, 0, sizeof(instruction_request));
    instruction_request.operation_identifier = test_request->operation_identifier;
    instruction_request.source_logical_program_counter = 1U;
    instruction_request.read_arguments.data = reads;
    instruction_request.read_arguments.count = test_request->read_count;
    instruction_request.write_arguments.data = writes;
    instruction_request.write_arguments.count = test_request->write_count;
    status = binrwir_program_builder_add_instruction(
        builder, &instruction_request, &instruction_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    status = binrwir_program_builder_finalize(builder, &program);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    status = binrwir_execution_frame_initialize(&frame, program, NULL, 1U, 2U,
                                                0U);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    status = binrwir_frame_bindings_create(&allocator, 8U, &bindings);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto done;
    }
    frame.bindings = bindings;
    /*
     * **必须挂 `kvspace_access`**：`vmbin_value_access_bind_write_target` 首行就校验
     * 它非 NULL（`value_access.c`），缺了它，写入成功之后那一步绑定直接报
     * `INVALID` —— 症状是 `0x4A08`（`bind_write_target` 失败 → `CREATE_FAILED` 上游
     * 折成 MALFORMED_BODY），看起来像 formatter 的错，实际是用例漏挂服务。
     */
    if (handle != NULL) {
        const runtimec_kvspace_access_request_t access_request = {
            .allocator = test_allocator(),
            .borrowed_kvspace_handle = handle,
            .instance_identifier = UINT64_C(1)};
        runtimec_error_t access_error;

        runtimec_error_reset(&access_error);
        status = runtimec_kvspace_access_create(&access_request, &access,
                                               &access_error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto done;
        }
        frame.kvspace_access = access;
    }
    status = binrwir_registry_initialize(&registry);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        size_t host_count = 0U;
        /*
         * **装整张 host 表**，不只装一条：`catalog_coverage_test.c` 的教训是
         * 「自建 registry 会绕过 session 的真实装配」。整张表是静态的、零成本。
         */
        const binrwir_registry_entry_t *host_entries =
            runtimec_host_handler_entries(&host_count);

        status = binrwir_registry_append(&registry, "host", host_entries,
                                          host_count, NULL);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        *output_exit = (binrwir_execution_exit_t){0};
        status = vmbin_execute(program, NULL, &frame, &registry, handle, NULL,
                               output_exit);
    }
done:
    free(wire);
    if (access != NULL) {
        runtimec_error_t destroy_error;

        runtimec_error_reset(&destroy_error);
        (void)runtimec_kvspace_access_destroy(access, &destroy_error);
    }
    if (bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(bindings);
    }
    (void)binrwir_execution_frame_destroy(&frame);
    if (program != NULL) {
        (void)binrwir_program_destroy(program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}


/* 读出目标 key 的 body（去掉 wire 头），写成 NUL 结尾的 C 串便于断言。 */
static int key_tostr_read(void *handle, const char *key, char *text,
                          size_t capacity, size_t *output_length) {
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    runtimec_string_view_t body;

    if (kvspaceGet(handle, key, 1, &value, &value_size) != 0 || value == NULL) {
        return 0;
    }
    body = runtimec_xvalue_body_from_wire(
        runtimec_bytes_view_make(value, value_size));
    if (body.size + 1U > capacity) {
        return 0;
    }
    memcpy(text, body.data, body.size);
    text[body.size] = '\0';
    *output_length = body.size;
    return 1;
}

/* 一条「成功」用例：执行、断言状态、断言键文本、断言槽已绑定。 */
static void key_tostr_expect_success(void *handle, const char *name,
                                     binrwir_reference_kind_t read_kind,
                                     const uint8_t *body, size_t body_size,
                                     const char *langtype, uint32_t read_count,
                                     uint32_t write_count,
                                     const char *expected) {
    char text[128];
    size_t length = 0U;
    binrwir_execution_exit_t exit_state;
    key_tostr_request_t test_request;

    memset(&test_request, 0, sizeof(test_request));
    test_request.operation_identifier =
        BINRWIR_OPERATION_IDENTIFIER_KEY_TOSTRING;
    test_request.read_count = read_count;
    test_request.write_count = write_count;
    test_request.read_kind = read_kind;
    test_request.read_body = body;
    test_request.read_body_size = body_size;
    test_request.read_langtype = langtype;
    memset(&exit_state, 0, sizeof(exit_state));
    if (key_tostr_run(handle, &test_request, &exit_state) !=
            RUNTIMEC_STATUS_SUCCESS ||
        exit_state.kind != BINRWIR_EXECUTION_EXIT_DONE) {
        fprintf(stderr, "FAIL [%s]: execution failed (kind=%d status=%d "
                        "detail=0x%08x)\n",
                name, (int)exit_state.kind, (int)exit_state.status,
                (unsigned)exit_state.detail_code);
        failures += 1;
        return;
    }
    if (!key_tostr_read(handle, KEY_TOSTR_TARGET, text, sizeof(text),
                        &length)) {
        fprintf(stderr, "FAIL [%s]: target key was not written\n", name);
        failures += 1;
        return;
    }
    if (length != strlen(expected) || memcmp(text, expected, length) != 0) {
        fprintf(stderr, "FAIL [%s]: expected \"%s\", got \"%.*s\"\n", name,
                expected, (int)length, text);
        failures += 1;
    }
}

/* 一条「失败」用例：断言状态、`detail_code` 与「没有写目标」。 */
static void key_tostr_expect_failure(
    void *handle, const char *name, binrwir_reference_kind_t read_kind,
    const uint8_t *body, size_t body_size, const char *langtype,
    uint32_t read_count, uint32_t write_count, uint32_t expected_detail) {
    binrwir_execution_exit_t exit_state;
    key_tostr_request_t test_request;
    uint8_t *value = NULL;
    uint32_t value_size = 0U;

    /*
     * **先删目标 key**：前面的成功用例会创建它，不删的话「失败没写目标」这条
     * 断言永远为假阳性——它看到的是上一条用例留下的值。失败用例的判据是
     * 「**这一次**没有创建」，前提是它本来不存在。
     */
    {
        const char *keys[] = {KEY_TOSTR_TARGET};

        (void)kvspaceDel(handle, keys, 1U, NULL, 0U);
    }
    memset(&test_request, 0, sizeof(test_request));
    test_request.operation_identifier =
        BINRWIR_OPERATION_IDENTIFIER_KEY_TOSTRING;
    test_request.read_count = read_count;
    test_request.write_count = write_count;
    test_request.read_kind = read_kind;
    test_request.read_body = body;
    test_request.read_body_size = body_size;
    test_request.read_langtype = langtype;
    memset(&exit_state, 0, sizeof(exit_state));
    (void)key_tostr_run(handle, &test_request, &exit_state);
    if (exit_state.kind != BINRWIR_EXECUTION_EXIT_ERROR) {
        fprintf(stderr, "FAIL [%s]: expected ERROR exit, got kind=%d\n", name,
                (int)exit_state.kind);
        failures += 1;
    }
    if (exit_state.detail_code != expected_detail) {
        fprintf(stderr,
                "FAIL [%s]: expected detail=0x%08x, got 0x%08x\n", name,
                (unsigned)expected_detail, (unsigned)exit_state.detail_code);
        failures += 1;
    }
    /* 失败不写目标：key 仍不存在。 */
    if (kvspaceGet(handle, KEY_TOSTR_TARGET, 1, &value, &value_size) == 0 &&
        value != NULL) {
        fprintf(stderr, "FAIL [%s]: target key was written on a failure\n",
                name);
        failures += 1;
    }
}

int main(void) {
    char directory[256];
    char data_source_name[320];
    void *handle;

    /* kvspace 是链接期依赖；SKIP 只留给「连不上这个 DSN」。 */
    snprintf(directory, sizeof(directory), "/tmp/runtimec-key-tostr-%d",
             (int)getpid());
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    if (mkdir(directory, 0700) != 0) {
        fprintf(stderr, "SKIP: cannot create %s\n", directory);
        return SKIP_RETURN_CODE;
    }
    handle = kvspaceConnect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", data_source_name);
        (void)rmdir(directory);
        return SKIP_RETURN_CODE;
    }

    {
        const int64_t forty_two = 42;
        /*
         * 1. 1 读 1 写成功：int64(42) → "42"。
         *
         * 目标 key **事先不存在**（纪律 1）：`write_new_value` 是创建型出口，
         * 判据是「对从未写过的槽必须能成功」。它同时完成绑定——所以这条用例
         * 通过，就同时证明了「写到了存储」与「槽已绑定」两件事。
         */
        key_tostr_expect_success(handle, "int64 42 写入 char/utf8 键文本",
                                 BINRWIR_REFERENCE_LITERAL,
                                 (const uint8_t *)&forty_two,
                                 sizeof(forty_two), "int64", 1U, 1U, "42");
    }
    {
        /* 2. arity 不符：0 读 1 写 ⇒ 0x4A01（搬运层段：WRONG_ARITY）。 */
        key_tostr_expect_failure(handle, "arity 0 读 1 写",
                                 BINRWIR_REFERENCE_LITERAL, NULL, 0U, "int64",
                                 0U, 1U,
                                 VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY);
    }
    {
        /* 2b. arity 不符：1 读 2 写 ⇒ 0x4A01。 */
        key_tostr_expect_failure(handle, "arity 1 读 2 写",
                                 BINRWIR_REFERENCE_LITERAL, NULL, 0U, "int64",
                                 1U, 2U,
                                 VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY);
    }
    {
        /*
         * 3a. 元组 langtype ⇒ 0x4A07（搬运层段：UNSUPPORTED_LANGTYPE）。
         *
         * 元组没有值形态，`member_key_text` 报的就是这条。
         */
        const uint8_t tuple_body[8] = {1, 0, 0, 0, 2, 0, 0, 0};

        key_tostr_expect_failure(
            handle, "元组 langtype", BINRWIR_REFERENCE_LITERAL, tuple_body,
            sizeof(tuple_body), "[int32,int32]", 1U, 1U,
            VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE);
    }
    {
        /* 3b. body 宽 5 字节的 int32 ⇒ 0x4A08（搬运层段：MALFORMED_BODY）。 */
        const uint8_t wide_body[5] = {1, 2, 3, 4, 5};

        key_tostr_expect_failure(handle, "int32 body 5 字节",
                                 BINRWIR_REFERENCE_LITERAL, wide_body,
                                 sizeof(wide_body), "int32", 1U, 1U,
                                 VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY);
    }
    {
        /* 3c. NaN ⇒ 0x4A09（搬运层段：NON_FINITE）。 */
        const double not_a_number = 0.0 / 0.0;

        key_tostr_expect_failure(handle, "NaN", BINRWIR_REFERENCE_LITERAL,
                                 (const uint8_t *)&not_a_number,
                                 sizeof(not_a_number), "float64", 1U, 1U,
                                 VMBIN_HANDLER_CALL_DIAG_NON_FINITE);
    }
    {
        /*
         * 3d. 读参是 STATIC（只有 key 文本、没有声明类型）⇒ 0x4A07。
         *
         * 这条钉的是「值定位得到、只是没有类型」，报的是 `UNSUPPORTED_LANGTYPE`
         * 而不是 `KEY_UNRESOLVED`（host 段 0x4603）——两件事不能混。
         */
        key_tostr_expect_failure(
            handle, "读参无声明类型", BINRWIR_REFERENCE_STATIC, NULL, 0U, "",
            1U, 1U, VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE);
    }

    (void)kvspaceClose(handle);
    (void)rmdir(directory);
    if (failures != 0) {
        fprintf(stderr, "key_tostr_handler_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("key_tostr_handler_test: OK\n");
    return 0;
}
