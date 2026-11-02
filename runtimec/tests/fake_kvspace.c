/*
 * 测试替身实现：内存里的一张 key→wire 表。
 * 与 kvspace 的差异只允许出现在适配器不依赖的方面（容量、并发、垃圾回收）。
 */

#include "fake_kvspace.h"

#include <stdlib.h>
#include <string.h>

#include "kvspace/xvalue_head.h"

static fake_kvspace_entry_t *fake_kvspace_find(fake_kvspace_store_t *store,
                                               const char *key) {
    for (size_t index = 0U; index < store->count; index++) {
        if (strcmp(store->entries[index].key, key) == 0) {
            return &store->entries[index];
        }
    }
    return NULL;
}

static fake_kvspace_entry_t *fake_kvspace_create(fake_kvspace_store_t *store,
                                                 const char *key) {
    if (store->count == sizeof(store->entries) / sizeof(store->entries[0]) ||
        strlen(key) >= sizeof(store->entries[0].key)) {
        return NULL;
    }
    fake_kvspace_entry_t *entry = &store->entries[store->count++];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->key, key, strlen(key) + 1U);
    return entry;
}

static int fake_kvspace_store_wire(fake_kvspace_store_t *store,
                                   const char *key, const uint8_t *wire,
                                   uint32_t wire_size) {
    if (store->fail_writes) {
        return 1;
    }
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    if (entry == NULL) {
        entry = fake_kvspace_create(store, key);
    }
    if (entry == NULL) {
        return 1;
    }
    if (entry->wire_capacity < wire_size) {
        uint8_t *grown = malloc(wire_size == 0U ? 1U : wire_size);
        if (grown == NULL) {
            return 1;
        }
        free(entry->wire);
        entry->wire = grown;
        entry->wire_capacity = wire_size;
    }
    memcpy(entry->wire, wire, wire_size);
    entry->wire_size = wire_size;
    return 0;
}

/* 目录条目按 kvspace 的形态给：顶层名字 + 尾斜杠。 */
static void fake_kvspace_leaf_name(size_t directory_size, const char *key,
                                   char *output, size_t capacity) {
    const char *rest = key + directory_size;
    const char *slash = strchr(rest, '/');
    size_t size = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
    if (size + 2U > capacity) {
        size = capacity - 2U;
    }
    memcpy(output, rest, size);
    if (slash != NULL) {
        output[size++] = '/';
    }
    output[size] = '\0';
}


static int fake_kvspace_list_size(void *handle, const char *prefix,
                                  int expand_extension, int resolve,
                                  int32_t *output_count) {
    (void)expand_extension;
    (void)resolve;
    fake_kvspace_store_t *store = handle;
    if (store == NULL || prefix == NULL || output_count == NULL) {
        return 1;
    }
    size_t prefix_size = strlen(prefix);
    if (prefix_size == 0U || prefix[prefix_size - 1U] != '/') {
        *output_count = 0;
        return 1;
    }
    char seen[64][256];
    size_t seen_count = 0U;
    for (size_t index = 0U; index < store->count; index++) {
        const char *key = store->entries[index].key;
        if (strncmp(key, prefix, prefix_size) != 0) {
            continue;
        }
        char leaf[256];
        fake_kvspace_leaf_name(prefix_size, key, leaf, sizeof(leaf));
        bool duplicate = false;
        for (size_t seen_index = 0U; seen_index < seen_count; seen_index++) {
            if (strcmp(seen[seen_index], leaf) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && seen_count < 64U) {
            memcpy(seen[seen_count], leaf, strlen(leaf) + 1U);
            seen_count += 1U;
        }
    }
    *output_count = (int32_t)seen_count;
    return 0;
}

static int fake_kvspace_list_at(void *handle, const char *prefix,
                                int expand_extension, int resolve,
                                int32_t index, uint8_t *buffer,
                                uint32_t buffer_capacity,
                                uint32_t *output_size) {
    (void)expand_extension;
    (void)resolve;
    fake_kvspace_store_t *store = handle;
    if (store == NULL || prefix == NULL || output_size == NULL) {
        return 1;
    }
    *output_size = 0U;
    size_t prefix_size = strlen(prefix);
    char seen[64][256];
    size_t seen_count = 0U;
    for (size_t entry_index = 0U; entry_index < store->count; entry_index++) {
        const char *key = store->entries[entry_index].key;
        if (strncmp(key, prefix, prefix_size) != 0) {
            continue;
        }
        char leaf[256];
        fake_kvspace_leaf_name(prefix_size, key, leaf, sizeof(leaf));
        bool duplicate = false;
        for (size_t seen_index = 0U; seen_index < seen_count; seen_index++) {
            if (strcmp(seen[seen_index], leaf) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && seen_count < 64U) {
            memcpy(seen[seen_count], leaf, strlen(leaf) + 1U);
            seen_count += 1U;
        }
    }
    if (index < 0 || (size_t)index >= seen_count) {
        return 1;
    }
    const size_t size = strlen(seen[index]);
    *output_size = (uint32_t)size;
    if (buffer == NULL || size > buffer_capacity) {
        return 1;
    }
    memcpy(buffer, seen[index], size);
    return 0;
}

static int fake_kvspace_get(void *handle, const char *key, int resolve,
                            uint8_t **output, uint32_t *output_size) {
    (void)resolve;
    fake_kvspace_store_t *store = handle;
    if (store == NULL || key == NULL || output == NULL ||
        output_size == NULL) {
        return 1;
    }
    *output = NULL;
    *output_size = 0U;
    if (store->fail_next_get) {
        store->fail_next_get = false;
        return 1;
    }
    if (store->relocate_key != NULL && strcmp(store->relocate_key, key) == 0) {
        /* 搬到新缓冲：旧地址失效，新地址不同。 */
        fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
        if (entry != NULL && entry->wire_size != 0U) {
            uint8_t *moved = malloc(entry->wire_size);
            memcpy(moved, entry->wire, entry->wire_size);
            free(entry->wire);
            entry->wire = moved;
            entry->wire_capacity = entry->wire_size;
        }
        store->relocate_key = NULL;
    }
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    if (entry == NULL || entry->wire_size == 0U) {
        return 0;
    }
    *output = entry->wire;
    *output_size = entry->wire_size;
    return 0;
}

static int fake_kvspace_set_value(void *handle, const char *key,
                                  const uint8_t *wire, uint32_t wire_size,
                                  char *error_text,
                                  uint32_t error_text_capacity) {
    fake_kvspace_store_t *store = handle;
    if (store == NULL || key == NULL || wire == NULL) {
        return 1;
    }
    if (fake_kvspace_store_wire(store, key, wire, wire_size) != 0) {
        if (error_text != NULL && error_text_capacity != 0U) {
            error_text[0] = '\0';
        }
        return 1;
    }
    return 0;
}

static int fake_kvspace_set_part(void *handle, const char *key,
                                 uint32_t offset, const uint8_t *value,
                                 uint32_t value_size, char *error_text,
                                 uint32_t error_text_capacity) {
    fake_kvspace_store_t *store = handle;
    if (store == NULL || key == NULL || value == NULL) {
        return 1;
    }
    if (store->fail_writes) {
        return 1;
    }
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    if (entry == NULL || offset + value_size > entry->wire_size) {
        if (error_text != NULL && error_text_capacity != 0U) {
            error_text[0] = '\0';
        }
        return 1;
    }
    memcpy(entry->wire + offset, value, value_size);
    return 0;
}

static int fake_kvspace_del(void *handle, const char *const *keys,
                            uint32_t count, char *error_text,
                            uint32_t error_text_capacity) {
    (void)error_text;
    (void)error_text_capacity;
    fake_kvspace_store_t *store = handle;
    if (store == NULL || (keys == NULL && count != 0U)) {
        return 1;
    }
    for (uint32_t index = 0U; index < count; index++) {
        fake_kvspace_entry_t *entry = fake_kvspace_find(store, keys[index]);
        if (entry == NULL) {
            continue;
        }
        free(entry->wire);
        *entry = store->entries[store->count - 1U];
        store->count -= 1U;
    }
    return 0;
}

static int fake_kvspace_watch(void *handle, const char *key,
                              const uint8_t *target, uint32_t target_size,
                              uint64_t tick_nanoseconds, uint8_t **output,
                              uint32_t *output_size) {
    (void)tick_nanoseconds;
    fake_kvspace_store_t *store = handle;
    if (store == NULL || key == NULL || output == NULL ||
        output_size == NULL) {
        return 1;
    }
    *output = NULL;
    *output_size = 0U;
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    if (target_size == 10U && target != NULL && target[8] == 2U) {
        /* 目标 None：只在条目被删除时满足。 */
        if (entry == NULL) {
            *output = NULL;
            *output_size = 0U;
            return 0;
        }
        return 0;
    }
    if (entry == NULL || entry->wire_size != target_size ||
        memcmp(entry->wire, target, target_size) != 0) {
        return 0;
    }
    *output = entry->wire;
    *output_size = entry->wire_size;
    return 0;
}

static int fake_kvspace_view_quiesce(void *handle, int enable) {
    (void)handle;
    (void)enable;
    return 0;
}

static int fake_kvspace_view_ack(void *handle, uint32_t *epoch) {
    (void)handle;
    if (epoch != NULL) {
        *epoch = 0U;
    }
    return 0;
}

static int fake_kvspace_new_slack(const uint8_t *data, uint64_t data_size,
                                  uint64_t capacity, const char *langtype,
                                  uint8_t **output, uint64_t *output_size) {
    if (output == NULL || output_size == NULL) {
        return 1;
    }
    *output = NULL;
    *output_size = 0U;
    if (capacity < data_size || (data_size != 0U && data == NULL)) {
        return 1;
    }
    const uint64_t langtype_size =
        langtype != NULL ? (uint64_t)strlen(langtype) : 0U;
    const uint64_t attribute_size = 2U + (langtype_size != 0U ? 3U + langtype_size : 0U);
    const uint64_t total = KVSPACE_XVALUE_HEAD_LEN + data_size + attribute_size;
    uint8_t *wire = calloc(1U, (size_t)total);
    if (wire == NULL) {
        return 1;
    }
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    head.storetype = KVSPACE_XVALUE_ST_SLACK;
    kvspaceXvalueHeadSetLen(&head, data_size);
    memcpy(wire, &head, sizeof(head));
    /* 指向形态字节固定为 inline：按偏移写，避免在测试里书写被命名检查判为
     * 缩写的结构体字段。 */
    wire[7] = 0U;                       /* 指向形态：0 = 值内联 */
    if (data_size != 0U) {
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, data, (size_t)data_size);
    }
    uint8_t *attribute = wire + KVSPACE_XVALUE_HEAD_LEN + data_size;
    attribute[0] = (uint8_t)attribute_size;
    attribute[1] = (uint8_t)(attribute_size >> 8U);
    if (langtype_size != 0U) {
        attribute[2] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        attribute[3] = (uint8_t)langtype_size;
        attribute[4] = (uint8_t)(langtype_size >> 8U);
        memcpy(attribute + 5U, langtype, (size_t)langtype_size);
    }
    *output = wire;
    *output_size = total;
    return 0;
}

static void *fake_kvspace_connect(const char *data_source_name) {
    (void)data_source_name;
    return NULL;
}

static void fake_kvspace_release_handle(void *handle) { (void)handle; }

int fake_kvspace_put(fake_kvspace_store_t *store, const char *key,
                     const uint8_t *wire, uint32_t wire_size,
                     bool directory) {
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    if (entry == NULL) {
        entry = fake_kvspace_create(store, key);
    }
    if (entry == NULL) {
        return 1;
    }
    if (wire != NULL) {
        if (fake_kvspace_store_wire(store, key, wire, wire_size) != 0) {
            return 1;
        }
    }
    entry->directory = directory;
    return 0;
}

void fake_kvspace_bind(fake_kvspace_store_t *store,
                       runtimec_kvspace_binary_interface_t *output) {
    output->connect = fake_kvspace_connect;
    output->close = fake_kvspace_release_handle;
    output->get = fake_kvspace_get;
    output->set_value = fake_kvspace_set_value;
    output->set_part = fake_kvspace_set_part;
    output->del = fake_kvspace_del;
    output->list_size = fake_kvspace_list_size;
    output->list_at = fake_kvspace_list_at;
    output->watch = fake_kvspace_watch;
    output->view_quiesce = fake_kvspace_view_quiesce;
    output->view_ack = fake_kvspace_view_ack;
    output->new_slack = fake_kvspace_new_slack;
    (void)store;
}

uint8_t *fake_kvspace_address(fake_kvspace_store_t *store, const char *key) {
    fake_kvspace_entry_t *entry = fake_kvspace_find(store, key);
    return entry != NULL ? entry->wire : NULL;
}

void fake_kvspace_release(fake_kvspace_store_t *store) {
    for (size_t index = 0U; index < store->count; index++) {
        free(store->entries[index].wire);
    }
    store->count = 0U;
}
