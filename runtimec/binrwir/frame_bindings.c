/*
 * 组件：binrwir/frame_bindings 的实现
 * 作用：帧派生绑定表的创建、绑定、解析、失效与销毁。
 * 约束：只保存 XValue 基址与 generation，不保存 body 地址、不保存值，也不
 *       在解析成功之外给出任何猜测地址。
 */

#include "binrwir/frame_bindings.h"

#include <string.h>

struct binrwir_frame_bindings {
    runtimec_allocator_t allocator;
    binrwir_frame_binding_t *entries;
    size_t capacity;
};

static void binrwir_frame_binding_clear(binrwir_frame_binding_t *binding) {
    memset(binding, 0, sizeof(*binding));
    binding->valid = false;
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
    for (ordinal = 0U; ordinal < capacity; ++ordinal) {
        binrwir_frame_binding_clear(&entries[ordinal]);
    }
    bindings->allocator = *allocator;
    bindings->entries = entries;
    bindings->capacity = capacity;
    *output_bindings = bindings;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_destroy(
    binrwir_frame_bindings_t *bindings) {
    runtimec_allocator_t allocator;
    binrwir_frame_binding_t *entries;
    size_t capacity;

    if (!binrwir_frame_bindings_is_valid(bindings)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = bindings->allocator;
    entries = bindings->entries;
    capacity = bindings->capacity;
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

    if (!binrwir_frame_bindings_is_valid(bindings) || slot >= bindings->capacity ||
        !runtimec_key_view_is_valid(slot_key) ||
        !runtimec_key_view_is_valid(caller_key) ||
        slot_key.size == 0U ||
        xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binding = &bindings->entries[slot];
    binding->slot_key = slot_key;
    binding->caller_key = caller_key;
    binding->xvalue_address = xvalue_address;
    binding->generation = generation;
    binding->valid = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_resolve(
    const binrwir_frame_bindings_t *bindings, uint16_t slot,
    uint64_t expected_generation,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation) {
    const binrwir_frame_binding_t *binding;

    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (output_generation != NULL) {
        *output_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    }
    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity || output_xvalue_address == NULL ||
        expected_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binding = &bindings->entries[slot];
    if (!binding->valid || binding->generation != expected_generation) {
        /* 失效或 generation 不匹配：绝不返回旧地址。 */
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    if (binding->xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    *output_xvalue_address = binding->xvalue_address;
    if (output_generation != NULL) {
        *output_generation = binding->generation;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_invalidate(
    binrwir_frame_bindings_t *bindings, uint16_t slot) {
    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binrwir_frame_binding_clear(&bindings->entries[slot]);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_frame_bindings_get(
    const binrwir_frame_bindings_t *bindings, uint16_t slot,
    const binrwir_frame_binding_t **output_binding) {
    if (output_binding != NULL) {
        *output_binding = NULL;
    }
    if (!binrwir_frame_bindings_is_valid(bindings) ||
        slot >= bindings->capacity || output_binding == NULL ||
        !bindings->entries[slot].valid) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_binding = &bindings->entries[slot];
    return RUNTIMEC_STATUS_SUCCESS;
}
