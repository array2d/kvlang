/*
 * `ptreq` / `ptreq·not`：四种操作数组合的语义。
 *
 * 为什么单独一个目标而不是塞进 `handler_behavior`：那个数据驱动用例走的是
 * **字面量**操作数（`LITERAL` 引用，wire 的 `ptr` 字节恒为 INLINE），而指针
 * 语义取决于 **`ptr` 字节 + body 是否为空**——不构造 `ptr=1` 的 wire 就走不到
 * 那几条分支。用字面量测 ptreq，测的永远是「两个非指针比较」，而那不是这条
 * 算子存在的理由。
 *
 * 判据出处：
 *   matrix:66 pointer-equality —— 按目标 key 比身份；None equals None；
 *                                 指针对非指针是 INVALID
 *   spec 03-类型系统/03 的 None 判据 —— 非指针且 body 为空
 *   spec 16-ptr.kv —— `p == None` 是唯一空引用判定写法
 *
 * **最后一条是重点**：`p != None` 若在「两侧都为空」这一支算错，不会崩，
 * 只会让所有判空逻辑反过来。所以四种组合各一条，逐条断结果。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "binrwir/registry.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/interpreter.h"

#define TEST_BODY_CAPACITY 32U
#define TEST_VALUE_CAPACITY (KVSPACE_XVALUE_HEAD_LEN + TEST_BODY_CAPACITY)

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* 一个可长期存活的 XValue：16 字节头 + body。 */
typedef struct test_value {
    uint8_t storage[TEST_VALUE_CAPACITY];
    size_t body_size;
} test_value_t;

/*
 * `pointer` 非零 → head.ptr = PTR（指针值，body 是目标 key）；
 * 零 → head.ptr = INLINE（普通值／None）。
 *
 * **body_size 为零 + pointer 为零 = None**。这是 spec 的判据，本测试不另立规则。
 */
static void test_value_initialize(test_value_t *value, int pointer,
                                  const char *body, size_t body_size) {
    kvspaceXvalueHead head;

    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, body_size);
    head.storetype = KVSPACE_XVALUE_ST_SHORT;
    /*
     * `head.ptr` 的字段名与取值都由 kvspace ABI 定义，本仓没有命名权。
     *
     * `check_names` 禁的是**自造**缩写；上游 ABI 名字的正确处理是把它加进
     * `check_names.py` 的 `FOREIGN_ABI_IDENTIFIERS` 白名单，**不是**在本仓
     * 另起一套常量——抄一份上游数值进来，上游变了这里会静默测错东西，
     * 而且看起来完全正常。
     */
    head.ptr = pointer != 0 ? KVSPACE_XVALUE_PTR_PTR
                            : KVSPACE_XVALUE_PTR_INLINE;
    memset(value->storage, 0, sizeof(value->storage));
    memcpy(value->storage, &head, sizeof(head));
    if (body_size != 0U) {
        memcpy(value->storage + KVSPACE_XVALUE_HEAD_LEN, body, body_size);
    }
    value->body_size = body_size;
}

static runtimec_value_body_address_t test_value_body_address(
    const test_value_t *value) {
    return (runtimec_value_body_address_t)(
        (uintptr_t)(value->storage + KVSPACE_XVALUE_HEAD_LEN));
}

static runtimec_xvalue_address_t test_value_xvalue_address(
    const test_value_t *value) {
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

static runtimec_allocator_t test_malloc_allocator(void) {
    runtimec_allocator_t allocator;

    allocator.context = NULL;
    allocator.allocate = test_allocate;
    allocator.deallocate = test_deallocate;
    return allocator;
}

/*
 * 跑一次 ptreq：两个操作数是**地址绑定**（不是字面量），所以它们的 head 里的
 * `ptr` 字节能被 `vmbin_value_access_reference_kind` 读到。
 */
static runtimec_status_t run_pointer_equality(
    const test_value_t *left, const test_value_t *right,
    binrwir_operation_identifier_t operation_identifier, uint8_t *output_result) {
    runtimec_allocator_t allocator = test_malloc_allocator();
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_registry_t registry;
    binrwir_instruction_build_request_t request;
    binrwir_argument_t reads[2];
    binrwir_argument_t write_argument;
    test_value_t result_storage;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    binrwir_execution_exit_t exit_state;
    size_t count = 0U;
    const binrwir_registry_entry_t *entries;
    runtimec_error_t error;
    runtimec_status_t status;

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

    /* 写槽是 STATIC，类型 bool（1 字节）。 */
    status = binrwir_program_builder_add_symbol(
        builder, runtimec_string_view_from_c_string("/t/result"),
        runtimec_string_view_from_c_string("bool"), &symbol_index);

    /*
     * 两个读参是**形参**（PARAMETER_READ）：绑定表给出真实 XValue 地址，
     * `reference_kind` 才能从 head 里读到 ptr 字节；用 LITERAL 就永远读到 INLINE。
     *
     * 形参槽号 = 读参序号（0/1），见 `vmbin_frame_slot_of_parameter`。
     * **不用 LOCAL**：LOCAL 的槽号是 `read_count + write_count + symbol_index`，
     * 而符号池会按名字字典序重排——按插入顺序猜槽号会绑到别的槽上。
     */
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ, 1U,
            runtimec_string_view_from_c_string("int64"), &parameter_index);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            status = binrwir_argument_create(1U,
                                             BINRWIR_REFERENCE_PARAMETER_READ,
                                             0U, 0U, &reads[0]);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ, 2U,
            runtimec_string_view_from_c_string("int64"), &parameter_index);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            status = binrwir_argument_create(2U,
                                             BINRWIR_REFERENCE_PARAMETER_READ,
                                             1U, 0U, &reads[1]);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        test_value_initialize(&result_storage, 0, "\0", 1U);
        status = binrwir_argument_create(
            3U, BINRWIR_REFERENCE_STATIC, symbol_index,
            (uint64_t)(uintptr_t)test_value_body_address(&result_storage),
            &write_argument);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return status;
    }

    memset(&request, 0, sizeof(request));
    request.operation_identifier = operation_identifier;
    request.source_logical_program_counter = 1U;
    request.read_arguments.data = reads;
    request.read_arguments.count = 2U;
    request.write_arguments.data = &write_argument;
    request.write_arguments.count = 1U;
    status = binrwir_program_builder_add_instruction(builder, &request,
                                                     &instruction_identifier);
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
        status = binrwir_frame_bindings_create(&allocator, 4U, &bindings);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        /* 两个 LOCAL 槽：槽号 0 与 1（LOCAL 的 slot 是符号池下标，绑定槽号
         * 由 frame_slot 决定；这里直接用 0/1 与上面的 argument slot 对应）。 */
        frame.bindings = bindings;
        status = binrwir_frame_bindings_bind(
            bindings, 0U, runtimec_key_view_from_c_string("/t/left"),
            runtimec_key_view_from_c_string("/t/left"),
            test_value_xvalue_address(left), 1U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_frame_bindings_bind(
            bindings, 1U, runtimec_key_view_from_c_string("/t/right"),
            runtimec_key_view_from_c_string("/t/right"),
            test_value_xvalue_address(right), 1U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_reset(&error);
        status = binrwir_registry_initialize(&registry);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            entries = runtimec_scalar_handler_entries(&count);
            status = binrwir_registry_append(&registry, "scalar", entries,
                                             count, &error);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        *output_result = 0xFFU;
        status = vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                               &exit_state);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            *output_result =
                *(const uint8_t *)(const void *)test_value_body_address(
                    &result_storage);
        }
    }
    if (bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(bindings);
    }
    (void)binrwir_execution_frame_destroy(&frame);
    (void)binrwir_program_destroy(program);
    return status;
}

/*
 * 四种组合 × 两个算子 —— 每条都断具体结果，不是断「没崩」。
 */
static void test_same_key(void) {
    test_value_t left;
    test_value_t right;
    uint8_t result = 0U;

    test_value_initialize(&left, 1, "/a", 2U);
    test_value_initialize(&right, 1, "/a", 2U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 1U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 0U);
}

static void test_different_key(void) {
    test_value_t left;
    test_value_t right;
    uint8_t result = 0U;

    test_value_initialize(&left, 1, "/a", 2U);
    test_value_initialize(&right, 1, "/b", 2U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 0U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 1U);
}

/*
 * `(Ptr, None)`：matrix:66「a pointer carrying a target never equals None」——
 * 这是**合法比较**，结果为不等，不是 INVALID。
 */
static void test_pointer_versus_none(void) {
    test_value_t pointer_value;
    test_value_t none_value;
    uint8_t result = 0U;

    test_value_initialize(&pointer_value, 1, "/a", 2U);
    test_value_initialize(&none_value, 0, NULL, 0U);

    EXPECT(run_pointer_equality(&pointer_value, &none_value,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 0U);

    EXPECT(run_pointer_equality(&pointer_value, &none_value,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 1U);
}

/*
 * `(None, None)` —— **本测试最重要的一条**。
 *
 * `None == None` 为 true，所以 `None != None` 必须为 **false**。
 * 取反实现最常见的错法是只反转「指针比较」的结果，而把「两侧都是 None」
 * 这一支的常量 1 忘了反转——那样 `p != None` 在 p 为空时给出 true，
 * **所有判空逻辑反向**，而且不会崩。
 */
static void test_none_versus_none(void) {
    test_value_t left;
    test_value_t right;
    uint8_t result = 0U;

    test_value_initialize(&left, 0, NULL, 0U);
    test_value_initialize(&right, 0, NULL, 0U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 1U);

    EXPECT(run_pointer_equality(&left, &right,
                                BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
                                &result) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == 0U);
}

int main(void) {
    test_same_key();
    test_different_key();
    test_pointer_versus_none();
    test_none_versus_none();
    if (failures != 0) {
        fprintf(stderr, "scalar_pointer_equality_test: %d failure(s)\n",
                failures);
        return 1;
    }
    fprintf(stdout, "scalar_pointer_equality_test: OK\n");
    return 0;
}
