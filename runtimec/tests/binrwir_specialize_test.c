/*
 * 特化轮次的验收（施工图 §五）。
 *
 * 核心判据是「**元组不命中就保留原 opid**，不拒绝整条 Program」——一条函数里某个
 * 局部变量没拿到静态类型，不该让同 Program 里无关的其他函数一起跑不了。
 *
 * 反向控制：
 *   - 把「不命中保留原 opid」改成「失败返回」，本用例的跨类型那一条必须红；
 *   - 把查找表换成恒返回 0，命中那条必须红。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/binfunc.h"
#include "binrwir/specialize.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"

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

/*
 * 造一个 `add(a, b) -> r` 形态的单指令 Program，三个操作数都是 **LOCAL**，
 * 类型由符号池给出——这正是特化要读的那条路径。
 */
static runtimec_status_t test_build_add_program(
    const runtimec_allocator_t *allocator, const char *left_type,
    const char *right_type, const char *result_type,
    binrwir_binfunc_t **output_program) {
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_argument_t arguments[3];
    binrwir_instruction_build_request_t request;
    uint16_t left_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    uint16_t right_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    uint16_t result_symbol = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    status = binrwir_binfunc_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_binfunc_builder_add_symbol(
        builder, runtimec_string_view_from_c_string("a"),
        runtimec_string_view_from_c_string(left_type), &left_symbol);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("b"),
            runtimec_string_view_from_c_string(right_type), &right_symbol);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_add_symbol(
            builder, runtimec_string_view_from_c_string("r"),
            runtimec_string_view_from_c_string(result_type), &result_symbol);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(0U, BINRWIR_REFERENCE_LOCAL,
                                         left_symbol, 0U, &arguments[0]);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL,
                                         right_symbol, 0U, &arguments[1]);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(2U, BINRWIR_REFERENCE_LOCAL,
                                         result_symbol, 0U, &arguments[2]);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = 1U;
        request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_ADD;
        request.read_arguments.data = &arguments[0];
        request.read_arguments.count = 2U;
        request.write_arguments.data = &arguments[2];
        request.write_arguments.count = 1U;
        status = binrwir_binfunc_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_finalize(builder, output_program);
    }
    (void)binrwir_binfunc_builder_destroy(builder);
    return status;
}

/* 特化后第一条指令的 opid。 */
static binrwir_operation_identifier_t test_first_opid(
    const binrwir_binfunc_t *program) {
    binrwir_instruction_view_t instruction;

    if (binrwir_binfunc_get_instruction(program, 0U, &instruction) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return BINRWIR_OPERATION_IDENTIFIER_NONE;
    }
    return instruction.instruction->operation_identifier;
}

int main(void) {
    runtimec_allocator_t allocator = {NULL, test_allocate, test_deallocate};
    runtimec_error_t error;

    /* ① 同型命中：add(int64,int64,int64) → 特化 opid ≠ 0x0002。 */
    {
        binrwir_binfunc_t *program = NULL;
        binrwir_binfunc_t *specialized = NULL;
        binrwir_binfunc_t *twice = NULL;
        binrwir_operation_identifier_t before;
        binrwir_operation_identifier_t after;

        EXPECT(test_build_add_program(&allocator, "int64", "int64", "int64",
                                      &program) == RUNTIMEC_STATUS_SUCCESS);
        before = test_first_opid(program);
        EXPECT(before == BINRWIR_OPERATION_IDENTIFIER_ADD);
        EXPECT(binrwir_binfunc_specialize(program, &allocator, &specialized,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
        after = test_first_opid(specialized);
        EXPECT(after != before);
        EXPECT(after >= BINRWIR_SPECIALIZED_BASE);

        /* ② 幂等：对已特化的 Program 再特化一次，opid 不变。 */
        EXPECT(binrwir_binfunc_specialize(specialized, &allocator, &twice,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test_first_opid(twice) == after);

        EXPECT(binrwir_binfunc_destroy(twice) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(specialized) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }

    /* ③ 不同宽度：add(int32,int32,int32) 与 int64 的特化 opid 不同。 */
    {
        binrwir_binfunc_t *wide = NULL;
        binrwir_binfunc_t *narrow = NULL;
        binrwir_binfunc_t *wide_specialized = NULL;
        binrwir_binfunc_t *narrow_specialized = NULL;

        EXPECT(test_build_add_program(&allocator, "int64", "int64", "int64",
                                      &wide) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test_build_add_program(&allocator, "int32", "int32", "int32",
                                      &narrow) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_specialize(wide, &allocator, &wide_specialized,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_specialize(narrow, &allocator,
                                          &narrow_specialized,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test_first_opid(wide_specialized) !=
               test_first_opid(narrow_specialized));

        EXPECT(binrwir_binfunc_destroy(narrow_specialized) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(wide_specialized) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(narrow) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(wide) == RUNTIMEC_STATUS_SUCCESS);
    }

    /*
     * ④ 表里没有的组合：add(int32, uint32, int32) —— 混合符号算术被
     * matrix:28 拒绝，所以这对元组**不在表里**。
     *
     * **必须拒绝整份 Program，error.code = 3**（NO_SPECIALIZATION_ROW）。
     *
     * 这是 2026-11-05 改的行为：基础 opid 的通用 handler 已按 p10 删掉
     * （`scalar.c` 的 8 条算术 handler），保留基础 opid 只会得到一条必然
     * `UNSUPPORTED` 的指令——那时错误出现在执行期、被归因成「注册表里没有」，
     * 而真实原因是「特化表缺一行」。回落把后者永久掩盖。
     *
     * **旧断言（EXPECT_SUCCESS + 首条 opid 仍为 ADD）已废**：它锁住的正是
     * `specialize.h` 里那段被删掉的设计。断言旧行为会让「改回去」看起来是对的。
     *
     * 注意别拿 add(int32,int64,int64) 当反例：那是**合法**组合
     * （NUMERIC_COMPATIBLE 允许同符号跨宽度），表里有它。
     */
    {
        binrwir_binfunc_t *program = NULL;
        binrwir_binfunc_t *specialized = NULL;

        EXPECT(test_build_add_program(&allocator, "int32", "uint32", "int32",
                                      &program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_error_reset(&error);
        /*
         * 拒绝时 `specialized` 必须是 NULL——「失败还留一个半成品」比失败更糟：
         * 调用方若按返回值之外的路子用它，就拿到一份没特化完的 Program。
         */
        EXPECT(binrwir_binfunc_specialize(program, &allocator, &specialized,
                                          &error) == RUNTIMEC_STATUS_INVALID);
        EXPECT(specialized == NULL);
        EXPECT(error.code == 3); /* BINRWIR_SPECIALIZE_DIAG_NO_SPECIALIZATION_ROW */
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }

    /* ⑤ 单组合算子（copy）：特化前后 opid 不变。 */
    {
        binrwir_binfunc_identity_t identity;
        binrwir_binfunc_builder_t *builder = NULL;
        binrwir_binfunc_t *program = NULL;
        binrwir_binfunc_t *specialized = NULL;
        binrwir_argument_t literal_argument;
        binrwir_instruction_build_request_t request;
        uint8_t wire[KVSPACE_XVALUE_HEAD_LEN + 8U];
        kvspaceXvalueHead head;
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;

        memset(&head, 0, sizeof(head));
        kvspaceXvalueHeadSetLen(&head, 8U);
        head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;  /* 新头：无 storetype */
        memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memset(wire + KVSPACE_XVALUE_HEAD_LEN, 0, 8U);

        memset(&identity, 0, sizeof(identity));
        identity.snapshot_generation = 1U;
        identity.program_generation = 1U;
        identity.binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION;
        EXPECT(binrwir_binfunc_builder_create(&allocator, &identity, &builder) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_builder_add_literal(
                   builder, runtimec_bytes_view_make(wire, sizeof(wire)),
                   runtimec_string_view_from_c_string("int64"),
                   &literal_index) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U,
                                       literal_index, &literal_argument) ==
               RUNTIMEC_STATUS_SUCCESS);
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = 1U;
        request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COPY;
        request.read_arguments.data = &literal_argument;
        request.read_arguments.count = 1U;
        request.write_arguments.data = &literal_argument;
        request.write_arguments.count = 1U;
        EXPECT(binrwir_binfunc_builder_add_instruction(builder, &request,
                                                       &identifier) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_builder_finalize(builder, &program) ==
               RUNTIMEC_STATUS_SUCCESS);
        (void)binrwir_binfunc_builder_destroy(builder);

        EXPECT(binrwir_binfunc_specialize(program, &allocator, &specialized,
                                          &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test_first_opid(specialized) == BINRWIR_OPERATION_IDENTIFIER_COPY);
        EXPECT(binrwir_binfunc_destroy(specialized) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }

    if (failures != 0) {
        printf("binrwir_specialize_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("binrwir_specialize_test: OK\n");
    return 0;
}
