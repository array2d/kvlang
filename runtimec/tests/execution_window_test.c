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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/argument.h"
#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/signature.h"
#include "binrwir_compiler/compiler.h"
#include "contract/checkpoint.h"
#include "contract/host_services.h"
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

#define TEST_GENERATION UINT64_C(7)
#define TEST_INSTANCE UINT64_C(1)
#define TEST_PROGRAM_GENERATION UINT64_C(11)
#define TEST_BODY_SIZE 8U
#define TEST_TARGET "/lib/route"
#define TEST_DONE_TARGET "/lib/route-done"
#define TEST_MISSING_TARGET "/lib/missing"
#define TEST_ZERO_ARGUMENT_TARGET "/lib/zero"
#define TEST_LOCALS_TARGET "/lib/locals"
#define TEST_VTHREAD "2"
#define TEST_FRAME_PATH "/vthread/2/[1]"
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
    test_frame_slot_t slots[2];
    size_t slot_count;
    size_t resolve_count;
    size_t read_count;
    size_t ensure_frame_count;
    char ensured_frame_path[64];
    char ensured_function_key[64];
    runtimec_status_t ensure_frame_status;
    bool snapshot_generation_override_active;
    /* 局部物化记录：key 顺序、以及按类型回读到的 body 宽度。 */
    char materialized_keys[8][128];
    size_t materialized_key_count;
    uint64_t materialized_int64_width;
    uint64_t materialized_bool_width;
    uint64_t snapshot_generation_override;
    size_t checkpoint_count;
    uint64_t checkpoint_sequence;
    runtimec_status_t checkpoint_outcome;
    int32_t checkpoint_stack_identifier;
    int32_t checkpoint_ordinal;
    char checkpoint_status[16];
    char checkpoint_vthread[32];
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

static runtimec_status_t test_fail(runtimec_error_t *error,
                                   runtimec_status_t status,
                                   const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_HOST, 0,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static void test_copy_text(char *output, size_t capacity,
                           runtimec_string_view_t view) {
    size_t size = view.size;

    if (size > capacity - 1U) {
        size = capacity - 1U;
    }
    if (size != 0U && view.data != NULL) {
        memcpy(output, view.data, size);
    }
    output[size] = '\0';
}

static runtimec_status_t test_source_revision(
    void *context, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error) {
    (void)context;
    (void)function_key;
    (void)error;
    test_context_t *test = context;

    runtimec_source_revision_reset(revision);
    revision->instance_identifier = TEST_INSTANCE;
    revision->generation = test->snapshot_generation_override_active
                               ? test->snapshot_generation_override
                               : TEST_GENERATION;
    revision->schema_version = 1U;
    revision->instruction_set_version = 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t test_resolve_frame_xvalue_address(
    void *context, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    test_context_t *test = context;
    size_t ordinal;

    (void)snapshot;
    (void)error;
    *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    *output_generation = 0U;
    for (ordinal = 0U; ordinal < test->slot_count; ++ordinal) {
        const char *slot_key = test->slots[ordinal].key;
        if (strlen(slot_key) == key.size &&
            memcmp(slot_key, key.data, key.size) == 0) {
            *output_xvalue_address =
                (runtimec_xvalue_address_t)(uintptr_t)test->slots[ordinal]
                    .value->storage;
            *output_generation = TEST_GENERATION;
            test->resolve_count += 1U;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_UNSUPPORTED;
}

/* 未命中路径会走到 vmkv 编码：读能力在这里明确失败，窗口必须失败封闭。 */
static runtimec_status_t test_read(void *context,
                                   const runtimec_snapshot_t *snapshot,
                                   runtimec_key_view_t key,
                                   runtimec_bytes_buffer_t *output,
                                   runtimec_error_t *error) {
    test_context_t *test = context;

    (void)snapshot;
    (void)key;
    (void)output;
    test->read_count += 1U;
    return test_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                     "test read capability is not available");
}

static runtimec_status_t test_read_header(
    void *context, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_host_value_header_t *output, runtimec_error_t *error) {
    (void)context;
    (void)snapshot;
    (void)key;
    (void)output;
    return test_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                     "test read_header capability is not available");
}

static runtimec_status_t test_list(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix, runtimec_host_list_visitor_function_t visitor,
    void *visitor_context, runtimec_error_t *error) {
    (void)context;
    (void)snapshot;
    (void)prefix;
    (void)visitor;
    (void)visitor_context;
    return test_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                     "test list capability is not available");
}

static runtimec_status_t test_ensure_frame(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error) {
    test_context_t *test = context;

    (void)snapshot;
    test->ensure_frame_count += 1U;
    test_copy_text(test->ensured_frame_path, sizeof(test->ensured_frame_path),
                   runtimec_string_view_make(frame_path.data, frame_path.size));
    test_copy_text(test->ensured_function_key,
                   sizeof(test->ensured_function_key), function_key);
    if (test->ensure_frame_status != RUNTIMEC_STATUS_SUCCESS) {
        return test_fail(error, test->ensure_frame_status,
                         "test ensure-frame failure");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 局部物化的替身：记录被要求建槽的 key，并按 langtype 回填约定的宽度。
 * 宽度按规范取值（int64 = 8、bool = 1），不是从别处抄来的表——
 * 这张小表只服务断言，不参与产品逻辑。
 */
static runtimec_status_t test_ensure_zero_value(
    void *context, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_string_view_t langtype, runtimec_error_t *error) {
    test_context_t *test = context;
    uint64_t width = 0U;

    (void)snapshot;
    if (test->materialized_key_count < 8U && key.size < 128U) {
        memcpy(test->materialized_keys[test->materialized_key_count], key.data,
               key.size);
        test->materialized_keys[test->materialized_key_count][key.size] = '\0';
        test->materialized_key_count += 1U;
    }
    if (langtype.size == 5U && memcmp(langtype.data, "int64", 5U) == 0) {
        width = 8U;
        test->materialized_int64_width = 8U;
    } else if (langtype.size == 4U && memcmp(langtype.data, "bool", 4U) == 0) {
        width = 1U;
        test->materialized_bool_width = 1U;
    } else {
        return test_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                         "test ensure-zero-value: unsupported langtype");
    }
    (void)width;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t test_checkpoint(
    void *context, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error) {
    test_context_t *test = context;

    (void)error;
    test->checkpoint_count += 1U;
    test->checkpoint_sequence = checkpoint->sequence;
    test->checkpoint_outcome = checkpoint->outcome;
    test->checkpoint_stack_identifier =
        checkpoint->logical_program_counter.stack_identifier;
    test->checkpoint_ordinal =
        checkpoint->logical_program_counter.source_instruction_ordinal;
    test_copy_text(test->checkpoint_status, sizeof(test->checkpoint_status),
                   checkpoint->persistent_status);
    test_copy_text(test->checkpoint_vthread, sizeof(test->checkpoint_vthread),
                   checkpoint->vthread_identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一个值：值头声明当前长度，值体在存储内可直接读写。 */
static void test_value_initialize(test_value_t *value, uint64_t body_value) {
    kvspaceXvalueHead head;

    memset(value, 0, sizeof(*value));
    memset(&head, 0, sizeof(head));
    head.storetype = KVSPACE_XVALUE_ST_SHORT;
    kvspaceXvalueHeadSetLen(&head, TEST_BODY_SIZE);
    memcpy(value->storage, &head, KVSPACE_XVALUE_HEAD_LEN);
    memcpy(value->storage + KVSPACE_XVALUE_HEAD_LEN, &body_value,
           sizeof(body_value));
}

static uint64_t test_value_body(const test_value_t *value) {
    uint64_t body = 0U;

    memcpy(&body, value->storage + KVSPACE_XVALUE_HEAD_LEN, sizeof(body));
    return body;
}

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
    output_key->snapshot.source_revision.schema_version = 1U;
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
    test_value_t read_value;
    test_value_t write_value;
    runtimec_host_services_t host_services;
    runtimec_session_options_t options;
    runtimec_session_t *session = NULL;
    runtimec_session_runtime_components_t components;
    runtimec_error_t error;
    binrwir_program_t *program = NULL;
    runtimec_string_view_t arguments[2];
    runtimec_execution_window_request_t request;
    runtimec_execution_window_result_t result;
    uint32_t canonical_encoding_revision = 0U;

    memset(&test, 0, sizeof(test));
    test_value_initialize(&read_value, UINT64_C(0x1122334455667788));
    test_value_initialize(&write_value, 0U);
    test.slots[0].key = TEST_READ_KEY;
    test.slots[0].value = &read_value;
    test.slots[1].key = TEST_WRITE_KEY;
    test.slots[1].value = &write_value;
    test.slot_count = 2U;
    test.ensure_frame_status = RUNTIMEC_STATUS_SUCCESS;

    runtimec_error_reset(&error);
    runtimec_host_services_initialize(&host_services);
    host_services.context = &test;
    host_services.allocator = allocator;
    host_services.read = test_read;
    host_services.read_header = test_read_header;
    host_services.list = test_list;
    host_services.source_revision = test_source_revision;
    host_services.resolve_frame_xvalue_address =
        test_resolve_frame_xvalue_address;
    host_services.ensure_frame = test_ensure_frame;
    host_services.ensure_zero_value = test_ensure_zero_value;
    host_services.checkpoint = test_checkpoint;
    EXPECT(runtimec_host_services_is_valid(&host_services));

    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    options.compilation_cache_capacity = 4U;
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    EXPECT(runtimec_session_create(&host_services, &options, &session,
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

    arguments[0] = runtimec_string_view_from_c_string(TEST_READ_KEY);
    arguments[1] = runtimec_string_view_from_c_string(TEST_WRITE_KEY);
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
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_CALL);
    EXPECT(result.execution_exit.resume_logical_program_counter ==
           TEST_RESUME_SOURCE_COUNTER);
    EXPECT(test_value_body(&write_value) == test_value_body(&read_value));
    EXPECT(test.resolve_count == 2U);
    EXPECT(test.checkpoint_count == 1U);
    EXPECT(test.checkpoint_outcome == RUNTIMEC_STATUS_SUCCESS);
    /* pc 是 struct：stackid 取自请求，s0 是可恢复位置。 */
    EXPECT(test.checkpoint_stack_identifier == TEST_STACK_IDENTIFIER);
    EXPECT(test.checkpoint_ordinal == (int32_t)TEST_RESUME_SOURCE_COUNTER);
    EXPECT(strcmp(test.checkpoint_status, "running") == 0);
    EXPECT(strcmp(test.checkpoint_vthread, TEST_VTHREAD) == 0);
    /* 建帧先于绑定：帧路径与函数 key 都必须按请求原样交给 host。 */
    EXPECT(test.ensure_frame_count == 1U);
    EXPECT(strcmp(test.ensured_frame_path, TEST_FRAME_PATH) == 0);
    EXPECT(strcmp(test.ensured_function_key, TEST_TARGET) == 0);

    /* 2. DONE 出口：正常结束也必须提交，位置是刚完成的 RETURN。 */
    request.function_path = runtimec_string_view_from_c_string(TEST_DONE_TARGET);
    request.source_logical_program_counter = TEST_DONE_COPY_SOURCE_COUNTER;
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
    EXPECT(result.execution_exit.logical_program_counter ==
           TEST_RETURN_SOURCE_COUNTER);
    EXPECT(test.checkpoint_count == 2U);
    EXPECT(test.checkpoint_stack_identifier == TEST_STACK_IDENTIFIER);
    EXPECT(test.checkpoint_ordinal == (int32_t)TEST_RETURN_SOURCE_COUNTER);
    EXPECT(strcmp(test.checkpoint_status, "done") == 0);

    /* 2.5 建帧失败：一条指令都不执行，不提交 checkpoint。 */
    {
        const size_t checkpoints_before = test.checkpoint_count;
        const size_t resolves_before = test.resolve_count;

        test.ensure_frame_status = RUNTIMEC_STATUS_ERROR;
        EXPECT(runtimec_execution_window_execute(session, &request, &result,
                                                 &error) !=
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(result.execution_exit.detail_code == 13U);
        EXPECT(test.checkpoint_count == checkpoints_before);
        /* 建帧在形参绑定之前：失败时连一次地址解析都不该发生。 */
        EXPECT(test.resolve_count == resolves_before);
        test.ensure_frame_status = RUNTIMEC_STATUS_SUCCESS;
    }

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
        const size_t checkpoints_before = test.checkpoint_count;

        EXPECT(runtimec_execution_window_execute(session, &zero_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
        /* 执行到了，且提交了 checkpoint。 */
        EXPECT(test.checkpoint_count == checkpoints_before + 1U);
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
        test.materialized_key_count = 0U;

        EXPECT(runtimec_execution_window_execute(session, &locals_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
        /* 两个局部都被物化，且 key 不带 local/ 前缀。 */
        EXPECT(test.materialized_key_count == 2U);
        EXPECT(strcmp(test.materialized_keys[0], TEST_FRAME_PATH "/flag") == 0 ||
               strcmp(test.materialized_keys[1], TEST_FRAME_PATH "/flag") == 0);
        EXPECT(strcmp(test.materialized_keys[0], TEST_FRAME_PATH "/x") == 0 ||
               strcmp(test.materialized_keys[1], TEST_FRAME_PATH "/x") == 0);
        EXPECT(test.materialized_int64_width == 8U);
        EXPECT(test.materialized_bool_width == 1U);

        /* 幂等：同一帧再执行一次，不报重复。 */
        test.materialized_key_count = 0U;
        EXPECT(runtimec_execution_window_execute(session, &locals_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(test.materialized_key_count == 2U);
    }

    /*
     * 2.95 反向：快照世代为 0（host 没给出可用的存储世代）时，**零参数帧同样被拒**。
     *
     * 这条守的不是绑定门，而是快照门：它在 `source revision is not addressable`
     * 处就失败，走不到建绑定。「空绑定也是一次绑定」不等于「没有世代也算绑定」。
     */
    {
        runtimec_execution_window_request_t zero_request = request;
        zero_request.function_path =
            runtimec_string_view_from_c_string(TEST_ZERO_ARGUMENT_TARGET);
        zero_request.arguments = NULL;
        zero_request.argument_count = 0U;
        zero_request.source_logical_program_counter = TEST_FIRST_SOURCE_COUNTER;
        test.snapshot_generation_override_active = true;
        test.snapshot_generation_override = 0U;

        EXPECT(runtimec_execution_window_execute(session, &zero_request,
                                                 &result, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        /* 诊断码 3 = SNAPSHOT_FAILED：证明它死在快照门而不是绑定门。 */
        EXPECT(result.execution_exit.detail_code == 3U);
        test.snapshot_generation_override_active = false;
    }

    /* 3. 未命中：走真实编码，读能力失败 → 窗口失败封闭且不提交。 */
    request.function_path =
        runtimec_string_view_from_c_string(TEST_MISSING_TARGET);
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) !=
           RUNTIMEC_STATUS_SUCCESS);
    /* 诊断码 5 = ENCODE_FAILED：证明真的走进了编码分支。 */
    EXPECT(result.execution_exit.detail_code == 5U);
    EXPECT(test.checkpoint_count == 5U);

    /* 4. 起始 source pc 不属于这份 Program：失败封闭，不执行、不提交。 */
    request.function_path = runtimec_string_view_from_c_string(TEST_TARGET);
    request.source_logical_program_counter = 4242U;
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);
    EXPECT(result.execution_exit.detail_code == 12U);
    EXPECT(test.checkpoint_count == 5U);

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
            EXPECT(result.execution_exit.detail_code == 1U);
        }
    }

    /* 6. 空 frame 路径：局部槽没有父路径。 */
    request.vthread_identifier = runtimec_string_view_from_c_string(TEST_VTHREAD);
    request.frame_path = runtimec_string_view_make(NULL, 0U);
    EXPECT(runtimec_execution_window_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);

    EXPECT(runtimec_session_destroy(session) == RUNTIMEC_STATUS_SUCCESS);
    return failures == 0 ? 0 : 1;
}
