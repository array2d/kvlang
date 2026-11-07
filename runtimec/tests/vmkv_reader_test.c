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
 * **清理**：后端把一个 DSN 摊成多份文件（.shm / .shm.garb / .shm.gen /
 * .shm.sbo.data / .shm.sbo.head），每份可达 128MB。只删 store.shm 会在 /tmp
 * 留下上百 MB，症状是「别的测试莫名失败」。store_free 删全五个后缀。
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
#include <kvspace/xvalue_head.h>

#include "contract/kvspace_access.h"
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

/*
 * 数据面：真 kvspace（shm）。本用例**不再注入函数指针**——`kvspace_access`
 * 直连 kvspace ABI，没有实现表可换。vmkv 看到的就是真实存储里的畸形数据。
 *
 * **造树必须自检**：每写一条 key 都读回来确认它真的在。造树失败而测试仍然绿，
 * 是这批迁移最容易出现的假信号（key 写错位置 → vmkv 走正常路径 → 断言照样过）。
 */
typedef struct test_store {
    void *handle;
    char directory[256];
    uint64_t revision_generation;
} test_store_t;

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

static void counting_header(uint8_t *body, uint16_t reads, uint16_t writes,
                            uint8_t dynamic) {
    body[0] = (uint8_t)(reads & 0xFFU);
    body[1] = (uint8_t)(reads >> 8U);
    body[2] = (uint8_t)(writes & 0xFFU);
    body[3] = (uint8_t)(writes >> 8U);
    body[4] = dynamic;
}

/*
 * 临时目录固定 /tmp + pid：不写进任何角色的 workspace（今天已发现过一处硬编码
 * 别人目录的写法）。返回 false 表示环境不可用——那是真·环境问题，各用例按 SKIP。
 */
static bool store_open(test_store_t *store) {
    /* 每棵树一个独立目录：同一进程里连两个 DSN 到同一个 shm 文件会让后端崩。 */
    static int sequence = 0;
    memset(store, 0, sizeof(*store));
    snprintf(store->directory, sizeof(store->directory),
             "/tmp/runtimec-vmkv-reader-%d-%d", (int)getpid(), sequence++);
    (void)mkdir(store->directory, 0700);
    char data_source[360];
    snprintf(data_source, sizeof(data_source), "shm://%s/store.shm",
             store->directory);
    store->handle = kvspaceConnect(data_source);
    return store->handle != NULL;
}

/*
 * 写一条值并**读回自检**。造树失败却不报，是本批迁移最容易出现的假信号：
 * key 写错位置 → vmkv 走正常路径 → 断言照样过。故每次写完都读回验存在。
 */
static void store_put(test_store_t *store, const char *key,
                      const uint8_t *body, size_t body_size,
                      const char *langtype) {
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
        fprintf(stderr, "store_put failed for %s: %s\n", key, error_text);
        return;
    }
    uint8_t *readback = NULL;
    uint32_t readback_size = 0U;
    const bool present =
        kvspaceGet(store->handle, key, 1, &readback, &readback_size) == 0 &&
        readback != NULL && readback_size != 0U;
    EXPECT(present);
    if (!present) {
        fprintf(stderr, "store_put self-check failed: %s is not readable\n",
                key);
    }
}

static void store_add_directory(test_store_t *store, const char *key) {
    char error_text[256];
    error_text[0] = '\0';
    EXPECT(kvspaceMkindex(store->handle, key, 0U, error_text,
                          (uint32_t)sizeof(error_text)) == 0);
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

static void store_free(test_store_t *store) {
    if (store->handle != NULL) {
        kvspaceClose(store->handle);
        store->handle = NULL;
    }
    /* 后端把一个 DSN 摊成多份文件（.shm.garb / .gen / .sbo.data / .sbo.head），
     * 只删 store.shm 会留下一整个目录。逐个删掉再删目录。 */
    static const char *const suffixes[] = {"store.shm", "store.shm.garb",
                                           "store.shm.gen", "store.shm.sbo.data",
                                           "store.shm.sbo.head"};
    char path[512];
    for (size_t index = 0U; index < sizeof(suffixes) / sizeof(suffixes[0]);
         index++) {
        snprintf(path, sizeof(path), "%s/%s", store->directory,
                 suffixes[index]);
        (void)unlink(path);
    }
    (void)rmdir(store->directory);
}

static void build_tree(test_store_t *store, int flags) {
    store_add_directory(store, "/lib/sum/");
    {
        /* `‥ctime` 是 source revision 的唯一单调源事实：没有它，打开阶段就失败，
         * 走不到本用例要验的那些读取语义。 */
        uint8_t ctime_body[8] = {0};
        ctime_body[0] = (uint8_t)store->revision_generation;
        store_put(store, "/lib/sum/\xE2\x80\xA5" "ctime", ctime_body,
                  sizeof(ctime_body), "int64");
    }
    if (flags != TREE_NO_ANCHOR) {
        uint8_t anchor[5];
        counting_header(anchor, 2U, 1U, 0U);
        store_put(store, "/lib/sum/[0,0]",
                  flags == TREE_BAD_ANCHOR ? anchor : anchor,
                  flags == TREE_BAD_ANCHOR ? 4U : 5U, "rwfunc");
    }
    if (flags != TREE_NO_PARAMETER) {
        store_put(store, "/lib/sum.[0,-1]", (const uint8_t *)"a\0[]int64", 9U,
                  "def langtype");
    } else {
        /* 破坏：只定义第二个参数。 */
        store_put(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
    }
    if (flags != TREE_NO_PARAMETER) {
        store_put(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
    }
    store_put(store, "/lib/sum.[0,1]", (const uint8_t *)"R\0int64", 7U,
              "def langtype");
    if (flags != TREE_BAD_PARAMETER) {
        /* 上面的参数已写好；BAD_PARAMETER 用无 NUL 的 body 覆盖第一条。 */
    } else {
        /* 覆盖写，不重建存储：store_free 会关掉句柄，后续 store_put 就没句柄了。 */
        uint8_t anchor[5];
        counting_header(anchor, 2U, 1U, 0U);
        store_put(store, "/lib/sum/[0,0]", anchor, 5U, "rwfunc");
        store_put(store, "/lib/sum.[0,-1]", (const uint8_t *)"int64", 5U,
                  "def langtype");
        store_put(store, "/lib/sum.[0,-2]", (const uint8_t *)"n\0int64", 7U,
                  "def langtype");
        store_put(store, "/lib/sum.[0,1]", (const uint8_t *)"R\0int64", 7U,
                  "def langtype");
    }

    uint8_t opcode[5 + 16];
    counting_header(opcode, 2U, 1U, 0U);
    const char *name = "int64·add";
    memcpy(opcode + 5, name, strlen(name));
    store_put(store, "/lib/sum/[1,0]", opcode,
              flags == TREE_EMPTY_OPCODE ? 5U : 5U + strlen(name), "rwir");

    if (flags != TREE_MISSING_READ_SLOT) {
        uint8_t slot[5 + 32];
        counting_header(slot, 0U, 0U, 0U);
        const char *expression = "*[0,-1]\0int64";
        memcpy(slot + 5, expression, 13U);
        store_put(store, "/lib/sum/[1,-1]", slot, 5U + 13U, "rwir");
    }
    {
        /* 内联 int64 字面量：rank0 的 1。 */
        static const uint8_t literal[8] = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        store_put(store, "/lib/sum/[1,-2]", literal, sizeof(literal), "int64");
    }
    {
        uint8_t slot[5 + 32];
        counting_header(slot, 0U, 0U, 0U);
        const char *expression = "*[0,1]\0int64";
        memcpy(slot + 5, expression, 12U);
        store_put(store, "/lib/sum/[1,1]", slot, 5U + 12U, "rwir");
    }
    if (flags != TREE_NO_OPCODE) {
        uint8_t ret[5 + 8];
        counting_header(ret, 0U, 0U, 0U);
        memcpy(ret + 5, "return", 6U);
        store_put(store, "/lib/sum/[2,0]", ret, 5U + 6U, "rwir");
    } else {
        /* 缺 opcode：第 2 行只有读槽，没有 `[2,0]`。 */
        uint8_t stray[5 + 16];
        counting_header(stray, 0U, 0U, 0U);
        memcpy(stray + 5, "*[0,-1]", 7U);
        store_put(store, "/lib/sum/[2,-1]", stray, 5U + 7U, "rwir");
    }
}

/*
 * 假存储不再是「可替换的实现表」：`kvspace_access` 现在直连真 kvspace ABI，
 * 没有函数指针可填。本用例要的是「一棵可造坏数据的树」，所以数据面改由
 * 本文件的 fake_* 直接提供，走 kvspace 的**测试替身实现**（链接期选择）。
 *
 * 判据不变：仍然验 vmkv 在各种畸形树上的行为。
 */
/*
 * 造一个 access 实例：直连 harness 里的真 kvspace 句柄，**没有函数指针可注入**。
 * 造不出来就是环境问题，返回 false 让调用方 SKIP。
 */
static bool init_services(test_store_t *store, runtimec_kvspace_access_t *out,
                          runtimec_kvspace_access_t **access,
                          runtimec_error_t *error) {
    const runtimec_kvspace_access_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = store->handle,
        .instance_identifier = UINT64_C(7),
    };
    *access = NULL;
    if (runtimec_kvspace_access_create(&request, access, error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        *access == NULL) {
        return false;
    }
    (void)out;
    return true;
}

static void test_happy_path(void) {
    test_store_t store = {0};
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    build_tree(&store, TREE_OK);
    runtimec_kvspace_access_t *services = NULL;
    runtimec_error_t open_error;
    runtimec_error_reset(&open_error);
    EXPECT(init_services(&store, NULL, &services, &open_error));
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t path =
        RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");

    {
        const runtimec_status_t open_status = vmkv_source_open_function(
            services, path, &function, &snapshot, &error);
        EXPECT(open_status == RUNTIMEC_STATUS_SUCCESS);
        if (open_status != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "open failed: status=%d code=%lld msg=%.*s\n",
                    (int)open_status, (long long)error.code,
                    (int)error.message.size, error.message.data);
            (void)runtimec_kvspace_access_destroy(services, &error);
        (void)runtimec_kvspace_access_destroy(services, &error);
    store_free(&store);
            return;
        }
    }
    EXPECT(function != NULL);
    EXPECT(snapshot.source_revision.instance_identifier == 7U);
    /* 真后端的 generation 从 1 起算（首次取 revision 即推进一次）；旧的假存储
     * 直接把 `revision_generation` 原样吐出，是 0。判据本身（「有值且稳定」）不变。 */
    EXPECT(snapshot.source_revision.generation != 0U);
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
    (void)runtimec_kvspace_access_destroy(services, &error);
    store_free(&store);
}

typedef struct malformed_case {
    int flags;
    runtimec_status_t expected_status;
    int64_t expected_code;
} malformed_case_t;

static void test_malformed(void) {
    const malformed_case_t cases[] = {
        /* 真 kvspace 里「key 不存在」与「key 存在但 body 为空」在**读回形态上
         * 不可区分**（都是 value=NULL / size=0），所以缺锚点与坏锚点落同一个诊断。
         * 旧的假存储在 read_header 里对缺失 key 返回 UNSUPPORTED，才把两者分开。
         * 见 workspace/622/coding4/vmkv-reader-migration-notes.md。 */
        {TREE_NO_ANCHOR, RUNTIMEC_STATUS_ERROR, 5},
        {TREE_BAD_ANCHOR, RUNTIMEC_STATUS_ERROR, 5},
        {TREE_NO_PARAMETER, RUNTIMEC_STATUS_ERROR, 7},
        {TREE_BAD_PARAMETER, RUNTIMEC_STATUS_ERROR, 5},
        {TREE_NO_OPCODE, RUNTIMEC_STATUS_ERROR, 8},
        {TREE_MISSING_READ_SLOT, RUNTIMEC_STATUS_ERROR, 9},
        {TREE_EMPTY_OPCODE, RUNTIMEC_STATUS_ERROR, 10},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_store_t store = {0};
        if (!store_open(&store)) {
            fprintf(stderr, "SKIP: cannot connect kvspace\n");
            continue;
        }
        build_tree(&store, cases[i].flags);
        runtimec_kvspace_access_t *services = NULL;
        runtimec_error_t error;
        runtimec_error_reset(&error);
        EXPECT(init_services(&store, NULL, &services, &error));
        runtimec_snapshot_t snapshot;
        vmkv_function_source_t *function = NULL;
        const runtimec_string_view_t path =
            RUNTIMEC_STRING_VIEW_LITERAL("/lib/sum");
        const runtimec_status_t open_status = vmkv_source_open_function(
            services, path, &function, &snapshot, &error);
        if (open_status != RUNTIMEC_STATUS_SUCCESS) {
            EXPECT(function == NULL);
            EXPECT(open_status == cases[i].expected_status);
            if (error.code != cases[i].expected_code) {
                fprintf(stderr, "case flags=%d got code=%lld want=%lld\n",
                        cases[i].flags, (long long)error.code,
                        (long long)cases[i].expected_code);
            }
            EXPECT(error.code == cases[i].expected_code);
            (void)runtimec_kvspace_access_destroy(services, &error);
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
        while (true) {
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
        (void)runtimec_kvspace_access_destroy(services, &error);
        store_free(&store);
    }
}

/*
 * 存储不可达时的失败路径。原来靠 `store.fail_read = true` 注入；现在数据面是
 * 真 kvspace，等价形态是**读一个不存在的函数**——`source_revision` 依赖的
 * `‥ctime` 读不到，打开必须失败而不是返回半初始化对象。
 */
static void test_host_read_failure(void) {
    test_store_t store = {0};
    if (!store_open(&store)) {
        fprintf(stderr, "SKIP: cannot connect kvspace\n");
        return;
    }
    runtimec_kvspace_access_t *services = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    EXPECT(init_services(&store, NULL, &services, &error));
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t path =
        RUNTIMEC_STRING_VIEW_LITERAL("/lib/absent");
    EXPECT(vmkv_source_open_function(services, path, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_ERROR);
    EXPECT(function == NULL);
    (void)runtimec_kvspace_access_destroy(services, &error);
    store_free(&store);
}

/* 纯入参校验：不碰存储，因此不需要 access 实例。 */
static void test_invalid_request(void) {
    runtimec_error_t error;
    runtimec_snapshot_t snapshot;
    vmkv_function_source_t *function = NULL;
    const runtimec_string_view_t empty = RUNTIMEC_STRING_VIEW_EMPTY;
    EXPECT(vmkv_source_open_function(NULL, empty, &function, &snapshot,
                                     &error) == RUNTIMEC_STATUS_INVALID);
    EXPECT(vmkv_source_close_function(NULL) == RUNTIMEC_STATUS_INVALID);
    EXPECT(vmkv_rwir_reader_create(NULL, NULL, &error) ==
           RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_happy_path();
    test_malformed();
    test_host_read_failure();
    test_invalid_request();
    if (failures != 0) {
        fprintf(stderr, "vmkv_reader_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("vmkv_reader_test: ok\n");
    return 0;
}
