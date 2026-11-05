/*
 * 组件：binrwir/frame_bindings 的实现
 * 作用：帧派生绑定表的创建、绑定、解析、失效与销毁。
 * 约束：只保存 XValue 基址与 generation，不保存 body 地址、不保存值，也不
 *       在解析成功之外给出任何猜测地址。
 */

#include "binrwir/frame_bindings.h"

#include <stdio.h>
#include <string.h>

struct binrwir_frame_bindings {
    runtimec_allocator_t allocator;
    binrwir_frame_binding_t *entries;
    /*
     * 每个槽一份自有 key 字节块：`[slot_key][caller_key]`。绑定表比创建它的
     * 那一层活得久（帧准备与 handler 都会在分配 key 缓冲之后绑完就返回），
     * 所以键必须由表自己拥有；存借用视图就是 use-after-free。
     * 块长度 = binding->slot_key.size + binding->caller_key.size。
     */
    char **key_storage;
    size_t capacity;
};

static void binrwir_frame_binding_clear(binrwir_frame_binding_t *binding) {
    memset(binding, 0, sizeof(*binding));
    binding->kind = (uint8_t)BINRWIR_BINDING_INVALID;
    binding->valid = false;
}

/* 释放某槽的自有 key 块。**必须在清空 binding 之前调用**：长度从视图里取。 */
static void binrwir_frame_bindings_release_keys(
    binrwir_frame_bindings_t *bindings, size_t slot) {
    size_t size;

    if (bindings->key_storage == NULL || bindings->key_storage[slot] == NULL) {
        return;
    }
    size = bindings->entries[slot].slot_key.size +
           bindings->entries[slot].caller_key.size;
    (void)runtimec_allocator_deallocate(&bindings->allocator,
                                         bindings->key_storage[slot], size, 1U);
    bindings->key_storage[slot] = NULL;
}

/*
 * 复制两张键并由表持有，返回指向自有字节的视图。**先分配成功再覆盖旧绑定**：
 * 分配失败时旧绑定原样保留，不制造「表里有一个半条绑定」。
 */
static runtimec_status_t binrwir_frame_bindings_copy_keys(
    binrwir_frame_bindings_t *bindings, size_t slot,
    runtimec_key_view_t slot_key, runtimec_key_view_t caller_key,
    runtimec_key_view_t *output_slot_key,
    runtimec_key_view_t *output_caller_key) {
    const size_t total = slot_key.size + caller_key.size;
    char *storage = NULL;

    if (total != 0U) {
        storage = (char *)runtimec_allocator_allocate(&bindings->allocator,
                                                       total, 1U);
        if (storage == NULL) {
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
        if (slot_key.size != 0U) {
            memcpy(storage, slot_key.data, slot_key.size);
        }
        if (caller_key.size != 0U) {
            memcpy(storage + slot_key.size, caller_key.data, caller_key.size);
        }
    }
    binrwir_frame_bindings_release_keys(bindings, slot);
    bindings->key_storage[slot] = storage;
    *output_slot_key = runtimec_key_view_make(storage, slot_key.size);
    *output_caller_key =
        caller_key.size != 0U
            ? runtimec_key_view_make(storage + slot_key.size, caller_key.size)
            : runtimec_key_view_make(NULL, 0U);
    return RUNTIMEC_STATUS_SUCCESS;
}

static int binrwir_frame_bindings_is_valid(
    const binrwir_frame_bindings_t *bindings) {
    return bindings != NULL && bindings->entries != NULL &&
           bindings->capacity != 0U &&
           runtimec_allocator_is_valid(&bindings->allocator);
}

runtimec_status_t binrwir_frame_bindings_create(
    const runtimec_allocator_t *allocator, size_t capacity,
    binrwir_frame_bindings_t **output_bindings) {
    binrwir_frame_bindings_t *bindings;
    binrwir_frame_binding_t *entries;
    char **key_storage;
    size_t ordinal;

    if (output_bindings != NULL) {
        *output_bindings = NULL;
    }
    if (output_bindings == NULL || !runtimec_allocator_is_valid(allocator) ||
        capacity == 0U || capacity > BINRWIR_FRAME_BINDING_SLOT_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    bindings = (binrwir_frame_bindings_t *)runtimec_allocator_allocate(
        allocator, sizeof(*bindings), _Alignof(binrwir_frame_bindings_t));
    if (bindings == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    entries = (binrwir_frame_binding_t *)runtimec_allocator_allocate(
        allocator, sizeof(*entries) * capacity,
        _Alignof(binrwir_frame_binding_t));
    if (entries == NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, bindings, sizeof(*bindings),
            _Alignof(binrwir_frame_bindings_t));
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    key_storage = (char **)runtimec_allocator_allocate(
        allocator, sizeof(*key_storage) * capacity, _Alignof(char *));
    if (key_storage == NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, entries, sizeof(*entries) * capacity,
            _Alignof(binrwir_frame_binding_t));
        (void)runtimec_allocator_deallocate(
            allocator, bindings, sizeof(*bindings),
            _Alignof(binrwir_frame_bindings_t));
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    for (ordinal = 0U; ordinal < capacity; ++ordinal) {
        binrwir_frame_binding_clear(&entries[ordinal]);
        key_storage[ordinal] = NULL;
    }
    bindings->allocator = *allocator;
    bindings->entries = entries;
    bindings->key_storage = key_storage;
    bindings->capacity = capacity;
    *output_bindings = bindings;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_destroy(
    binrwir_frame_bindings_t *bindings) {
    runtimec_allocator_t allocator;
    binrwir_frame_binding_t *entries;
    char **key_storage;
    size_t capacity;
    size_t ordinal;

    if (!binrwir_frame_bindings_is_valid(bindings)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (ordinal = 0U; ordinal < bindings->capacity; ++ordinal) {
        binrwir_frame_bindings_release_keys(bindings, ordinal);
    }
    allocator = bindings->allocator;
    entries = bindings->entries;
    key_storage = bindings->key_storage;
    capacity = bindings->capacity;
    (void)runtimec_allocator_deallocate(
        &allocator, key_storage, sizeof(*key_storage) * capacity,
        _Alignof(char *));
    (void)runtimec_allocator_deallocate(
        &allocator, bindings, sizeof(*bindings),
        _Alignof(binrwir_frame_bindings_t));
    (void)runtimec_allocator_deallocate(&allocator, entries,
                                        sizeof(*entries) * capacity,
                                        _Alignof(binrwir_frame_binding_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_reset(
    binrwir_frame_bindings_t *bindings) {
    size_t ordinal;

    if (!binrwir_frame_bindings_is_valid(bindings)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (ordinal = 0U; ordinal < bindings->capacity; ++ordinal) {
        binrwir_frame_bindings_release_keys(bindings, ordinal);
        binrwir_frame_binding_clear(&bindings->entries[ordinal]);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

size_t binrwir_frame_bindings_get_capacity(
    const binrwir_frame_bindings_t *bindings) {
    return bindings != NULL ? bindings->capacity : 0U;
}

runtimec_status_t binrwir_frame_bindings_bind(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_key_view_t caller_key,
    runtimec_xvalue_address_t xvalue_address, uint64_t generation) {
    binrwir_frame_binding_t *binding;
    runtimec_key_view_t owned_slot_key;
    runtimec_key_view_t owned_caller_key;
    runtimec_status_t status;

    if (!binrwir_frame_bindings_is_valid(bindings) || slot >= bindings->capacity ||
        !runtimec_key_view_is_valid(slot_key) ||
        !runtimec_key_view_is_valid(caller_key) ||
        slot_key.size == 0U ||
        xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_frame_bindings_copy_keys(bindings, slot, slot_key,
                                              caller_key, &owned_slot_key,
                                              &owned_caller_key);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    binding = &bindings->entries[slot];
    binding->slot_key = owned_slot_key;
    binding->caller_key = owned_caller_key;
    binding->xvalue_address = xvalue_address;
    binding->bytes = NULL;
    binding->length = 0U;
    binding->generation = generation;
    binding->kind = (uint8_t)BINRWIR_BINDING_ADDRESS;
    binding->valid = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_bind_inline(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_bytes_view_t bytes,
    uint64_t generation) {
    binrwir_frame_binding_t *binding;
    runtimec_key_view_t owned_slot_key;
    runtimec_key_view_t owned_caller_key;
    runtimec_status_t status;

    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity || !runtimec_key_view_is_valid(slot_key) ||
        !runtimec_bytes_view_is_valid(bytes) ||
        generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_frame_bindings_copy_keys(bindings, slot, slot_key,
                                              runtimec_key_view_make(NULL, 0U),
                                              &owned_slot_key,
                                              &owned_caller_key);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    binding = &bindings->entries[slot];
    binding->slot_key = owned_slot_key;
    binding->caller_key = owned_caller_key;
    binding->xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    binding->bytes = bytes.data;
    binding->length = bytes.size;
    binding->generation = generation;
    binding->kind = (uint8_t)BINRWIR_BINDING_INLINE;
    binding->valid = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_invalidate(
    binrwir_frame_bindings_t *bindings, uint16_t slot) {
    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binrwir_frame_bindings_release_keys(bindings, slot);
    binrwir_frame_binding_clear(&bindings->entries[slot]);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_get(
    const binrwir_frame_bindings_t *bindings, uint16_t slot,
    uint64_t expected_generation,
    const binrwir_frame_binding_t **output_binding) {
    const binrwir_frame_binding_t *entry;

    if (output_binding != NULL) {
        *output_binding = NULL;
    }
    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity || output_binding == NULL ||
        expected_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    entry = &bindings->entries[slot];
    if (!entry->valid || entry->generation != expected_generation) {
        /* 失效或 generation 不匹配：绝不交出旧绑定。 */
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    *output_binding = entry;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 帧内槽 key 的**唯一拼法**。声明处的长注释解释了为什么放这里而不是调用方各自拼。
 */

size_t binrwir_frame_local_key(runtimec_string_view_t frame_path,
                               runtimec_string_view_t name, char *output,
                               size_t capacity) {
    if (!runtimec_string_view_is_valid(frame_path) ||
        !runtimec_string_view_is_valid(name) || frame_path.size == 0U ||
        name.size == 0U || output == NULL) {
        return SIZE_MAX;
    }
    /* 需要的字节：帧路径 + '/' + 名字 + 结尾 NUL。 */
    if (frame_path.size > SIZE_MAX - name.size - 2U) {
        return SIZE_MAX;
    }
    const size_t needed = frame_path.size + 1U + name.size;
    if (capacity < needed + 1U) {
        return SIZE_MAX;
    }
    memcpy(output, frame_path.data, frame_path.size);
    output[frame_path.size] = '/';
    memcpy(output + frame_path.size + 1U, name.data, name.size);
    output[needed] = '\0';
    return needed;
}

size_t binrwir_frame_parameter_key(runtimec_string_view_t frame_path,
                                   uint16_t parameter_ordinal,
                                   bool write_parameter, char *output,
                                   size_t capacity) {
    if (!runtimec_string_view_is_valid(frame_path) ||
        frame_path.size == 0U || output == NULL) {
        return SIZE_MAX;
    }
    /*
     * 序号从 0 起、坐标从 1 起：argidx 0 对应 `[0,-1]`（读）或 `[0,+1]`（写）。
     * `parameter_ordinal` 是 uint16，加 1 不会溢出到符号位之外。
     */
    const unsigned coordinate = (unsigned)parameter_ordinal + 1U;
    const char sign = write_parameter ? '+' : '-';
    char suffix[32];
    const int suffix_length = snprintf(suffix, sizeof(suffix), "[0,%c%u]",
                                       sign, coordinate);
    if (suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix)) {
        return SIZE_MAX;
    }
    if (frame_path.size > SIZE_MAX - (size_t)suffix_length - 2U) {
        return SIZE_MAX;
    }
    const size_t needed = frame_path.size + 1U + (size_t)suffix_length;
    if (capacity < needed + 1U) {
        return SIZE_MAX;
    }
    memcpy(output, frame_path.data, frame_path.size);
    output[frame_path.size] = '/';
    memcpy(output + frame_path.size + 1U, suffix, (size_t)suffix_length);
    output[needed] = '\0';
    return needed;
}
