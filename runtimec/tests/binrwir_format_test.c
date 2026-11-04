/*
 * BinRWIR 打印：定义区 + 调用区 + 两个池，逐字节与 golden 比对。
 *
 * golden 是从 `workspace/622/architect/decision-binrwir-print.md` §2 的语法**推导**
 * 出来的，不是把实际输出抄进来——测试是规格，不是镜子。每条期望都在注释里写明
 * 它对应规格的哪一句。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/format.h"
#include "binrwir/program.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,          \
                    __LINE__, #condition);                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

#define TEST_STATIC_BODY_ADDRESS UINT64_C(0x1008)
#define TEST_FOREIGN_OPERATION UINT16_C(0xFFFE)
#define TEST_COPY_OPERATION UINT32_C(0x0001)
#define TEST_ADD_OPERATION UINT32_C(0x0002)
#define TEST_UNKNOWN_OPERATION UINT32_C(0x00F0)

/* ---------------- 分配器 ---------------- */

typedef struct test_allocator_context {
    size_t live_allocations;
} test_allocator_context_t;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    test_allocator_context_t *counters = (test_allocator_context_t *)context;
    size_t effective_alignment =
        alignment < sizeof(void *) ? sizeof(void *) : alignment;
    size_t rounded_size = size;
    void *allocation;

    if (rounded_size % effective_alignment != 0U) {
        rounded_size +=
            effective_alignment - (rounded_size % effective_alignment);
    }
    allocation = aligned_alloc(effective_alignment, rounded_size);
    if (allocation == NULL) {
        return NULL;
    }
    counters->live_allocations += 1U;
    return allocation;
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    test_allocator_context_t *counters = (test_allocator_context_t *)context;

    (void)size;
    (void)alignment;
    if (allocation == NULL) {
        return;
    }
    counters->live_allocations -= 1U;
    free(allocation);
}

static runtimec_allocator_t test_allocator(
    test_allocator_context_t *counters) {
    runtimec_allocator_t allocator = {
        .context = counters,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };

    return allocator;
}

/* ---------------- Program 组装 ---------------- */

typedef struct test_parameter {
    binrwir_parameter_direction_t direction;
    uint32_t argument_index;
    const char *type_name;
} test_parameter_t;

typedef struct test_instruction {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t source_program_counter;
    binrwir_argument_t reads[4];
    size_t read_count;
    binrwir_argument_t writes[2];
    size_t write_count;
} test_instruction_t;

typedef struct test_program_source {
    const test_parameter_t *parameters;
    size_t parameter_count;
    const char *symbols[8];
    const char *symbol_types[8];
    size_t symbol_count;
    const char *literal_types[8];
    const uint8_t *literal_bytes[8];
    size_t literal_sizes[8];
    size_t literal_count;
    const test_instruction_t *instructions;
    size_t instruction_count;
} test_program_source_t;

static runtimec_string_view_t text_view(const char *text) {
    return runtimec_string_view_from_c_string(text);
}

static runtimec_status_t test_program_build(
    const runtimec_allocator_t *allocator, const test_program_source_t *source,
    binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    size_t ordinal;
    runtimec_status_t status;

    /* identity 里的两个 generation 每次都填不同值：打印必须完全不受它们影响。 */
    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 99U;
    identity.program_generation = 12345U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;
    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->parameter_count;
         ++ordinal) {
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;

        status = binrwir_program_builder_add_parameter(
            builder, source->parameters[ordinal].direction,
            source->parameters[ordinal].argument_index,
            text_view(source->parameters[ordinal].type_name), &parameter_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->symbol_count;
         ++ordinal) {
        runtimec_string_view_t symbol_type =
            source->symbol_types[ordinal] != NULL
                ? text_view(source->symbol_types[ordinal])
                : runtimec_string_view_make(NULL, 0U);

        status = binrwir_program_builder_add_symbol(
            builder, text_view(source->symbols[ordinal]), symbol_type,
            &symbol_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->literal_count;
         ++ordinal) {
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

        status = binrwir_program_builder_add_literal(
            builder,
            runtimec_bytes_view_make(source->literal_bytes[ordinal],
                                     source->literal_sizes[ordinal]),
            text_view(source->literal_types[ordinal]), &literal_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->instruction_count;
         ++ordinal) {
        binrwir_instruction_build_request_t request;
        const test_instruction_t *instruction = &source->instructions[ordinal];

        memset(&request, 0, sizeof(request));
        request.operation_identifier = instruction->operation_identifier;
        request.source_logical_program_counter =
            instruction->source_program_counter;
        request.read_arguments.data = instruction->reads;
        request.read_arguments.count = instruction->read_count;
        request.write_arguments.data = instruction->writes;
        request.write_arguments.count = instruction->write_count;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

/* ---------------- 输出收集 ---------------- */

typedef struct test_sink {
    char *text;
    size_t size;
    size_t capacity;
} test_sink_t;

static runtimec_string_view_t test_sink_append(void *context,
                                               runtimec_string_view_t chunk) {
    test_sink_t *sink = (test_sink_t *)context;
    size_t required = sink->size + chunk.size + 1U;

    if (required > sink->capacity) {
        size_t grown = sink->capacity == 0U ? 256U : sink->capacity;
        char *resized;

        while (grown < required) {
            grown *= 2U;
        }
        resized = (char *)realloc(sink->text, grown);
        if (resized == NULL) {
            return runtimec_string_view_make(NULL, 0U);
        }
        sink->text = resized;
        sink->capacity = grown;
    }
    memcpy(sink->text + sink->size, chunk.data, chunk.size);
    sink->size += chunk.size;
    sink->text[sink->size] = '\0';
    return chunk;
}

/* 只认两个算子名：copy` 与 add。其余一律返回 false——打印必须退化成数值。 */
static bool test_resolve_operation(
    binrwir_operation_identifier_t operation_identifier,
    runtimec_string_view_t *output_name, void *context) {
    (void)context;
    *output_name = runtimec_string_view_make(NULL, 0U);
    if (operation_identifier == TEST_COPY_OPERATION) {
        *output_name = text_view("copy");
        return true;
    }
    if (operation_identifier == TEST_ADD_OPERATION) {
        *output_name = text_view("add");
        return true;
    }
    return false;
}

/* 格式化到一个新分配的字符串；调用方负责 free。 */
static char *test_format(const binrwir_program_t *program,
                         const char *function_name,
                         binrwir_operation_name_function resolve) {
    test_sink_t sink = {NULL, 0U, 0U};

    if (binrwir_program_format(program, text_view(function_name), resolve, NULL,
                               test_sink_append, &sink) !=
        RUNTIMEC_STATUS_SUCCESS) {
        free(sink.text);
        return NULL;
    }
    return sink.text;
}

/* ---------------- 用例 ---------------- */

/*
 * 主用例：一个 Program 覆盖全部六种引用种类、两个池、以及「解析不出名字」与
 * `FOREIGN`。
 *
 * 签名（规格 §2.1）：读参 argidx 1；写参 argidx 2。
 * 指令（规格 §2.2，按 identifier 升序）：
 *   [pc 1] copy(7:local[1]) -> (8:local[2])        local 1 = k0, local 2 = k1
 *       （builder 里按登记顺序是 0/1，finalize 按名字字典序重排：
 *        "/v/x" < "k0" < "k1"，所以 k0→1、k1→2、"/v/x"→0。打印看到的是
 *        **重排后**的槽号，这正是规格 §2.3「按下标升序」要暴露的事实。）
 *   [pc 2] foreign()                               FOREIGN 打印 foreign
 *   [pc 3] add(3:literal[0], 4:literal[1]) -> (5:local[2])
 *   [pc 4] opid=0x000000f0(6:argument0)            回调解析不出 → 打印数值
 *   [pc 5] copy(1:argument0) -> (2:result0)        读参/写参引用
 *   [pc 6] copy(9:static[0]) -> (10:local[1])      STATIC 只打印符号下标
 * 池（规格 §2.3）：symbol 按名字字典序；literal 按下标。
 */
static void test_format_matches_derived_golden(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_argument_t arguments[6];
    test_instruction_t instructions[6];
    test_parameter_t parameters[2];
    test_program_source_t source;
    const uint8_t literal_zero[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t literal_one[8] = {0, 1, 0, 0, 0, 0, 0, 0};
    char *formatted = NULL;

    /* 名称按字典序排：/v/x < k0 < k1（'/' 0x2f < 'k' 0x6b），k0 < k1。 */
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "k0";
    source.symbol_types[0] = "int64";
    source.symbols[1] = "k1";
    source.symbol_types[1] = "int64";
    source.symbols[2] = "/v/x";
    source.symbol_types[2] = NULL; /* STATIC 的 key 没有值类型 */
    source.symbol_count = 3U;
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = literal_zero;
    source.literal_sizes[0] = sizeof(literal_zero);
    source.literal_types[1] = "int64";
    source.literal_bytes[1] = literal_one;
    source.literal_sizes[1] = sizeof(literal_one);
    source.literal_count = 2U;

    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    parameters[1].argument_index = 2U;
    parameters[1].type_name = "float64";
    source.parameters = parameters;
    source.parameter_count = 2U;

    memset(instructions, 0, sizeof(instructions));
    /* [pc 1] copy(7:local[0]) -> (8:local[1]) */
    instructions[0].operation_identifier = TEST_COPY_OPERATION;
    instructions[0].source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(7U, BINRWIR_REFERENCE_LOCAL, 0U, 0U,
                                            &arguments[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[0].reads[0] = arguments[0];
    instructions[0].read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(8U, BINRWIR_REFERENCE_LOCAL, 1U, 0U,
                                            &arguments[1]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[0].writes[0] = arguments[1];
    instructions[0].write_count = 1U;
    /* [pc 2] foreign() */
    instructions[1].operation_identifier = TEST_FOREIGN_OPERATION;
    instructions[1].source_program_counter = 2U;
    /* [pc 3] add(3:literal[0], 4:literal[1]) -> (5:local[1]) */
    instructions[2].operation_identifier = TEST_ADD_OPERATION;
    instructions[2].source_program_counter = 3U;
    RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &instructions[2].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(4U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            1U, &instructions[2].reads[1]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[2].read_count = 2U;
    RUNTIMEC_EXPECT(binrwir_argument_create(5U, BINRWIR_REFERENCE_LOCAL, 1U, 0U,
                                            &instructions[2].writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[2].write_count = 1U;
    /* [pc 4] 未知 opid，读参是 argument0 */
    instructions[3].operation_identifier = TEST_UNKNOWN_OPERATION;
    instructions[3].source_program_counter = 4U;
    /* 参数引用的 argidx 必须等于签名里同槽参数的 argidx（读参 slot0 = 1）。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &instructions[3].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[3].read_count = 1U;
    /* [pc 5] copy(1:argument0) -> (2:result0) */
    instructions[4].operation_identifier = TEST_COPY_OPERATION;
    instructions[4].source_program_counter = 5U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &instructions[4].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[4].read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(2U,
                                            BINRWIR_REFERENCE_PARAMETER_WRITE,
                                            0U, 0U,
                                            &instructions[4].writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[4].write_count = 1U;
    /* [pc 6] copy(9:static[2]) -> (10:local[0]) */
    instructions[5].operation_identifier = TEST_COPY_OPERATION;
    instructions[5].source_program_counter = 6U;
    RUNTIMEC_EXPECT(binrwir_argument_create(9U, BINRWIR_REFERENCE_STATIC, 2U,
                                            TEST_STATIC_BODY_ADDRESS,
                                            &instructions[5].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[5].read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(10U, BINRWIR_REFERENCE_LOCAL, 0U,
                                            0U, &instructions[5].writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[5].write_count = 1U;
    source.instructions = instructions;
    source.instruction_count = 6U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    formatted = test_format(program, "test", test_resolve_operation);
    RUNTIMEC_EXPECT(formatted != NULL);
    if (formatted != NULL) {
        const char *golden =
            "rwir test(argument1: int64) -> (result2: float64)\n"
            "\n"
            "[pc 1] copy(7:local[1]) -> (8:local[2])\n"
            "[pc 2] foreign()\n"
            "[pc 3] add(3:literal[0], 4:literal[1]) -> (5:local[2])\n"
            "[pc 4] opid=0x000000f0(1:argument0)\n"
            "[pc 5] copy(1:argument0) -> (2:result0)\n"
            "[pc 6] copy(9:static[0]) -> (10:local[1])\n"
            "\n"
            "symbol[0] /v/x : \n"
            "symbol[1] k0 : int64\n"
            "symbol[2] k1 : int64\n"
            "literal[0] int64 : 01 00 00 00 00 00 00 00\n"
            "literal[1] int64 : 00 01 00 00 00 00 00 00\n";

        if (strcmp(formatted, golden) != 0) {
            fprintf(stderr, "format golden mismatch\n--- actual ---\n%s"
                            "--- expected ---\n%s",
                    formatted, golden);
            ++failures;
        }
        free(formatted);
    }
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 同一 Program 连续打印两次，字节必须相同。 */
static void test_format_is_repeatable(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_argument_t arguments[2];
    test_instruction_t instruction;
    test_program_source_t source;
    char *first = NULL;
    char *second = NULL;

    memset(&source, 0, sizeof(source));
    source.symbols[0] = "k0";
    source.symbol_types[0] = "int64";
    source.symbol_count = 1U;
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = TEST_COPY_OPERATION;
    instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL, 0U, 0U,
                                            &arguments[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instruction.reads[0] = arguments[0];
    instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LOCAL, 0U, 0U,
                                            &arguments[1]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instruction.writes[0] = arguments[1];
    instruction.write_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    first = test_format(program, "repeat", test_resolve_operation);
    second = test_format(program, "repeat", test_resolve_operation);
    RUNTIMEC_EXPECT(first != NULL && second != NULL);
    if (first != NULL && second != NULL) {
        RUNTIMEC_EXPECT(strcmp(first, second) == 0);
    }
    free(first);
    free(second);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 没有名字回调时一律打印 opid，不猜。 */
static void test_format_without_resolver_prints_opids(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    test_instruction_t instruction;
    test_program_source_t source;
    char *formatted = NULL;

    memset(&source, 0, sizeof(source));
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = TEST_COPY_OPERATION;
    instruction.source_program_counter = 7U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    formatted = test_format(program, "", NULL);
    RUNTIMEC_EXPECT(formatted != NULL);
    if (formatted != NULL) {
        const char *golden =
            "rwir () -> ()\n"
            "\n"
            "[pc 7] opid=0x00000001()\n";

        if (strcmp(formatted, golden) != 0) {
            fprintf(stderr, "no-resolver golden mismatch\n--- actual ---\n%s"
                            "--- expected ---\n%s",
                    formatted, golden);
            ++failures;
        }
        free(formatted);
    }
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 缓冲 sink：无论分几段交付，结果与一次性收集相同。 */
static void test_buffer_sink_matches_accumulation(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    test_instruction_t instruction;
    test_program_source_t source;
    binrwir_format_buffer_sink_context_t sink_context;
    runtimec_bytes_buffer_t buffer = {NULL, 0U, 0U};
    char *formatted = NULL;

    memset(&source, 0, sizeof(source));
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = TEST_COPY_OPERATION;
    instruction.source_program_counter = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    sink_context.allocator = allocator;
    sink_context.buffer = &buffer;
    RUNTIMEC_EXPECT(binrwir_program_format(program, text_view("buf"),
                                           test_resolve_operation, NULL,
                                           binrwir_format_buffer_sink,
                                           &sink_context) ==
                    RUNTIMEC_STATUS_SUCCESS);
    formatted = test_format(program, "buf", test_resolve_operation);
    RUNTIMEC_EXPECT(formatted != NULL);
    if (formatted != NULL) {
        RUNTIMEC_EXPECT(buffer.size == strlen(formatted));
        RUNTIMEC_EXPECT(memcmp(buffer.data, formatted, buffer.size) == 0);
        free(formatted);
    }
    if (buffer.data != NULL) {
        (void)test_deallocate(&counters, buffer.data, buffer.capacity,
                              _Alignof(uint8_t));
    }
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * 乱序登记：同一组符号按相反顺序登记，dump 必须逐字节相同。
 *
 * 为什么必须有这条：只有一个符号、或登记顺序恰好等于字典序时，**重排有没有做
 * 都看不出来**——恒绿会藏住重定位错误。符号池按名字字节字典序规范化是**规范**
 * （序列化、跨进程比对、golden 都依赖「同一批符号 → 同一组下标」），不是实现细节。
 *
 * 同时覆盖：重排的是**下标**，不是引用关系——`STATIC` 与 `LOCAL` 重排后仍指向
 * 原来那一条的符号文本。
 */
static void test_symbol_registration_order_is_normalized(bool reversed) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_argument_t local_argument;
    binrwir_argument_t static_argument;
    test_instruction_t instruction;
    test_parameter_t parameters[1];
    test_program_source_t source;
    char *formatted = NULL;
    /* "/v/x"(0x2f) < "k0"(0x6b) < "kz"(0x7a)：字典序与「先登记 k 再登记 /」相反，
     * 所以倒序登记时重排真的发生。重排后 "k0" 恒为 1、"/v/x" 恒为 0。 */
    /* 参数项的 slot 是**登记期下标**（builder 内），finalize 才把它重映射成
     * 规范化下标——encoder 拿到 add_symbol 的返回值直接填进去，这里照做。
     * "k0" 两种顺序下都登记在 1；"/v/x" 正序在 0、倒序在 2。 */
    uint16_t local_slot = 1U;
    uint16_t static_slot = reversed ? 2U : 0U;

    memset(&source, 0, sizeof(source));
    /* 类型**跟着名字走**，不跟位置：否则「按位置赋类型」会让两种登记顺序产生
     * 不同的池内容，测的就不是重排了。只有 LOCAL 引用的 "k0" 需要类型。 */
    if (!reversed) {
        source.symbols[0] = "/v/x";
        source.symbol_types[0] = NULL;
        source.symbols[1] = "k0";
        source.symbol_types[1] = "int64";
        source.symbols[2] = "kz";
        source.symbol_types[2] = NULL;
    } else {
        source.symbols[0] = "kz";
        source.symbol_types[0] = NULL;
        source.symbols[1] = "k0";
        source.symbol_types[1] = "int64";
        source.symbols[2] = "/v/x";
        source.symbol_types[2] = NULL;
    }
    source.symbol_count = 3U;

    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    source.parameters = parameters;
    source.parameter_count = 1U;

    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = TEST_COPY_OPERATION;
    instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL,
                                            local_slot, 0U,
                                            &local_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_STATIC,
                                            static_slot, TEST_STATIC_BODY_ADDRESS,
                                            &static_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instruction.reads[0] = static_argument;
    instruction.reads[1] = local_argument;
    instruction.read_count = 2U;
    RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_LOCAL,
                                            local_slot, 0U,
                                            &instruction.writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instruction.write_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    formatted = test_format(program, "order", test_resolve_operation);
    RUNTIMEC_EXPECT(formatted != NULL);
    if (formatted != NULL) {
        /* 期望值按规范推导：symbol 恒为字典序 /v/x, k0, kz；LOCAL "k0" → 1，
         * STATIC "/v/x" → 0；引用关系不随登记顺序改变。 */
        const char *golden =
            "rwir order() -> (result1: int64)\n"
            "\n"
            "[pc 1] copy(2:static[0], 1:local[1]) -> (3:local[1])\n"
            "\n"
            "symbol[0] /v/x : \n"
            "symbol[1] k0 : int64\n"
            "symbol[2] kz : \n";

        if (strcmp(formatted, golden) != 0) {
            fprintf(stderr, "order golden mismatch (reversed=%d)\n"
                            "--- actual ---\n%s--- expected ---\n%s",
                    reversed ? 1 : 0, formatted, golden);
            ++failures;
        }
        free(formatted);
    }
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * 两种登记顺序必须产出**同一份文本**——这条比「各自等于 golden」更强：
 * 它直接断言重排的结果与登记顺序无关，而不是靠两份 golden 恰好相同。
 *
 * 注意它与 Program 校验的分工：`binrwir_program_validate` 要求池**严格字典升序**，
 * 所以「重排没做」会先被 validate 挡成 INVALID，而不会走到打印。也就是说这条用例
 * 守的是**重排 + 重映射这一对动作的正确性**（下标变了、引用关系没变），不是
 * 「有没有重排」——后者由 validate 守。反向控制时两者会同时红，这是预期的。
 */
static void test_registration_order_does_not_change_output(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *forward_program = NULL;
    binrwir_program_t *reverse_program = NULL;
    char *forward_text = NULL;
    char *reverse_text = NULL;
    size_t ordinal;
    const char *forward_names[3] = {"/v/x", "k0", "kz"};
    const char *reverse_names[3] = {"kz", "k0", "/v/x"};
    const char *forward_types[3] = {NULL, "int64", NULL};
    const char *reverse_types[3] = {NULL, "int64", NULL};

    for (ordinal = 0U; ordinal < 2U; ++ordinal) {
        binrwir_argument_t local_argument;
        binrwir_argument_t static_argument;
        test_instruction_t instruction;
        test_parameter_t parameters[1];
        test_program_source_t source;
        uint16_t local_slot = 1U;
        uint16_t static_slot = ordinal == 0U ? 0U : 2U;
        binrwir_program_t **program =
            ordinal == 0U ? &forward_program : &reverse_program;
        char **formatted = ordinal == 0U ? &forward_text : &reverse_text;

        memset(&source, 0, sizeof(source));
        for (size_t index = 0U; index < 3U; ++index) {
            source.symbols[index] =
                ordinal == 0U ? forward_names[index] : reverse_names[index];
            source.symbol_types[index] =
                ordinal == 0U ? forward_types[index] : reverse_types[index];
        }
        source.symbol_count = 3U;
        parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
        parameters[0].argument_index = 1U;
        parameters[0].type_name = "int64";
        source.parameters = parameters;
        source.parameter_count = 1U;
        memset(&instruction, 0, sizeof(instruction));
        instruction.operation_identifier = TEST_COPY_OPERATION;
        instruction.source_program_counter = 1U;
        RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL,
                                                local_slot, 0U,
                                                &local_argument) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_argument_create(
                            2U, BINRWIR_REFERENCE_STATIC, static_slot,
                            TEST_STATIC_BODY_ADDRESS, &static_argument) ==
                        RUNTIMEC_STATUS_SUCCESS);
        instruction.reads[0] = static_argument;
        instruction.reads[1] = local_argument;
        instruction.read_count = 2U;
        RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_LOCAL,
                                                local_slot, 0U,
                                                &instruction.writes[0]) ==
                        RUNTIMEC_STATUS_SUCCESS);
        instruction.write_count = 1U;
        source.instructions = &instruction;
        source.instruction_count = 1U;
        RUNTIMEC_EXPECT(test_program_build(&allocator, &source, program) ==
                        RUNTIMEC_STATUS_SUCCESS);
        *formatted = test_format(*program, "order", test_resolve_operation);
        RUNTIMEC_EXPECT(*formatted != NULL);
    }
    if (forward_text != NULL && reverse_text != NULL) {
        if (strcmp(forward_text, reverse_text) != 0) {
            fprintf(stderr, "registration order changed the dump\n"
                            "--- forward ---\n%s--- reverse ---\n%s",
                    forward_text, reverse_text);
            ++failures;
        }
    }
    free(forward_text);
    free(reverse_text);
    RUNTIMEC_EXPECT(binrwir_program_destroy(forward_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(reverse_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

int main(void) {
    test_format_matches_derived_golden();
    test_format_is_repeatable();
    test_format_without_resolver_prints_opids();
    test_buffer_sink_matches_accumulation();
    test_symbol_registration_order_is_normalized(false);
    test_symbol_registration_order_is_normalized(true);
    test_registration_order_does_not_change_output();
    if (failures != 0) {
        fprintf(stderr, "binrwir_format_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
