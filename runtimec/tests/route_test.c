/*
 * engine/route 验收：ROUTE_FUNCTION 的完整链路（取快照 → 命中 compilation
 * repository → 读执行目标 → 顶层帧形参绑定 → vmbin 执行 → 提交 checkpoint），
 * 未命中时的编码失败封闭，以及另外三种 kind 的失败封闭。
 *
 * 命中的那条路径由测试自己预置 compilation：Program 用 builder 构造、经
 * BinRWIR Compiler 编译、按 route 会算出的同一把 key 入仓。未命中的那条路径
 * 走真实 vmkv 编码，host 的读能力给的是失败封闭的替身，因此不产生 Program。
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
#include "engine/route.h"
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
#define TEST_VTHREAD "vthread-1"
#define TEST_READ_KEY "/vthread/vthread-1/[0,0]"
#define TEST_WRITE_KEY "/vthread/vthread-1/[0,-1]"
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
    size_t checkpoint_count;
    uint64_t checkpoint_sequence;
    runtimec_status_t checkpoint_outcome;
    char checkpoint_program_counter[64];
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
                       runtimec_string_view_make(NULL, 0U));
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
    runtimec_source_revision_reset(revision);
    revision->instance_identifier = TEST_INSTANCE;
    revision->generation = TEST_GENERATION;
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

/* 未命中路径会走到 vmkv 编码：读能力在这里明确失败，route 必须失败封闭。 */
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

static runtimec_status_t test_checkpoint(
    void *context, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error) {
    test_context_t *test = context;

    (void)error;
    test->checkpoint_count += 1U;
    test->checkpoint_sequence = checkpoint->sequence;
    test->checkpoint_outcome = checkpoint->outcome;
    test_copy_text(test->checkpoint_program_counter,
                   sizeof(test->checkpoint_program_counter),
                   checkpoint->logical_program_counter);
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
    binrwir_program_builder_t *builder, uint32_t operation_identifier,
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
            builder, runtimec_string_view_from_c_string("g"), &symbol_index) !=
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
 * CALL 的后继存在，所以它的 resume source pc 是 5——route 提交一个可恢复位置。
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

/* route 会算出的同一把 key：snapshot + 四个版本号。 */
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

    /* 编译器接管 program：route 不再销毁它。 */
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
    runtimec_route_request_t request;
    runtimec_route_result_t result;
    const runtimec_route_kind_t unsupported_kinds[3] = {
        RUNTIMEC_ROUTE_PROGRAM_COUNTER, RUNTIMEC_ROUTE_VTHREAD,
        RUNTIMEC_ROUTE_BOOTSTRAP};
    const uint32_t unsupported_details[3] = {2U, 3U, 4U};
    uint32_t canonical_encoding_revision = 0U;
    size_t ordinal;

    memset(&test, 0, sizeof(test));
    test_value_initialize(&read_value, UINT64_C(0x1122334455667788));
    test_value_initialize(&write_value, 0U);
    test.slots[0].key = TEST_READ_KEY;
    test.slots[0].value = &read_value;
    test.slots[1].key = TEST_WRITE_KEY;
    test.slots[1].value = &write_value;
    test.slot_count = 2U;

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

    arguments[0] = runtimec_string_view_from_c_string(TEST_READ_KEY);
    arguments[1] = runtimec_string_view_from_c_string(TEST_WRITE_KEY);
    memset(&request, 0, sizeof(request));
    request.kind = RUNTIMEC_ROUTE_FUNCTION;
    request.target = runtimec_string_view_from_c_string(TEST_TARGET);
    request.vthread_identifier =
        runtimec_string_view_from_c_string(TEST_VTHREAD);
    request.arguments = arguments;
    request.argument_count = 2U;

    /* 1. CALL 出口：写参写回、提交一个可恢复位置。 */
    EXPECT(runtimec_route_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_CALL);
    EXPECT(result.execution_exit.resume_logical_program_counter ==
           TEST_RESUME_SOURCE_COUNTER);
    EXPECT(test_value_body(&write_value) == test_value_body(&read_value));
    EXPECT(test.resolve_count == 2U);
    EXPECT(test.checkpoint_count == 1U);
    EXPECT(test.checkpoint_outcome == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(strcmp(test.checkpoint_program_counter,
                  "/lib/route/[5,0]") == 0);
    EXPECT(strcmp(test.checkpoint_status, "running") == 0);
    EXPECT(strcmp(test.checkpoint_vthread, TEST_VTHREAD) == 0);

    /* 2. DONE 出口：正常结束也必须提交，位置是刚完成的 RETURN。 */
    request.target = runtimec_string_view_from_c_string(TEST_DONE_TARGET);
    EXPECT(runtimec_route_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_DONE);
    EXPECT(result.execution_exit.logical_program_counter ==
           TEST_RETURN_SOURCE_COUNTER);
    EXPECT(test.checkpoint_count == 2U);
    EXPECT(strcmp(test.checkpoint_program_counter,
                  "/lib/route-done/[10,0]") == 0);
    EXPECT(strcmp(test.checkpoint_status, "done") == 0);

    /* 3. 未命中：走真实编码，读能力失败 → route 失败封闭且不提交。 */
    request.target = runtimec_string_view_from_c_string(TEST_MISSING_TARGET);
    EXPECT(runtimec_route_execute(session, &request, &result, &error) !=
           RUNTIMEC_STATUS_SUCCESS);
    /* 诊断码 8 = ENCODE_FAILED：证明真的走进了编码分支。 */
    EXPECT(result.execution_exit.detail_code == 8U);
    EXPECT(test.checkpoint_count == 2U);

    /* 4. 另外三种 kind：诊断码各自独立，不绑定、不提交。 */
    request.target = runtimec_string_view_from_c_string(TEST_TARGET);
    for (ordinal = 0U; ordinal < 3U; ++ordinal) {
        request.kind = unsupported_kinds[ordinal];
        EXPECT(runtimec_route_execute(session, &request, &result, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(result.execution_exit.detail_code ==
               unsupported_details[ordinal]);
    }
    EXPECT(test.checkpoint_count == 2U);

    /* 5. 空 vthread：checkpoint 没有落点。 */
    request.kind = RUNTIMEC_ROUTE_FUNCTION;
    request.vthread_identifier = runtimec_string_view_make(NULL, 0U);
    EXPECT(runtimec_route_execute(session, &request, &result, &error) ==
           RUNTIMEC_STATUS_INVALID);

    EXPECT(runtimec_session_destroy(session) == RUNTIMEC_STATUS_SUCCESS);
    return failures == 0 ? 0 : 1;
}
