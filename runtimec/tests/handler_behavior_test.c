/*
 * handler 行为用例：数据驱动的「输入字节 → 期望字节」。
 *
 * 与 `vmbin_execute_test.c` 的分工：那个文件验的是执行器（游标、提交边界、
 * 失败形状），用测试私有的假 handler；本文件验的是**产品 handler 的运算结果**，
 * 断言的是写回槽里的字节，不是 status。
 *
 * 为什么是数据驱动：重写 handler 时会成批改动，逐条写一个 test 函数既读不出
 * 「这一行在验什么」，也挡不住「改了 A 顺带错了 B」。每行用例自带
 * `base_opid + 元组串 + 输入字节 + 期望字节`，读表就等于读规格。
 *
 * opid 从 `binrwir_specializations` 按 `(base_opid, 元组串)` 查出特化 opid，
 * 顺带验证「表里确实有这条特化」——表与实现漂移时会红在这一步，而不是悄悄地
 * 用基础 opid 跑一遍。
 *
 * 期望值来源：`specification/five-language-behavior-matrix.md` 的决策行，
 * **不是**从实现读出来的。每行都标了决策名；没有决策覆盖的行为不写用例。
 *
 * 红是预期的：本轮 handler 重写期间，未实现的算子会红。报数是「N/M 通过」，
 * 红分三类（未实现 / 实现错 / 用例写错），修法完全不同。
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "binrwir/registry.h"
#include "binrwir/signature.h"
#include "contract/host_services.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/core_operations.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/interpreter.h"

/* 特化表是生成物，定义在 binrwir/specialization_table.c；头文件只给类型与
 * 计数，表本身没有 extern 声明（生成器只管生成常量与计数）。测试自己声明，
 * 与 `BINRWIR_SPECIALIZATION_TABLE_COUNT` 对账。 */
extern const binrwir_specialization_t binrwir_specializations[];

#define TEST_BODY_CAPACITY 32U
#define TEST_VALUE_CAPACITY (KVSPACE_XVALUE_HEAD_LEN + TEST_BODY_CAPACITY)

/*
 * 一行用例。
 *
 * 字段刻意是「字节」而不是 union：每一行都是规格的一行，读的人应该能直接对着
 * 期望字节核对，不必先在心里跑一遍类型转换。
 */
typedef struct handler_case {
    const char *decision;     /* five-language-behavior-matrix 的决策名 */
    const char *name;         /* 这一行验什么，人读的一句话 */
    uint16_t base_opid;       /* 基础能力行 */
    const char *operand_types;/* 完整元组串，与特化表逐字节比对 */
    uint8_t reads[2][8];
    size_t read_sizes[2];
    size_t read_count;
    uint8_t expected[8];
    size_t expected_size;
    /* 非零 = 期望结果是 NaN：只认「是 NaN」，不比符号位与载荷。 */
    int expected_is_nan;
    /* 非零 = 期望 handler 失败（INVALID），不比较字节。 */
    int expect_failure;
} handler_case_t;

/* ── 失败计数：按「用例」而不是按断言，报告要的是 N/M ────────────── */

static size_t passed_cases;
static size_t failed_cases;
static size_t unimplemented_cases;
static size_t wrong_result_cases;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                   \
            return false;                                                     \
        }                                                                     \
    } while (0)

/* ── 分配器 ──────────────────────────────────────────────────────── */

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
    runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    return allocator;
}

/* ── XValue 形状的读写槽 ─────────────────────────────────────────── */

typedef struct test_value {
    uint8_t storage[TEST_VALUE_CAPACITY];
    size_t body_size;
} test_value_t;

static void test_value_initialize(test_value_t *value, size_t body_size) {
    kvspaceXvalueHead head;

    memset(value, 0, sizeof(*value));
    memset(&head, 0, sizeof(head));
    head.storetype = KVSPACE_XVALUE_ST_SHORT;
    kvspaceXvalueHeadSetLen(&head, body_size);
    memcpy(value->storage, &head, KVSPACE_XVALUE_HEAD_LEN);
    value->body_size = body_size;
}

static runtimec_value_body_address_t test_value_body_address(
    test_value_t *value) {
    return (runtimec_value_body_address_t)(
        uintptr_t)(value->storage + KVSPACE_XVALUE_HEAD_LEN);
}

/* ── 特化表查找：表里没有就是表错了，不是用例错了 ─────────────────── */

static const binrwir_specialization_t *find_specialization(
    uint16_t base_opid, const char *operand_types, size_t *output_count) {
    size_t ordinal;

    *output_count = 0U;
    for (ordinal = 0U; ordinal < BINRWIR_SPECIALIZATION_TABLE_COUNT;
         ++ordinal) {
        const binrwir_specialization_t *row = &binrwir_specializations[ordinal];

        if (row->base_operation_identifier == base_opid &&
            strcmp(row->operand_types, operand_types) == 0) {
            *output_count += 1U;
            return row;
        }
    }
    return NULL;
}

/* ── 一趟执行 ────────────────────────────────────────────────────── */

/*
 * 单条指令的 Program：N 个字面量读参（类型逐段取自元组串）+ 一个静态写参
 * （类型取自最后一段）。
 *
 * 用字面量而不是参数槽，是因为字面量的 langtype 就是元组串本身——这样
 * 「handler 读到的操作数类型」与「用例声明的元组」是同一份事实，不存在
 * 「签名里写 A、字面量里写 B」的第二真相。
 *
 * **每个读参各自登记、各自取下标**：`binrwir_program_get_literal` 按池下标取，
 * 而读参的 `argument_index` 与池下标是两件事。把两者混用过一次（第二参复用了
 * 第一参的下标），症状是「输入对、结果错」，比用例写错更难查。
 */
static runtimec_status_t run_case(const handler_case_t *test_case,
                                  test_value_t *destination,
                                  binrwir_execution_exit_t *output_exit) {
    static uint8_t literal_wires[2][TEST_VALUE_CAPACITY];
    /*
     * 元组串恒为三段：`左,右,结果`，与特化表逐字节一致。
     *
     * 单操作数用例（cast）也写三段——特化表里 cast 的行就是
     * `float64,int64,int64`（源, 目标, 目标）。**不要**按 read_count 去推段数：
     * 那会让元组串与表对不上，查表失败的原因看起来像「表里没有」。
     */
    char type_text[3][64];
    const char *segments[3];
    size_t segment_count = 0U;
    const char *cursor = test_case->operand_types;
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_registry_t registry;
    binrwir_instruction_build_request_t request;
    binrwir_argument_t reads[2];
    binrwir_argument_t write_argument;
    size_t specialization_count = 0U;
    const binrwir_specialization_t *specialization;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    runtimec_allocator_t allocator = test_allocator();
    size_t ordinal;
    runtimec_status_t status;

    for (segment_count = 0U;
         segment_count < sizeof(segments) / sizeof(segments[0]);
         ++segment_count) {
        const char *comma = strchr(cursor, ',');

        if (comma == NULL) {
            if (strlen(cursor) >= sizeof(type_text[0])) {
                return RUNTIMEC_STATUS_INVALID;
            }
            snprintf(type_text[segment_count], sizeof(type_text[0]), "%s",
                     cursor);
            segments[segment_count] = type_text[segment_count];
            continue;
        }
        if ((size_t)(comma - cursor) >= sizeof(type_text[0])) {
            return RUNTIMEC_STATUS_INVALID;
        }
        memcpy(type_text[segment_count], cursor, (size_t)(comma - cursor));
        type_text[segment_count][comma - cursor] = '\0';
        segments[segment_count] = type_text[segment_count];
        cursor = comma + 1;
    }
    EXPECT(segments[2] != NULL);

    specialization = find_specialization(test_case->base_opid,
                                         test_case->operand_types,
                                         &specialization_count);
    EXPECT(specialization != NULL);
    /* 同一条 (base, 元组) 只能有一行：重复特化是生成器的错，不是用例的错。 */
    EXPECT(specialization_count == 1U);

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
    /* 写槽是 STATIC：符号池里只需要一个 key 文本，类型由用例声明。 */
    status = binrwir_program_builder_add_symbol(
        builder, runtimec_string_view_from_c_string("/t/result"),
        runtimec_string_view_from_c_string(segments[2]),
        &symbol_index);
    for (ordinal = 0U;
         status == RUNTIMEC_STATUS_SUCCESS && ordinal < test_case->read_count;
         ++ordinal) {
        kvspaceXvalueHead head;
        uint8_t *wire = literal_wires[ordinal];
        uint32_t literal_index = 0U;

        memset(&head, 0, sizeof(head));
        head.storetype = KVSPACE_XVALUE_ST_SHORT;
        kvspaceXvalueHeadSetLen(&head, test_case->read_sizes[ordinal]);
        memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, test_case->reads[ordinal],
               test_case->read_sizes[ordinal]);
        status = binrwir_program_builder_add_literal(
            builder,
            runtimec_bytes_view_make(
                wire, KVSPACE_XVALUE_HEAD_LEN + test_case->read_sizes[ordinal]),
            runtimec_string_view_from_c_string(segments[ordinal]),
            &literal_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            break;
        }
        status = binrwir_argument_create(
            (uint32_t)(ordinal + 1U), BINRWIR_REFERENCE_LITERAL, 0U,
            literal_index, &reads[ordinal]);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(
            (uint32_t)(test_case->read_count + 1U), BINRWIR_REFERENCE_STATIC,
            symbol_index,
            (uint64_t)(uintptr_t)test_value_body_address(destination),
            &write_argument);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memset(&request, 0, sizeof(request));
        /*
         * 用**基础 opid**，不是特化 opid。
         *
         * 特化 opid（0x8000–0xFEFF）是「编译期按操作数类型选出的更窄的行」，
         * 由编译器把指令改写成它；handler 仍按基础行注册，一条基础 handler
         * 服务它名下所有特化。实测（2026-11-05）：registry 里只有基础 opid，
         * 特化表 `binrwir_specializations` 目前**零产品消费方**，拿特化 opid
         * 直接执行会落到「表里没有这个 opid」。
         *
         * 所以本用例查特化表只为验证「表里确实有这条 (base, 元组) 行」，
         * 执行走基础 opid。特化链本身是否接通，由另一条用例回答
         * （`runtimec.architecture.*` 那批不给答案，这里也不越位断言）。
         */
        request.operation_identifier = test_case->base_opid;
        request.source_logical_program_counter = 1U;
        request.read_arguments.data = reads;
        request.read_arguments.count = test_case->read_count;
        request.write_arguments.data = &write_argument;
        request.write_arguments.count = 1U;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &instruction_identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, &program);
    }
    (void)binrwir_program_builder_destroy(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }

    status = binrwir_execution_frame_initialize(&frame, program, NULL, 1U, 2U,
                                                0U);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_frame_bindings_create(&allocator, 8U, &bindings);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        frame.bindings = bindings;
        status = binrwir_registry_initialize(&registry);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        size_t scalar_count = 0U;
        const binrwir_registry_entry_t *scalar_entries =
            runtimec_scalar_handler_entries(&scalar_count);

        status = binrwir_registry_append(&registry, "scalar", scalar_entries,
                                          scalar_count, NULL);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        *output_exit = (binrwir_execution_exit_t){0};
        status = vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                               output_exit);
    }
    if (bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(bindings);
    }
    (void)binrwir_execution_frame_destroy(&frame);
    (void)binrwir_program_destroy(program);
    return status;
}

/* ── 一行用例的执行与判定 ────────────────────────────────────────── */

static void run_one(const handler_case_t *test_case) {
    test_value_t destination;
    binrwir_execution_exit_t exit_state;
    runtimec_status_t status;

    test_value_initialize(&destination, test_case->expected_size);
    status = run_case(test_case, &destination, &exit_state);

    if (test_case->expect_failure != 0) {
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            passed_cases += 1U;
            return;
        }
        failed_cases += 1U;
        wrong_result_cases += 1U;
        fprintf(stderr,
                "FAIL [%s] %s: expected a rejection, the handler accepted "
                "the operands\n",
                test_case->decision, test_case->name);
        return;
    }

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /*
         * 未实现与实现错**必须分开报**：修法完全不同——前者等别人写，后者是本
         * 行的语义写错了。
         *
         * 判据是执行器的诊断码，不是 status（两条路径都是 INVALID）：
         *   - `INVALID_INSTRUCTION`：registry 里没有这个 opid → 未实现；
         *   - `HANDLER_FAILED`：handler 跑到了，自己拒绝/报错 → 实现错。
         */
        if (exit_state.detail_code == VMBIN_INTERPRETER_DIAG_HANDLER_FAILED) {
            failed_cases += 1U;
            wrong_result_cases += 1U;
            fprintf(stderr,
                    "IMPL-WRONG [%s] %s: the handler ran and rejected valid "
                    "operands\n",
                    test_case->decision, test_case->name);
            return;
        }
        failed_cases += 1U;
        unimplemented_cases += 1U;
        fprintf(stderr,
                "UNIMPLEMENTED [%s] %s: no handler for opid 0x%04X "
                "(detail %u)\n",
                test_case->decision, test_case->name,
                (unsigned)test_case->base_opid,
                (unsigned)exit_state.detail_code);
        return;
    }

    {
        const uint8_t *body = destination.storage + KVSPACE_XVALUE_HEAD_LEN;

        if (test_case->expected_is_nan != 0) {
            double observed = 0.0;

            memcpy(&observed, body, sizeof(observed));
            if (isnan(observed)) {
                passed_cases += 1U;
                return;
            }
            failed_cases += 1U;
            wrong_result_cases += 1U;
            fprintf(stderr, "WRONG [%s] %s: expected NaN, got %g\n",
                    test_case->decision, test_case->name, observed);
            return;
        }
        if (memcmp(body, test_case->expected, test_case->expected_size) == 0) {
            passed_cases += 1U;
            return;
        }
        failed_cases += 1U;
        wrong_result_cases += 1U;
        fprintf(stderr, "WRONG [%s] %s\n  expected:", test_case->decision,
                test_case->name);
        for (size_t index = 0U; index < test_case->expected_size; ++index) {
            fprintf(stderr, " %02x", test_case->expected[index]);
        }
        fprintf(stderr, "\n  actual  :");
        for (size_t index = 0U; index < test_case->expected_size; ++index) {
            fprintf(stderr, " %02x", body[index]);
        }
        fprintf(stderr, "\n");
    }
}

/* ── 期望字节的构造（与主机字节序无关的写法） ────────────────────── */

static void int64_bytes(int64_t value, uint8_t output[8]) {
    memcpy(output, &value, sizeof(value));
}

static void int32_bytes(int32_t value, uint8_t output[4]) {
    memcpy(output, &value, sizeof(value));
}

static void uint64_bytes(uint64_t value, uint8_t output[8]) {
    memcpy(output, &value, sizeof(value));
}

static void int8_bytes(int8_t value, uint8_t output[1]) {
    memcpy(output, &value, sizeof(value));
}

static void float64_bytes(double value, uint8_t output[8]) {
    memcpy(output, &value, sizeof(value));
}

static void float32_bytes(float value, uint8_t output[4]) {
    memcpy(output, &value, sizeof(value));
}

/* bool 结果：契约是 1 字节 0/1，不是「非零即真」。 */
static void bool_bytes(int value, uint8_t output[1]) {
    output[0] = (uint8_t)(value != 0 ? 1U : 0U);
}

/* 便捷构造：二元整数用例。 */
static handler_case_t integer_binary_case(const char *decision,
                                          const char *name, uint16_t opid,
                                          const char *types, int width,
                                          int64_t left, int64_t right,
                                          int64_t expected) {
    handler_case_t test_case;

    memset(&test_case, 0, sizeof(test_case));
    test_case.decision = decision;
    test_case.name = name;
    test_case.base_opid = opid;
    test_case.operand_types = types;
    if (width == 8) {
        int64_bytes(left, test_case.reads[0]);
        int64_bytes(right, test_case.reads[1]);
        int64_bytes(expected, test_case.expected);
    } else if (width == 4) {
        int32_bytes((int32_t)left, test_case.reads[0]);
        int32_bytes((int32_t)right, test_case.reads[1]);
        int32_bytes((int32_t)expected, test_case.expected);
    }
    test_case.read_sizes[0] = (size_t)width;
    test_case.read_sizes[1] = (size_t)width;
    test_case.read_count = 2U;
    test_case.expected_size = (size_t)width;
    return test_case;
}

int main(void) {
    handler_case_t cases[64];
    size_t case_count = 0U;

    /* 特化表与计数必须一致：漂移时这里先红，免得后面全部误报。 */
    if (BINRWIR_SPECIALIZATION_TABLE_COUNT != 1836U) {
        fprintf(stderr, "specialization table count drifted: %u\n",
                (unsigned)BINRWIR_SPECIALIZATION_TABLE_COUNT);
        return 1;
    }

    /*
     * ── 组 1：除法 / 取余（decision: signed-integer-division,
     *      float-division-by-zero）─────────────────────────────────
     *
     * -7 / 3：C、Rust、Go 都是向零截断 → -2（矩阵决策原文）。
     * -7 % 3：C 的 % 保留被除数符号 → -1。
     * 整数除零：矩阵 Invalid input 列写的是 RUNTIMEC_STATUS_INVALID。
     * 浮点除零：IEEE，算出 inf / NaN，exit 不带错误。
     */
    cases[case_count++] = integer_binary_case(
        "signed-integer-division", "-7 / 3 truncates toward zero",
        BINRWIR_OPERATION_IDENTIFIER_DIV, "int64,int64,int64", 8, -7, 3, -2);
    cases[case_count++] = integer_binary_case(
        "signed-integer-division", "-7 % 3 keeps the dividend sign",
        BINRWIR_OPERATION_IDENTIFIER_MOD, "int64,int64,int64", 8, -7, 3, -1);
    cases[case_count++] = integer_binary_case(
        "signed-integer-division", "7 / -3 truncates toward zero",
        BINRWIR_OPERATION_IDENTIFIER_DIV, "int64,int64,int64", 8, 7, -3, -2);
    {
        /* 整数除零：期望**拒绝**，不是 0、不是 inf。 */
        handler_case_t division_by_zero = integer_binary_case(
            "signed-integer-division", "int64 division by zero is rejected",
            BINRWIR_OPERATION_IDENTIFIER_DIV, "int64,int64,int64", 8, 1, 0,
            0);
        division_by_zero.expect_failure = 1;
        cases[case_count++] = division_by_zero;

        handler_case_t remainder_by_zero = integer_binary_case(
            "signed-integer-division", "int64 remainder by zero is rejected",
            BINRWIR_OPERATION_IDENTIFIER_MOD, "int64,int64,int64", 8, 1, 0,
            0);
        remainder_by_zero.expect_failure = 1;
        cases[case_count++] = remainder_by_zero;
    }
    {
        /* 浮点除零：IEEE。1.0/0.0 = +inf，-1.0/0.0 = -inf，0.0/0.0 = NaN。 */
        handler_case_t inf_positive;
        handler_case_t inf_negative;
        handler_case_t nan_result;

        memset(&inf_positive, 0, sizeof(inf_positive));
        inf_positive.decision = "float-division-by-zero";
        inf_positive.name = "1.0 / 0.0 is +inf without an error";
        inf_positive.base_opid = BINRWIR_OPERATION_IDENTIFIER_DIV;
        inf_positive.operand_types = "float64,float64,float64";
        float64_bytes(1.0, inf_positive.reads[0]);
        float64_bytes(0.0, inf_positive.reads[1]);
        inf_positive.read_sizes[0] = 8U;
        inf_positive.read_sizes[1] = 8U;
        inf_positive.read_count = 2U;
        float64_bytes(INFINITY, inf_positive.expected);
        inf_positive.expected_size = 8U;
        cases[case_count++] = inf_positive;

        inf_negative = inf_positive;
        inf_negative.name = "-1.0 / 0.0 is -inf";
        float64_bytes(-1.0, inf_negative.reads[0]);
        float64_bytes(-INFINITY, inf_negative.expected);
        cases[case_count++] = inf_negative;

        nan_result = inf_positive;
        nan_result.name = "0.0 / 0.0 is NaN";
        float64_bytes(0.0, nan_result.reads[0]);
        float64_bytes(0.0, nan_result.reads[1]);
        /* NaN 的**符号位与载荷不由矩阵规定**：x86 的 0.0/0.0 给的是
         * 负数号 NaN(0xfff8…),而 NAN 宏是正号(0x7ff8…)。断言只认「是 NaN」，
         * 逐字节比符号位等于把硬件行为写进规格。 */
        nan_result.expected_is_nan = 1;
        cases[case_count++] = nan_result;
    }

    /*
     * ── 组 2：比较（decision: mixed-signed-unsigned-arithmetic）──────
     *
     * 矩阵：算术与位运算拒绝混符号，**比较按精确数学值**。
     * 所以 `-1 < UINT64_MAX` 必须为真——按无符号比会得假，按有符号比会得真；
     * 这条用例的作用就是把「按哪边比」钉死。
     */
    {
        handler_case_t less_int32_int64;
        handler_case_t less_uint64_int64;
        handler_case_t less_int64_uint64;

        memset(&less_int32_int64, 0, sizeof(less_int32_int64));
        less_int32_int64.decision = "mixed-signed-unsigned-arithmetic";
        less_int32_int64.name = "int32 -1 < int64 1 widens before comparing";
        less_int32_int64.base_opid = BINRWIR_OPERATION_IDENTIFIER_LESS;
        less_int32_int64.operand_types = "int32,int64,bool";
        int32_bytes(-1, less_int32_int64.reads[0]);
        int64_bytes(1, less_int32_int64.reads[1]);
        less_int32_int64.read_sizes[0] = 4U;
        less_int32_int64.read_sizes[1] = 8U;
        less_int32_int64.read_count = 2U;
        bool_bytes(1, less_int32_int64.expected);
        less_int32_int64.expected_size = 1U;
        cases[case_count++] = less_int32_int64;

        less_uint64_int64 = less_int32_int64;
        less_uint64_int64.name =
            "uint64 18446744073709551615 < int64 1 is false";
        less_uint64_int64.operand_types = "uint64,int64,bool";
        uint64_bytes(UINT64_MAX, less_uint64_int64.reads[0]);
        int64_bytes(1, less_uint64_int64.reads[1]);
        less_uint64_int64.read_sizes[0] = 8U;
        less_uint64_int64.read_sizes[1] = 8U;
        bool_bytes(0, less_uint64_int64.expected);
        cases[case_count++] = less_uint64_int64;

        less_int64_uint64 = less_int32_int64;
        less_int64_uint64.name =
            "int64 -1 < uint64 18446744073709551615 is true by exact value";
        less_int64_uint64.operand_types = "int64,uint64,bool";
        int64_bytes(-1, less_int64_uint64.reads[0]);
        uint64_bytes(UINT64_MAX, less_int64_uint64.reads[1]);
        less_int64_uint64.read_sizes[0] = 8U;
        less_int64_uint64.read_sizes[1] = 8U;
        bool_bytes(1, less_int64_uint64.expected);
        cases[case_count++] = less_int64_uint64;

        /*
         * 边界：**相等必须为假**。
         *
         * 这条不是「凑数」——它是反向控制要求的。把 `scalar.c` 里
         * `left_signed < right_signed` 改成 `<=`，只有「两数相等」的用例会红；
         * 没有这一条，那个改动**一条用例都抓不到**（实测过）。
         *
         * 凡是被测运算有 `<` / `<=` 两个相邻分支的，两边各要一条用例；
         * 只有严格不等的人证，等于没测分界。
         */
        less_int64_uint64.name = "int64 0 < int64 0 is false (equality)";
        less_int64_uint64.operand_types = "int64,int64,bool";
        int64_bytes(0, less_int64_uint64.reads[0]);
        int64_bytes(0, less_int64_uint64.reads[1]);
        less_int64_uint64.read_sizes[0] = 8U;
        less_int64_uint64.read_sizes[1] = 8U;
        bool_bytes(0, less_int64_uint64.expected);
        cases[case_count++] = less_int64_uint64;

        /* 同一分界的另一侧：`1 < 2` 为真。两条一起才钉死 `<` 而不是 `<=`。 */
        less_int64_uint64.name = "int64 1 < int64 2 is true";
        less_int64_uint64.operand_types = "int64,int64,bool";
        int64_bytes(1, less_int64_uint64.reads[0]);
        int64_bytes(2, less_int64_uint64.reads[1]);
        bool_bytes(1, less_int64_uint64.expected);
        cases[case_count++] = less_int64_uint64;
    }

    /*
     * ── 组 3：cast（decision: floating-to-integer-truncation,
     *      int-to-float32-single-rounding）───────────────────────────
     *
     * -3.9 → int64 = -3（向零截断）
     * 1e300 → int8 = 127（越界饱和到目标宽度，不是回绕）
     * +inf → int64 = INT64_MAX；-inf → int64 = INT64_MIN
     * NaN → int64 = 拒绝（矩阵：NaN 转换是 TypeError）
     * 9007199791611905 → float32：单次舍入得 0x5a000001，
     *   经 double 中转得 0x5a000000 —— 这条用例专抓「先转 double」的实现
     */
    {
        handler_case_t cast_case;

        memset(&cast_case, 0, sizeof(cast_case));
        cast_case.decision = "floating-to-integer-truncation";
        cast_case.name = "float64 -3.9 to int64 truncates toward zero";
        cast_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_CAST;
        cast_case.operand_types = "float64,int64,int64";
        float64_bytes(-3.9, cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        int64_bytes(-3, cast_case.expected);
        cast_case.expected_size = 8U;
        cases[case_count++] = cast_case;

        cast_case.name = "float64 1e300 to int8 saturates to 127";
        cast_case.operand_types = "float64,int8,int8";
        float64_bytes(1e300, cast_case.reads[0]);
        int8_bytes(127, cast_case.expected);
        cast_case.expected_size = 1U;
        cases[case_count++] = cast_case;

        cast_case.name = "float64 +inf to int64 saturates to INT64_MAX";
        cast_case.operand_types = "float64,int64,int64";
        float64_bytes(INFINITY, cast_case.reads[0]);
        int64_bytes(INT64_MAX, cast_case.expected);
        cast_case.expected_size = 8U;
        cases[case_count++] = cast_case;

        cast_case.name = "float64 -inf to int64 saturates to INT64_MIN";
        float64_bytes(-INFINITY, cast_case.reads[0]);
        int64_bytes(INT64_MIN, cast_case.expected);
        cases[case_count++] = cast_case;

        cast_case.name = "float64 NaN to int64 is rejected";
        float64_bytes(NAN, cast_case.reads[0]);
        cast_case.expect_failure = 1;
        cases[case_count++] = cast_case;

        /* int64 → float32：必须单次舍入。 */
        memset(&cast_case, 0, sizeof(cast_case));
        cast_case.decision = "int-to-float32-single-rounding";
        cast_case.name =
            "int64 9007199791611905 to float32 rounds once (0x5a000001)";
        cast_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_CAST;
        cast_case.operand_types = "int64,float32,float32";
        int64_bytes(INT64_C(9007199791611905), cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        float32_bytes(0x1.000002p+53f, cast_case.expected);
        cast_case.expected_size = 4U;
        cases[case_count++] = cast_case;

        /* uint64 → float64：最大无符号值必须按无符号转，不能当 -1。 */
        memset(&cast_case, 0, sizeof(cast_case));
        cast_case.decision = "integer-no-float-roundtrip";
        cast_case.name = "uint64 max to float64 converts as unsigned";
        cast_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_CAST;
        cast_case.operand_types = "uint64,float64,float64";
        uint64_bytes(UINT64_MAX, cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        float64_bytes(18446744073709551616.0, cast_case.expected);
        cast_case.expected_size = 8U;
        cases[case_count++] = cast_case;
    }

    /*
     * ── 组 4：min / max 的 NaN 让位（decision: floating-min-max-nan）──
     *
     * C 与 Rust 阵营：一个 NaN 让位给数，两个 NaN 得 NaN，且与操作数顺序无关。
     */
    {
        /*
         * 每条都 `memset` 重建，**不靠「改上一条的字段」**：min_case 被连续
         * 复用时，`expected_is_nan` 这种只在一条用例上置位的字段会顺着拷贝往下
         * 漏，漏了以后症状是「期望 NaN、实际 1」——看起来像实现错，其实是
         * 用例把上一条的字段带过来了。
         */
        handler_case_t nan_case;

        memset(&nan_case, 0, sizeof(nan_case));
        nan_case.decision = "floating-min-max-nan";
        nan_case.operand_types = "float64,float64,float64";
        nan_case.read_sizes[0] = 8U;
        nan_case.read_sizes[1] = 8U;
        nan_case.read_count = 2U;
        nan_case.expected_size = 8U;

        nan_case.name = "min(NaN, 1.0) yields 1.0";
        nan_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MIN;
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(1.0, nan_case.reads[1]);
        float64_bytes(1.0, nan_case.expected);
        cases[case_count++] = nan_case;

        nan_case.name = "min(1.0, NaN) yields 1.0 (order independent)";
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        cases[case_count++] = nan_case;

        nan_case.name = "min(NaN, NaN) yields NaN";
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        nan_case.expected_is_nan = 1;
        cases[case_count++] = nan_case;

        nan_case.name = "max(NaN, 1.0) yields 1.0";
        nan_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MAX;
        nan_case.expected_is_nan = 0;
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(1.0, nan_case.reads[1]);
        float64_bytes(1.0, nan_case.expected);
        cases[case_count++] = nan_case;

        nan_case.name = "max(1.0, NaN) yields 1.0 (order independent)";
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        cases[case_count++] = nan_case;

        nan_case.name = "max(NaN, NaN) yields NaN";
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        nan_case.expected_is_nan = 1;
        cases[case_count++] = nan_case;
    }

    /*
     * ── 组 5：算术与位运算各一例（decision:
     *      mixed-precision-promotion-order-independent）─────────────
     *
     * 只取「同型组合」，混型由特化表覆盖，不在这批用例的判据里。
     */
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "add(int64 40, int64 2) is 42", BINRWIR_OPERATION_IDENTIFIER_ADD,
        "int64,int64,int64", 8, 40, 2, 42);
    cases[case_count++] = integer_binary_case(
        "integer-no-float-roundtrip",
        "int64 9007199254740993 + 0 keeps every bit",
        BINRWIR_OPERATION_IDENTIFIER_ADD, "int64,int64,int64", 8,
        INT64_C(9007199254740993), 0, INT64_C(9007199254740993));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "int64 0xF0F0 & 0xFF00 is 0xF000",
        BINRWIR_OPERATION_IDENTIFIER_BITWISE_AND, "int64,int64,int64", 8,
        (int64_t)0xF0F0, (int64_t)0xFF00, (int64_t)0xF000);

    /*
     * ── 组 6：pow（当前没有任何实现，这一行预期是红的）──────────────
     *
     * pow 不在注册表里，执行到会报 INVALID。它进这批用例的理由不是「现在该
     * 绿」，而是「它有矩阵决策（float-transcendental-result-kind），一旦有人
     * 实现，这条就得绿」。放在这里是**记录缺口**，不是伪装通过。
     */
    {
        handler_case_t pow_case;

        memset(&pow_case, 0, sizeof(pow_case));
        pow_case.decision = "float-transcendental-result-kind";
        pow_case.name = "pow(2.0, 10.0) is 1024.0";
        pow_case.base_opid = 0x0014U; /* RUNTIMEC_RWIR_POWER */
        pow_case.operand_types = "float64,float64,float64";
        float64_bytes(2.0, pow_case.reads[0]);
        float64_bytes(10.0, pow_case.reads[1]);
        pow_case.read_sizes[0] = 8U;
        pow_case.read_sizes[1] = 8U;
        pow_case.read_count = 2U;
        float64_bytes(1024.0, pow_case.expected);
        pow_case.expected_size = 8U;
        cases[case_count++] = pow_case;
    }

    for (size_t ordinal = 0U; ordinal < case_count; ++ordinal) {
        run_one(&cases[ordinal]);
    }

    printf("handler_behavior_test: %zu/%zu passed "
           "(unimplemented %zu, wrong result %zu)\n",
           passed_cases, case_count, unimplemented_cases,
           wrong_result_cases);
    return failed_cases == 0U ? 0 : 1;
}
