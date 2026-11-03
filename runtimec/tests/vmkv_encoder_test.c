/*
 * vmkv/encoder 验收：固定 /lib 语料上的 canonical 编码。
 *
 * 覆盖 architected 的六条编码规则：opid 只经别名表解析、参数收敛成单一具体
 * langtype 并与能力行掩码求交、写参类型按结果规则算出、引用形态按 A.1 分类、
 * 并集参数被拒、动态 key 返回 UNSUPPORTED 且不产生 Program。
 *
 * 语料（每条用例只声明自己需要的那一棵函数树）：
 *   /lib/enc   add(a:int64, b:float64) -> R:float64     相对指针 + 相对指针
 *   /lib/plus  +  (a:int64, b:float64) -> R:float64     别名解析到同一行
 *   /lib/lit   add(a:int64, 1)        -> R:int64        内联字面量
 *   /lib/abs   add(a:int64, /v/x)     -> R:int64        静态绝对路径
 *   /lib/union add(a:int64|float64 …)                   并集参数
 *   /lib/mix   add(a:int64, b:uint64)                   混合符号
 *   /lib/noteq !=(a:bool,  bool)                        取反特化
 *   /lib/local add(a:int64, x)        -> R:int64        具名帧槽（不支持）
 *   /lib/code  nosuchop(a:int64)                        未注册 operation
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/signature.h"
#include "contract/host_services.h"
#include "contract/snapshot.h"
#include "kvspace/xvalue_head.h"
#include "vmkv/encoder.h"
#include "vmkv/parameter_type.h"
#include "vmkv/rwir.h"
#include "vmkv/rwir_manifest.h"
#include "vmkv/source.h"

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
        }                                                                      \
    } while (0)

typedef struct fake_entry {
    char *key;
    uint8_t *value;
    size_t size;
    bool directory;
} fake_entry_t;

typedef struct fake_store {
    fake_entry_t entries[64];
    size_t count;
} fake_store_t;

static int failures;

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

static runtimec_status_t fail(runtimec_error_t *error,
                              runtimec_status_t status, const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_HOST, 0,
                       runtimec_string_view_from_c_string(message),
                       runtimec_string_view_make(NULL, 0U));
    return status;
}

static fake_entry_t *find_entry(fake_store_t *store, const char *key,
                                size_t size) {
    for (size_t ordinal = 0U; ordinal < store->count; ++ordinal) {
        const size_t length = strlen(store->entries[ordinal].key);
        if (length == size && memcmp(store->entries[ordinal].key, key, size) == 0) {
            return &store->entries[ordinal];
        }
    }
    return NULL;
}

static void store_free(fake_store_t *store) {
    for (size_t ordinal = 0U; ordinal < store->count; ++ordinal) {
        free(store->entries[ordinal].key);
        free(store->entries[ordinal].value);
    }
    store->count = 0U;
}

/* head(长度字段) + body + attr(langtype) 的 wire。 */
static uint8_t *make_wire(const uint8_t *body, size_t body_size,
                          const char *langtype, size_t *output_size) {
    const size_t langtype_size = strlen(langtype);
    const size_t attr_size = 2U + (langtype_size == 0U ? 0U : 3U + langtype_size);
    const size_t total = KVSPACE_XVALUE_HEAD_LEN + body_size + attr_size;
    uint8_t *wire = calloc(1, total);
    if (wire == NULL) {
        return NULL;
    }
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, (uint64_t)body_size);
    head.storetype = KVSPACE_XVALUE_ST_SLACK;
    memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
    if (body_size != 0U) {
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, body_size);
    }
    uint8_t *attr = wire + KVSPACE_XVALUE_HEAD_LEN + body_size;
    const uint16_t attr_length = (uint16_t)attr_size;
    attr[0] = (uint8_t)(attr_length & 0xFFU);
    attr[1] = (uint8_t)(attr_length >> 8U);
    if (langtype_size != 0U) {
        attr[2] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        attr[3] = (uint8_t)(langtype_size & 0xFFU);
        attr[4] = (uint8_t)(langtype_size >> 8U);
        memcpy(attr + 5, langtype, langtype_size);
    }
    *output_size = total;
    return wire;
}

/* 只依赖 C11：不用 strdup。 */
static char *test_duplicate(const char *text) {
    const size_t size = strlen(text) + 1U;
    char *copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static void store_add(fake_store_t *store, const char *key, const uint8_t *body,
                      size_t body_size, const char *langtype) {
    size_t size = 0U;
    uint8_t *wire = make_wire(body, body_size, langtype, &size);
    EXPECT(wire != NULL);
    store->entries[store->count].key = test_duplicate(key);
    store->entries[store->count].value = wire;
    store->entries[store->count].size = size;
    store->entries[store->count].directory = false;
    store->count += 1U;
}

static void store_add_directory(fake_store_t *store, const char *key) {
    store->entries[store->count].key = test_duplicate(key);
    store->entries[store->count].value = NULL;
    store->entries[store->count].size = 0U;
    store->entries[store->count].directory = true;
    store->count += 1U;
}

static void counting_header(uint8_t *body, uint16_t reads, uint16_t writes,
                            uint8_t dynamic) {
    body[0] = (uint8_t)(reads & 0xFFU);
    body[1] = (uint8_t)(reads >> 8U);
    body[2] = (uint8_t)(writes & 0xFFU);
    body[3] = (uint8_t)(writes >> 8U);
    body[4] = dynamic;
}

static void key_of(char *buffer, size_t capacity, const char *path, int line,
                   int column) {
    snprintf(buffer, capacity, "%s/[%d,%d]", path, line, column);
}

static void add_function(fake_store_t *store, const char *path, uint16_t reads,
                         uint16_t writes) {
    char key[128];
    char directory[96];
    uint8_t anchor[5];
    snprintf(directory, sizeof(directory), "%s/", path);
    store_add_directory(store, directory);
    counting_header(anchor, reads, writes, 0U);
    key_of(key, sizeof(key), path, 0, 0);
    store_add(store, key, anchor, sizeof(anchor), "rwfunc");
}

static void add_parameter(fake_store_t *store, const char *path, int column,
                          const char *name, const char *type) {
    char key[128];
    char body[96];
    const size_t used = (size_t)snprintf(body, sizeof(body), "%s", name) + 1U;
    memcpy(body + used, type, strlen(type));
    snprintf(key, sizeof(key), "%s.[0,%d]", path, column);
    store_add(store, key, (const uint8_t *)body, used + strlen(type),
              "def langtype");
}

static void add_opcode(fake_store_t *store, const char *path, int line,
                       uint16_t reads, uint16_t writes, const char *operation) {
    char key[128];
    uint8_t body[128];
    const size_t text_size = strlen(operation);
    counting_header(body, reads, writes, 0U);
    memcpy(body + 5, operation, text_size);
    key_of(key, sizeof(key), path, line, 0);
    store_add(store, key, body, 5U + text_size, "rwir");
}

/* 相对指针 / 具名帧槽：参数槽是 `rwir` 值，body = 表达式 NUL 类型串。 */
static void add_reference(fake_store_t *store, const char *path, int line,
                          int column, const char *expression,
                          const char *type) {
    char key[128];
    uint8_t body[128];
    const size_t expression_size = strlen(expression);
    counting_header(body, 0U, 0U, 0U);
    memcpy(body + 5, expression, expression_size);
    body[5 + expression_size] = 0U;
    memcpy(body + 6 + expression_size, type, strlen(type));
    key_of(key, sizeof(key), path, line, column);
    store_add(store, key, body, 6U + expression_size + strlen(type), "rwir");
}

/* 内联字面量：槽 langtype 就是值的类型，body 是自身编码。 */
static void add_inline_literal(fake_store_t *store, const char *path, int line,
                               int column, int64_t value, const char *type) {
    char key[128];
    uint8_t body[8];
    for (size_t ordinal = 0U; ordinal < 8U; ++ordinal) {
        body[ordinal] = (uint8_t)((uint64_t)value >> (ordinal * 8U));
    }
    key_of(key, sizeof(key), path, line, column);
    store_add(store, key, body, sizeof(body), type);
}

static runtimec_status_t fake_read_header(void *context,
                                          const runtimec_snapshot_t *snapshot,
                                          runtimec_key_view_t key,
                                          runtimec_host_value_header_t *output,
                                          runtimec_error_t *error) {
    fake_store_t *store = context;
    (void)snapshot;
    runtimec_host_value_header_reset(output);
    fake_entry_t *entry = find_entry(store, key.data, key.size);
    if (entry == NULL || entry->directory) {
        return fail(error, RUNTIMEC_STATUS_UNSUPPORTED, "fake: key missing");
    }
    output->value_size = entry->size;
    output->value_revision = 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t fake_read(void *context,
                                   const runtimec_snapshot_t *snapshot,
                                   runtimec_key_view_t key,
                                   runtimec_bytes_buffer_t *output,
                                   runtimec_error_t *error) {
    fake_store_t *store = context;
    (void)snapshot;
    fake_entry_t *entry = find_entry(store, key.data, key.size);
    if (entry == NULL || entry->directory) {
        return fail(error, RUNTIMEC_STATUS_UNSUPPORTED, "fake: key missing");
    }
    if (output->capacity < entry->size) {
        return fail(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                    "fake: capacity");
    }
    memcpy(output->data, entry->value, entry->size);
    output->size = entry->size;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t fake_list(void *context,
                                   const runtimec_snapshot_t *snapshot,
                                   runtimec_key_view_t prefix,
                                   runtimec_host_list_visitor_function_t visitor,
                                   void *visitor_context,
                                   runtimec_error_t *error) {
    fake_store_t *store = context;
    (void)snapshot;
    for (size_t ordinal = 0U; ordinal < store->count; ++ordinal) {
        const fake_entry_t *entry = &store->entries[ordinal];
        const size_t length = strlen(entry->key);
        if (length < prefix.size ||
            memcmp(entry->key, prefix.data, prefix.size) != 0) {
            continue;
        }
        const runtimec_status_t status = visitor(
            visitor_context, runtimec_key_view_make(entry->key, length), error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t fake_source_revision(void *context,
                                              runtimec_key_view_t function_key,
                                              runtimec_source_revision_t *output,
                                              runtimec_error_t *error) {
    (void)context;
    (void)function_key;
    (void)error;
    runtimec_source_revision_reset(output);
    output->instance_identifier = 7U;
    output->generation = 11U;
    output->schema_version = 1U;
    output->instruction_set_version = 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 每个可解引用的 key 给一个稳定且非零的 XValue 基址。 */
static runtimec_status_t fake_resolve_static_xvalue_address(
    void *context, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    fake_store_t *store = context;
    (void)snapshot;
    *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    fake_entry_t *entry = find_entry(store, key.data, key.size);
    if (entry == NULL || entry->directory) {
        return fail(error, RUNTIMEC_STATUS_UNSUPPORTED, "fake: key missing");
    }
    *output_xvalue_address = UINT64_C(0x10000);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_host_services_t make_host_services(fake_store_t *store) {
    runtimec_host_services_t services;
    runtimec_host_services_initialize(&services);
    services.context = store;
    services.allocator.allocate = test_allocate;
    services.allocator.deallocate = test_deallocate;
    services.read = fake_read;
    services.read_header = fake_read_header;
    services.list = fake_list;
    services.source_revision = fake_source_revision;
    services.resolve_static_xvalue_address = fake_resolve_static_xvalue_address;
    return services;
}

static vmkv_encode_request_t make_request(const runtimec_host_services_t *host,
                                          const char *path) {
    vmkv_encode_request_t request;
    memset(&request, 0, sizeof(request));
    request.host_services = host;
    request.function_path = runtimec_string_view_from_c_string(path);
    request.expected_snapshot.contract_version = runtimec_contract_version();
    runtimec_source_revision_reset(&request.expected_snapshot.source_revision);
    request.expected_snapshot.source_revision.instance_identifier = 7U;
    request.expected_snapshot.source_revision.generation = 11U;
    request.expected_snapshot.source_revision.schema_version = 1U;
    request.expected_snapshot.source_revision.instruction_set_version = 1U;
    request.expected_snapshot.function_key =
        runtimec_key_view_from_c_string(path);
    request.expected_snapshot.logical_program_counter =
        runtimec_string_view_from_c_string("[1,0]");
    return request;
}

/*
 * canonical 指令借用函数源内存（类型文本、参数视图）。源关闭后视图即失效，
 * 因此这里在源仍打开时深拷贝一份，断言只对副本做。
 */
#define TEST_CANONICAL_ARGUMENT_CAPACITY 6U
#define TEST_CANONICAL_TYPE_CAPACITY 32U

typedef struct test_canonical {
    vmkv_canonical_instruction_t instruction;
    binrwir_argument_t arguments[TEST_CANONICAL_ARGUMENT_CAPACITY];
    runtimec_string_view_t types[TEST_CANONICAL_ARGUMENT_CAPACITY];
    char type_text[TEST_CANONICAL_ARGUMENT_CAPACITY][TEST_CANONICAL_TYPE_CAPACITY];
} test_canonical_t;

static void test_canonical_copy(test_canonical_t *target,
                               const vmkv_canonical_instruction_t *source) {
    memset(target, 0, sizeof(*target));
    target->instruction = *source;
    const size_t read_count = source->read_arguments.count;
    const size_t write_count = source->write_arguments.count;
    const size_t capacity = TEST_CANONICAL_ARGUMENT_CAPACITY;
    EXPECT(read_count + write_count <= capacity);
    if (read_count + write_count > capacity) {
        return;
    }
    for (size_t ordinal = 0U; ordinal < read_count + write_count; ++ordinal) {
        target->arguments[ordinal] = ordinal < read_count
                                         ? source->read_arguments.data[ordinal]
                                         : source->write_arguments.data[ordinal - read_count];
        const runtimec_string_view_t type =
            ordinal < read_count
                ? source->read_parameter_types[ordinal]
                : source->write_parameter_types[ordinal - read_count];
        const size_t size = type.size < TEST_CANONICAL_TYPE_CAPACITY - 1U
                                ? type.size
                                : TEST_CANONICAL_TYPE_CAPACITY - 1U;
        memcpy(target->type_text[ordinal], type.data, size);
        target->types[ordinal] = runtimec_string_view_make(
            target->type_text[ordinal], size);
    }
    target->instruction.read_arguments.data = target->arguments;
    target->instruction.write_arguments.data = target->arguments + read_count;
    target->instruction.read_parameter_types = target->types;
    target->instruction.write_parameter_types = target->types + read_count;
}

/* canonical 指令的符号池与字面量池下标需要一个 builder 才能分配。 */
static bool make_builder(const runtimec_host_services_t *host,
                         binrwir_program_builder_t **output_builder) {
    const binrwir_program_identity_t identity = {
        .snapshot_generation = 11U,
        .program_generation = 1U,
        .schema_version = 1U,
        .rwir_manifest_version = 1U,
        .binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision = 1U,
    };
    return binrwir_program_builder_create(&host->allocator, &identity,
                                          output_builder) ==
           RUNTIMEC_STATUS_SUCCESS;
}

/* 读第一条指令并按 canonical 形状编码。 */
static runtimec_status_t encode_first_instruction(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    vmkv_encode_function_t function, test_canonical_t *output,
    runtimec_error_t *error) {
    memset(output, 0, sizeof(*output));
    /* 非形参引用的 argidx 从形参区间之后开始，与 vmkv_encode 同一规则。 */
    function.next_operand_argument_index = function.read_parameter_count +
                                           function.write_parameter_count;
    binrwir_program_builder_t *builder = NULL;
    if (!make_builder(request->host_services, &builder)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    vmkv_function_source_t *source = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    runtimec_status_t status = vmkv_source_open_function(
        request->host_services, request->function_path, &source, &snapshot,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    vmkv_rwir_reader_t *reader = NULL;
    status = vmkv_rwir_reader_create(source, &reader, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        vmkv_rwir_read_result_t read_result = VMKV_RWIR_READ_RESULT_INVALID;
        vmkv_rwir_instruction_view_t instruction;
        status = vmkv_rwir_reader_next(reader, &read_result, &instruction, error);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            EXPECT(read_result == VMKV_RWIR_READ_RESULT_INSTRUCTION);
            vmkv_canonical_instruction_t canonical;
            status = vmkv_encode_instruction(encoder, request, builder,
                                             &function, &instruction,
                                             &canonical, error);
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                test_canonical_copy(output, &canonical);
            }
        }
        vmkv_rwir_reader_destroy(reader);
    }
    binrwir_program_builder_destroy(builder);
    vmkv_source_close_function(source);
    return status;
}

static bool view_equals(runtimec_string_view_t view, const char *text) {
    const size_t size = strlen(text);
    return view.size == size &&
           (size == 0U || memcmp(view.data, text, size) == 0);
}

/* add(int64, float64) -> float64，两个读参都是相对指针。 */
static void build_add_function(fake_store_t *store, const char *path,
                               const char *operation) {
    add_function(store, path, 2U, 1U);
    add_parameter(store, path, -1, "a", "int64");
    add_parameter(store, path, -2, "b", "float64");
    add_parameter(store, path, 1, "R", "float64");
    add_opcode(store, path, 1, 2U, 1U, operation);
    add_reference(store, path, 1, -1, "*[0,-1]", "int64");
    add_reference(store, path, 1, -2, "*[0,-2]", "float64");
    add_reference(store, path, 1, 1, "*[0,1]", "float64");
}

static void test_alias_resolution_and_type_convergence(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    build_add_function(&store, "/lib/enc", "add");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);

    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    const runtimec_status_t status = encode_first_instruction(
        encoder, &request, function, &instruction, &error);
    EXPECT(status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.instruction.operation_identifier == 0x0002U);
    EXPECT(instruction.instruction.logical_program_counter == 1U);
    EXPECT(instruction.instruction.read_arguments.count == 2U);
    EXPECT(instruction.instruction.write_arguments.count == 1U);
    EXPECT(view_equals(instruction.instruction.read_parameter_types[0], "int64"));
    EXPECT(view_equals(instruction.instruction.read_parameter_types[1], "float64"));
    /* WIDEN(int64, float64) = float64：结果类型由矩阵算出，不是默认提升。 */
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "float64"));
    /* 相对指针：PARAMETER_READ/WRITE + slot + payload 0。 */
    EXPECT(instruction.arguments[0].reference_kind ==
           BINRWIR_REFERENCE_PARAMETER_READ);
    EXPECT(instruction.arguments[0].slot == 0U);
    EXPECT(instruction.arguments[0].payload == 0U);
    EXPECT(instruction.arguments[0].argument_index == 0U);
    EXPECT(instruction.arguments[1].slot == 1U);
    EXPECT(instruction.arguments[1].argument_index == 1U);
    EXPECT(instruction.arguments[instruction.instruction.read_arguments.count + 0].reference_kind ==
           BINRWIR_REFERENCE_PARAMETER_WRITE);
    EXPECT(instruction.arguments[instruction.instruction.read_arguments.count + 0].slot == 0U);
    EXPECT(instruction.arguments[instruction.instruction.read_arguments.count + 0].argument_index == 2U);
    EXPECT(instruction.arguments[instruction.instruction.read_arguments.count + 0].payload == 0U);

    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* `+` 与 `add` 必须解析到同一个能力行、同一个 opid。 */
static void test_plus_and_add_share_one_operation_identifier(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    build_add_function(&store, "/lib/enc", "add");
    build_add_function(&store, "/lib/plus", "+");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t add_instruction;
    test_canonical_t plus_instruction;
    vmkv_encode_request_t add_request = make_request(&host, "/lib/enc");
    vmkv_encode_request_t plus_request = make_request(&host, "/lib/plus");
    EXPECT(encode_first_instruction(encoder, &add_request, function,
                                    &add_instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(encode_first_instruction(encoder, &plus_request, function,
                                    &plus_instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(add_instruction.instruction.operation_identifier ==
           plus_instruction.instruction.operation_identifier);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 并集参数声明：BinRWIR 只接受单一具体类型。 */
static void test_union_parameter_is_rejected(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/union", 1U, 1U);
    add_parameter(&store, "/lib/union", -1, "a", "int64|float64");
    add_parameter(&store, "/lib/union", 1, "R", "int64");
    add_opcode(&store, "/lib/union", 1, 1U, 1U, "add");
    add_reference(&store, "/lib/union", 1, -1, "*[0,-1]", "int64|float64");
    add_reference(&store, "/lib/union", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/union");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(error.code == 24);
    EXPECT(instruction.instruction.read_arguments.data == NULL);
    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(program == NULL);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/*
 * 声明是并集、但操作数槽写的是具体类型：具体优先，按 int16 编码。
 * 这正是 layout 写出实参具体类型后要走的路径。
 */
static void test_concrete_operand_type_wins_over_declaration(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/concrete", 1U, 1U);
    add_parameter(&store, "/lib/concrete", -1, "a", "int8|int16");
    add_parameter(&store, "/lib/concrete", 1, "R", "int16");
    add_opcode(&store, "/lib/concrete", 1, 1U, 1U, "abs");
    add_reference(&store, "/lib/concrete", 1, -1, "*[0,-1]", "int16");
    add_reference(&store, "/lib/concrete", 1, 1, "*[0,1]", "int16");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/concrete");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(view_equals(instruction.instruction.read_parameter_types[0], "int16"));
    /* SAME_FIRST：写参类型就是读参的具体类型。 */
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "int16"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* alias policy：declared 的行（`=` 即 copy）产生 (read, write) MAY_ALIAS 对。 */
static void test_alias_policy_becomes_signature_rule(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/copyfn", 1U, 1U);
    add_parameter(&store, "/lib/copyfn", -1, "a", "int64");
    add_parameter(&store, "/lib/copyfn", 1, "R", "int64");
    add_opcode(&store, "/lib/copyfn", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/copyfn", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/copyfn", 1, 1, "*[0,1]", "int64");
    build_add_function(&store, "/lib/enc", "add");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t copy_request = make_request(&host, "/lib/copyfn");
    vmkv_encode_request_t add_request = make_request(&host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);

    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &copy_request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        binrwir_program_view_t view;
        EXPECT(binrwir_program_get_view(program, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view.signature.alias_rule_count == 1U);
        binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
        EXPECT(binrwir_signature_get_alias_policy(&view.signature, 0U, 0U,
                                                  &policy) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(policy == BINRWIR_ALIAS_MAY_ALIAS);
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }

    program = NULL;
    EXPECT(vmkv_encode(encoder, &add_request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        binrwir_program_view_t view;
        EXPECT(binrwir_program_get_view(program, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        /* forbidden：不产生规则，也不猜成 NO_ALIAS。 */
        EXPECT(view.signature.alias_rule_count == 0U);
        binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
        EXPECT(binrwir_signature_get_alias_policy(&view.signature, 0U, 0U,
                                                  &policy) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 指针参数：langtype 文本推不出指针性，本阶段一律 UNSUPPORTED，不猜。 */
static void test_pointer_parameter_is_unsupported(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/ptraddr", 1U, 1U);
    add_parameter(&store, "/lib/ptraddr", -1, "a", "int64");
    add_parameter(&store, "/lib/ptraddr", 1, "R", "int64");
    add_opcode(&store, "/lib/ptraddr", 1, 1U, 1U, "kvlang·abs");
    add_reference(&store, "/lib/ptraddr", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/ptraddr", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/ptraddr");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(error.code == 25);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 混合符号算术：矩阵裁决为拒绝。 */
static void test_mixed_signed_unsigned_is_rejected(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/mix", 2U, 1U);
    add_parameter(&store, "/lib/mix", -1, "a", "int64");
    add_parameter(&store, "/lib/mix", -2, "b", "uint64");
    add_parameter(&store, "/lib/mix", 1, "R", "int64");
    add_opcode(&store, "/lib/mix", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/mix", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/mix", 1, -2, "*[0,-2]", "uint64");
    add_reference(&store, "/lib/mix", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/mix");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(error.code == 27);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/*
 * `!=` 在 char/pointer/bool 上改选专用「不等于」能力行（不取反）：
 * catalog 的 Invert 位已全部为 false，指令头因此不需要取反位。
 */
static void test_not_equal_specialization_selects_dedicated_row(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/noteq", 2U, 1U);
    add_parameter(&store, "/lib/noteq", -1, "a", "bool");
    add_parameter(&store, "/lib/noteq", -2, "b", "bool");
    add_parameter(&store, "/lib/noteq", 1, "R", "bool");
    add_opcode(&store, "/lib/noteq", 1, 2U, 1U, "!=");
    add_reference(&store, "/lib/noteq", 1, -1, "*[0,-1]", "bool");
    add_reference(&store, "/lib/noteq", 1, -2, "*[0,-2]", "bool");
    add_reference(&store, "/lib/noteq", 1, 1, "*[0,1]", "bool");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/noteq");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    /* BOOLEAN_NOT_EQUAL = 0x002B；BOOLEAN 结果规则给出 bool。 */
    EXPECT(instruction.instruction.operation_identifier == 0x002BU);
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "bool"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 内联字面量：LITERAL + 池序号，参数类型取字面量自己的类型。 */
static void test_inline_literal_uses_literal_reference(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/lit", 2U, 1U);
    add_parameter(&store, "/lib/lit", -1, "a", "int64");
    add_parameter(&store, "/lib/lit", -2, "b", "int64");
    add_parameter(&store, "/lib/lit", 1, "R", "int64");
    add_opcode(&store, "/lib/lit", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/lit", 1, -1, "*[0,-1]", "int64");
    add_inline_literal(&store, "/lib/lit", 1, -2, 1, "int64");
    add_reference(&store, "/lib/lit", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/lit");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[1].reference_kind ==
           BINRWIR_REFERENCE_LITERAL);
    EXPECT(instruction.arguments[1].payload == 0U);
    /* 非形参引用的 argidx 从 nr+nw = 3 起按出现次序递增。 */
    EXPECT(instruction.arguments[1].argument_index == 3U);
    EXPECT(view_equals(instruction.instruction.read_parameter_types[1], "int64"));
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "int64"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 绝对路径实参：STATIC + host 基址推导出的 body 地址。 */
static void test_static_path_derives_body_address(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/abs", 2U, 1U);
    add_parameter(&store, "/lib/abs", -1, "a", "int64");
    add_parameter(&store, "/lib/abs", -2, "b", "int64");
    add_parameter(&store, "/lib/abs", 1, "R", "int64");
    add_opcode(&store, "/lib/abs", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/abs", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/abs", 1, -2, "/v/x", "int64");
    add_reference(&store, "/lib/abs", 1, 1, "*[0,1]", "int64");
    store_add(&store, "/v/x", (const uint8_t *)"\0\0\0\0\0\0\0\0", 8U, "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/abs");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[1].reference_kind ==
           BINRWIR_REFERENCE_STATIC);
    EXPECT(instruction.arguments[1].argument_index == 3U);
    EXPECT(instruction.arguments[1].payload ==
           UINT64_C(0x10000) + KVSPACE_XVALUE_HEAD_LEN);
    /* STATIC 的 slot 是符号池下标：vmbin 按 key 访问的依据。 */
    EXPECT(instruction.arguments[1].slot == 0U);

    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_program_get_symbol(program,
                                          instruction.arguments[1].slot,
                                          &symbol) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(symbol, "/v/x"));
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 具名帧槽：裸标识符是帧局部变量名，登进 Program 符号池（LOCAL）。 */
static void test_named_frame_slot_becomes_local_symbol(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/local", 2U, 1U);
    add_parameter(&store, "/lib/local", -1, "a", "int64");
    add_parameter(&store, "/lib/local", -2, "b", "int64");
    add_parameter(&store, "/lib/local", 1, "R", "int64");
    add_opcode(&store, "/lib/local", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/local", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/local", 1, -2, "temporary", "int64");
    add_reference(&store, "/lib/local", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/local");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[1].reference_kind ==
           BINRWIR_REFERENCE_LOCAL);
    EXPECT(instruction.arguments[1].payload == 0U);
    /* 裸名不消耗形参编号：它是非形参引用，argidx 从 nr+nw = 3 起。 */
    EXPECT(instruction.arguments[1].argument_index == 3U);

    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_program_get_symbol(program,
                                          instruction.arguments[1].slot,
                                          &symbol) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(symbol, "temporary"));
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 帧内槽表达式 `*[d,±k]`（d 非 0）：没有可编译的槽契约 → UNSUPPORTED。 */
static void test_framed_slot_expression_is_unsupported(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/framed", 2U, 1U);
    add_parameter(&store, "/lib/framed", -1, "a", "int64");
    add_parameter(&store, "/lib/framed", -2, "b", "int64");
    add_parameter(&store, "/lib/framed", 1, "R", "int64");
    add_opcode(&store, "/lib/framed", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/framed", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/framed", 1, -2, "*[1,-1]", "int64");
    add_reference(&store, "/lib/framed", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/framed");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(program == NULL);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

static void test_unknown_operation_is_rejected(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/code", 1U, 1U);
    add_parameter(&store, "/lib/code", -1, "a", "int64");
    add_parameter(&store, "/lib/code", 1, "R", "int64");
    add_opcode(&store, "/lib/code", 1, 1U, 1U, "nosuchoperation");
    add_reference(&store, "/lib/code", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/code", 1, 1, "*[0,1]", "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/code");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(error.code == 22);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 全函数编码：Program 的签名、opid、参数与 canonical 指令逐字段一致。 */
static void test_program_encoding_matches_canonical_instruction(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    build_add_function(&store, "/lib/enc", "add");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_program_t *program = NULL;
    const runtimec_status_t status = vmkv_encode(encoder, &request, &program,
                                                 &error);
    EXPECT(status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(program != NULL);
    if (program != NULL) {
        EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        binrwir_program_view_t view;
        EXPECT(binrwir_program_get_view(program, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view.instruction_count == 1U);
        EXPECT(view.signature.read_parameter_count == 2U);
        EXPECT(view.signature.write_parameter_count == 1U);
        EXPECT(view.signature.read_parameter_argument_indices[0] == 0U);
        EXPECT(view.signature.read_parameter_argument_indices[1] == 1U);
        EXPECT(view.signature.write_parameter_argument_indices[0] == 2U);
        EXPECT(view_equals(view.signature.read_parameter_types[0], "int64"));
        EXPECT(view_equals(view.signature.read_parameter_types[1], "float64"));
        EXPECT(view_equals(view.signature.write_parameter_types[0], "float64"));
        binrwir_instruction_view_t instruction;
        EXPECT(binrwir_program_get_instruction(program, 0U, &instruction) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(instruction.instruction->operation_identifier == 0x0002U);
        EXPECT(instruction.source_logical_program_counter == 1U);
        EXPECT(instruction.read_arguments.count == 2U);
        EXPECT(instruction.write_arguments.count == 1U);
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* 请求快照与源 revision 不一致：拒绝，不产生 Program。 */
static void test_stale_snapshot_is_rejected(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    build_add_function(&store, "/lib/enc", "add");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/enc");
    request.expected_snapshot.source_revision.generation = 12U;
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_INVALID);
    EXPECT(program == NULL);
    EXPECT(error.code == 33);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}



/*
 * 符号池按字节字典序在 finalize 规范化：登记序（`a` 先、`/v/x` 后）与规范序
 * （`/v/x` 先、`a` 后）相反。断言只能用「下标回查得到原名字」的 round-trip——
 * 单符号是恒等映射，永远测不出漏重定位；错位表现为校验通过、名字是错的。
 */
static void test_symbol_pool_normalization_remaps_every_reference(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/two", 2U, 1U);
    add_parameter(&store, "/lib/two", -1, "a", "int64");
    add_parameter(&store, "/lib/two", -2, "b", "int64");
    add_parameter(&store, "/lib/two", 1, "R", "int64");
    add_opcode(&store, "/lib/two", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/two", 1, -1, "a", "int64");
    add_reference(&store, "/lib/two", 1, -2, "/v/x", "int64");
    add_reference(&store, "/lib/two", 1, 1, "*[0,1]", "int64");
    store_add(&store, "/v/x", (const uint8_t *)"\0\0\0\0\0\0\0\0", 8U,
              "int64");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/two");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        binrwir_instruction_view_t instruction;
        EXPECT(binrwir_program_get_instruction(program, 0U, &instruction) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(instruction.read_arguments.count == 2U);
        EXPECT(instruction.read_arguments.data[0].reference_kind ==
               BINRWIR_REFERENCE_LOCAL);
        EXPECT(instruction.read_arguments.data[1].reference_kind ==
               BINRWIR_REFERENCE_STATIC);
        runtimec_string_view_t local_symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t static_symbol = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_program_get_symbol(
                   program, instruction.read_arguments.data[0].slot,
                   &local_symbol) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_program_get_symbol(
                   program, instruction.read_arguments.data[1].slot,
                   &static_symbol) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(local_symbol, "a"));
        EXPECT(view_equals(static_symbol, "/v/x"));
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

/* `call` 的第一个读参是函数名：FUNCTION 引用 + 符号池下标，名字不含 /lib/。 */
static void test_call_target_becomes_function_symbol(void) {
    fake_store_t store;
    memset(&store, 0, sizeof(store));
    add_function(&store, "/lib/callfn", 1U, 0U);
    add_parameter(&store, "/lib/callfn", -1, "a", "int64");
    add_opcode(&store, "/lib/callfn", 1, 1U, 0U, "call");
    add_reference(&store, "/lib/callfn", 1, -1, "other·function", "");
    runtimec_host_services_t host = make_host_services(&store);
    vmkv_encode_request_t request = make_request(&host, "/lib/callfn");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host.allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 0U};
    test_canonical_t instruction;
    EXPECT(encode_first_instruction(encoder, &request, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[0].reference_kind ==
           BINRWIR_REFERENCE_FUNCTION);
    EXPECT(instruction.arguments[0].payload == 0U);
    binrwir_program_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_program_get_symbol(program, instruction.arguments[0].slot,
                                          &symbol) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(symbol.size == strlen("other·function"));
        EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

static void test_parameter_type_classification(void) {
    uint32_t type_set = 0U;
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_INT64);
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("int64|float64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_UNION);
    EXPECT(vmkv_parameter_type_classify(runtimec_string_view_make(NULL, 0U),
                                        &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_NONE);
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("float16"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_UNMAPPED);
    /* 以字符为基的形状是字符串，不是数组位。 */
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("[3]char/utf8"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_CHARACTER_UTF8);
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("[]int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_ARRAY);
    /* 连续形状段仍是同一个数组位。 */
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("[][]float64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_ARRAY);
}

int main(void) {
    test_alias_resolution_and_type_convergence();
    test_plus_and_add_share_one_operation_identifier();
    test_union_parameter_is_rejected();
    test_concrete_operand_type_wins_over_declaration();
    test_alias_policy_becomes_signature_rule();
    test_pointer_parameter_is_unsupported();
    test_mixed_signed_unsigned_is_rejected();
    test_not_equal_specialization_selects_dedicated_row();
    test_inline_literal_uses_literal_reference();
    test_static_path_derives_body_address();
    test_named_frame_slot_becomes_local_symbol();
    test_framed_slot_expression_is_unsupported();
    test_unknown_operation_is_rejected();
    test_program_encoding_matches_canonical_instruction();
    test_stale_snapshot_is_rejected();
    test_symbol_pool_normalization_remaps_every_reference();
    test_call_target_becomes_function_symbol();
    test_parameter_type_classification();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vmkv encoder: ok\n");
    return 0;
}
