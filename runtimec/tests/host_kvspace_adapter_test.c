/*
 * host/kvspace_adapter 验收：用 fake 二进制接口跑通能力表，验证静态地址
 * 解析的生命周期判据、失败封闭、以及 checkpoint/handoff 的持久效果。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/host_services.h"
#include "host/host.h"
#include "host/kvspace_adapter.h"
#include "kvspace/xvalue_head.h"
#include "tests/fake_kvspace.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,             \
                    #condition);                                               \
        }                                                                      \
    } while (0)

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

/* 构造一条无 langtype 的 slack wire（与 layout 写给系统键的形态一致）。 */
static void store_text(fake_kvspace_store_t *store, const char *key,
                       const char *text) {
    runtimec_kvspace_binary_interface_t binary_interface;
    fake_kvspace_bind(store, &binary_interface);
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    EXPECT(binary_interface.new_slack((const uint8_t *)text, strlen(text),
                                      strlen(text), NULL, &wire,
                                      &wire_size) == 0);
    EXPECT(fake_kvspace_put(store, key, wire, (uint32_t)wire_size, false) == 0);
    free(wire);
}

/* 构造一条 int64 wire（`‥ctime` 的形态：body 是 8 字节小端，无 attr）。 */
static void store_int64(fake_kvspace_store_t *store, const char *key,
                        int64_t value) {
    uint8_t wire[KVSPACE_XVALUE_HEAD_LEN + 8U];
    memset(wire, 0, sizeof(wire));
    /* 16 字节 head：bodylen(u64)@0、attrslen(u32)@8、storetype@12、inline@13、
     * padding@14-15；attr 区为空时 attrslen = 0。按字节偏移拼，不写结构体字段名。 */
    wire[0] = 8U;                       /* bodylen = 8，小端 */
    wire[12] = KVSPACE_XVALUE_ST_SHORT;
    wire[13] = 0U;                      /* 指向形态：0 = 值内联 */
    for (size_t index = 0U; index < 8U; index++) {
        wire[KVSPACE_XVALUE_HEAD_LEN + index] =
            (uint8_t)(((uint64_t)value >> (8U * index)) & 0xffU);
    }
    EXPECT(fake_kvspace_put(store, key, wire, (uint32_t)sizeof(wire), false) ==
           0);
}

/* 列表访问者：收集交出的完整键，供主流程断言。 */
static int list_visited;
static char list_seen[16][256];

static runtimec_status_t collect_list_key(void *context,
                                          runtimec_key_view_t key,
                                          runtimec_error_t *error) {
    (void)context;
    (void)error;
    if (list_visited < 16 && key.size < 256U) {
        memcpy(list_seen[list_visited], key.data, key.size);
        list_seen[list_visited][key.size] = '\0';
        list_visited += 1;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}


/* 写入序列记录：用一层 set_value 包住替身，记录每一次写的 key 与 body。
 * 这不是为了测替身，而是为了观察「两次写之间的中间态」——终态自洽，看不出过程。 */
typedef struct write_record {
    char key[128];
    uint8_t body[64];
    size_t body_size;
} write_record_t;

typedef struct write_log {
    write_record_t records[32];
    size_t count;
    /* 任何 SetPart 都被记为「出现了不该出现的分支」。 */
    size_t part_count;
    runtimec_kvspace_binary_interface_t *inner;
} write_log_t;

static write_log_t write_log;

static int logging_set_part(void *handle, const char *key, uint32_t offset,
                            const uint8_t *value, uint32_t value_size,
                            char *error_text, uint32_t error_text_capacity) {
    write_log.part_count += 1U;
    return write_log.inner->set_part(handle, key, offset, value, value_size,
                                     error_text, error_text_capacity);
}

static int logging_set_value(void *handle, const char *key,
                             const uint8_t *wire, uint32_t wire_size,
                             char *error_text, uint32_t error_text_capacity) {
    if (write_log.count < 32U) {
        write_record_t *record = &write_log.records[write_log.count++];
        snprintf(record->key, sizeof(record->key), "%s", key);
        if (wire_size >= KVSPACE_XVALUE_HEAD_LEN) {
            kvspaceXvalueHead head;
            memcpy(&head, wire, sizeof(head));
            const uint64_t body_length = kvspaceXvalueHeadLen(&head);
            if (body_length <= sizeof(record->body)) {
                record->body_size = (size_t)body_length;
                memcpy(record->body, wire + KVSPACE_XVALUE_HEAD_LEN,
                       record->body_size);
            }
        }
    }
    return write_log.inner->set_value(handle, key, wire, wire_size, error_text,
                                      error_text_capacity);
}

/* 读一条 slack 值的 body 文本（去掉 head 与 attr）。 */
static void read_body_text(fake_kvspace_store_t *store, const char *key,
                           char *output, size_t capacity) {
    uint8_t *wire = fake_kvspace_address(store, key);
    EXPECT(wire != NULL);
    if (wire == NULL) {
        output[0] = '\0';
        return;
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    const uint64_t body_size = kvspaceXvalueHeadLen(&head);
    if (body_size + 1U > capacity) {
        output[0] = '\0';
        return;
    }
    memcpy(output, wire + KVSPACE_XVALUE_HEAD_LEN, (size_t)body_size);
    output[body_size] = '\0';
}

int main(void) {
    fake_kvspace_store_t store;
    memset(&store, 0, sizeof(store));
    runtimec_kvspace_binary_interface_t binary_interface;
    fake_kvspace_bind(&store, &binary_interface);

    runtimec_kvspace_adapter_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = &store,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_host_services_t services;
    runtimec_error_t error;

    /* 1. 表必须被完整填满。 */
    EXPECT(runtimec_kvspace_adapter_create_services(
               &request, &binary_interface, &services, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(runtimec_host_services_is_valid(&services));
    EXPECT(services.read != NULL && services.read_header != NULL &&
           services.write != NULL && services.write_partial != NULL &&
           services.remove != NULL && services.list != NULL &&
           services.watch != NULL && services.signal != NULL &&
           services.source_revision != NULL &&
           services.resolve_static_xvalue_address != NULL &&
           services.resolve_frame_xvalue_address != NULL &&
           services.checkpoint != NULL && services.handoff != NULL &&
           services.ensure_frame != NULL &&
           services.ensure_zero_value != NULL);

    /* 2. 不完整的二进制接口必须被拒绝，且输出保持空表。 */
    {
        runtimec_kvspace_binary_interface_t partial = binary_interface;
        partial.set_part = NULL;
        runtimec_host_services_t rejected;
        runtimec_host_services_initialize(&rejected);
        EXPECT(runtimec_kvspace_adapter_create_services(
                   &request, &partial, &rejected, &error) ==
               RUNTIMEC_STATUS_INVALID);
        EXPECT(!runtimec_host_services_is_valid(&rejected));
    }

    /* 3. 空句柄必须被拒绝。 */
    {
        runtimec_kvspace_adapter_request_t empty = request;
        empty.borrowed_kvspace_handle = NULL;
        runtimec_host_services_t rejected;
        EXPECT(runtimec_kvspace_adapter_create_services(
                   &empty, &binary_interface, &rejected, &error) ==
               RUNTIMEC_STATUS_INVALID);
    }

    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.function_key = runtimec_key_view_from_c_string("/lib/sum");


    /*
     * 3b. 零值局部槽：定长标量建档、幂等、变长拒绝且不留半写。
     *     time 与 time/duration 是 8 字节 int64 纳秒（规范定的定长标量）。
     */
    {
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/x");

        /* time：建槽后 body 长度必须是 8。 */
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("time"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        uint8_t *wire = fake_kvspace_address(&store, "/vthread/v1/[0]/x");
        EXPECT(wire != NULL);
        if (wire != NULL) {
            kvspaceXvalueHead head;
            memcpy(&head, wire, sizeof(head));
            EXPECT(kvspaceXvalueHeadLen(&head) == 8U);
            /* 零值：body 全是 0。 */
            for (uint64_t index = 0U; index < 8U; ++index) {
                EXPECT(wire[KVSPACE_XVALUE_HEAD_LEN + index] == 0U);
            }
        }

        /* 幂等：再调一次不报错，且地址不变（证明没重写）。 */
        uint8_t *before = wire;
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("time"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(fake_kvspace_address(&store, "/vthread/v1/[0]/x") ==
               before);
    }

    {
        /* time/duration 同样是 8 字节。时长 kind 的规范名带 lib 前缀。 */
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/d");
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("time/duration"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        uint8_t *wire = fake_kvspace_address(&store, "/vthread/v1/[0]/d");
        EXPECT(wire != NULL);
        if (wire != NULL) {
            kvspaceXvalueHead head;
            memcpy(&head, wire, sizeof(head));
            EXPECT(kvspaceXvalueHeadLen(&head) == 8U);
        }
    }

    {
        /*
         * 变长类型必须 UNSUPPORTED，且**不留下半写状态**——key 必须不存在。
         * 字符值恒为一维序列、数组个数可变，零值长度都是 0，而写入要求与当前
         * body 等长，零长 body 永远写不进真实值。
         */
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/s");
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("[]char/utf8"), &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(fake_kvspace_address(&store, "/vthread/v1/[0]/s") == NULL);
    }

    {
        /*
         * 反向：旧的 `duration` 不再是合法类型名，必须 UNSUPPORTED——
         * 不能因为「曾经叫这个名字」就静默建出 8 字节槽。不留别名。
         */
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/old");
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("duration"), &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(fake_kvspace_address(&store, "/vthread/v1/[0]/old") ==
               NULL);
    }

    {
        /* 宽度表里没有的类型同样 UNSUPPORTED，不猜。 */
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/u");
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("stringkeymap"),
                   &error) == RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(fake_kvspace_address(&store, "/vthread/v1/[0]/u") == NULL);
    }

    {
        /* 已存在但宽度与类型不符：冲突，失败封闭而不是静默覆盖。 */
        const runtimec_key_view_t slot =
            RUNTIMEC_KEY_VIEW_LITERAL("/vthread/v1/[0]/y");
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("int64"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(services.ensure_zero_value(
                   services.context, &snapshot, slot,
                   runtimec_string_view_from_c_string("int8"), &error) ==
               RUNTIMEC_STATUS_INVALID);
    }

    /* 4. 静态 key → 非零基址，且与后端当前地址一致。 */
    store_text(&store, "/lib/sum.[0,-1]", "a\0[]int64");
    runtimec_xvalue_address_t address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    const runtimec_key_view_t static_key =
        RUNTIMEC_KEY_VIEW_LITERAL("/lib/sum.[0,-1]");
    EXPECT(services.resolve_static_xvalue_address(
               services.context, &snapshot, static_key, &address, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(address != RUNTIMEC_XVALUE_ADDRESS_NONE);
    EXPECT(address == (runtimec_xvalue_address_t)(uintptr_t)
                           fake_kvspace_address(&store, "/lib/sum.[0,-1]"));

    /* 5. 动态/相对 key 必须 UNSUPPORTED，输出保持 NONE。 */
    {
        const runtimec_key_view_t relative =
            RUNTIMEC_KEY_VIEW_LITERAL("*[0,-1]");
        address = UINT64_C(0xdead);
        EXPECT(services.resolve_static_xvalue_address(
                   services.context, &snapshot, relative, &address, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(address == RUNTIMEC_XVALUE_ADDRESS_NONE);
    }

    /* 6. 不存在/无值的 key 不得给出地址。 */
    {
        const runtimec_key_view_t missing =
            RUNTIMEC_KEY_VIEW_LITERAL("/lib/sum/[9,0]");
        address = UINT64_C(0xdead);
        EXPECT(services.resolve_static_xvalue_address(
                   services.context, &snapshot, missing, &address, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(address == RUNTIMEC_XVALUE_ADDRESS_NONE);
    }

    /* 7. 运行期会被重写的 /vthread 子树不得给出地址。 */
    {
        store_text(&store, "/vthread/v1/\xE2\x80\xA5" "pc", "/lib/sum/[1,0]");
        const runtimec_key_view_t volatile_key = RUNTIMEC_KEY_VIEW_LITERAL(
            "/vthread/v1/\xE2\x80\xA5" "pc");
        address = UINT64_C(0xdead);
        EXPECT(services.resolve_static_xvalue_address(
                   services.context, &snapshot, volatile_key, &address,
                   &error) == RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(address == RUNTIMEC_XVALUE_ADDRESS_NONE);
    }

    /* 7b. 帧 key 的解析：合法帧槽给出非零基址 + 非零 generation。 */
    {
        const runtimec_key_view_t frame_key = RUNTIMEC_KEY_VIEW_LITERAL(
            "/vthread/v1/[1]/[0,-1]");
        store_text(&store, "/vthread/v1/[1]/[0,-1]", "int64");
        runtimec_xvalue_address_t frame_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        uint64_t frame_generation = 0U;
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, frame_key, &frame_address,
                   &frame_generation, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(frame_address != RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(frame_address ==
               (runtimec_xvalue_address_t)(uintptr_t)fake_kvspace_address(
                   &store, "/vthread/v1/[1]/[0,-1]"));
        EXPECT(frame_generation != 0U);

        /* 同一条帧 key 再解一次：generation 不变（没有事实变化）。 */
        uint64_t again_generation = 0U;
        runtimec_xvalue_address_t again_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, frame_key, &again_address,
                   &again_generation, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(again_generation == frame_generation);

        /* 静态 key 走这条必须被拒（两条路径不合并）。 */
        runtimec_xvalue_address_t cross_address = UINT64_C(0xdead);
        uint64_t cross_generation = UINT64_C(0xdead);
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, static_key, &cross_address,
                   &cross_generation, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(cross_address == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(cross_generation == 0U);

        /* 动态拼出的相对 key 必须被拒。 */
        const runtimec_key_view_t relative = RUNTIMEC_KEY_VIEW_LITERAL(
            "*[0,-1]");
        runtimec_xvalue_address_t relative_address = UINT64_C(0xdead);
        uint64_t relative_generation = UINT64_C(0xdead);
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, relative, &relative_address,
                   &relative_generation, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(relative_address == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(relative_generation == 0U);

        /* 尚未分配的帧槽：没有值，不能交地址。 */
        const runtimec_key_view_t absent = RUNTIMEC_KEY_VIEW_LITERAL(
            "/vthread/v1/[2]/[0,-1]");
        runtimec_xvalue_address_t absent_address = UINT64_C(0xdead);
        uint64_t absent_generation = UINT64_C(0xdead);
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, absent, &absent_address,
                   &absent_generation, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(absent_address == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(absent_generation == 0U);

    }

    /* 8. 读：容量不足报 INSUFFICIENT_CAPACITY，充足时拷出完整 wire。 */
    {
        uint8_t small[4];
        runtimec_bytes_buffer_t buffer = {small, 0U, sizeof(small)};
        EXPECT(services.read(services.context, &snapshot, static_key, &buffer,
                             &error) == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
        uint8_t roomy[64];
        buffer = (runtimec_bytes_buffer_t){roomy, 0U, sizeof(roomy)};
        EXPECT(services.read(services.context, &snapshot, static_key, &buffer,
                             &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(buffer.size > KVSPACE_XVALUE_HEAD_LEN);
    }

    /* 9. 读 header：size 与值长度一致；缺失 key 报 0 而不是失败。 */
    {
        runtimec_host_value_header_t header;
        runtimec_host_value_header_reset(&header);
        EXPECT(services.read_header(services.context, &snapshot, static_key,
                                    &header, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        uint8_t *wire = fake_kvspace_address(&store, "/lib/sum.[0,-1]");
        EXPECT(wire != NULL);
        kvspaceXvalueHead head;
        memcpy(&head, wire, sizeof(head));
        EXPECT(header.value_size == kvspaceXvalueHeadTotalLen(&head));
        const runtimec_key_view_t missing =
            RUNTIMEC_KEY_VIEW_LITERAL("/lib/sum/nothere");
        EXPECT(services.read_header(services.context, &snapshot, missing,
                                    &header, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(header.value_size == 0U);
    }

    /* 10. 列表：交出完整键，且能同时看到目录键与兄弟键。 */
    {
        store_text(&store, "/lib/sum/[1,0]", "int64\xC2\xB7" "add");
        store_text(&store, "/lib/sum/[2,0]", "return");
        list_visited = 0;
        EXPECT(services.list(
                   services.context, &snapshot,
                   runtimec_key_view_from_c_string("/lib/sum"),
                   collect_list_key, NULL, &error) == RUNTIMEC_STATUS_SUCCESS);
        bool saw_parameter = false;
        bool saw_instruction = false;
        bool saw_foreign = false;
        for (int index = 0; index < list_visited; index++) {
            if (strcmp(list_seen[index], "/lib/sum.[0,-1]") == 0) {
                saw_parameter = true;
            }
            if (strcmp(list_seen[index], "/lib/sum/[2,0]") == 0) {
                saw_instruction = true;
            }
            if (strncmp(list_seen[index], "/lib/sum", 8U) != 0) {
                saw_foreign = true;
            }
        }
        EXPECT(saw_parameter);
        EXPECT(saw_instruction);
        EXPECT(!saw_foreign);
    }

    /* 11. checkpoint 写 pc / status；handoff 写队列并等条目被删除。 */
    {
        runtimec_checkpoint_t checkpoint;
        runtimec_checkpoint_reset(&checkpoint);
        checkpoint.vthread_identifier =
            runtimec_string_view_from_c_string("v7");
        checkpoint.logical_program_counter =
            (runtimec_program_counter_t){1, 2};
        checkpoint.persistent_status =
            runtimec_string_view_from_c_string("running");
        EXPECT(services.checkpoint(services.context, &checkpoint, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        /*
         * pc 是单 key 整值：body = [stackid:i32 LE][s0:i32 LE]，共 8 字节。
         * 一次写完整位置——「改 stackid 必改 s0」由形态保证，不靠顺序或哨兵。
         */
        {
            uint8_t *wire = fake_kvspace_address(&store,
                                                 "/vthread/v7/\xE2\x80\xA5pc");
            EXPECT(wire != NULL);
            if (wire != NULL) {
                kvspaceXvalueHead head;
                memcpy(&head, wire, sizeof(head));
                EXPECT(kvspaceXvalueHeadLen(&head) == 8U);
                int32_t stack_identifier = 0;
                int32_t ordinal = 0;
                memcpy(&stack_identifier, wire + KVSPACE_XVALUE_HEAD_LEN, 4U);
                memcpy(&ordinal, wire + KVSPACE_XVALUE_HEAD_LEN + 4U, 4U);
                EXPECT(stack_identifier == 1);
                EXPECT(ordinal == 2);
            }
        }
        char text[64];
        read_body_text(&store, "/vthread/v7/\xE2\x80\xA5status", text,
                       sizeof(text));
        EXPECT(strcmp(text, "running") == 0);
    }


    /*
     * 12b. `‥pc` 的三步哨兵协议。
     *
     * 没有原子多键写，所以「两个字段整体生效」靠**让中间态可被发现**：
     * 改 stackid 时先写 s0 = -1，再写 stackid，最后写新 s0。中间态**必然**含
     * 负数，与新旧指令范围是否恰好错开无关。
     *
     * 做法：把 set_value 包一层，记录每次写的 key 与值，事后核对调用序列——
     * 这是唯一能观察到「中间态」的方式，因为最终态是自洽的，看不出过程。
     */
    {
        runtimec_checkpoint_t checkpoint;
        runtimec_checkpoint_reset(&checkpoint);
        checkpoint.vthread_identifier =
            runtimec_string_view_from_c_string("v8");
        checkpoint.logical_program_counter =
            (runtimec_program_counter_t){5, 9};
        checkpoint.persistent_status =
            runtimec_string_view_from_c_string("running");
        EXPECT(services.checkpoint(services.context, &checkpoint, &error) ==
               RUNTIMEC_STATUS_SUCCESS);

        /*
         * 写入序列：必须**恰好一次** SetValue 写 ‥pc，body 8 字节；
         * 且**不得出现 SetPart**——一旦出现就是有人提前加了「只改 s0」的热路径分支。
         * 只看终态测不出「整值写」与「两次部分写」的区别，所以这里断言过程。
         */
        {
            runtimec_kvspace_binary_interface_t tapped = binary_interface;
            write_log.count = 0U;
            write_log.part_count = 0U;
            write_log.inner = &binary_interface;
            tapped.set_value = logging_set_value;
            tapped.set_part = logging_set_part;
            runtimec_host_services_t traced;
            EXPECT(runtimec_kvspace_adapter_create_services(
                       &request, &tapped, &traced, &error) ==
                   RUNTIMEC_STATUS_SUCCESS);

            runtimec_checkpoint_t second;
            runtimec_checkpoint_reset(&second);
            second.vthread_identifier =
                runtimec_string_view_from_c_string("v10");
            second.logical_program_counter =
                (runtimec_program_counter_t){5, 9};
            second.persistent_status =
                runtimec_string_view_from_c_string("running");
            EXPECT(traced.checkpoint(traced.context, &second, &error) ==
                   RUNTIMEC_STATUS_SUCCESS);

            const char *pc_key = "/vthread/v10/\xE2\x80\xA5pc";
            size_t pc_writes = 0U;
            for (size_t index = 0U; index < write_log.count; ++index) {
                const write_record_t *record = &write_log.records[index];
                if (strcmp(record->key, pc_key) != 0) {
                    continue;
                }
                pc_writes += 1U;
                EXPECT(record->body_size == 8U);
                if (record->body_size == 8U) {
                    int32_t stack_identifier = 0;
                    int32_t ordinal = 0;
                    memcpy(&stack_identifier, record->body, 4U);
                    memcpy(&ordinal, record->body + 4U, 4U);
                    EXPECT(stack_identifier == 5);
                    EXPECT(ordinal == 9);
                }
            }
            EXPECT(pc_writes == 1U);
            EXPECT(write_log.part_count == 0U);
        }

        /* 终态正确：单 key 里是 [2]int32。 */
        {
            uint8_t *wire =
                fake_kvspace_address(&store, "/vthread/v8/\xE2\x80\xA5pc");
            EXPECT(wire != NULL);
            if (wire != NULL) {
                kvspaceXvalueHead head;
                memcpy(&head, wire, sizeof(head));
                EXPECT(kvspaceXvalueHeadLen(&head) == 8U);
                int32_t stack_identifier = 0;
                int32_t ordinal = 0;
                memcpy(&stack_identifier, wire + KVSPACE_XVALUE_HEAD_LEN, 4U);
                memcpy(&ordinal, wire + KVSPACE_XVALUE_HEAD_LEN + 4U, 4U);
                EXPECT(stack_identifier == 5);
                EXPECT(ordinal == 9);
                /*
                 * langtype 必须带精确长度 `[2]int32`——`[]int32` 会丢掉
                 * 「恰好 2 个元素」，body 8 字节与 langtype 的一致性就没人能校验。
                 */
                const uint8_t *langtype = NULL;
                const uint16_t langtype_size = kvspaceXvalueHeadLangtypeBounded(
                    wire, kvspaceXvalueHeadTotalLen(&head), &langtype);
                const char *expected_langtype = "[2]int32";
                const size_t expected_size = strlen(expected_langtype);
                EXPECT((size_t)langtype_size == expected_size);
                EXPECT(langtype != NULL && (size_t)langtype_size == expected_size &&
                       memcmp(langtype, expected_langtype, expected_size) == 0);
            }
        }

        /* 负数 s0 是非法输入：入口必须拒绝，不把哨兵当合法位置写出去。 */
        checkpoint.logical_program_counter =
            (runtimec_program_counter_t){5, RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE};
        EXPECT(services.checkpoint(services.context, &checkpoint, &error) ==
               RUNTIMEC_STATUS_INVALID);
    }

    /* 12. source_revision：读不到 ctime 必须失败，不伪造 revision。 */
    {
        runtimec_source_revision_t revision;
        EXPECT(services.source_revision(
                   services.context,
                   runtimec_key_view_from_c_string("/lib/sum"), &revision,
                   &error) == RUNTIMEC_STATUS_ERROR);
        store_int64(&store, "/lib/sum/\xE2\x80\xA5" "ctime", 41);
        EXPECT(services.source_revision(
                   services.context,
                   runtimec_key_view_from_c_string("/lib/sum"), &revision,
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(revision.generation != 0U);
        EXPECT(revision.instruction_set_version == 41U);
        const uint64_t stable_generation = revision.generation;
        /* 同一事实重复取：generation 不变。 */
        EXPECT(services.source_revision(
                   services.context,
                   runtimec_key_view_from_c_string("/lib/sum"), &revision,
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(revision.generation == stable_generation);
        /* 已交出的地址被搬移：generation 必须推进。 */
        store.relocate_key = "/lib/sum.[0,-1]";
        EXPECT(services.source_revision(
                   services.context,
                   runtimec_key_view_from_c_string("/lib/sum"), &revision,
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(revision.generation != stable_generation);
    }

    /* 12b. generation 变化后旧地址不可用：搬移该帧槽，source_revision 推进
     *      generation，调用方比对到不等即必须重绑。 */
    {
        const runtimec_key_view_t frame_key = RUNTIMEC_KEY_VIEW_LITERAL(
            "/vthread/v1/[1]/[0,-1]");
        runtimec_xvalue_address_t before_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        uint64_t before_generation = 0U;
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, frame_key, &before_address,
                   &before_generation, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(before_address != RUNTIMEC_XVALUE_ADDRESS_NONE);
        store.relocate_key = "/vthread/v1/[1]/[0,-1]";
        runtimec_source_revision_t moved_revision;
        EXPECT(services.source_revision(
                   services.context,
                   runtimec_key_view_from_c_string("/lib/sum"),
                   &moved_revision, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(moved_revision.generation != before_generation);
        runtimec_xvalue_address_t rebound_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        uint64_t rebound_generation = 0U;
        EXPECT(services.resolve_frame_xvalue_address(
                   services.context, &snapshot, frame_key, &rebound_address,
                   &rebound_generation, &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(rebound_generation == moved_revision.generation);
        EXPECT(rebound_address != before_address);
    }

    /* 13. signal 明确未实现。 */
    EXPECT(services.signal(services.context, static_key, &error) ==
           RUNTIMEC_STATUS_NOT_IMPLEMENTED);

    /* 14. host 层把 NONE 地址当错误，不向下透传。 */
    {
        runtimec_host_t *host = NULL;
        EXPECT(runtimec_host_create(&services, &host, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        runtimec_xvalue_address_t output = UINT64_C(0xdead);
        const runtimec_key_view_t volatile_key = RUNTIMEC_KEY_VIEW_LITERAL(
            "/vthread/v1/\xE2\x80\xA5" "pc");
        EXPECT(runtimec_host_resolve_static_xvalue_address(
                   host, &snapshot, volatile_key, &output, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(output == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(runtimec_host_destroy(host, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
    }

    /* 15. 销毁：释放所有者结构并把表置空；句柄不被关闭。 */
    EXPECT(runtimec_kvspace_adapter_destroy_services(&services, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(!runtimec_host_services_is_valid(&services));

    fake_kvspace_release(&store);
    if (failures != 0) {
        fprintf(stderr, "host_kvspace_adapter_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("host_kvspace_adapter_test: ok\n");
    return 0;
}
