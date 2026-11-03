/*
 * vmkv 读取层验收：用最小 fake host 提供一棵固定 /lib 树，验证
 * source/rwir 的读取语义、相对指针保留、以及畸形输入的失败路径。
 *
 * 固定树（`/lib/sum`，nr=2 nw=1）：
 *   /lib/sum/                    目录（无值）
 *   /lib/sum/[0,0]               rwfunc  counts(2,1,0)
 *   /lib/sum.[0,-1]              def langtype  "a\0[]int64"
 *   /lib/sum.[0,-2]              def langtype  "n\0int64"
 *   /lib/sum.[0,1]               def langtype  "R\0int64"
 *   /lib/sum/[1,0]               rwir    counts(2,1,0) + "int64·add"
 *   /lib/sum/[1,-1]              rwir    counts(0,0,0) + "*[0,-1]\0int64"
 *   /lib/sum/[1,-2]              int64   raw(8B) = 1
 *   /lib/sum/[1,1]               rwir    counts(0,0,0) + "*[0,1]\0int64"
 *   /lib/sum/[2,0]               rwir    counts(0,0,0) + "return"
 *
 * 畸形树（每个用例只破坏一处）：
 *   missing-anchor     无 [0,0]
 *   bad-anchor-body    [0,0] body 不是 5 字节
 *   missing-opcode     [2,0] 缺失
 *   missing-read-slot  [1,-2] 缺失（声明 2 读但只有 1 个）
 *   empty-opcode       [1,0] body 只有 counts
 *   missing-parameter  .[0,-2] 缺失
 *   bad-parameter      .[0,-1] body 没有 NUL
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvspace/xvalue_head.h"
#include "vmkv/rwir.h"
#include "vmkv/source.h"

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
        }                                                                      \
    } while (0)

typedef struct fake_entry {
    const char *key;
    uint8_t *value;
    size_t size;
    bool directory;
} fake_entry_t;

typedef struct fake_store {
    fake_entry_t entries[32];
    size_t count;
    int64_t revision_generation;
    bool fail_read;
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

static runtimec_status_t fail(runtimec_error_t *error, runtimec_status_t status,
                              const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_HOST, 0,
                       runtimec_string_view_from_c_string(message),
                       runtimec_string_view_make(NULL, 0U));
    return status;
}

static fake_entry_t *find_entry(fake_store_t *store, const char *key,
                                size_t size) {
    for (size_t i = 0U; i < store->count; i++) {
        const size_t length = strlen(store->entries[i].key);
        if (length == size && memcmp(store->entries[i].key, key, size) == 0) {
            return &store->entries[i];
        }
    }
    return NULL;
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
    if (store->fail_read) {
        return fail(error, RUNTIMEC_STATUS_ERROR, "fake: read failure");
    }
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
    for (size_t i = 0U; i < store->count; i++) {
        const fake_entry_t *entry = &store->entries[i];
        const size_t length = strlen(entry->key);
        if (length < prefix.size ||
            memcmp(entry->key, prefix.data, prefix.size) != 0) {
            continue;
        }
        runtimec_status_t status = visitor(
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
    fake_store_t *store = context;
    (void)function_key;
    (void)error;
    runtimec_source_revision_reset(output);
    output->instance_identifier = 7U;
    output->generation = (uint64_t)store->revision_generation;
    output->schema_version = 1U;
    output->instruction_set_version = 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 构造一个 wire 值：head(长度字段=[dims]T 的长度) + body + 空 attr。 */
static uint8_t *make_wire(const uint8_t *body, size_t body_size,
                          const char *langtype, size_t *output_size) {
    const size_t langtype_size = strlen(langtype);
    /* attr 区无长度前缀：长度进 head 的 attrslen。 */
    const size_t attr_size = (langtype_size == 0U ? 0U : 3U + langtype_size);
    const size_t total = KVSPACE_XVALUE_HEAD_LEN + body_size + attr_size;
    uint8_t *wire = calloc(1, total);
    if (wire == NULL) {
        return NULL;
    }
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, (uint64_t)body_size);
    kvspaceXvalueHeadSetAttrsLen(&head, (uint32_t)attr_size);
    head.storetype = KVSPACE_XVALUE_ST_SLACK;
    memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
    if (body_size != 0U) {
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, body_size);
    }
    if (langtype_size != 0U) {
        uint8_t *attr = wire + KVSPACE_XVALUE_HEAD_LEN + body_size;
        attr[0] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        attr[1] = (uint8_t)(langtype_size & 0xFFU);
        attr[2] = (uint8_t)(langtype_size >> 8U);
        memcpy(attr + 3, langtype, langtype_size);
    }
    *output_size = total;
    return wire;
}

static void counting_header(uint8_t *body, uint16_t reads, uint16_t writes,
                            uint8_t dynamic) {
    body[0] = (uint8_t)(reads & 0xFFU);
    body[1] = (uint8_t)(reads >> 8U);
    body[2] = (uint8_t)(writes & 0xFFU);
    body[3] = (uint8_t)(writes >> 8U);
    body[4] = dynamic;
}

static void store_add(fake_store_t *store, const char *key, const uint8_t *body,
                      size_t body_size, const char *langtype) {
    size_t size = 0U;
    uint8_t *wire = make_wire(body, body_size, langtype, &size);
    EXPECT(wire != NULL);
    store->entries[store->count++] =
        (fake_entry_t){.key = key, .value = wire, .size = size};
}

static void store_add_directory(fake_store_t *store, const char *key) {
    store->entries[store->count++] =
        (fake_entry_t){.key = key, .value = NULL, .size = 0U, .directory = true};
}

static void store_free(fake_store_t *store) {
    for (size_t i = 0U; i < store->count; i++) {
        free(store->entries[i].value);
    }
    store->count = 0U;
}

/* 基准树；flags 控制破坏点。 */
enum {
    TREE_OK = 0,
    TREE_NO_ANCHOR = 1,
    TREE_BAD_ANCHOR = 2,
    TREE_NO_OPCODE = 3,
    TREE_MISSING_READ_SLOT = 4,
    TREE_EMPTY_OPCODE = 5,
    TREE_NO_PARAMETER = 6,
    TREE_BAD_PARAMETER = 7,
};

static void build_tree(fake_store_t *store, int flags) {
    store_free(store);
    store_add_directory(store, "/lib/sum/");
    if (flags != TREE_NO_ANCHOR) {
        uint8_t anchor[5];
        counting_header(anchor, 2U, 1U, 0U);
        store_add(store, "/lib/sum/[0,0]",
                  flags == TREE_BAD_ANCHOR ? anchor : anchor,
                  flags == TREE_BAD_ANCHOR ? 4U : 5U, "rwfunc");
    }
    if (flags != TREE_NO_PARAMETER) {
        store_add(store, "/lib/sum.[0,-1]", (const uint8_t *)"a\0[]int64", 9U,
                  "def langtype");
    } else {
        /* 破坏：只定义第二个参数。 */
        store_add(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
    }
    if (flags != TREE_NO_PARAMETER) {
        store_add(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
    }
    store_add(store, "/lib/sum.[0,1]", (const uint8_t *)"R\0int64", 7U,
              "def langtype");
    if (flags != TREE_BAD_PARAMETER) {
        /* 上面的参数已写好；BAD_PARAMETER 用无 NUL 的 body 覆盖第一条。 */
    } else {
        store_free(store);
        store_add_directory(store, "/lib/sum/");
        uint8_t anchor[5];
        counting_header(anchor, 2U, 1U, 0U);
        store_add(store, "/lib/sum/[0,0]", anchor, 5U, "rwfunc");
        store_add(store, "/lib/sum.[0,-1]", (const uint8_t *)"int64", 5U,
                  "def langtype");
        store_add(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
        store_add(store, "/lib/sum.[0,1]", (const uint8_t *)"R\0int64", 7U,
                  "def langtype");
    }

    uint8_t opcode[5 + 16];
    counting_header(opcode, 2U, 1U, 0U);
    const char *name = "int64·add";
    memcpy(opcode + 5, name, strlen(name));
    store_add(store, "/lib/sum/[1,0]", opcode,
              flags == TREE_EMPTY_OPCODE ? 5U : 5U + strlen(name), "rwir");

    if (flags != TREE_MISSING_READ_SLOT) {
        uint8_t slot[5 + 32];
        counting_header(slot, 0U, 0U, 0U);
        const char *expression = "*[0,-1]\0int64";
        memcpy(slot + 5, expression, 13U);
        store_add(store, "/lib/sum/[1,-1]", slot, 5U + 13U, "rwir");
    }
    {
        /* 内联 int64 字面量：rank0 的 1。 */
        static const uint8_t literal[8] = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        store_add(store, "/lib/sum/[1,-2]", literal, sizeof(literal), "int64");
    }
    {
        uint8_t slot[5 + 32];
        counting_header(slot, 0U, 0U, 0U);
        const char *expression = "*[0,1]\0int64";
        memcpy(slot + 5, expression, 12U);
        store_add(store, "/lib/sum/[1,1]", slot, 5U + 12U, "rwir");
    }
    if (flags != TREE_NO_OPCODE) {
        uint8_t ret[5 + 8];
        counting_header(ret, 0U, 0U, 0U);
        memcpy(ret + 5, "return", 6U);
        store_add(store, "/lib/sum/[2,0]", ret, 5U + 6U, "rwir");
    } else {
        /* 缺 opcode：第 2 行只有读槽，没有 `[2,0]`。 */
        uint8_t stray[5 + 16];
        counting_header(stray, 0U, 0U, 0U);
        memcpy(stray + 5, "*[0,-1]", 7U);
        store_add(store, "/lib/sum/[2,-1]", stray, 5U + 7U, "rwir");
    }
}

static void init_services(fake_store_t *store, runtimec_host_services_t *out) {
    runtimec_host_services_initialize(out);
    out->context = store;
    out->allocator = (runtimec_allocator_t){NULL, test_allocate,
                                            test_deallocate};
    out->read = fake_read;
    out->read_header = fake_read_header;
    out->list = fake_list;
    out->source_revision = fake_source_revision;
}

static void test_happy_path(void) {
    fake_store_t store = {0};
    build_tree(&store, TREE_OK);
    runtimec_host_services_t services;
    init_services(&store, &services);
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t path =
        RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");

    {
        const runtimec_status_t open_status = vmkv_source_open_function(
            &services, path, &function, &snapshot, &error);
        EXPECT(open_status == RUNTIMEC_STATUS_SUCCESS);
        if (open_status != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "open failed: status=%d code=%lld msg=%.*s\n",
                    (int)open_status, (long long)error.code,
                    (int)error.message.size, error.message.data);
            store_free(&store);
            return;
        }
    }
    EXPECT(function != NULL);
    EXPECT(snapshot.source_revision.instance_identifier == 7U);
    EXPECT(snapshot.source_revision.generation == 0U);
    EXPECT(snapshot.function_key.size == path.size);

    vmkv_rwir_reader_t *reader = NULL;
    EXPECT(vmkv_rwir_reader_create(function, &reader, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_rwir_read_result_t result = VMKV_RWIR_READ_RESULT_INVALID;
    vmkv_rwir_instruction_view_t instruction;

    EXPECT(vmkv_rwir_reader_next(reader, &result, &instruction, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == VMKV_RWIR_READ_RESULT_INSTRUCTION);
    EXPECT(instruction.logical_program_counter == 1U);
    const char *expected_operation = "int64·add";
    EXPECT(instruction.operation_name.size == strlen(expected_operation));
    EXPECT(memcmp(instruction.operation_name.data, expected_operation,
                  strlen(expected_operation)) == 0);
    EXPECT(instruction.read_argument_count == 2U);
    EXPECT(instruction.write_argument_count == 1U);

    /* 读参 1：相对指针，保留为未解析的 key expression（DYNAMIC）。 */
    const vmkv_rwir_argument_view_t *first = &instruction.read_arguments[0];
    EXPECT(first->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC);
    EXPECT(first->key.size == 7U);
    EXPECT(memcmp(first->key.data, "*[0,-1]", 7U) == 0);
    EXPECT(first->parameter_name.size == 1U && first->parameter_name.data[0] == 'a');
    /* 具体优先：槽上写了 `int64`，声明的 `[]int64` 只是匹配类，不覆盖它。 */
    EXPECT(first->parameter_type_name.size == 5U);
    EXPECT(memcmp(first->parameter_type_name.data, "int64", 5U) == 0);

    /* 读参 2：内联字面量：STATIC + 槽自身 key + 字面量字节视图。 */
    const vmkv_rwir_argument_view_t *second = &instruction.read_arguments[1];
    EXPECT(second->key_addressability == VMKV_KEY_ADDRESSABILITY_STATIC);
    EXPECT(second->parameter_type_name.size == 5U);
    EXPECT(second->key.size == strlen("/lib/sum/[1,-2]"));
    EXPECT(memcmp(second->key.data, "/lib/sum/[1,-2]", second->key.size) == 0);
    /* 字面量视图：完整 wire 字节 + 同值 langtype，值就是 int64 的 1。 */
    EXPECT(runtimec_bytes_view_is_valid(second->literal.bytes));
    /* attr 区无前缀：16 + body 8 + 记录 (tag1 + len2 + "int64" 5)。 */
    EXPECT(second->literal.bytes.size ==
           KVSPACE_XVALUE_HEAD_LEN + 8U + 3U + 5U);
    EXPECT(second->literal.langtype.size == 5U);
    EXPECT(memcmp(second->literal.langtype.data, "int64", 5U) == 0);
    if (second->literal.bytes.size >= KVSPACE_XVALUE_HEAD_LEN + 8U) {
        int64_t literal_value = 0;
        memcpy(&literal_value,
               second->literal.bytes.data + KVSPACE_XVALUE_HEAD_LEN,
               sizeof(literal_value));
        EXPECT(literal_value == 1);
    }
    /* 变量引用侧：字面量视图必须为空。 */
    EXPECT(first->literal.bytes.size == 0U && first->literal.bytes.data == NULL);
    EXPECT(first->literal.langtype.size == 0U);

    /* 写参：相对指针 -> 命名参数 R:int64。 */
    const vmkv_rwir_argument_view_t *write = &instruction.write_arguments[0];
    EXPECT(write->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC);
    EXPECT(write->key.size == 6U);
    EXPECT(write->parameter_name.size == 1U && write->parameter_name.data[0] == 'R');
    EXPECT(write->parameter_type_name.size == 5U);

    EXPECT(vmkv_rwir_reader_next(reader, &result, &instruction, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == VMKV_RWIR_READ_RESULT_INSTRUCTION);
    EXPECT(instruction.logical_program_counter == 2U);
    EXPECT(instruction.operation_name.size == 6U);
    EXPECT(memcmp(instruction.operation_name.data, "return", 6U) == 0);
    EXPECT(instruction.read_argument_count == 0U);
    EXPECT(instruction.write_argument_count == 0U);

    EXPECT(vmkv_rwir_reader_next(reader, &result, &instruction, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(result == VMKV_RWIR_READ_RESULT_END);

    EXPECT(vmkv_rwir_reader_destroy(reader) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(vmkv_source_close_function(function) == RUNTIMEC_STATUS_SUCCESS);
    store_free(&store);
}

typedef struct malformed_case {
    int flags;
    runtimec_status_t expected_status;
    int64_t expected_code;
} malformed_case_t;

static void test_malformed(void) {
    const malformed_case_t cases[] = {
        {TREE_NO_ANCHOR, RUNTIMEC_STATUS_ERROR, 6},
        {TREE_BAD_ANCHOR, RUNTIMEC_STATUS_ERROR, 5},
        {TREE_NO_PARAMETER, RUNTIMEC_STATUS_ERROR, 7},
        {TREE_BAD_PARAMETER, RUNTIMEC_STATUS_ERROR, 5},
        {TREE_NO_OPCODE, RUNTIMEC_STATUS_ERROR, 8},
        {TREE_MISSING_READ_SLOT, RUNTIMEC_STATUS_ERROR, 9},
        {TREE_EMPTY_OPCODE, RUNTIMEC_STATUS_ERROR, 10},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fake_store_t store = {0};
        build_tree(&store, cases[i].flags);
        runtimec_host_services_t services;
        init_services(&store, &services);
        runtimec_error_t error;
        runtimec_snapshot_t snapshot;
        vmkv_function_source_t *function = NULL;
        const runtimec_string_view_t path =
            RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");
        const runtimec_status_t open_status = vmkv_source_open_function(
            &services, path, &function, &snapshot, &error);
        if (open_status != RUNTIMEC_STATUS_SUCCESS) {
            EXPECT(function == NULL);
            EXPECT(open_status == cases[i].expected_status);
            EXPECT(error.code == cases[i].expected_code);
            store_free(&store);
            continue;
        }
        /* 打开成功 = 畸形只出现在读取阶段。 */
        vmkv_rwir_reader_t *reader = NULL;
        EXPECT(vmkv_rwir_reader_create(function, &reader, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        vmkv_rwir_read_result_t result = VMKV_RWIR_READ_RESULT_INVALID;
        vmkv_rwir_instruction_view_t instruction;
        runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
        bool failed = false;
        for (int step = 0; step < 4; step++) {
            status = vmkv_rwir_reader_next(reader, &result, &instruction,
                                           &error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                failed = true;
                break;
            }
            if (result == VMKV_RWIR_READ_RESULT_END) {
                break;
            }
        }
        EXPECT(failed);
        if (!failed) {
            fprintf(stderr, "malformed case %zu (flags=%d) was accepted\n", i,
                    cases[i].flags);
        }
        EXPECT(status == cases[i].expected_status);
        if (failed) {
            EXPECT(error.code == cases[i].expected_code);
        }
        EXPECT(vmkv_rwir_reader_destroy(reader) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(vmkv_source_close_function(function) ==
               RUNTIMEC_STATUS_SUCCESS);
        store_free(&store);
    }
}

static void test_missing_capability(void) {
    fake_store_t store = {0};
    build_tree(&store, TREE_OK);
    runtimec_host_services_t services;
    init_services(&store, &services);
    services.read_header = NULL;
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t path =
        RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");
    EXPECT(vmkv_source_open_function(&services, path, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    EXPECT(function == NULL);
    EXPECT(error.code == 2);
    store_free(&store);
}

static void test_host_read_failure(void) {
    fake_store_t store = {0};
    build_tree(&store, TREE_OK);
    store.fail_read = true;
    runtimec_host_services_t services;
    init_services(&store, &services);
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t path =
        RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");
    EXPECT(vmkv_source_open_function(&services, path, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(function == NULL);
    store_free(&store);
}

static void test_invalid_request(void) {
    fake_store_t store = {0};
    runtimec_host_services_t services;
    init_services(&store, &services);
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t empty = RUNTIMEC_STRING_VIEW_EMPTY;
    EXPECT(vmkv_source_open_function(&services, empty, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_INVALID);
    EXPECT(vmkv_source_open_function(NULL, empty, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_INVALID);
    EXPECT(vmkv_source_close_function(NULL) == RUNTIMEC_STATUS_INVALID);
    EXPECT(vmkv_rwir_reader_create(NULL, NULL, &error) ==
           RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_happy_path();
    test_malformed();
    test_missing_capability();
    test_host_read_failure();
    test_invalid_request();
    if (failures != 0) {
        fprintf(stderr, "vmkv_reader_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("vmkv_reader_test: ok\n");
    return 0;
}
