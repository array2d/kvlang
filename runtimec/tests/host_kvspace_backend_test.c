/*
 * host/kvspace_adapter 对接真实 kvspace 的 shm 后端用例。
 *
 * 通过 dlopen 加载调用方指定的库（默认 $HOME/.../kvlang/lib/libkvspace.so），
 * 把一个真实句柄交给适配器，验证静态地址解析、目录枚举、持久写与
 * checkpoint 在真后端上成立。库或 shm 目录不可用时返回 77（ctest SKIP）。
 */

#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "contract/kvspace_access.h"
#include <kvspace/kvspace.h>
#include "kvspace/xvalue_head.h"

#define SKIP_RETURN_CODE 77

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

/* 在已加载的库里按名字取符号；交给绑定层的是函数指针类型的地址。 */
static bool store_text(                       void *handle, const char *key, const char *text) {
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack((const uint8_t *)text, strlen(text),
                                    strlen(text), NULL, &wire,
                                    &wire_size) != 0) {
        return false;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int status = kvspaceSetValue(
        handle, key, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (status != 0) {
        fprintf(stderr, "set %s failed: %s\n", key, error_text);
        return false;
    }
    return true;
}

static bool store_int64(                        void *handle, const char *key, int64_t value) {
    uint8_t wire[KVSPACE_XVALUE_HEAD_LEN + 8U];
    memset(wire, 0, sizeof(wire));
    /* 16 字节 head：bodylen(u64)@0、attrslen(u32)@8、layout@12、padding@13-15；
     * 无 attr 时 attrslen = 0。按字节偏移拼，不写结构体字段名。
     * 新格式没有 storetype/ptr：存储形态与引用性都在 langtype（本夹具无类型）。 */
    wire[0] = 8U;                       /* bodylen = 8，小端 */
    wire[12] = KVSPACE_XVALUE_LAYOUT_LENGTH;
    for (size_t index = 0U; index < 8U; index++) {
        wire[KVSPACE_XVALUE_HEAD_LEN + index] =
            (uint8_t)(((uint64_t)value >> (8U * index)) & 0xffU);
    }
    char error_text[256];
    error_text[0] = '\0';
    return kvspaceSetValue(handle, key, wire,
                                       (uint32_t)sizeof(wire), error_text,
                                       (uint32_t)sizeof(error_text)) == 0;
}

static uint64_t monotonic_nanoseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

/* 扮演认领方：等队列条目出现后删除它，模拟外部 runtime 完成 handoff。 */
typedef struct handoff_claimer {
    void *handle;
    const char *queue_key;
    bool claimed;
} handoff_claimer_t;

static void *claim_handoff_entry(void *context) {
    handoff_claimer_t *claimer = context;
    for (int attempt = 0; attempt < 2000; attempt++) {
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        if (kvspaceGet(claimer->handle, claimer->queue_key,
                                           1, &value, &value_size) == 0 &&
            value != NULL && value_size != 0U) {
            const char *keys[1];
            keys[0] = claimer->queue_key;
            char error_text[256];
            error_text[0] = '\0';
            if (kvspaceDel(
                    claimer->handle, keys, 1U, error_text,
                    (uint32_t)sizeof(error_text)) == 0) {
                claimer->claimed = true;
            }
            return NULL;
        }
        /* 1ms：足够让等待方在超时前观察到删除。 */
        usleep(1000);
    }
    return NULL;
}

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

int main(int argument_count, char **arguments) {
    (void)argument_count;
    (void)arguments;
    /* kvspace 是**链接期依赖**：缺库在构建阶段就失败。此处不再有「没装库」这条
     * 运行时路径——SKIP 只留给「连不上这个 DSN」这种真·环境问题。 */
    runtimec_error_t error;
    runtimec_error_reset(&error);

    char directory[256];
    snprintf(directory, sizeof(directory), "/tmp/runtimec-host-%d",
             (int)getpid());
    char data_source_name[320];
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    if (mkdir(directory, 0700) != 0) {
        fprintf(stderr, "SKIP: cannot create %s\n", directory);
        return SKIP_RETURN_CODE;
    }
    void *handle = kvspaceConnect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", data_source_name);
        rmdir(directory);
        return SKIP_RETURN_CODE;
    }

    const runtimec_kvspace_access_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_kvspace_access_t *services = NULL;
    EXPECT(runtimec_kvspace_access_create(&request, &services, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(services != NULL);

    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.function_key = runtimec_key_view_from_c_string("/lib/sum");

    /* 1. 真实后端上的静态地址解析。 */
    EXPECT(store_text(handle, "/lib/sum.[0,-1]",
                      "a\x00[]int64"));
    EXPECT(store_text(handle, "/lib/sum/[1,0]",
                      "\x02\x00\x01\x00\x00int64\xC2\xB7" "add"));
    EXPECT(store_text(handle, "/lib/sum/[2,0]",
                      "\x00\x00\x00\x00\x00return"));
    EXPECT(store_int64(handle,
                       "/lib/sum/\xE2\x80\xA5" "ctime", 17));

    runtimec_xvalue_address_t address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    EXPECT(runtimec_kvspace_access_resolve_static_xvalue_address(
               services, &snapshot,
               runtimec_key_view_from_c_string("/lib/sum.[0,-1]"), &address,
               &error) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(address != RUNTIMEC_XVALUE_ADDRESS_NONE);
    /* 交出的基址必须真的是该值的 wire 起点：头 8 字节自洽。 */
    const kvspaceXvalueHead *head =
        (const kvspaceXvalueHead *)(uintptr_t)address;
    EXPECT(head != NULL);
    if (head != NULL) {
        /* layout 只能是 0/1；padding 必须为零——这是新头唯一的自洽判据。 */
        EXPECT(head->layout == KVSPACE_XVALUE_LAYOUT_LENGTH ||
               head->layout == KVSPACE_XVALUE_LAYOUT_CAPACITY);
        EXPECT(((const uint8_t *)head)[13] == 0U &&
               ((const uint8_t *)head)[14] == 0U &&
               ((const uint8_t *)head)[15] == 0U);
    }

    /* 1b. 真实后端上的帧地址解析：走 /vthread 槽，给出基址 + generation。 */
    {
        EXPECT(store_text(handle,
                          "/vthread/v3/[0]/[0,-1]", "int64"));
        runtimec_xvalue_address_t frame_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        uint64_t frame_generation = 0U;
        EXPECT(runtimec_kvspace_access_resolve_frame_xvalue_address(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/vthread/v3/[0]/[0,-1]"),
                   &frame_address, &frame_generation, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(frame_address != RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(frame_generation != 0U);

        /* 静态 key 走帧解析必须被拒；动态相对 key 也一样。 */
        runtimec_xvalue_address_t rejected = UINT64_C(0xdead);
        uint64_t rejected_generation = UINT64_C(0xdead);
        EXPECT(runtimec_kvspace_access_resolve_frame_xvalue_address(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/lib/sum.[0,-1]"),
                   &rejected, &rejected_generation, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(rejected == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(rejected_generation == 0U);
        EXPECT(runtimec_kvspace_access_resolve_frame_xvalue_address(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("*[0,-1]"), &rejected,
                   &rejected_generation, &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
        EXPECT(rejected == RUNTIMEC_XVALUE_ADDRESS_NONE);
        EXPECT(rejected_generation == 0U);

        /* 搬移该帧槽后，source_revision 的 generation 必须推进，旧地址随之
         * 失效（调用方比对 generation 不等即重绑）。 */
        EXPECT(store_text(handle,
                          "/vthread/v3/[0]/[0,-1]", "int64"));
        runtimec_source_revision_t moved;
        EXPECT(runtimec_kvspace_access_source_revision(
                   services,
                   runtimec_key_view_from_c_string("/lib/sum"), &moved,
                   &error) == RUNTIMEC_STATUS_SUCCESS);
        EXPECT(moved.generation != 0U);
        runtimec_xvalue_address_t rebound = RUNTIMEC_XVALUE_ADDRESS_NONE;
        uint64_t rebound_generation = 0U;
        EXPECT(runtimec_kvspace_access_resolve_frame_xvalue_address(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/vthread/v3/[0]/[0,-1]"),
                   &rebound, &rebound_generation, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(rebound_generation == moved.generation);
    }

    /* 2. 目录枚举：参数定义键与指令槽都在同一棵子树里被交出。 */
    list_visited = 0;
    EXPECT(runtimec_kvspace_access_list(services, &snapshot,
                         runtimec_key_view_from_c_string("/lib/sum"),
                         collect_list_key, NULL, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    bool saw_parameter = false;
    bool saw_instruction = false;
    for (int index = 0; index < list_visited; index++) {
        if (strcmp(list_seen[index], "/lib/sum.[0,-1]") == 0) {
            saw_parameter = true;
        }
        if (strcmp(list_seen[index], "/lib/sum/[2,0]") == 0) {
            saw_instruction = true;
        }
    }
    EXPECT(saw_parameter);
    EXPECT(saw_instruction);

    /* 3. 读：交出的 wire 与地址一致。 */
    {
        uint8_t buffer[128];
        runtimec_bytes_buffer_t output = {buffer, 0U, sizeof(buffer)};
        EXPECT(runtimec_kvspace_access_read(services, &snapshot,
                             runtimec_key_view_from_c_string(
                                 "/lib/sum.[0,-1]"),
                             &output, &error) == RUNTIMEC_STATUS_SUCCESS);
        /* 总长只经 TotalLen：它含 head + bodylen + attrslen 并校验 padding，
         * 与探针同一判据，两者不会分叉。 */
        EXPECT(output.size == kvspaceXvalueHeadTotalLen(
                                  (const kvspaceXvalueHead *)(const void *)
                                      buffer));
    }

    /* 4. source_revision：读函数的 `‥ctime`，重复取不变，搬移后推进。 */
    {
        runtimec_source_revision_t revision;
        runtimec_key_view_t function_key =
            runtimec_key_view_from_c_string("/lib/sum");
        EXPECT(runtimec_kvspace_access_source_revision(services, function_key,
                                        &revision, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(revision.generation == 1U);
        EXPECT(revision.instruction_set_version == 17U);
        EXPECT(runtimec_kvspace_access_source_revision(services, function_key,
                                        &revision, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(revision.generation == 1U);
        /* 重写同一个 key：若后端把值搬进新 box，交出的旧基址失效，generation
         * 必须推进；若后端就地复用同一个 box（同尺寸重写），地址与长度都不变，
         * 此时没有可观察的失效，generation 保持不变才是正确结果。 */
        uint8_t *before =
            (uint8_t *)(uintptr_t)address;
        EXPECT(store_text(handle, "/lib/sum.[0,-1]",
                          "a\x00[]int64"));
        uint8_t *after = NULL;
        uint32_t after_size = 0U;
        EXPECT(kvspaceGet(handle, "/lib/sum.[0,-1]", 1, &after,
                                    &after_size) == 0);
        EXPECT(after != NULL);
        EXPECT(runtimec_kvspace_access_source_revision(services, function_key,
                                        &revision, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        if (after == before) {
            EXPECT(revision.generation == 1U);
        } else {
            EXPECT(revision.generation == 2U);
        }
    }

    /* 5. checkpoint 落到真实的 /vthread 槽。 */
    {
        runtimec_checkpoint_t checkpoint;
        runtimec_checkpoint_reset(&checkpoint);
        checkpoint.vthread_identifier =
            runtimec_string_view_from_c_string("v9");
        checkpoint.logical_program_counter =
            (runtimec_program_counter_t){1, 2};
        checkpoint.persistent_status =
            runtimec_string_view_from_c_string("running");
        EXPECT(runtimec_kvspace_access_checkpoint(services, &checkpoint, &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        EXPECT(kvspaceGet(handle, "/vthread/v9/\xE2\x80\xA5" "status",
                                    1, &value, &value_size) == 0);
        EXPECT(value != NULL && value_size > KVSPACE_XVALUE_HEAD_LEN);
        if (value != NULL && value_size > KVSPACE_XVALUE_HEAD_LEN) {
            kvspaceXvalueHead slot_head;
            memcpy(&slot_head, value, sizeof(slot_head));
            const uint64_t body_size = kvspaceXvalueHeadLen(&slot_head);
            EXPECT(body_size == 7U);
            EXPECT(memcmp(value + KVSPACE_XVALUE_HEAD_LEN, "running",
                          body_size < 7U ? (size_t)body_size : 7U) == 0);
        }
    }

    /* 5b. 建帧：真实后端的 kvspaceMkindex。目录必须建出来，`‥lib` 写入后
     *     能读回函数 key，重复建帧幂等。 */
    {
        runtimec_snapshot_reset(&snapshot);
        snapshot.function_key = runtimec_key_view_from_c_string("/lib/sum");
        EXPECT(runtimec_kvspace_access_ensure_frame(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/vthread/v9/[1]"),
                   runtimec_string_view_from_c_string("/lib/sum"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);

        /*
         * 帧目录存在：枚举它的**父**目录 `/vthread/v9/`，条目里必须出现 `[1]/`。
         * 不枚举 `[1]/` 自身——真后端对叶子目录返回 0 条（目录条目由 mkindex
         * 建立，值不入目录索引），这是后端行为，不是失败。
         */
        int32_t parent_count = 0;
        EXPECT(kvspaceListLen(handle, "/vthread/v9/", 1, 0,
                                          &parent_count) == 0);
        EXPECT(parent_count >= 1);
        bool saw_frame_directory = false;
        for (int32_t ordinal = 0; ordinal < parent_count; ++ordinal) {
            uint8_t entry[256];
            uint32_t entry_size = 0U;
            if (kvspaceListAt(handle, "/vthread/v9/", 1, 0, ordinal,
                                         entry, (uint32_t)sizeof(entry),
                                         &entry_size) != 0) {
                continue;
            }
            if (entry_size == 4U && memcmp(entry, "[1]/", 4U) == 0) {
                saw_frame_directory = true;
            }
        }
        EXPECT(saw_frame_directory);

        /* `‥lib` 读回的就是函数 key。 */
        uint8_t *library_value = NULL;
        uint32_t library_size = 0U;
        EXPECT(kvspaceGet(handle,
                                    "/vthread/v9/[1]/\xE2\x80\xA5" "lib", 1,
                                    &library_value, &library_size) == 0);
        EXPECT(library_value != NULL && library_size > KVSPACE_XVALUE_HEAD_LEN);
        if (library_value != NULL &&
            library_size > KVSPACE_XVALUE_HEAD_LEN) {
            kvspaceXvalueHead slot_head;
            memcpy(&slot_head, library_value, sizeof(slot_head));
            const uint64_t body_size = kvspaceXvalueHeadLen(&slot_head);
            const char *expected = "/lib/sum";
            EXPECT(body_size == strlen(expected));
            EXPECT(memcmp(library_value + KVSPACE_XVALUE_HEAD_LEN, expected,
                          body_size < strlen(expected) ? (size_t)body_size
                                                       : strlen(expected)) ==
                   0);
        }

        /* 幂等：同一帧再建一次，不报错、不重写（值地址与长度都不变）。 */
        uint8_t *before_second = NULL;
        uint32_t before_second_size = 0U;
        EXPECT(kvspaceGet(handle,
                                    "/vthread/v9/[1]/\xE2\x80\xA5" "lib", 1,
                                    &before_second, &before_second_size) == 0);
        EXPECT(runtimec_kvspace_access_ensure_frame(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/vthread/v9/[1]"),
                   runtimec_string_view_from_c_string("/lib/sum"), &error) ==
               RUNTIMEC_STATUS_SUCCESS);
        uint8_t *after_second = NULL;
        uint32_t after_second_size = 0U;
        EXPECT(kvspaceGet(handle,
                                    "/vthread/v9/[1]/\xE2\x80\xA5" "lib", 1,
                                    &after_second, &after_second_size) == 0);
        EXPECT(after_second == before_second);
        EXPECT(after_second_size == before_second_size);

        /* 同一帧换成别的函数：冲突，必须失败。 */
        EXPECT(runtimec_kvspace_access_ensure_frame(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/vthread/v9/[1]"),
                   runtimec_string_view_from_c_string("/lib/other"),
                   &error) != RUNTIMEC_STATUS_SUCCESS);

        /* 非 /vthread 的路径：拒绝，不猜。 */
        EXPECT(runtimec_kvspace_access_ensure_frame(
                   services, &snapshot,
                   runtimec_key_view_from_c_string("/lib/not-a-frame"),
                   runtimec_string_view_from_c_string("/lib/sum"), &error) ==
               RUNTIMEC_STATUS_UNSUPPORTED);
    }

    /* 6. handoff 的完成条件是「条目被删除」。kvspace 的 watch 只在值变化时
     *    唤醒、删除不唤醒（见下一条用例），因此实现用轮询判定；这里用另一个
     *    线程扮演认领方，断言 handoff 在删除后立刻返回而不是拖满超时。 */
    {
        runtimec_handoff_request_t handoff;
        runtimec_handoff_request_reset(&handoff);
        handoff.operation = runtimec_string_view_from_c_string("sum");
        handoff.checkpoint.vthread_identifier =
            runtimec_string_view_from_c_string("v9");
        handoff.checkpoint.logical_program_counter =
            (runtimec_program_counter_t){1, 2};
        handoff_claimer_t claimer = {
            .handle = handle,
            .queue_key = "/lib/sum/vids/v9",
        };
        pthread_t claimer_thread;
        EXPECT(pthread_create(&claimer_thread, NULL, claim_handoff_entry,
                              &claimer) == 0);
        const uint64_t handoff_start = monotonic_nanoseconds();
        const runtimec_status_t handoff_status =
            runtimec_kvspace_access_handoff(services, &handoff, &error);
        const uint64_t handoff_elapsed =
            monotonic_nanoseconds() - handoff_start;
        EXPECT(pthread_join(claimer_thread, NULL) == 0);
        EXPECT(claimer.claimed);
        EXPECT(handoff_status == RUNTIMEC_STATUS_SUCCESS);
        /* 删除不唤醒 watch；若实现改用 watch(None) 兜底，这里会是 ~30s。 */
        EXPECT(handoff_elapsed < UINT64_C(2000000000));
        /* 认领方删除后，队列里不该再有该条目。 */
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        EXPECT(kvspaceGet(handle, "/lib/sum/vids/v9", 1, &value,
                                    &value_size) == 0);
        EXPECT(value == NULL || value_size == 0U);
    }

    EXPECT(runtimec_kvspace_access_destroy(services, &error) ==
           RUNTIMEC_STATUS_SUCCESS);
    kvspaceClose(handle);
    char cleanup[320];
    snprintf(cleanup, sizeof(cleanup), "rm -f %s/store.shm*", directory);
    if (system(cleanup) != 0) {
        fprintf(stderr, "cleanup failed: %s\n", cleanup);
    }
    rmdir(directory);
    if (failures != 0) {
        fprintf(stderr, "host_kvspace_backend_test: %d failure(s)\n",
                failures);
        return 1;
    }
    printf("host_kvspace_backend_test: ok\n");
    return 0;
}
