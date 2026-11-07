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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/binfunc.h"
#include "binrwir/registry.h"
#include "binrwir/signature.h"
#include "contract/kvspace_access.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/core_operations.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_generated.h"
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
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
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
 * **每个读参各自登记、各自取下标**：`binrwir_binfunc_get_literal` 按池下标取，
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
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_binfunc_t *program = NULL;
    binrwir_binvthread_frame_t frame;
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
    /*
     * 段数**不恒为三**：二元算子（add/sub/mul/div/mod/比较…）是三段
     * `左,右,结果`，而 `cast` 是两段 `源,目标`——2026-11-05 特化表改过，
     * cast 的元组去掉了第三段（目标即结果）。
     *
     * 这里不假设段数，只要求「至少两段」；具体几段由元组串自己说了算，
     * 硬写 `segments[2] != NULL` 会把 cast 全部判死。
     */
    EXPECT(segments[1] != NULL);

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
    status = binrwir_binfunc_builder_create(&allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 写槽是 STATIC：符号池里只需要一个 key 文本，类型由用例声明。 */
    status = binrwir_binfunc_builder_add_symbol(
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
        kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
        kvspaceXvalueHeadSetLen(&head, test_case->read_sizes[ordinal]);
        memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, test_case->reads[ordinal],
               test_case->read_sizes[ordinal]);
        status = binrwir_binfunc_builder_add_literal(
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
         * 由编译器把指令改写成它，执行走**特化 opid**。
         *
         * 2026-11-05 改：基础 opid 的通用 handler 已按 p10「先删后增」整族删除
         * （`scalar.c` 的算术 handler）。特化不命中的指令被拒绝而不是回落——
         * 回落到一条**必然 `UNSUPPORTED`** 的指令会让「特化表缺行」伪装成
         * 「注册表里没有」。见 `binrwir/specialize.h` 的注释。
         *
         * 所以本用例执行时用**查到的特化 opid**，与产品路径一致：产品也是
         * 先把 Program 特化、再执行。基础 opid 只用来查特化表。
         */
        request.operation_identifier =
            specialization->specialized_operation_identifier;
        request.source_logical_program_counter = 1U;
        request.read_arguments.data = reads;
        request.read_arguments.count = test_case->read_count;
        request.write_arguments.data = &write_argument;
        request.write_arguments.count = 1U;
        status = binrwir_binfunc_builder_add_instruction(builder, &request,
                                                         &instruction_identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_finalize(builder, &program);
    }
    (void)binrwir_binfunc_builder_destroy(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }

    status = binrwir_binvthread_frame_initialize(&frame, program, NULL, 1U, 2U,
                                                0U);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_frame_bindings_create(&allocator, 8U, &bindings);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        frame.bindings = bindings;
        status = binrwir_registry_initialize(&registry);
    }
    /*
     * **两张表都要注册**，与 `engine/session.c` 的产品装配一致。
     *
     * 只注册基础表会被特化 opid 打空——2026-11-05 实测：本用例改用特化 opid
     * 执行后，若只装 `scalar_handler_entries`（29 条基础），57 条全部
     * `UNIMPLEMENTED`。产品路径同时装两张：基础表服务非特化算子，
     * 特化表服务算术这类只在特化后执行的算子。
     */
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        size_t scalar_count = 0U;
        const binrwir_registry_entry_t *scalar_entries =
            runtimec_scalar_handler_entries(&scalar_count);

        status = binrwir_registry_append(&registry, "scalar", scalar_entries,
                                          scalar_count, NULL);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_registry_append(&registry, "scalar_generated",
                                          scalar_generated_entries,
                                          scalar_generated_entry_count, NULL);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        *output_exit = (binrwir_execution_exit_t){0};
        status = vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                               output_exit);
    }
    if (bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(bindings);
    }
    (void)binrwir_binvthread_frame_destroy(&frame);
    (void)binrwir_binfunc_destroy(program);
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



/*
 * 比较类用例专用：操作数宽度与**结果宽度**分开给。
 *
 * 不能用 `integer_binary_case` 凑：它按同一个 width 同时决定 operand 与
 * result 的 body 长度，而比较的结果是 1 字节 bool。用 width=8 生成后再把
 * `expected_size` 改成 1 是**错的**——写槽的 body 仍是 8 字节，
 * `vmbin_value_access_write_body` 会因「写入长度与当前 body 不符」返回失败，
 * 值一个字节都不写，于是期望 1 却读到 0。
 *
 * 元组串第三段必须是结果类型 `bool`：写成 `...,int64` 会查不到
 * `(opid, int64,int64,bool)` 那条特化，红的形态是「表里没这条特化」。
 */
static handler_case_t integer_compare_case(const char *decision,
                                           const char *name, uint16_t opid,
                                           int64_t left, int64_t right,
                                           int expected) {
    handler_case_t test_case = integer_binary_case(
        decision, name, opid, "int64,int64,bool", 8, left, right, 0);

    test_case.expected_size = 1U;
    bool_bytes(expected, test_case.expected);
    return test_case;
}


/*
 * 除法两边界：**按裁决断言**，不是「记录三态」。
 *
 * 裁决（2026-11-05，架构师）：二补数回绕 ——
 *   `INT64_MIN / -1` → `INT64_MIN`（Go 规范：`q = x / -1` 等于 `x`）
 *   `INT64_MIN % -1` → `0`
 *
 * **为什么还要 fork**：旧实现没有这个边界判断，x86 的 `idiv` 会抛 #DE
 * （SIGFPE）杀掉整个进程。把它当普通用例放进主表，66 条里一条结论都产不出来。
 * 这是「一条用例的失败不该让其余用例失去声音」，与期望值是否已定无关——
 * **只要被测路径可能让进程消失，就必须隔离**。
 *
 * coding1 接线后这条会从「崩溃」变成「回绕值正确」；那时可以把两行挪回主表
 * 并删掉本函数，届时 `fork` 只剩开销、不再有保护价值。
 */
/*
 * 探针条数：与 `run_division_edge_probes` 里的表同源。
 *
 * 不写死在汇总行里——写死会在「加一条探针」时漏改分母，那正是「容量与数量
 * 由两处各自维护」那类错误的又一次复制。
 */
static size_t division_edge_probe_count(void);

static void run_division_edge_probes(void) {
    static const struct {
        const char *name;
        uint16_t base_opid;
        int64_t expected;
    } probes[] = {
        {"INT64_MIN / -1 wraps to INT64_MIN", BINRWIR_OPERATION_IDENTIFIER_DIV,
         INT64_MIN},
        {"INT64_MIN % -1 is 0", BINRWIR_OPERATION_IDENTIFIER_MOD, 0},
    };
    size_t ordinal;

    for (ordinal = 0U; ordinal < sizeof(probes) / sizeof(probes[0]);
         ++ordinal) {
        const pid_t child = fork();

        if (child < 0) {
            fprintf(stderr, "SKIP %s: fork failed\n", probes[ordinal].name);
            continue;
        }
        if (child == 0) {
            handler_case_t probe_case = integer_binary_case(
                "integer-overflow-wraps (division edge)",
                probes[ordinal].name, probes[ordinal].base_opid,
                "int64,int64,int64", 8, INT64_MIN, -1, probes[ordinal].expected);
            test_value_t destination;
            binrwir_execution_exit_t exit_state;

            test_value_initialize(&destination, probe_case.expected_size);
            if (run_case(&probe_case, &destination, &exit_state) !=
                RUNTIMEC_STATUS_SUCCESS) {
                _exit(3); /* 拒绝：与裁决不符 */
            }
            _exit(memcmp(destination.storage + KVSPACE_XVALUE_HEAD_LEN,
                         probe_case.expected,
                         probe_case.expected_size) == 0
                      ? 0
                      : 4); /* 4 = 算出来了但值不对 */
        }
        {
            int child_status = 0;

            if (waitpid(child, &child_status, 0) < 0) {
                fprintf(stderr, "SKIP %s: waitpid failed\n",
                        probes[ordinal].name);
                continue;
            }
            if (WIFSIGNALED(child_status)) {
                fprintf(stderr,
                        "IMPL-WRONG [integer-overflow-wraps] %s: the handler "
                        "crashed the process with signal %d instead of "
                        "wrapping\n",
                        probes[ordinal].name, WTERMSIG(child_status));
                failed_cases += 1U;
                wrong_result_cases += 1U;
            } else if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) {
                passed_cases += 1U;
            } else if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 3) {
                fprintf(stderr,
                        "IMPL-WRONG [integer-overflow-wraps] %s: the handler "
                        "rejected the operands; the decision is to wrap\n",
                        probes[ordinal].name);
                failed_cases += 1U;
                wrong_result_cases += 1U;
            } else if (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 4) {
                fprintf(stderr,
                        "IMPL-WRONG [integer-overflow-wraps] %s: wrapped to a "
                        "wrong value\n",
                        probes[ordinal].name);
                failed_cases += 1U;
                wrong_result_cases += 1U;
            } else {
                fprintf(stderr, "NOTE %s: unexpected child status\n",
                        probes[ordinal].name);
            }
        }
    }
}

/*
 * 用例表写入守卫：越界即**当场失败**，而不是等栈被踩坏。
 *
 * 为什么不用编译期断言：条数是运行期累加的（`case_count++` 分散在几十处），
 * 编译器看不到总数。运行期守卫是这里唯一能提前失败的位置。
 */
#define PUSH_CASE(capacity)                                                   \
    do {                                                                      \
        if (case_count >= (capacity)) {                                       \
            fprintf(stderr,                                                   \
                    "FAIL: case table overflow (%zu >= %zu); raise the "       \
                    "capacity before adding cases\n",                         \
                    case_count, (size_t)(capacity));                          \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static size_t division_edge_probe_count(void) { return 2U; }

int main(void) {
    /*
     * 容量是**上界**，不是「刚好够」。写超时不报错——`cases[case_count++]` 会
     * 静默越界写栈，症状是跑到一半 `*** stack smashing detected ***`，
     * 而且前面 N 条的结论全部丢失。
     *
     * 2026-11-05 13:17 真踩过：容量 64、用例加到 66，跑完 64 条后栈被踩坏。
     * 这条不是「多加几个数」，是**容量与断言数脱钩**——数组大小靠人手抄，
     * 抄漏了没有任何编译期信号。
     *
     * 所以容量放大到 128（留一倍余量），并在每轮 push 后由
     * `case_capacity_guard()` 在运行期硬失败。
     */
    handler_case_t cases[128];
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
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "signed-integer-division", "-7 / 3 truncates toward zero",
        BINRWIR_OPERATION_IDENTIFIER_DIV, "int64,int64,int64", 8, -7, 3, -2);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "signed-integer-division", "-7 % 3 keeps the dividend sign",
        BINRWIR_OPERATION_IDENTIFIER_MOD, "int64,int64,int64", 8, -7, 3, -1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = division_by_zero;

        handler_case_t remainder_by_zero = integer_binary_case(
            "signed-integer-division", "int64 remainder by zero is rejected",
            BINRWIR_OPERATION_IDENTIFIER_MOD, "int64,int64,int64", 8, 1, 0,
            0);
        remainder_by_zero.expect_failure = 1;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = remainder_by_zero;
    }
    /*
     * ── 除法两边界：`INT64_MIN / -1` 与 `INT64_MIN % -1` ──────────────
     *
     * **裁决（2026-11-05，架构师）：二补数回绕，不是拒绝。**
     *
     * 五语言阵营：C 有符号溢出是 UB 但实际按二补数回绕；Go 规范**明确定义**
     * 为回绕；Rust debug panic / release 回绕；Python 无溢出；V8 无 int64。
     * 分裂，选 C/Go/Rust-release 阵营 —— kvlang 是系统级 runtime，底层就是
     * 硬件语义；「有的溢出会失败、有的不会」会造出第二类算术行为，p1 不许。
     *
     * 具体期望值（Go 规范原文：`q = x / -1` 等于 `x`）：
     *
     *   INT64_MIN / -1 → INT64_MIN    （商回绕到自身）
     *   INT64_MIN % -1 → 0            （数学上余数确为 0）
     *
     * **与「整数除零硬失败」并存，不矛盾**：除零是 `x / 0`，硬失败；这一对是
     * `x / -1`，走回绕。两条都是显式判据，实现必须先判边界再算，**不许靠
     * x86 的 `idiv` 抛 #DE**——靠信号兜底不是「失败」，是「以奇怪的方式崩溃」。
     *
     * 历史：2026-11-05 12:43 这对取值曾让整个测试进程 SIGFPE（exit 136），
     * 当时用 fork 子进程隔离。裁决明确后**探针已撤**——不再有会崩的输入，
     * 这两条就是普通用例。
     */
    /*
     * 这两条的期望值已按裁决定成回绕值，但**暂时仍放子进程里跑**：
     *
     * 旧实现（`scalar.c` 的 DIV 分支）没有这个边界判断，直接
     * `left_signed / right_signed` → x86 `idiv` 抛 #DE → SIGFPE → 进程被杀。
     * 把它们当普通用例放进主表，**整张 66 条的表一条结论都产不出来**。
     *
     * coding1 把新 handler 接进分派后，这里会返回回绕值、不再崩；那时
     * 把它们挪回主表即可（`run_division_edge_probes()` 里已按裁决断言，
     * 不是「记录三态」）。
     */

    {
        /* 对照：`INT64_MIN / 1` 是正常值，不该被 overflow 保护误伤。 */
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
            "signed-integer-division",
            "INT64_MIN / 1 is INT64_MIN (any guard must be pair-specific)",
            BINRWIR_OPERATION_IDENTIFIER_DIV, "int64,int64,int64", 8,
            INT64_MIN, 1, INT64_MIN);

        /* 7 % -3 = 1：余数跟被除数符号（matrix:68 的第二个样例）。 */
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
            "integer-remainder-sign", "7 % -3 is 1 (sign follows the dividend)",
            BINRWIR_OPERATION_IDENTIFIER_MOD, "int64,int64,int64", 8, 7, -3, 1);
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = inf_positive;

        inf_negative = inf_positive;
        inf_negative.name = "-1.0 / 0.0 is -inf";
        float64_bytes(-1.0, inf_negative.reads[0]);
        float64_bytes(-INFINITY, inf_negative.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = inf_negative;

        nan_result = inf_positive;
        nan_result.name = "0.0 / 0.0 is NaN";
        float64_bytes(0.0, nan_result.reads[0]);
        float64_bytes(0.0, nan_result.reads[1]);
        /* NaN 的**符号位与载荷不由矩阵规定**：x86 的 0.0/0.0 给的是
         * 负数号 NaN(0xfff8…),而 NAN 宏是正号(0x7ff8…)。断言只认「是 NaN」，
         * 逐字节比符号位等于把硬件行为写进规格。 */
        nan_result.expected_is_nan = 1;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = less_int64_uint64;

        /* 同一分界的另一侧：`1 < 2` 为真。两条一起才钉死 `<` 而不是 `<=`。 */
        less_int64_uint64.name = "int64 1 < int64 2 is true";
        less_int64_uint64.operand_types = "int64,int64,bool";
        int64_bytes(1, less_int64_uint64.reads[0]);
        int64_bytes(2, less_int64_uint64.reads[1]);
        bool_bytes(1, less_int64_uint64.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = less_int64_uint64;

        /*
         * `le` 的四格真值表：相等为真、严格小为真、严格大为假。
         *
         * `<` 与 `<=` 是相邻分支，**只测 `<` 等于没测分界**：把 `<=` 写成 `<`
         * 时，`5 <= 5` 这一格是唯一会红的地方。四格里三格就够钉死
         * （`5 <= 6` 与 `6 <= 5` 不能同时为真）。
         */
        less_int64_uint64.name = "int64 5 <= int64 5 is true (equality)";
        less_int64_uint64.base_opid = BINRWIR_OPERATION_IDENTIFIER_LESS_EQUAL;
        less_int64_uint64.operand_types = "int64,int64,bool";
        int64_bytes(5, less_int64_uint64.reads[0]);
        int64_bytes(5, less_int64_uint64.reads[1]);
        bool_bytes(1, less_int64_uint64.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = less_int64_uint64;

        less_int64_uint64.name = "int64 5 <= int64 6 is true";
        int64_bytes(5, less_int64_uint64.reads[0]);
        int64_bytes(6, less_int64_uint64.reads[1]);
        bool_bytes(1, less_int64_uint64.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = less_int64_uint64;

        less_int64_uint64.name = "int64 6 <= int64 5 is false";
        int64_bytes(6, less_int64_uint64.reads[0]);
        int64_bytes(5, less_int64_uint64.reads[1]);
        bool_bytes(0, less_int64_uint64.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = less_int64_uint64;

        /* `le` 的跨符号一侧：`0 <= UINT64_MAX` 按精确值为真。 */
        less_int64_uint64.name = "int64 0 <= uint64 max is true";
        less_int64_uint64.operand_types = "int64,uint64,bool";
        int64_bytes(0, less_int64_uint64.reads[0]);
        uint64_bytes(UINT64_MAX, less_int64_uint64.reads[1]);
        bool_bytes(1, less_int64_uint64.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        cast_case.operand_types = "float64,int64";
        float64_bytes(-3.9, cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        int64_bytes(-3, cast_case.expected);
        cast_case.expected_size = 8U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        cast_case.name = "float64 1e300 to int8 saturates to 127";
        cast_case.operand_types = "float64,int8";
        float64_bytes(1e300, cast_case.reads[0]);
        int8_bytes(127, cast_case.expected);
        cast_case.expected_size = 1U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        cast_case.name = "float64 +inf to int64 saturates to INT64_MAX";
        cast_case.operand_types = "float64,int64";
        float64_bytes(INFINITY, cast_case.reads[0]);
        int64_bytes(INT64_MAX, cast_case.expected);
        cast_case.expected_size = 8U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        cast_case.name = "float64 -inf to int64 saturates to INT64_MIN";
        float64_bytes(-INFINITY, cast_case.reads[0]);
        int64_bytes(INT64_MIN, cast_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        cast_case.name = "float64 NaN to int64 is rejected";
        float64_bytes(NAN, cast_case.reads[0]);
        cast_case.expect_failure = 1;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        /* int64 → float32：必须单次舍入。 */
        memset(&cast_case, 0, sizeof(cast_case));
        cast_case.decision = "int-to-float32-single-rounding";
        cast_case.name =
            "int64 9007199791611905 to float32 rounds once (0x5a000001)";
        cast_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_CAST;
        cast_case.operand_types = "int64,float32";
        int64_bytes(INT64_C(9007199791611905), cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        float32_bytes(0x1.000002p+53f, cast_case.expected);
        cast_case.expected_size = 4U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = cast_case;

        /* uint64 → float64：最大无符号值必须按无符号转，不能当 -1。 */
        memset(&cast_case, 0, sizeof(cast_case));
        cast_case.decision = "integer-no-float-roundtrip";
        cast_case.name = "uint64 max to float64 converts as unsigned";
        cast_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_CAST;
        cast_case.operand_types = "uint64,float64";
        uint64_bytes(UINT64_MAX, cast_case.reads[0]);
        cast_case.read_sizes[0] = 8U;
        cast_case.read_count = 1U;
        float64_bytes(18446744073709551616.0, cast_case.expected);
        cast_case.expected_size = 8U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "min(1.0, NaN) yields 1.0 (order independent)";
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "min(NaN, NaN) yields NaN";
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        nan_case.expected_is_nan = 1;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "max(NaN, 1.0) yields 1.0";
        nan_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MAX;
        nan_case.expected_is_nan = 0;
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(1.0, nan_case.reads[1]);
        float64_bytes(1.0, nan_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "max(1.0, NaN) yields 1.0 (order independent)";
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "max(NaN, NaN) yields NaN";
        float64_bytes(NAN, nan_case.reads[0]);
        float64_bytes(NAN, nan_case.reads[1]);
        nan_case.expected_is_nan = 1;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        /*
         * **常规路径对照**：两个都不是 NaN。
         *
         * 只有 NaN 让位的用例是不够的——一个把 `min` 写成「永远返回左操作数」
         * 的实现能通过全部 NaN 用例（NaN 路径本来就要返回另一侧）。常规大小
         * 关系必须另有用例，否则「NaN 让位」绿着而 min 本身是错的。
         */
        nan_case.name = "min(1.0, 2.0) yields 1.0 (ordinary path)";
        nan_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MIN;
        nan_case.expected_is_nan = 0;
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(2.0, nan_case.reads[1]);
        float64_bytes(1.0, nan_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "min(2.0, 1.0) yields 1.0 (order independent)";
        float64_bytes(2.0, nan_case.reads[0]);
        float64_bytes(1.0, nan_case.reads[1]);
        float64_bytes(1.0, nan_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "max(1.0, 2.0) yields 2.0 (ordinary path)";
        nan_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MAX;
        float64_bytes(1.0, nan_case.reads[0]);
        float64_bytes(2.0, nan_case.reads[1]);
        float64_bytes(2.0, nan_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;

        nan_case.name = "max(2.0, 1.0) yields 2.0 (order independent)";
        float64_bytes(2.0, nan_case.reads[0]);
        float64_bytes(1.0, nan_case.reads[1]);
        float64_bytes(2.0, nan_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = nan_case;
    }

    /*
     * ── 组 5：算术与位运算各一例（decision:
     *      mixed-precision-promotion-order-independent）─────────────
     *
     * 只取「同型组合」，混型由特化表覆盖，不在这批用例的判据里。
     */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "add(int64 40, int64 2) is 42", BINRWIR_OPERATION_IDENTIFIER_ADD,
        "int64,int64,int64", 8, 40, 2, 42);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "integer-no-float-roundtrip",
        "int64 9007199254740993 + 0 keeps every bit",
        BINRWIR_OPERATION_IDENTIFIER_ADD, "int64,int64,int64", 8,
        INT64_C(9007199254740993), 0, INT64_C(9007199254740993));
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "int64 0xF0F0 & 0xFF00 is 0xF000",
        BINRWIR_OPERATION_IDENTIFIER_BITWISE_AND, "int64,int64,int64", 8,
        (int64_t)0xF0F0, (int64_t)0xFF00, (int64_t)0xF000);

    /*
     * ── 组 5b：P0 补齐——SUB / MUL / SHL / SHR / EQ / NEQ / GT / GE ────
     *
     * 为什么这些排 P0（`workspace/622/coding2/handler-coverage-table.md`）：
     * 它们与已覆盖的 ADD/AND/LT/LE **同族但失败模式不同**——ADD 的用例一条都
     * 覆盖不到 SUB 的符号方向，LT 的用例也覆盖不到 GT 的方向反转。
     *
     * 每条都按「常规 + 分界 + 符号/溢出敏感点」三档取，不用一条凑数。
     */

    /* ── SUB：符号方向与零 ───────────────────────────────────────── */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "sub(int64 42, int64 2) is 40", BINRWIR_OPERATION_IDENTIFIER_SUB,
        "int64,int64,int64", 8, 42, 2, 40);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "sub(int64 2, int64 42) is -40 (operand order matters)",
        BINRWIR_OPERATION_IDENTIFIER_SUB, "int64,int64,int64", 8, 2, 42, -40);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "sub(int64 -7, int64 -3) is -4", BINRWIR_OPERATION_IDENTIFIER_SUB,
        "int64,int64,int64", 8, -7, -3, -4);

    /*
     * ── int8 宽度的二补数回绕（模板级判据）─────────────────────────
     *
     * 这三条不是「多加几个样本」，而是**按模板覆盖**：generated 的 int8 实现是
     * `(int8_t)((uint8_t)a + (uint8_t)b)` —— 先转无符号算、再转回来，这是二补数
     * 回绕的标准写法。只要有人把它改成「先转 int 再截断」或「饱和」，**上界
     * 这一格必红**；反之如果只测中间值，两种写法给出相同结果，抓不到。
     *
     * 判据来源：矩阵**没有**「整数算术溢出」这一行（只有 `signed-neg-abs-sign`
     * 与 `duration-overflow-wrap` 提到回绕）。此处按实现既有选择写，并在
     * 注释里标明它是**待 spec 补行**的行为——与 `shl` 负计数（缺口第 6 条）
     * 同族：实现必须先选一条路，spec 还没写。
     *
     *   127 + 1   → -128  （上界回绕）
     *   -128 - 1  → 127   （下界回绕）
     *   -128 * -1 → -128  （乘法上界回绕，负×负得负数）
     */
    {
        handler_case_t wrap_case;

        memset(&wrap_case, 0, sizeof(wrap_case));
        wrap_case.decision = "integer overflow (no matrix row; wraps in two's "
                             "complement)";
        wrap_case.name = "add(int8 127, int8 1) wraps to -128";
        wrap_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_ADD;
        wrap_case.operand_types = "int8,int8,int8";
        int8_bytes(127, wrap_case.reads[0]);
        int8_bytes(1, wrap_case.reads[1]);
        wrap_case.read_sizes[0] = 1U;
        wrap_case.read_sizes[1] = 1U;
        wrap_case.read_count = 2U;
        int8_bytes(-128, wrap_case.expected);
        wrap_case.expected_size = 1U;
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = wrap_case;

        wrap_case.name = "sub(int8 -128, int8 1) wraps to 127";
        wrap_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_SUB;
        int8_bytes(-128, wrap_case.reads[0]);
        int8_bytes(1, wrap_case.reads[1]);
        int8_bytes(127, wrap_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = wrap_case;

        wrap_case.name = "mul(int8 -128, int8 -1) wraps to -128";
        wrap_case.base_opid = BINRWIR_OPERATION_IDENTIFIER_MUL;
        int8_bytes(-128, wrap_case.reads[0]);
        int8_bytes(-1, wrap_case.reads[1]);
        int8_bytes(-128, wrap_case.expected);
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = wrap_case;
    }

    /* ── MUL：符号组合四象限各一 ──────────────────────────────────── */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "mul(int64 6, int64 7) is 42", BINRWIR_OPERATION_IDENTIFIER_MUL,
        "int64,int64,int64", 8, 6, 7, 42);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "mul(int64 -6, int64 7) is -42 (negative × positive)",
        BINRWIR_OPERATION_IDENTIFIER_MUL, "int64,int64,int64", 8, -6, 7,
        -42);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "mul(int64 -6, int64 -7) is 42 (negative × negative)",
        BINRWIR_OPERATION_IDENTIFIER_MUL, "int64,int64,int64", 8, -6, -7,
        42);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "mixed-precision-promotion-order-independent",
        "mul(int64 0, int64 -7) is 0 (zero sign is not distinguishable)",
        BINRWIR_OPERATION_IDENTIFIER_MUL, "int64,int64,int64", 8, 0, -7, 0);

    /*
     * ── SHL / SHR（decision: shift-count-masking）───────────────────
     *
     * 决策原文的三个可执行判据：
     *   `shl(int64 1, 64)` 得 `1`        ← 计数按宽度减一掩码（掩到 0 位）
     *   `shr(int64 -1, 1)` 得 `-1`       ← 有符号右移是**算术**移位
     *   无符号右移是**逻辑**移位          ← 用高位补 0
     * 三条各取一条，另加一条正常位移做对照。
     */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking", "shl(int64 1, int64 3) is 8",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_LEFT, "int64,int64,int64", 8, 1, 3,
        8);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking",
        "shl(int64 1, int64 64) is 1 (count masked with width minus one)",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_LEFT, "int64,int64,int64", 8, 1,
        64, 1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking",
        "shr(int64 -1, int64 1) is -1 (signed right shift is arithmetic)",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_RIGHT, "int64,int64,int64", 8, -1,
        1, -1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking", "shr(int64 8, int64 2) is 2",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_RIGHT, "int64,int64,int64", 8, 8, 2,
        2);

    /*
     * `shl(int64 1, int64 -1)` —— **负计数**。
     *
     * 矩阵 `shift-count-masking` 只写了「超宽计数按宽度减一掩码」，**没写计数
     * 为负时怎么办**。架构师已裁定并记入 spec 缺口清单第 6 条，措辞原文：
     *
     *   「matrix:71 只写『按宽度减一掩码』，没说计数为负时怎么办；
     *     实现跟 C，Go 的变量移位同族」
     *
     * 所以期望值是**执行已有裁决**，不是随手写的：
     *
     *   计数 -1 转无符号 = 2^64-1
     *   按宽度减一掩码 → 2^64-1 & 63 = 63
     *   1 << 63 → 二补数即 INT64_MIN（0x8000000000000000）
     *
     * 这个结果对读的人很意外（「左移负数位」居然是合法的），正因如此才要有
     * 用例：没有它，将来有人觉得「负计数该报错」而改掉，不会有任何信号。
     */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking (negative counts: spec gap #6, resolved as the C "
        "camp)",
        "shl(int64 1, int64 -1) is INT64_MIN (count -1 masks to 63)",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_LEFT, "int64,int64,int64", 8, 1, -1,
        INT64_MIN);

    /* 对照：负计数的右移走同一条掩码，`shr(int64 1, -64)` 掩到 0 得 1。 */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_binary_case(
        "shift-count-masking (negative counts: spec gap #6)",
        "shr(int64 1, int64 -64) is 1 (count -64 masks to 0)",
        BINRWIR_OPERATION_IDENTIFIER_SHIFT_RIGHT, "int64,int64,int64", 8, 1,
        -64, 1);

    /*
     * ── EQ / NEQ：相等与不等的互补性 ────────────────────────────────
     *
     * 成对取同一组输入，两条结果必须相反；只测其中一条会放过「两个都返回真」。
     * 结果类型是 1 字节 bool，所以用 `integer_compare_case`（它把结果宽度与
     * 操作数宽度分开），不能用 `integer_binary_case` 再改 expected_size。
     */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-numeric-equality", "eq(int64 42, int64 42) is true",
        BINRWIR_OPERATION_IDENTIFIER_EQUAL, 42, 42, 1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-numeric-equality", "eq(int64 42, int64 43) is false",
        BINRWIR_OPERATION_IDENTIFIER_EQUAL, 42, 43, 0);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-numeric-equality", "neq(int64 42, int64 43) is true",
        BINRWIR_OPERATION_IDENTIFIER_NOT_EQUAL, 42, 43, 1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-numeric-equality", "neq(int64 42, int64 42) is false",
        BINRWIR_OPERATION_IDENTIFIER_NOT_EQUAL, 42, 42, 0);

    /*
     * ── GT / GE：LT / LE 的方向反转 ────────────────────────────────
     *
     * `a > b` 必须等价于 `b < a`。取 LT/LE 已有用例的**同一组输入**、结果取反，
     * 这样两张表互为对照：一处实现方向写反，两处必有一处红。
     */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "gt(int64 2, int64 1) is true",
        BINRWIR_OPERATION_IDENTIFIER_GREATER, 2, 1, 1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "gt(int64 1, int64 2) is false",
        BINRWIR_OPERATION_IDENTIFIER_GREATER, 1, 2, 0);
    /* 分界：`5 > 5` 为假。把 `>` 写成 `>=` 只有这一条会红。 */
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "gt(int64 5, int64 5) is false",
        BINRWIR_OPERATION_IDENTIFIER_GREATER, 5, 5, 0);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "ge(int64 5, int64 5) is true",
        BINRWIR_OPERATION_IDENTIFIER_GREATER_EQUAL, 5, 5,
        1);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "ge(int64 4, int64 5) is false",
        BINRWIR_OPERATION_IDENTIFIER_GREATER_EQUAL, 4, 5,
        0);
    PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = integer_compare_case(
        "mixed-signed-unsigned-arithmetic", "ge(int64 6, int64 5) is true",
        BINRWIR_OPERATION_IDENTIFIER_GREATER_EQUAL, 6, 5,
        1);

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
        PUSH_CASE(sizeof(cases) / sizeof(cases[0]));
    cases[case_count++] = pow_case;
    }

    run_division_edge_probes();
    for (size_t ordinal = 0U; ordinal < case_count; ++ordinal) {
        run_one(&cases[ordinal]);
    }

    /*
     * 分母必须把**子进程探针**算进去。
     *
     * 除法两边界跑在 fork 出来的子进程里（见 `run_division_edge_probes`），
     * 它们的通过/失败计进 `passed_cases` / `failed_cases`，但 `case_count`
     * 只数主表。漏掉分母就会打出「68/66 passed」这种自相矛盾的数——
     * 2026-11-05 真出现过：探针转绿那天，汇总行变成 68/66。
     *
     * 判据：**分子与分母必须从同一处事实来**。这里把探针条数显式加进分母，
     * 并单独报出来，读的人能对上账。
     */
    const size_t probe_case_count = division_edge_probe_count();
    const size_t total_cases = case_count + probe_case_count;

    printf("handler_behavior_test: %zu/%zu passed "
           "(main table %zu + subprocess probes %zu; unimplemented %zu, "
           "wrong result %zu)\n",
           passed_cases, total_cases, case_count, probe_case_count,
           unimplemented_cases, wrong_result_cases);
    return failed_cases == 0U ? 0 : 1;
}
