/* Verify BinRWIR Compiler and its JIT compile-versus-execute boundaries. */

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <kvspace/kvspace.h>

#include "contract/kvspace_access.h"

#include "binrwir_compiler/compiler.h"
#include "binrwir_compiler/jit/compiler.h"
#include "binrwir_compiler/jit/execution_interface.h"
#include "binrwir_compiler/jit/executor.h"
#include "binrwir_compiler/jit/register_allocation.h"
#include "binrwir_compiler/jit/safepoint.h"
#include "binrwir_compiler/optimization.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

#define RUNTIMEC_ASSERT_FUNCTION_TYPE(function_name, function_pointer_type)       \
    _Static_assert(_Generic(&(function_name), function_pointer_type: 1,           \
                            default: 0),                                          \
                   "unexpected declaration for " #function_name)

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                         \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

typedef runtimec_status_t (*binrwir_compile_function_t)(
    binrwir_compiler_t *, binrwir_binfunc_t *,
    binrwir_compilation_t **, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_compiler_revision_function_t)(
    const binrwir_compiler_t *, uint32_t *);
typedef runtimec_status_t (*binrwir_build_execution_plan_function_t)(
    const runtimec_allocator_t *, const binrwir_binfunc_t *, uint64_t,
    uint32_t, binrwir_execution_plan_t **, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_compile_function_t)(
    binrwir_jit_compiler_t *, const binrwir_binfunc_t *,
    const binrwir_execution_plan_t *,
    binrwir_jit_compilation_result_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_execute_function_t)(
    const binrwir_jit_code_t *, binrwir_binvthread_frame_t *,
    binrwir_execution_exit_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_register_allocation_create_function_t)(
    const binrwir_jit_verified_program_t *, const binrwir_execution_plan_t *,
    const binrwir_jit_target_t *, const runtimec_allocator_t *,
    binrwir_jit_register_allocation_t **, binrwir_jit_refusal_t *,
    runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_plan_create_function_t)(
    const binrwir_jit_verified_program_t *,
    const binrwir_jit_register_allocation_t *, const runtimec_allocator_t *,
    binrwir_jit_safepoint_plan_t **, binrwir_jit_refusal_t *,
    runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_commit_function_t)(
    const binrwir_jit_safepoint_plan_t *,
    binrwir_jit_safepoint_boundary_t, uint32_t, uint32_t, void *,
    binrwir_binvthread_frame_t *, runtimec_error_t *);
typedef runtimec_status_t (*binrwir_jit_safepoint_discard_function_t)(
    const binrwir_jit_safepoint_plan_t *,
    binrwir_jit_safepoint_boundary_t, uint32_t, void *,
    binrwir_binvthread_frame_t *, runtimec_error_t *);
typedef runtimec_status_t (*vmkv_encoder_revision_function_t)(
    const vmkv_encoder_t *, uint32_t *);

RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_compile, binrwir_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_get_revision, binrwir_compiler_revision_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_compiler_build_execution_plan,
    binrwir_build_execution_plan_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(binrwir_jit_compile, binrwir_jit_compile_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(binrwir_jit_execute, binrwir_jit_execute_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_register_allocation_create,
    binrwir_jit_register_allocation_create_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_plan_create,
    binrwir_jit_safepoint_plan_create_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_commit_completed_state,
    binrwir_jit_safepoint_commit_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    binrwir_jit_safepoint_discard_incomplete_state,
    binrwir_jit_safepoint_discard_function_t);
RUNTIMEC_ASSERT_FUNCTION_TYPE(
    vmkv_encoder_get_canonical_encoding_revision,
    vmkv_encoder_revision_function_t);

_Static_assert(BINRWIR_EXECUTION_TARGET_INVALID == 0,
               "invalid execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_VMBIN == 1,
               "vmbin execution target changed");
_Static_assert(BINRWIR_EXECUTION_TARGET_JIT == 2,
               "JIT execution target changed");

_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_INVALID == 0,
               "invalid JIT compilation outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_COMPILED == 1,
               "compiled JIT outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE == 2,
               "not-compilable JIT outcome changed");
_Static_assert(BINRWIR_JIT_COMPILATION_OUTCOME_ERROR == 3,
               "error JIT outcome changed");

_Static_assert(sizeof(binrwir_jit_body_origin_t) == 16U,
               "JIT body origins must remain sixteen bytes");
_Static_assert(sizeof(binrwir_jit_location_mapping_t) == 24U,
               "JIT location mappings must remain twenty-four bytes");
_Static_assert(sizeof(binrwir_jit_materialization_entry_t) == 24U,
               "JIT materialization entries must remain twenty-four bytes");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION == 1,
               "instruction-completion boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_PAUSE == 2,
               "pause boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_HANDOFF == 3,
               "handoff boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_CALL == 4,
               "call boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_EXTERNAL_OBSERVATION == 5,
               "external-observation boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_RECOVERY == 6,
               "recovery boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_RESUME == 7,
               "resume boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_GUARD_FAILURE == 8,
               "guard-failure boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR == 9,
               "error boundary changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_ACTION_COMMIT_COMPLETED_STATE == 1,
               "completed-state commit action changed");
_Static_assert(BINRWIR_JIT_SAFEPOINT_ACTION_DISCARD_INCOMPLETE_STATE == 2,
               "incomplete-state discard action changed");
_Static_assert(BINRWIR_JIT_MATERIALIZATION_FLAG_COMPLETED_DIRTY == 1,
               "completed dirty materialization flag changed");
_Static_assert(BINRWIR_JIT_WRITE_ALIAS_ACTION_UPDATE_READ_LOCATION == 1,
               "write alias update action changed");
_Static_assert(BINRWIR_JIT_WRITE_ALIAS_ACTION_INVALIDATE_READ_LOCATION == 2,
               "write alias invalidation action changed");

static int failures;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    return NULL;
}

/* 总线内存分配器：符号池登记需要真实分配，与「vmkv 不分配」的探针区分开。 */
static void *test_allocate_working(void *context, size_t size,
                                   size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void test_deallocate(
    void *context, void *allocation, size_t size, size_t alignment) {
    (void)context;
    (void)allocation;
    (void)size;
    (void)alignment;
}

/*
 * 场景数据：两条 key，一条已分配、一条未分配。判据仍是「编码器把 STATIC 参数
 * 编成 host 给出的值体地址；host 说『尚未分配』(UNSUPPORTED) 时 payload 记 NONE」。
 *
 * 原来用注入的 `resolve_static_xvalue_address` 造这两种返回；现在数据面是**真
 * kvspace**：已分配 = key 真的存在，未分配 = key 真的不存在。判据不变，载体换真。
 */
static bool store_open(void **output_handle) {
    static int sequence = 0;
    char directory[256];
    static char data_source[360];
    /* 目录与 DSN 必须活到进程结束：句柄指向它们。 */
    snprintf(directory, sizeof(directory),
             "/tmp/runtimec-compiler-contracts-%d-%d", (int)getpid(),
             sequence++);
    (void)mkdir(directory, 0700);
    snprintf(data_source, sizeof(data_source), "shm://%s/store.shm", directory);
    *output_handle = kvspaceConnect(data_source);
    return *output_handle != NULL;
}

/*
 * 把 8 字节 int64 写进 key，并读回自检。
 * **必须写完整 wire（16 字节 head + body + attr）**：kvspace 存的是 XValue，
 * 塞裸 8 字节会落进一条没有 head 的缓冲，之后按值头解析就是畸形值。
 */
static bool store_put_int64(void *handle, const char *key) {
    uint8_t body[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack(body, sizeof(body), sizeof(body), "int64",
                                  &wire, &wire_size) != 0 ||
        wire == NULL) {
        return false;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int written = kvspaceSetValue(handle, key, wire, (uint32_t)wire_size,
                                        error_text,
                                        (uint32_t)sizeof(error_text));
    free(wire);
    if (written != 0) {
        fprintf(stderr, "store_put_int64(%s) failed: %s\n", key, error_text);
        return false;
    }
    uint8_t *readback = NULL;
    uint32_t readback_size = 0U;
    return kvspaceGet(handle, key, 1, &readback, &readback_size) == 0 &&
           readback != NULL && readback_size != 0U;
}

static void test_vmkv_xvalue_address_encoding(void) {
    void *handle = NULL;
    if (!store_open(&handle)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    /*
     * `/value` 已分配；`/missing` 不存在，用来造 host 的 UNSUPPORTED（「尚未分配」）。
     */
    if (!store_put_int64(handle, "/value")) {
        fprintf(stderr, "SKIP: cannot seed /value\n");
        kvspaceClose(handle);
        return;
    }

    runtimec_kvspace_access_t *access = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    /* `test_allocate` 是「故意返回 NULL」的失败探针，不能用来建 access。 */
    const runtimec_kvspace_access_request_t access_request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate_working,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1),
    };
    RUNTIMEC_EXPECT(runtimec_kvspace_access_create(&access_request, &access,
                                                   &host_error) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(access != NULL);

    runtimec_snapshot_t snapshot;
    const vmkv_rwir_argument_view_t source_argument = {
        .parameter_type_name = RUNTIMEC_STRING_VIEW_LITERAL("int64"),
        .parameter_name = RUNTIMEC_STRING_VIEW_LITERAL("value"),
        .key = RUNTIMEC_KEY_VIEW_LITERAL("/value"),
        .key_addressability = VMKV_KEY_ADDRESSABILITY_STATIC,
    };
    vmkv_rwir_argument_view_t dynamic_argument = source_argument;
    binrwir_argument_t encoded_argument = {
        .argument_index = UINT32_MAX,
        .reference_kind = BINRWIR_REFERENCE_INVALID,
        .dereference = UINT8_MAX,
        .slot = UINT16_MAX,
        .payload = UINT64_MAX,
    };

    /* request 的有效性判据要求快照里有函数键、源位置与契约版本——原实现用
     * `resolution_context` 注入，绕过了这条；真后端下必须给全。 */
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key = runtimec_key_view_from_c_string("/lib/test");
    snapshot.source_position_text =
        runtimec_string_view_from_c_string("[1,0]");
    const vmkv_encode_request_t request = {
        .kvspace_access = access,
        .function_path = RUNTIMEC_STRING_VIEW_LITERAL("/lib/test"),
        .expected_snapshot = snapshot,
    };
    /* STATIC 的 slot 现在是符号池下标，登记需要一个 builder。 */
    const binrwir_binfunc_identity_t identity = {
        .snapshot_generation = 1U,
        .program_generation = 1U,
        .schema_version = 1U,
        .rwir_manifest_version = 1U,
        .binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision = 1U,
    };
    const runtimec_allocator_t builder_allocator = {
        .context = NULL,
        .allocate = test_allocate_working,
        .deallocate = test_deallocate,
    };
    binrwir_binfunc_builder_t *builder = NULL;
    RUNTIMEC_EXPECT(binrwir_binfunc_builder_create(
                        &builder_allocator, &identity, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* error 必须非 NULL：`kvspace_access` 把 `error == NULL` 判为 INVALID。
     * 原来走注入 resolver，这条校验被绕过了。 */
    runtimec_error_t encode_error;
    runtimec_error_reset(&encode_error);
    RUNTIMEC_EXPECT(vmkv_encode_static_argument(
        &request, builder, &source_argument, 7U, &encoded_argument,
        &encode_error) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(encoded_argument.argument_index == 7U);
    RUNTIMEC_EXPECT(encoded_argument.reference_kind ==
                    BINRWIR_REFERENCE_STATIC);
    RUNTIMEC_EXPECT(encoded_argument.dereference == 0U);
    RUNTIMEC_EXPECT(encoded_argument.slot == 0U);
    /* payload 是推导出的值体地址：**真后端给出的基址** + 头长度，不写死地址。 */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0U;
        RUNTIMEC_EXPECT(kvspaceGet(handle, "/value", 1, &wire, &wire_size) == 0);
        RUNTIMEC_EXPECT(wire != NULL);
        RUNTIMEC_EXPECT(encoded_argument.payload ==
                        (uint64_t)(uintptr_t)wire +
                            RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    }

    /* 每次调用前重建 builder：符号池是累积的，复用会让 slot 断言漂移。 */
    RUNTIMEC_EXPECT(binrwir_binfunc_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_binfunc_builder_create(
                        &builder_allocator, &identity, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    dynamic_argument.key_addressability = VMKV_KEY_ADDRESSABILITY_DYNAMIC;
    RUNTIMEC_EXPECT(vmkv_encode_static_argument(
        &request, builder, &dynamic_argument, 7U, &encoded_argument,
        &encode_error) == RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(encoded_argument.argument_index == 0U);
    RUNTIMEC_EXPECT(encoded_argument.reference_kind ==
                    BINRWIR_REFERENCE_INVALID);
    RUNTIMEC_EXPECT(encoded_argument.dereference == 0U);
    RUNTIMEC_EXPECT(encoded_argument.slot == 0U);
    RUNTIMEC_EXPECT(encoded_argument.payload == BINRWIR_BODY_ADDRESS_NONE);

    /*
     * **2026-11-05 语义变更**：host 报 UNSUPPORTED 的含义是「key 尚未分配」，
     * 不等于「编码失败」——程序自己会创建 key。此时仍登记符号、payload 记 NONE，
     * 地址由执行期按 key 解析。载体换真后这一条是**真·不存在的 key**。
     */
    {
        const vmkv_rwir_argument_view_t missing_argument = {
            .parameter_type_name = RUNTIMEC_STRING_VIEW_LITERAL("int64"),
            .parameter_name = RUNTIMEC_STRING_VIEW_LITERAL("value"),
            .key = RUNTIMEC_KEY_VIEW_LITERAL("/missing"),
            .key_addressability = VMKV_KEY_ADDRESSABILITY_STATIC,
        };
        RUNTIMEC_EXPECT(binrwir_binfunc_builder_destroy(builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_binfunc_builder_create(
                            &builder_allocator, &identity, &builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        encoded_argument = (binrwir_argument_t){
            .argument_index = UINT32_MAX,
            .reference_kind = BINRWIR_REFERENCE_INVALID,
            .dereference = UINT8_MAX,
            .slot = UINT16_MAX,
            .payload = UINT64_MAX,
        };
        const runtimec_status_t missing_status = vmkv_encode_static_argument(
            &request, builder, &missing_argument, 7U, &encoded_argument,
            &encode_error);
        RUNTIMEC_EXPECT(missing_status == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(encoded_argument.argument_index == 7U);
        RUNTIMEC_EXPECT(encoded_argument.reference_kind ==
                        BINRWIR_REFERENCE_STATIC);
        RUNTIMEC_EXPECT(encoded_argument.payload ==
                        BINRWIR_BODY_ADDRESS_NONE);
    }

    /*
     * 地址溢出那条（基址 + 头长度越过 UINT64_MAX）在真后端下**造不出来**：
     * 进程内地址远小于 UINT64_MAX - 16。那是 `runtimec_xvalue_body_address` 的
     * 纯函数性质，已由 `runtimec.contracts` 的单元断言覆盖，不在这里重复。
     */

    RUNTIMEC_EXPECT(binrwir_binfunc_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(runtimec_kvspace_access_destroy(access, &host_error) ==
                    RUNTIMEC_STATUS_SUCCESS);
    kvspaceClose(handle);
}

static void test_compiler_configuration(void) {
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    binrwir_compiler_t *compiler =
        (binrwir_compiler_t *)(uintptr_t)1U;
    binrwir_compiler_configuration_t configuration = {
        .structure_size = sizeof(configuration),
        .compiler_revision = 1U,
        .enable_jit = false,
        .jit_machine_code_capacity = 0U,
        .jit_target = {0},
    };

    /* 这个分配器故意返回 NULL：create 必须失败封闭，不留半初始化对象。 */
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_OUT_OF_MEMORY);
    RUNTIMEC_EXPECT(compiler == NULL);

    /* VMBIN 档是确定能力：正常分配器下 enable_jit 为假必须建得起来。 */
    const runtimec_allocator_t working_allocator = {
        .context = NULL,
        .allocate = test_allocate_working,
        .deallocate = test_deallocate,
    };
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &working_allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(compiler != NULL);
    RUNTIMEC_EXPECT(binrwir_compiler_destroy(compiler, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
    compiler = (binrwir_compiler_t *)(uintptr_t)1U;

    configuration.compiler_revision = 0U;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);
    configuration.compiler_revision = 1U;

    configuration.jit_machine_code_capacity = 1U;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);
    configuration.jit_machine_code_capacity = 0U;

    configuration.enable_jit = true;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler == NULL);

    configuration.jit_machine_code_capacity = 1U;
    configuration.jit_target.architecture = BINRWIR_JIT_ARCHITECTURE_X86_64;
    RUNTIMEC_EXPECT(binrwir_compiler_create(
        &allocator, &configuration, &compiler, NULL) ==
        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(compiler == NULL);
}

static void test_compiler_revision_failure_shape(void) {
    uint32_t compiler_revision = 0U;

    RUNTIMEC_EXPECT(binrwir_compiler_get_revision(
        NULL, &compiler_revision) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compiler_revision == UINT32_MAX);
}

static void test_encoder_revision_failure_shape(void) {
    uint32_t canonical_encoding_revision = 0U;

    RUNTIMEC_EXPECT(vmkv_encoder_get_canonical_encoding_revision(
        NULL, &canonical_encoding_revision) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(canonical_encoding_revision == UINT32_MAX);
}

static void test_compilation_shapes(void) {
    const binrwir_jit_compilation_result_t not_compilable = {
        .outcome = BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = NULL,
        .refusal = {.reason = BINRWIR_JIT_REFUSAL_UNSUPPORTED_PROGRAM},
    };
    binrwir_compilation_view_t view = {
        .execution_target = BINRWIR_EXECUTION_TARGET_JIT,
        .canonical_program = (const binrwir_binfunc_t *)(uintptr_t)1U,
        .execution_plan = (const binrwir_execution_plan_t *)(uintptr_t)1U,
        .binrwir_jit_code = (const binrwir_jit_code_t *)(uintptr_t)1U,
    };

    RUNTIMEC_EXPECT(not_compilable.outcome ==
                    BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE);
    RUNTIMEC_EXPECT(not_compilable.code == NULL);
    RUNTIMEC_EXPECT(binrwir_compilation_get_view(NULL, &view, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.execution_target == BINRWIR_EXECUTION_TARGET_INVALID);
    RUNTIMEC_EXPECT(view.canonical_program == NULL);
    RUNTIMEC_EXPECT(view.execution_plan == NULL);
    RUNTIMEC_EXPECT(view.binrwir_jit_code == NULL);
}

static void test_optimization_failure_shape(void) {
    binrwir_execution_plan_t *execution_plan =
        (binrwir_execution_plan_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_compiler_build_execution_plan(
        NULL, NULL, 0U, 0U, &execution_plan, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_plan == NULL);
}

static void test_jit_failure_shapes(void) {
    binrwir_jit_compilation_result_t compilation_result = {
        .outcome = BINRWIR_JIT_COMPILATION_OUTCOME_NOT_COMPILABLE,
        .code = (binrwir_jit_code_t *)(uintptr_t)1U,
    };
    binrwir_execution_exit_t execution_exit = {
        .kind = BINRWIR_EXECUTION_EXIT_DONE,
        .status = RUNTIMEC_STATUS_SUCCESS,
    };

    RUNTIMEC_EXPECT(binrwir_jit_compile(NULL, NULL, NULL, &compilation_result, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(compilation_result.outcome ==
                    BINRWIR_JIT_COMPILATION_OUTCOME_INVALID);
    RUNTIMEC_EXPECT(compilation_result.code == NULL);

    RUNTIMEC_EXPECT(binrwir_jit_execute(NULL, NULL, &execution_exit, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(execution_exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(execution_exit.status == RUNTIMEC_STATUS_INVALID);
}

static void test_jit_register_and_safepoint_failure_shapes(void) {
    binrwir_jit_register_allocation_t *register_allocation =
        (binrwir_jit_register_allocation_t *)(uintptr_t)1U;
    binrwir_jit_safepoint_plan_t *safepoint_plan =
        (binrwir_jit_safepoint_plan_t *)(uintptr_t)1U;
    binrwir_jit_refusal_t refusal = {
        .reason = BINRWIR_JIT_REFUSAL_NONE,
    };
    binrwir_jit_execution_interface_t execution_interface = {
        .binary_interface_major_version = UINT16_MAX,
    };
    binrwir_binvthread_frame_t frame = {
        .logical_program_counter = 11U,
    };

    RUNTIMEC_EXPECT(binrwir_jit_register_allocation_create(
        NULL, NULL, NULL, NULL, &register_allocation, &refusal, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(register_allocation == NULL);
    RUNTIMEC_EXPECT(refusal.reason == BINRWIR_JIT_REFUSAL_INVALID_PROGRAM);

    refusal.reason = BINRWIR_JIT_REFUSAL_NONE;
    RUNTIMEC_EXPECT(binrwir_jit_safepoint_plan_create(
        NULL, NULL, NULL, &safepoint_plan, &refusal, NULL) ==
        RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(safepoint_plan == NULL);
    RUNTIMEC_EXPECT(refusal.reason == BINRWIR_JIT_REFUSAL_INVALID_PROGRAM);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        NULL, BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_commit_completed_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U, 12U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_discard_incomplete_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION, 11U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_safepoint_discard_incomplete_state(
        (const binrwir_jit_safepoint_plan_t *)(uintptr_t)1U,
        BINRWIR_JIT_SAFEPOINT_BOUNDARY_ERROR, 11U,
        (void *)(uintptr_t)1U, &frame, NULL) == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 11U);

    RUNTIMEC_EXPECT(binrwir_jit_execution_interface(&execution_interface) ==
                    RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(execution_interface.binary_interface_major_version == 0U);
    RUNTIMEC_EXPECT(execution_interface.value_body_address_size_bytes == 0U);
    RUNTIMEC_EXPECT(execution_interface.argument_index_size_bytes == 0U);
    RUNTIMEC_EXPECT(execution_interface.safepoint_contract_revision == 0U);
}

/* ---- Program 所有权：编译接管，成败都不还给调用方 ---- */

typedef struct test_allocation_counts {
    size_t outstanding;
    bool fail_next;
} test_allocation_counts_t;

static void *counting_allocate(void *context, size_t size, size_t alignment) {
    test_allocation_counts_t *counts = context;

    (void)alignment;
    if (counts->fail_next) {
        counts->fail_next = false;
        return NULL;
    }
    void *memory = malloc(size);
    if (memory != NULL) {
        counts->outstanding += 1U;
    }
    return memory;
}

static void counting_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    test_allocation_counts_t *counts = context;

    (void)size;
    (void)alignment;
    if (allocation != NULL) {
        counts->outstanding -= 1U;
        free(allocation);
    }
}

/* 一条无操作数的 COPY 指令就足够：编译只看 Program 形状，不看语义。 */
static runtimec_status_t build_program(const runtimec_allocator_t *allocator,
                                       binrwir_binfunc_t **output_program) {
    binrwir_binfunc_identity_t identity = {
        .snapshot_generation = 1U,
        .program_generation = 1U,
        .schema_version = 1U,
        .rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION,
        .binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision = 1U,
    };
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_instruction_build_request_t instruction = {
        .source_logical_program_counter = 1U,
        .operation_identifier = 1U,
        .read_arguments = {.data = NULL, .count = 0U},
        .write_arguments = {.data = NULL, .count = 0U},
    };
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    *output_program = NULL;
    status = binrwir_binfunc_builder_create(allocator, &identity, &builder);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_add_instruction(
            builder, &instruction, &instruction_identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_finalize(builder, output_program);
    }
    (void)binrwir_binfunc_builder_destroy(builder);
    return status;
}

static void test_program_ownership(void) {
    test_allocation_counts_t program_counts = {0U, false};
    test_allocation_counts_t compiler_counts = {0U, false};
    const runtimec_allocator_t program_allocator = {
        .context = &program_counts,
        .allocate = counting_allocate,
        .deallocate = counting_deallocate,
    };
    const runtimec_allocator_t compiler_allocator = {
        .context = &compiler_counts,
        .allocate = counting_allocate,
        .deallocate = counting_deallocate,
    };
    binrwir_compiler_configuration_t configuration = {
        .structure_size = sizeof(configuration),
        .compiler_revision = 1U,
        .enable_jit = false,
        .jit_machine_code_capacity = 0U,
        .jit_target = {0},
    };
    binrwir_compiler_t *compiler = NULL;
    binrwir_binfunc_t *program = NULL;
    binrwir_compilation_t *compilation = NULL;
    binrwir_compilation_view_t view;

    RUNTIMEC_EXPECT(binrwir_compiler_create(&compiler_allocator, &configuration,
                                            &compiler, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* 成功路径：compilation 接管 Program，销毁它时必须连 Program 一起释放。 */
    RUNTIMEC_EXPECT(build_program(&program_allocator, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(program_counts.outstanding != 0U);
    RUNTIMEC_EXPECT(binrwir_compiler_compile(compiler, program, &compilation,
                                             NULL) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(compilation != NULL);
    RUNTIMEC_EXPECT(binrwir_compilation_get_view(compilation, &view, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(view.execution_target == BINRWIR_EXECUTION_TARGET_VMBIN);
    RUNTIMEC_EXPECT(view.canonical_program != NULL);
    RUNTIMEC_EXPECT(view.execution_plan != NULL);
    RUNTIMEC_EXPECT(view.binrwir_jit_code == NULL);
    RUNTIMEC_EXPECT(binrwir_compilation_destroy(compilation, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 调用方没有、也不再销毁 Program：归零就证明是 compilation 释放的。 */
    RUNTIMEC_EXPECT(program_counts.outstanding == 0U);

    /* 失败路径：plan 分配失败，编译器必须就地销毁 Program，不泄漏。 */
    RUNTIMEC_EXPECT(build_program(&program_allocator, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(program_counts.outstanding != 0U);
    compiler_counts.fail_next = true;
    RUNTIMEC_EXPECT(binrwir_compiler_compile(compiler, program, &compilation,
                                             NULL) ==
                    RUNTIMEC_STATUS_OUT_OF_MEMORY);
    RUNTIMEC_EXPECT(compilation == NULL);
    RUNTIMEC_EXPECT(program_counts.outstanding == 0U);

    RUNTIMEC_EXPECT(binrwir_compiler_destroy(compiler, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
}

int main(void) {
    test_vmkv_xvalue_address_encoding();
    test_compiler_configuration();
    test_compiler_revision_failure_shape();
    test_encoder_revision_failure_shape();
    test_compilation_shapes();
    test_optimization_failure_shape();
    test_jit_failure_shapes();
    test_jit_register_and_safepoint_failure_shapes();
    test_program_ownership();
    if (failures != 0) {
        fprintf(stderr, "binrwir_compiler_contracts_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
