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

#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <kvspace/kvspace.h>

#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "binrwir/signature.h"
#include "contract/kvspace_access.h"
#include "contract/snapshot.h"
#include "kvspace/xvalue_head.h"
#include "vmkv/encoder.h"
#include "vmkv/parameter_type.h"
#include "vmkv/local_types.h"
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

/*
 * 数据面：真 kvspace（shm）。本用例不再注入函数指针——`kvspace_access` 直连
 * kvspace ABI，没有实现表可换。
 *
 * **造树必须自检**：每条 key 写完立刻读回，不在就红。造树失败而测试仍然绿，
 * 是本批迁移最容易出现的假信号（key 写错位置 → 编码器走正常路径 → 断言照样过）。
 *
 * **清理**：后端把一个 DSN 摊成五份文件（.shm / .garb / .gen / .sbo.data /
 * .sbo.head），每份可达 128MB。只删 store.shm 会在 /tmp 留下上百 MB，症状是
 * 「别的测试莫名失败」。store_free 删全五个后缀。
 */
typedef struct fake_store {
    void *handle;
    char directory[256];
} fake_store_t;

/* 每个 store 一个独立目录：同一进程连两个 DSN 到同一个 shm 文件会让后端崩。 */
static bool store_open(fake_store_t *store);

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

static bool store_open(fake_store_t *store) {
    static int sequence = 0;
    memset(store, 0, sizeof(*store));
    snprintf(store->directory, sizeof(store->directory),
             "/tmp/runtimec-vmkv-encoder-%d-%d", (int)getpid(), sequence++);
    (void)mkdir(store->directory, 0700);
    char data_source[360];
    snprintf(data_source, sizeof(data_source), "shm://%s/store.shm",
             store->directory);
    store->handle = kvspaceConnect(data_source);
    return store->handle != NULL;
}

static void store_free(fake_store_t *store) {
    if (store->handle != NULL) {
        kvspaceClose(store->handle);
        store->handle = NULL;
    }
    static const char *const suffixes[] = {"store.shm", "store.shm.garb",
                                           "store.shm.gen", "store.shm.sbo.data",
                                           "store.shm.sbo.head"};
    char path[512];
    for (size_t index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]);
         index++) {
        snprintf(path, sizeof(path), "%s/%s", store->directory, suffixes[index]);
        (void)unlink(path);
    }
    (void)rmdir(store->directory);
}

/* head(长度字段) + body + attr(langtype) 的 wire。 */
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
    head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;
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

/* 只依赖 C11：不用 strdup。 */
static void store_add(fake_store_t *store, const char *key, const uint8_t *body,
                      size_t body_size, const char *langtype) {
    size_t size = 0U;
    uint8_t *wire = make_wire(body, body_size, langtype, &size);
    EXPECT(wire != NULL);
    if (wire == NULL) {
        return;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int written =
        kvspaceSetValue(store->handle, key, wire, (uint32_t)size, error_text,
                        (uint32_t)sizeof(error_text));
    free(wire);
    EXPECT(written == 0);
    if (written != 0) {
        fprintf(stderr, "store_add failed for %s: %s\n", key, error_text);
        return;
    }
    /* 读回自检：造树失败却不报，测试会「绿着但测不到东西」。 */
    uint8_t *readback = NULL;
    uint32_t readback_size = 0U;
    const bool present =
        kvspaceGet(store->handle, key, 1, &readback, &readback_size) == 0 &&
        readback != NULL && readback_size != 0U;
    EXPECT(present);
    if (!present) {
        fprintf(stderr, "store_add self-check failed: %s is not readable\n",
                key);
    }
}

static void store_add_directory(fake_store_t *store, const char *key) {
    char error_text[256];
    error_text[0] = '\0';
    EXPECT(kvspaceMkindex(store->handle, key, 0U, error_text,
                          (uint32_t)sizeof(error_text)) == 0);
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

/* `‥ctime`：source revision 的唯一单调源事实；没有它，编码器打开阶段就失败。 */
static void add_ctime(fake_store_t *store, const char *path) {
    char key[192];
    snprintf(key, sizeof(key), "%s/\xE2\x80\xA5" "ctime", path);
    uint8_t body[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    store_add(store, key, body, sizeof(body), "int64");
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
    add_ctime(store, path);
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

/* opcode 槽；`langtype` 决定它是普通指令（rwir）还是一次 rwfunc 调用（rwfunc）。 */
static void add_opcode_with_langtype(fake_store_t *store, const char *path,
                                     int line, uint16_t reads, uint16_t writes,
                                     const char *operation,
                                     const char *langtype) {
    char key[128];
    uint8_t body[128];
    const size_t text_size = strlen(operation);
    counting_header(body, reads, writes, 0U);
    memcpy(body + 5, operation, text_size);
    key_of(key, sizeof(key), path, line, 0);
    store_add(store, key, body, 5U + text_size, langtype);
}

static void add_opcode(fake_store_t *store, const char *path, int line,
                       uint16_t reads, uint16_t writes, const char *operation) {
    add_opcode_with_langtype(store, path, line, reads, writes, operation,
                             "rwir");
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

/*
 * 造一个 access 实例：直连 harness 的真 kvspace 句柄，**没有函数指针可注入**。
 * `instance_identifier` 与 expected_snapshot 里的一致（7），否则编码器会判源变了。
 */
static bool make_host_services(fake_store_t *store,
                               runtimec_kvspace_access_t **output,
                               runtimec_error_t *error) {
    const runtimec_kvspace_access_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = store->handle,
        .instance_identifier = UINT64_C(7),
    };
    *output = NULL;
    return runtimec_kvspace_access_create(&request, output, error) ==
               RUNTIMEC_STATUS_SUCCESS &&
           *output != NULL;
}

static vmkv_encode_request_t make_request(runtimec_kvspace_access_t *host,
                                          const char *path) {
    vmkv_encode_request_t request;
    memset(&request, 0, sizeof(request));
    request.kvspace_access = host;
    request.function_path = runtimec_string_view_from_c_string(path);
    request.expected_snapshot.contract_version = runtimec_contract_version();
    runtimec_source_revision_reset(&request.expected_snapshot.source_revision);
    request.expected_snapshot.source_revision.instance_identifier = 7U;
    /* 真后端首次取 revision 即推进一次 ⇒ generation 从 1 起算；旧假存储直接吐 11。
     * instruction_set_version 来自 `‥ctime` body（本 harness 写 1）。 */
    request.expected_snapshot.source_revision.generation = 1U;
    request.expected_snapshot.source_revision.schema_version = 0U;
    request.expected_snapshot.source_revision.instruction_set_version = 1U;
    request.expected_snapshot.function_key =
        runtimec_key_view_from_c_string(path);
    request.expected_snapshot.source_position_text =
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
    EXPECT((read_count == 0U || source->read_arguments.data != NULL) &&
           (write_count == 0U || source->write_arguments.data != NULL));
    if (read_count + write_count > capacity ||
        (read_count != 0U && source->read_arguments.data == NULL) ||
        (write_count != 0U && source->write_arguments.data == NULL)) {
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
static bool make_builder(const runtimec_kvspace_access_t *host,
                         binrwir_binfunc_builder_t **output_builder) {
    const binrwir_binfunc_identity_t identity = {
        .snapshot_generation = 11U,
        .program_generation = 1U,
        .schema_version = 1U,
        .rwir_manifest_version = 1U,
        .binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision = 1U,
    };
    return binrwir_binfunc_builder_create(&host->allocator, &identity,
                                          output_builder) ==
           RUNTIMEC_STATUS_SUCCESS;
}

/* 读第 instruction_ordinal 条指令（0 起）并按 canonical 形状编码。 */
static runtimec_status_t encode_instruction_at(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    uint32_t instruction_ordinal, vmkv_encode_function_t function,
    test_canonical_t *output, runtimec_error_t *error) {
    memset(output, 0, sizeof(*output));
    /* 非形参引用的 argidx 从形参区间之后开始，与 vmkv_encode 同一规则。 */
    function.next_operand_argument_index = function.read_parameter_count +
                                           function.write_parameter_count;
    binrwir_binfunc_builder_t *builder = NULL;
    if (!make_builder(request->kvspace_access, &builder)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    vmkv_function_source_t *source = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    runtimec_status_t status = vmkv_source_open_function(
        request->kvspace_access, request->function_path, &source, &snapshot,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    vmkv_local_types_t *local_types = NULL;
    status = vmkv_local_types_collect(source, request->kvspace_access->allocator,
                                      &local_types, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        binrwir_binfunc_builder_destroy(builder);
        vmkv_source_close_function(source);
        return status;
    }
    vmkv_rwir_reader_t *reader = NULL;
    status = vmkv_rwir_reader_create(source, &reader, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        vmkv_rwir_read_result_t read_result = VMKV_RWIR_READ_RESULT_INVALID;
        vmkv_rwir_instruction_view_t instruction;
        for (uint32_t ordinal = 0U;
             ordinal <= instruction_ordinal &&
             status == RUNTIMEC_STATUS_SUCCESS;
             ++ordinal) {
            status =
                vmkv_rwir_reader_next(reader, &read_result, &instruction, error);
        }
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            EXPECT(read_result == VMKV_RWIR_READ_RESULT_INSTRUCTION);
            vmkv_canonical_instruction_t canonical;
            status = vmkv_encode_instruction(encoder, request, builder,
                                             local_types, &function,
                                             &instruction, &canonical, error);
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                test_canonical_copy(output, &canonical);
            }
        }
        vmkv_rwir_reader_destroy(reader);
    }
    vmkv_local_types_destroy(local_types,
                             request->kvspace_access->allocator);
    binrwir_binfunc_builder_destroy(builder);
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
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    build_add_function(&store, "/lib/enc", "add");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);

    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    const runtimec_status_t status = encode_instruction_at(
        encoder, &request, 0U, function, &instruction, &error);
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
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* `+` 与 `add` 必须解析到同一个能力行、同一个 opid。 */
static void test_plus_and_add_share_one_operation_identifier(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    build_add_function(&store, "/lib/enc", "add");
    build_add_function(&store, "/lib/plus", "+");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        store_free(&store);
        return;
    }
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t add_instruction;
    test_canonical_t plus_instruction;
    vmkv_encode_request_t add_request = make_request(host, "/lib/enc");
    vmkv_encode_request_t plus_request = make_request(host, "/lib/plus");
    EXPECT(encode_instruction_at(
        encoder, &add_request, 0U, function,
                                    &add_instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(encode_instruction_at(
        encoder, &plus_request, 0U, function,
                                    &plus_instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(add_instruction.instruction.operation_identifier ==
           plus_instruction.instruction.operation_identifier);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 并集参数声明：BinRWIR 只接受单一具体类型。 */
static void test_union_parameter_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/union", 1U, 1U);
    add_parameter(&store, "/lib/union", -1, "a", "int64|float64");
    add_parameter(&store, "/lib/union", 1, "R", "int64");
    add_opcode(&store, "/lib/union", 1, 1U, 1U, "add");
    add_reference(&store, "/lib/union", 1, -1, "*[0,-1]", "int64|float64");
    add_reference(&store, "/lib/union", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/union");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(error.code == 24);
    EXPECT(instruction.instruction.read_arguments.data == NULL);
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(program == NULL);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * 声明是并集、但操作数槽写的是具体类型：具体优先，按 int16 编码。
 * 这正是 layout 写出实参具体类型后要走的路径。
 */
static void test_concrete_operand_type_wins_over_declaration(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/concrete", 1U, 1U);
    add_parameter(&store, "/lib/concrete", -1, "a", "int8|int16");
    add_parameter(&store, "/lib/concrete", 1, "R", "int16");
    add_opcode(&store, "/lib/concrete", 1, 1U, 1U, "abs");
    add_reference(&store, "/lib/concrete", 1, -1, "*[0,-1]", "int16");
    add_reference(&store, "/lib/concrete", 1, 1, "*[0,1]", "int16");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/concrete");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(view_equals(instruction.instruction.read_parameter_types[0], "int16"));
    /* SAME_FIRST：写参类型就是读参的具体类型。 */
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "int16"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* alias policy：declared 的行（`=` 即 copy）产生 (read, write) MAY_ALIAS 对。 */
static void test_alias_policy_becomes_signature_rule(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/copyfn", 1U, 1U);
    add_parameter(&store, "/lib/copyfn", -1, "a", "int64");
    add_parameter(&store, "/lib/copyfn", 1, "R", "int64");
    add_opcode(&store, "/lib/copyfn", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/copyfn", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/copyfn", 1, 1, "*[0,1]", "int64");
    build_add_function(&store, "/lib/enc", "add");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        store_free(&store);
        return;
    }
    vmkv_encode_request_t copy_request = make_request(host, "/lib/copyfn");
    vmkv_encode_request_t add_request = make_request(host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);

    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &copy_request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        binrwir_binfunc_view_t view;
        EXPECT(binrwir_binfunc_get_view(program, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view.signature.alias_rule_count == 1U);
        binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
        EXPECT(binrwir_signature_get_alias_policy(&view.signature, 0U, 0U,
                                                  &policy) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(policy == BINRWIR_ALIAS_MAY_ALIAS);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }

    program = NULL;
    EXPECT(vmkv_encode(encoder, &add_request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        binrwir_binfunc_view_t view;
        EXPECT(binrwir_binfunc_get_view(program, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        /* forbidden：不产生规则，也不猜成 NO_ALIAS。 */
        EXPECT(view.signature.alias_rule_count == 0U);
        binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
        EXPECT(binrwir_signature_get_alias_policy(&view.signature, 0U, 0U,
                                                  &policy) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * 指针结果：**写侧掩码只有 POINTER 一位**时信能力行、放行。
 *
 * 旧判据是「掩码含 POINTER 就整条 UNSUPPORTED」，于是 `kvspace/key·abs`
 * （写掩码 0x00040000，且**没有读参**）被挡死，`&x` 进不了 BinRWIR。
 * 现在只有「该侧是多位并集且操作数是裸指针文本」才拒绝。
 *
 * 注：今天 catalog 里**没有任何一行**的 POINTER 位与其它位并集，
 * 所以「并集拒绝」与「无操作数拒绝」两条分支没有可构造的真实能力行——
 * 它们是防御分支，这里只钉住可达的那一条（见编码器调用点注释）。
 */
static void test_pointer_result_from_single_bit_mask_is_encodable(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    /* 与真实能力行一致：`kvspace/key·abs` 是 **0 读 1 写**（读掩码为 0）。 */
    add_function(&store, "/lib/ptraddr", 0U, 1U);
    add_parameter(&store, "/lib/ptraddr", 1, "R", "*int64");
    add_opcode(&store, "/lib/ptraddr", 1, 0U, 1U, "kvspace/key·abs");
    add_reference(&store, "/lib/ptraddr", 1, 1, "*[0,1]", "*int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/ptraddr");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 混合符号算术：矩阵裁决为拒绝。 */
static void test_mixed_signed_unsigned_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/mix", 2U, 1U);
    add_parameter(&store, "/lib/mix", -1, "a", "int64");
    add_parameter(&store, "/lib/mix", -2, "b", "uint64");
    add_parameter(&store, "/lib/mix", 1, "R", "int64");
    add_opcode(&store, "/lib/mix", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/mix", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/mix", 1, -2, "*[0,-2]", "uint64");
    add_reference(&store, "/lib/mix", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/mix");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(error.code == 27);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * `!=` 在 char/pointer/bool 上改选专用「不等于」能力行（不取反）：
 * catalog 的 Invert 位已全部为 false，指令头因此不需要取反位。
 */
static void test_not_equal_specialization_selects_dedicated_row(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/noteq", 2U, 1U);
    add_parameter(&store, "/lib/noteq", -1, "a", "bool");
    add_parameter(&store, "/lib/noteq", -2, "b", "bool");
    add_parameter(&store, "/lib/noteq", 1, "R", "bool");
    add_opcode(&store, "/lib/noteq", 1, 2U, 1U, "!=");
    add_reference(&store, "/lib/noteq", 1, -1, "*[0,-1]", "bool");
    add_reference(&store, "/lib/noteq", 1, -2, "*[0,-2]", "bool");
    add_reference(&store, "/lib/noteq", 1, 1, "*[0,1]", "bool");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/noteq");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    /* BOOLEAN_NOT_EQUAL = 0x002B；BOOLEAN 结果规则给出 bool。 */
    EXPECT(instruction.instruction.operation_identifier == 0x002BU);
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "bool"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 内联字面量：LITERAL + 池序号，参数类型取字面量自己的类型。 */
static void test_inline_literal_uses_literal_reference(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/lit", 2U, 1U);
    add_parameter(&store, "/lib/lit", -1, "a", "int64");
    add_parameter(&store, "/lib/lit", -2, "b", "int64");
    add_parameter(&store, "/lib/lit", 1, "R", "int64");
    add_opcode(&store, "/lib/lit", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/lit", 1, -1, "*[0,-1]", "int64");
    add_inline_literal(&store, "/lib/lit", 1, -2, 1, "int64");
    add_reference(&store, "/lib/lit", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/lit");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[1].reference_kind ==
           BINRWIR_REFERENCE_LITERAL);
    EXPECT(instruction.arguments[1].payload == 0U);
    /* 非形参引用的 argidx 从 nr+nw = 3 起按出现次序递增。 */
    EXPECT(instruction.arguments[1].argument_index == 3U);
    EXPECT(view_equals(instruction.instruction.read_parameter_types[1], "int64"));
    EXPECT(view_equals(instruction.instruction.write_parameter_types[0], "int64"));
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 绝对路径实参：STATIC + host 基址推导出的 body 地址。 */
static void test_static_path_derives_body_address(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/abs", 2U, 1U);
    add_parameter(&store, "/lib/abs", -1, "a", "int64");
    add_parameter(&store, "/lib/abs", -2, "b", "int64");
    add_parameter(&store, "/lib/abs", 1, "R", "int64");
    add_opcode(&store, "/lib/abs", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/abs", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/abs", 1, -2, "/v/x", "int64");
    add_reference(&store, "/lib/abs", 1, 1, "*[0,1]", "int64");
    store_add(&store, "/v/x", (const uint8_t *)"\0\0\0\0\0\0\0\0", 8U, "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/abs");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(
        encoder, &request, 0U, function, &instruction,
                                    &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[1].reference_kind ==
           BINRWIR_REFERENCE_STATIC);
    EXPECT(instruction.arguments[1].argument_index == 3U);
    /* 基址由**真后端**给出（不再固定 0x10000）：判据是「payload 恰为该基址 +
     * 固定头长度」，即目标是值体地址而不是 wire 起点。把基址读回来再比，
     * 断言才不依赖某个假存储的返回值。 */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0U;
        EXPECT(kvspaceGet(store.handle, "/v/x", 1, &wire, &wire_size) == 0);
        EXPECT(wire != NULL);
        const uint64_t base = (uint64_t)(uintptr_t)wire;
        EXPECT(instruction.arguments[1].payload ==
               base + KVSPACE_XVALUE_HEAD_LEN);
    }
    /* STATIC 的 slot 是符号池下标：vmbin 按 key 访问的依据。 */
    EXPECT(instruction.arguments[1].slot == 0U);

    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t symbol_type = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_binfunc_get_symbol(program, instruction.arguments[1].slot,
                                          &symbol, &symbol_type) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(symbol, "/v/x"));
        /* STATIC 登记的是目标路径与目标值类型，不是局部槽。 */
        EXPECT(view_equals(symbol_type, "int64"));
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 具名帧槽：裸标识符是帧局部变量名，登进 Program 符号池（LOCAL）并带类型。 */
static void test_named_frame_slot_becomes_local_symbol(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/local", 2U, 1U);
    add_parameter(&store, "/lib/local", -1, "a", "int64");
    add_parameter(&store, "/lib/local", -2, "b", "int64");
    add_parameter(&store, "/lib/local", 1, "R", "int64");
    /* 第 1 条先给 temporary 定值，第 2 条才用它：局部必须有写位置。 */
    add_opcode(&store, "/lib/local", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/local", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/local", 1, 1, "temporary", "int64");
    add_opcode(&store, "/lib/local", 2, 2U, 1U, "add");
    add_reference(&store, "/lib/local", 2, -1, "temporary", "int64");
    add_reference(&store, "/lib/local", 2, -2, "*[0,-2]", "int64");
    add_reference(&store, "/lib/local", 2, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/local");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 2U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(encoder, &request, 1U, function, &instruction,
                                 &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.arguments[0].reference_kind ==
           BINRWIR_REFERENCE_LOCAL);
    EXPECT(instruction.arguments[0].payload == 0U);
    /* 裸名不消耗形参编号：argidx 从 nr+nw = 3 起按出现次序递增
     * （vmkv_encode 里该计数器跨整份 Program 递增）。 */
    EXPECT(instruction.arguments[0].argument_index == 3U);

    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t symbol_type = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_binfunc_get_symbol(program, instruction.arguments[0].slot,
                                          &symbol, &symbol_type) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(symbol, "temporary"));
        /* 局部符号带类型：route 靠它决定槽的长度。 */
        EXPECT(view_equals(symbol_type, "int64"));
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 帧内槽表达式 `*[d,±k]`（d 非 0）：没有可编译的槽契约 → UNSUPPORTED。 */
static void test_framed_slot_expression_is_unsupported(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/framed", 2U, 1U);
    add_parameter(&store, "/lib/framed", -1, "a", "int64");
    add_parameter(&store, "/lib/framed", -2, "b", "int64");
    add_parameter(&store, "/lib/framed", 1, "R", "int64");
    add_opcode(&store, "/lib/framed", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/framed", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/framed", 1, -2, "*[1,-1]", "int64");
    add_reference(&store, "/lib/framed", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/framed");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(program == NULL);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * 能力外的 opcode 不再报错：它被编成 FOREIGN 结构标记，只携带 source pc，
 * 零读参零写参。它既不做类型收敛，也不登符号池或消耗 argidx。
 */
static void test_unknown_operation_becomes_foreign(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/code", 1U, 1U);
    add_parameter(&store, "/lib/code", -1, "a", "int64");
    add_parameter(&store, "/lib/code", 1, "R", "int64");
    /*
     * 用测试自声明的 `ext·probe` 而不是 `println`：`println` 在
     * `RUNTIMEC_TEMP_PRINTLN=ON` 的构建里是 runtimec 的本地实现、不再 FOREIGN，
     * 这个测试的期望会随构建变化。`ext·probe` 两个构建都必然是 FOREIGN。
     */
    add_opcode(&store, "/lib/code", 1, 1U, 1U, "ext·probe");
    add_reference(&store, "/lib/code", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/code", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/code");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 1U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(encoder, &request, 0U, function, &instruction,
                                 &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.instruction.operation_identifier ==
           BINRWIR_OPERATION_IDENTIFIER_FOREIGN);
    EXPECT(instruction.instruction.read_arguments.count == 0U);
    EXPECT(instruction.instruction.write_arguments.count == 0U);
    EXPECT(instruction.instruction.logical_program_counter == 1U);

    /* 整份 Program 里它仍是一条合法指令，且 Program 校验通过。 */
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        binrwir_instruction_view_t view;
        EXPECT(binrwir_binfunc_get_instruction(program, 0U, &view) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view.instruction->operation_identifier ==
               BINRWIR_OPERATION_IDENTIFIER_FOREIGN);
        EXPECT(view.source_logical_program_counter == 1U);
        EXPECT(view.read_arguments.count == 0U);
        EXPECT(view.write_arguments.count == 0U);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * 覆盖完整性：`[1..N]` 每个 source pc 必须恰好归属一条指令，这一条由读取器
 * 保证（从 1 起逐行递增，任何一行缺 opcode 槽即 MISSING_OPCODE）。编码器不再
 * 重复判——这里钉住「洞在读取层就失败」，让「编码器不需要第二份判据」这个
 * 结论有可执行的依据。
 */
static void test_missing_source_program_counter_fails_in_reader(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/gap", 1U, 1U);
    add_parameter(&store, "/lib/gap", -1, "a", "int64");
    add_parameter(&store, "/lib/gap", 1, "R", "int64");
    /* 第 1 行缺席，只有第 2 行。 */
    add_opcode(&store, "/lib/gap", 2, 1U, 1U, "=");
    add_reference(&store, "/lib/gap", 2, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/gap", 2, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/gap");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_ERROR);
    /* 8 = vmkv/source 的 MISSING_OPCODE：洞在读层就被挡住。 */
    EXPECT(error.code == 8);
    EXPECT(program == NULL);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 全函数编码：Program 的签名、opid、参数与 canonical 指令逐字段一致。 */
static void test_program_encoding_matches_canonical_instruction(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    build_add_function(&store, "/lib/enc", "add");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/enc");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_binfunc_t *program = NULL;
    const runtimec_status_t status = vmkv_encode(encoder, &request, &program,
                                                 &error);
    EXPECT(status == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(program != NULL);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        binrwir_binfunc_view_t view;
        EXPECT(binrwir_binfunc_get_view(program, &view) ==
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
        EXPECT(binrwir_binfunc_get_instruction(program, 0U, &instruction) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(instruction.instruction->operation_identifier == 0x0002U);
        EXPECT(instruction.source_logical_program_counter == 1U);
        EXPECT(instruction.read_arguments.count == 2U);
        EXPECT(instruction.write_arguments.count == 1U);
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 请求快照与源 revision 不一致：拒绝，不产生 Program。 */
static void test_stale_snapshot_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    build_add_function(&store, "/lib/enc", "add");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/enc");
    request.expected_snapshot.source_revision.generation = 12U;
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_INVALID);
    EXPECT(program == NULL);
    EXPECT(error.code == 33);
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}



/*
 * 符号池按字节字典序在 finalize 规范化：登记序（`a` 先、`/v/x` 后）与规范序
 * （`/v/x` 先、`a` 后）相反。断言只能用「下标回查得到原名字」的 round-trip——
 * 单符号是恒等映射，永远测不出漏重定位；错位表现为校验通过、名字是错的。
 */
static void test_symbol_pool_normalization_remaps_every_reference(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/two", 2U, 1U);
    add_parameter(&store, "/lib/two", -1, "a", "int64");
    add_parameter(&store, "/lib/two", -2, "b", "int64");
    add_parameter(&store, "/lib/two", 1, "R", "int64");
    /* 第 1 条给局部 a 定值，第 2 条同时读 a 与绝对路径：局部与 STATIC 同现。 */
    add_opcode(&store, "/lib/two", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/two", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/two", 1, 1, "a", "int64");
    add_opcode(&store, "/lib/two", 2, 2U, 1U, "add");
    add_reference(&store, "/lib/two", 2, -1, "a", "int64");
    add_reference(&store, "/lib/two", 2, -2, "/v/x", "int64");
    add_reference(&store, "/lib/two", 2, 1, "*[0,1]", "int64");
    store_add(&store, "/v/x", (const uint8_t *)"\0\0\0\0\0\0\0\0", 8U,
              "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/two");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        binrwir_instruction_view_t instruction;
        EXPECT(binrwir_binfunc_get_instruction(program, 1U, &instruction) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(instruction.read_arguments.count == 2U);
        EXPECT(instruction.read_arguments.data[0].reference_kind ==
               BINRWIR_REFERENCE_LOCAL);
        EXPECT(instruction.read_arguments.data[1].reference_kind ==
               BINRWIR_REFERENCE_STATIC);
        runtimec_string_view_t local_symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t static_symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t local_type = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t static_type = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_binfunc_get_symbol(
                   program, instruction.read_arguments.data[0].slot,
                   &local_symbol, &local_type) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(binrwir_binfunc_get_symbol(
                   program, instruction.read_arguments.data[1].slot,
                   &static_symbol, &static_type) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(local_symbol, "a"));
        EXPECT(view_equals(static_symbol, "/v/x"));
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/*
 * rwfunc 调用：opcode 槽的 langtype 是 `rwfunc`，body 是目标函数路径。
 *
 * 这是 layout 的真实形态（`new_rwfunc_call` 把计数头与路径写在一起）。调用目标由
 * **槽类型**判定，不靠名字形状——拼错的裸名也可能以 `/lib/` 开头，靠名字猜会把
 * 它当成合法调用。
 *
 * 编码结果是：一个 FUNCTION 读参（指向符号池里的**函数名**，不含 `/lib/`），
 * 后面跟这次调用的真实实参。
 */
static void test_call_target_becomes_function_symbol(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/callfn", 1U, 0U);
    add_parameter(&store, "/lib/callfn", -1, "a", "int64");
    add_opcode_with_langtype(&store, "/lib/callfn", 1, 1U, 0U, "/lib/add1",
                             "rwfunc");
    add_inline_literal(&store, "/lib/callfn", 1, -1, 41, "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/callfn");
    vmkv_encoder_t *encoder = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(vmkv_encoder_create(host->allocator, &encoder, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    vmkv_encode_function_t function = {.read_parameter_count = 1U,
                                       .write_parameter_count = 0U};
    test_canonical_t instruction;
    EXPECT(encode_instruction_at(encoder, &request, 0U, function, &instruction,
                                 &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(instruction.instruction.operation_identifier == 0x0024U);
    EXPECT(instruction.instruction.read_arguments.count == 2U);
    EXPECT(instruction.arguments[0].reference_kind ==
           BINRWIR_REFERENCE_FUNCTION);
    EXPECT(instruction.arguments[0].payload == 0U);
    EXPECT(instruction.arguments[1].reference_kind == BINRWIR_REFERENCE_LITERAL);

    binrwir_binfunc_t *program = NULL;
    EXPECT(vmkv_encode(encoder, &request, &program, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    if (program != NULL) {
        EXPECT(binrwir_binfunc_validate(program) == RUNTIMEC_STATUS_SUCCESS);
        runtimec_string_view_t symbol = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t symbol_type = runtimec_string_view_make(NULL, 0U);
        EXPECT(binrwir_binfunc_get_symbol(program, instruction.arguments[0].slot,
                                          &symbol, &symbol_type) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(view_equals(symbol, "add1"));
        EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    }
    EXPECT(vmkv_encoder_destroy(encoder) == RUNTIMEC_STATUS_SUCCESS);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 打开函数源并跑局部类型采集；成功后由调用方关闭源。 */
static runtimec_status_t collect_local_types(
    const vmkv_encode_request_t *request, runtimec_allocator_t allocator,
    vmkv_local_types_t **output_local_types, vmkv_function_source_t **output_source,
    runtimec_error_t *error) {
    *output_local_types = NULL;
    *output_source = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    runtimec_status_t status = vmkv_source_open_function(
        request->kvspace_access, request->function_path, output_source, &snapshot,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmkv_local_types_collect(*output_source, allocator,
                                      output_local_types, error);
    return status;
}

/* 同一局部在两条指令被写成同一类型：采集成功，类型唯一。 */
static void test_local_types_collects_unique_write_type(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/uniq", 1U, 1U);
    add_parameter(&store, "/lib/uniq", -1, "a", "int64");
    add_parameter(&store, "/lib/uniq", 1, "R", "int64");
    add_opcode(&store, "/lib/uniq", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/uniq", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/uniq", 1, 1, "x", "int64");
    add_opcode(&store, "/lib/uniq", 2, 1U, 1U, "=");
    add_reference(&store, "/lib/uniq", 2, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/uniq", 2, 1, "x", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/uniq");
    vmkv_local_types_t *local_types = NULL;
    vmkv_function_source_t *source = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(collect_local_types(&request, host->allocator, &local_types, &source,
                               &error) == RUNTIMEC_STATUS_SUCCESS);
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    EXPECT(vmkv_local_types_find(local_types,
                                 runtimec_string_view_from_c_string("x"),
                                 &langtype));
    EXPECT(view_equals(langtype, "int64"));
    EXPECT(!vmkv_local_types_find(local_types,
                                  runtimec_string_view_from_c_string("nope"),
                                  &langtype));
    (void)vmkv_local_types_destroy(local_types, host->allocator);
    vmkv_source_close_function(source);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 同一局部被写成两种类型：编译期失败，不选其一、不做提升。 */
static void test_local_types_conflict_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/conflict", 1U, 1U);
    add_parameter(&store, "/lib/conflict", -1, "a", "int64");
    add_parameter(&store, "/lib/conflict", 1, "R", "int64");
    add_opcode(&store, "/lib/conflict", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/conflict", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/conflict", 1, 1, "x", "int64");
    add_opcode(&store, "/lib/conflict", 2, 1U, 1U, "=");
    add_reference(&store, "/lib/conflict", 2, -1, "*[0,-1]", "float64");
    add_reference(&store, "/lib/conflict", 2, 1, "x", "float64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/conflict");
    vmkv_local_types_t *local_types = NULL;
    vmkv_function_source_t *source = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(collect_local_types(&request, host->allocator, &local_types, &source,
                               &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(error.code == 41);
    EXPECT(local_types == NULL);
    vmkv_source_close_function(source);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 只读不写的局部：没有定值就没有槽大小，失败封闭。 */
static void test_local_types_read_only_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/readonly", 2U, 1U);
    add_parameter(&store, "/lib/readonly", -1, "a", "int64");
    add_parameter(&store, "/lib/readonly", -2, "b", "int64");
    add_parameter(&store, "/lib/readonly", 1, "R", "int64");
    add_opcode(&store, "/lib/readonly", 1, 2U, 1U, "add");
    add_reference(&store, "/lib/readonly", 1, -1, "x", "int64");
    add_reference(&store, "/lib/readonly", 1, -2, "*[0,-2]", "int64");
    add_reference(&store, "/lib/readonly", 1, 1, "*[0,1]", "int64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/readonly");
    vmkv_local_types_t *local_types = NULL;
    vmkv_function_source_t *source = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(collect_local_types(&request, host->allocator, &local_types, &source,
                               &error) == RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(error.code == 42);
    EXPECT(local_types == NULL);
    vmkv_source_close_function(source);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

/* 写位置的类型不具体（并集）：推不出槽大小，失败封闭。 */
static void test_local_types_non_concrete_write_is_rejected(void) {
    fake_store_t store;
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    add_function(&store, "/lib/ambiguous", 1U, 1U);
    add_parameter(&store, "/lib/ambiguous", -1, "a", "int64");
    add_parameter(&store, "/lib/ambiguous", 1, "R", "int64");
    add_opcode(&store, "/lib/ambiguous", 1, 1U, 1U, "=");
    add_reference(&store, "/lib/ambiguous", 1, -1, "*[0,-1]", "int64");
    add_reference(&store, "/lib/ambiguous", 1, 1, "x", "int64|float64");
    runtimec_kvspace_access_t *host = NULL;
    runtimec_error_t host_error;
    runtimec_error_reset(&host_error);
    EXPECT(make_host_services(&store, &host, &host_error));
    if (host == NULL) {
        (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
        return;
    }
    vmkv_encode_request_t request = make_request(host, "/lib/ambiguous");
    vmkv_local_types_t *local_types = NULL;
    vmkv_function_source_t *source = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(collect_local_types(&request, host->allocator, &local_types, &source,
                               &error) == RUNTIMEC_STATUS_UNSUPPORTED);
    EXPECT(error.code == 43);
    EXPECT(local_types == NULL);
    vmkv_source_close_function(source);
    (void)runtimec_kvspace_access_destroy(host, &host_error);
    store_free(&store);
}

static void test_parameter_type_classification(void) {
    uint32_t type_set = 0U;
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_INT64);
    /* 标量集里**不含**指针位：类别位判错要到特化改选时才炸，离根因很远。 */
    EXPECT((type_set & VMKV_RWIR_TYPE_SET_POINTER) == 0U);
    /* 指针：首字节 `*` 即指针类；`**T`（rwfunc 帧槽）必须同支。 */
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("*int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_POINTER);
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("**int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_POINTER);
    EXPECT(vmkv_parameter_type_classify(
               runtimec_string_view_from_c_string("*[3]int64"), &type_set) ==
           VMKV_PARAMETER_TYPE_RESULT_SINGLE);
    EXPECT(type_set == VMKV_RWIR_TYPE_SET_POINTER);
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
    test_pointer_result_from_single_bit_mask_is_encodable();
    test_mixed_signed_unsigned_is_rejected();
    test_not_equal_specialization_selects_dedicated_row();
    test_inline_literal_uses_literal_reference();
    test_static_path_derives_body_address();
    test_named_frame_slot_becomes_local_symbol();
    test_framed_slot_expression_is_unsupported();
    test_unknown_operation_becomes_foreign();
    test_missing_source_program_counter_fails_in_reader();
    test_program_encoding_matches_canonical_instruction();
    test_stale_snapshot_is_rejected();
    test_symbol_pool_normalization_remaps_every_reference();
    test_call_target_becomes_function_symbol();
    test_local_types_collects_unique_write_type();
    test_local_types_conflict_is_rejected();
    test_local_types_read_only_is_rejected();
    test_local_types_non_concrete_write_is_rejected();
    test_parameter_type_classification();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vmkv encoder: ok\n");
    return 0;
}
