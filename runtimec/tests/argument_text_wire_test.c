/*
 * TASK-070：`kvlang_runtime_read_argument_text` 读回的必须是**文本本身**。
 *
 * 症状：读回来的字符串前面多 16 字节垃圾——那是 XValue 的 head 被当成了内容。
 *
 * 判据（两条，一条正例一条反向对照）：
 *   1. 已知文本读回**逐字节相等且长度相等**；
 *   2. 含前导 `\0` 与非 ASCII 的文本同样逐字节相等——这条挡住「按 C 串复制」
 *      和「按 head 长度算长度」两种偷懒：前导 `\0` 一遇 strlen 就截断，非 ASCII
 *      一遇按字符数的写入就丢字节。
 *
 * 为什么要多带这两条：只测 `"hello"` 时，「跳过 16 字节」和「写错偏移但仍落在
 * 文本里」都过得去，而反向对照必须能把它们打红。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvlang/runtimec.h>
#include <kvlang/xvalue.h>

#include "binrwir/argument.h"
#include "binrwir/binfunc.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "contract/allocator.h"
#include "contract/extension_interface.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

/* 默认分配器：本测试需要与产品同一条冷路径分配，不注入替身。 */
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
    const runtimec_allocator_t allocator = {NULL, test_allocate,
                                            test_deallocate};
    return allocator;
}

/*
 * 造一个只含一条指令的 Program：读参 0 是字面量 `wire`，langtype 是 `langtype`。
 *
 * 字面量实参走的是 `value_access_locate` 的 LITERAL 分支（`is_whole_wire = true`），
 * 也就是「池里那一段是整条 wire」——正是 TASK-070 要覆盖的形态。
 */
typedef struct fixture {
    binrwir_binfunc_t *program;
    binrwir_instruction_view_t instruction;
    binrwir_frame_bindings_t *bindings;
    binrwir_binvthread_frame_t frame;
    runtimec_allocator_t allocator;
} fixture_t;

static int fixture_build(fixture_t *fixture, const uint8_t *wire,
                         size_t wire_size, const char *langtype) {
    const runtimec_allocator_t allocator = test_allocator();
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    uint32_t literal_index = 0U;
    uint32_t instruction_identifier = 0U;
    uint32_t parameter_index = 0U;
    binrwir_argument_t argument;
    binrwir_instruction_build_request_t request;

    memset(fixture, 0, sizeof(*fixture));
    fixture->allocator = allocator;

    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;

    if (binrwir_binfunc_builder_create(&allocator, &identity, &builder) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    if (binrwir_binfunc_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ, 0U,
            runtimec_string_view_from_c_string(langtype),
            &parameter_index) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    if (binrwir_binfunc_builder_add_literal(
            builder, runtimec_bytes_view_make(wire, wire_size),
            runtimec_string_view_from_c_string(langtype),
            &literal_index) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    if (binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U,
                                literal_index, &argument) !=
        RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = 1U;
    request.operation_identifier = UINT16_C(0x7001);
    request.read_arguments = (binrwir_argument_span_t){&argument, 1U};
    request.write_arguments = (binrwir_argument_span_t){NULL, 0U};
    if (binrwir_binfunc_builder_add_instruction(builder, &request,
                                                &instruction_identifier) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_builder_finalize(builder, &fixture->program) !=
            RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    (void)binrwir_binfunc_builder_destroy(builder);

    if (binrwir_binfunc_get_instruction(fixture->program, 0U,
                                        &fixture->instruction) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    if (binrwir_frame_bindings_create(&allocator, 1U, &fixture->bindings) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    memset(&fixture->frame, 0, sizeof(fixture->frame));
    fixture->frame.program = fixture->program;
    fixture->frame.bindings = fixture->bindings;
    fixture->frame.snapshot_generation = 1U;
    fixture->frame.program_generation = 1U;
    return 1;
}

static void fixture_release(fixture_t *fixture) {
    if (fixture->bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(fixture->bindings);
    }
    if (fixture->program != NULL) {
        (void)binrwir_binfunc_destroy(fixture->program);
    }
}

/*
 * 一例：给定 body 字节，编码成 wire，再经 `kvlang_runtime_read_argument_text`
 * 读回，要求**逐字节相等**。
 */
static void check_round_trip(const uint8_t *body, size_t body_size,
                             const char *label) {
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    fixture_t fixture;
    char output[256];
    kvlang_runtime_text_buffer_t buffer;
    kvlang_runtime_error_t error;
    char message[256];

    if (kvlangXvalueHeadNewSlack(body, (uint64_t)body_size, "[]char/utf8",
                                 &wire, &wire_size) != 0 ||
        wire == NULL) {
        printf("[FAIL] %s: cannot encode the wire\n", label);
        ++g_failures;
        return;
    }
    if (!fixture_build(&fixture, wire, (size_t)wire_size, "[]char/utf8")) {
        printf("[FAIL] %s: cannot build the fixture\n", label);
        ++g_failures;
        free(wire);
        return;
    }
    memset(output, 0, sizeof(output));
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = output;
    buffer.capacity = sizeof(output);
    memset(&error, 0, sizeof(error));
    memset(message, 0, sizeof(message));
    error.message.data = message;
    error.message.capacity = sizeof(message);

    const runtimec_status_t status = kvlang_runtime_read_argument_text(
        &fixture.frame, &fixture.instruction, 0U, &buffer, &error);
    snprintf(message, sizeof(message),
             "%s: read_argument_text status=%d (expected 0)", label,
             (int)status);
    check(status == RUNTIMEC_STATUS_SUCCESS, message);
    snprintf(message, sizeof(message),
             "%s: length=%zu (expected %zu)", label, buffer.length, body_size);
    check(buffer.length == body_size, message);
    snprintf(message, sizeof(message),
             "%s: bytes differ from the stored text", label);
    check(buffer.length == body_size &&
              memcmp(output, body, body_size) == 0,
          message);
    if (buffer.length == body_size && memcmp(output, body, body_size) != 0) {
        printf("        read  : ");
        for (size_t at = 0U; at < buffer.length && at < 32U; ++at) {
            printf("%02x ", (unsigned char)output[at]);
        }
        printf("\n        stored: ");
        for (size_t at = 0U; at < body_size && at < 32U; ++at) {
            printf("%02x ", body[at]);
        }
        printf("\n");
    }

    fixture_release(&fixture);
    free(wire);
}

/*
 * 兄弟入口 `kvlang_runtime_read_argument_integer` 走同一个 `read`：整数按整条
 * wire 解释时，头里的 bodylen 会当成低字节——数看着合法、与声明值无关。
 *
 * 取值 `0x0102030405060708`：八个字节全不同，任何偏移/端序错都对不上；
 * 若误按 wire 读，最低 8 字节是 `[len:u64 LE]` = `0x0000000000000008`。
 */
static void check_integer_round_trip(void) {
    static const uint8_t body[] = {0x08, 0x07, 0x06, 0x05,
                                   0x04, 0x03, 0x02, 0x01};
    const int64_t expected = INT64_C(0x0102030405060708);
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    fixture_t fixture;
    kvlang_runtime_error_t error;
    char message[128];
    int64_t value = 0;

    if (kvlangXvalueHeadNewSlack(body, sizeof(body), "int64", &wire,
                                 &wire_size) != 0 ||
        wire == NULL) {
        printf("[FAIL] integer: cannot encode the wire\n");
        ++g_failures;
        return;
    }
    if (!fixture_build(&fixture, wire, (size_t)wire_size, "int64")) {
        printf("[FAIL] integer: cannot build the fixture\n");
        ++g_failures;
        free(wire);
        return;
    }
    memset(&error, 0, sizeof(error));
    memset(message, 0, sizeof(message));
    error.message.data = message;
    error.message.capacity = sizeof(message);

    const runtimec_status_t status = kvlang_runtime_read_argument_integer(
        &fixture.frame, &fixture.instruction, 0U, &value, &error);
    snprintf(message, sizeof(message),
             "integer: status=%d (expected 0)", (int)status);
    check(status == RUNTIMEC_STATUS_SUCCESS, message);
    snprintf(message, sizeof(message),
             "integer: value=%lld (expected %lld)",
             (long long)value, (long long)expected);
    check(value == expected, message);

    fixture_release(&fixture);
    free(wire);
}

int main(void) {
    /* 1. 正例：已知 ASCII 文本。 */
    check_round_trip((const uint8_t *)"hello", 5U, "ascii text");
    /* 2. 反向对照：前导 `\0`——按 C 串算长度会得到 0 长度。 */
    {
        static const uint8_t leading_nul[] = {0x00, 'a', 'b', 0x00, 'c'};
        check_round_trip(leading_nul, sizeof(leading_nul), "leading NUL");
    }
    /* 3. 反向对照：非 ASCII（`中文`，UTF-8 六字节）——按字符数算会丢字节。 */
    {
        static const uint8_t chinese[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
        check_round_trip(chinese, sizeof(chinese), "non-ASCII text");
    }
    /* 4. 边界：空 body 是合法的空串，不是失败。 */
    check_round_trip((const uint8_t *)"", 0U, "empty text");
    /* 5. 兄弟入口：整数读参同样要值体。 */
    check_integer_round_trip();

    if (g_failures == 0) {
        printf("argument_text_wire_test: OK\n");
        return 0;
    }
    printf("argument_text_wire_test: %d failure(s)\n", g_failures);
    return 1;
}
