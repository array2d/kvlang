#include "contract/key_directory.h"

#include <string.h>

#include "contract/key_order.h"

/* 名字块的对齐：`char` 块只要能按字节读即可，但分配器要求 2 的幂。 */
#define RUNTIMEC_KEY_DIRECTORY_NAMES_ALIGNMENT 1U
#define RUNTIMEC_KEY_DIRECTORY_OFFSETS_ALIGNMENT \
    _Alignof(uint32_t)

void runtimec_key_directory_reset(runtimec_key_directory_t *directory,
                                  const runtimec_allocator_t *allocator,
                                  void *source_context,
                                  runtimec_key_directory_source_t source) {
    if (directory == NULL) {
        return;
    }
    memset(directory, 0, sizeof(*directory));
    if (allocator != NULL) {
        directory->allocator = *allocator;
    }
    directory->source_context = source_context;
    directory->source = source;
}

runtimec_status_t runtimec_key_directory_destroy(
    runtimec_key_directory_t *directory) {
    if (directory == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (directory->names != NULL) {
        runtimec_allocator_deallocate(&directory->allocator, directory->names,
                                      directory->names_capacity,
                                      RUNTIMEC_KEY_DIRECTORY_NAMES_ALIGNMENT);
    }
    if (directory->offsets != NULL) {
        runtimec_allocator_deallocate(
            &directory->allocator, directory->offsets,
            (size_t)directory->offsets_capacity * sizeof(uint32_t),
            RUNTIMEC_KEY_DIRECTORY_OFFSETS_ALIGNMENT);
    }
    directory->names = NULL;
    directory->names_capacity = 0U;
    directory->names_length = 0U;
    directory->offsets = NULL;
    directory->offsets_capacity = 0U;
    directory->count = 0U;
    directory->generation = 0U;
    directory->valid = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 从 `name\0name\0…` 里数项数并切出 offsets。 */
static uint32_t key_directory_split(runtimec_key_directory_t *directory) {
    uint32_t count = 0U;
    uint32_t at = 0U;
    while (at < directory->names_length) {
        if (count < directory->offsets_capacity) {
            directory->offsets[count] = at;
        }
        ++count;
        /* 跳过这一项的字节与它的 NUL。 */
        while (at < directory->names_length && directory->names[at] != '\0') {
            ++at;
        }
        if (at < directory->names_length) {
            ++at;
        }
    }
    return count;
}

/* 一项的原始名字视图（**含**尾 `/`）：排序用这个，与后端 `child_cmp` 一致。 */
static void key_directory_entry(const runtimec_key_directory_t *directory,
                                uint32_t index, const char **output_name,
                                size_t *output_size) {
    const uint32_t start = directory->offsets[index];
    size_t size = 0U;
    while (start + size < directory->names_length &&
           directory->names[start + size] != '\0') {
        ++size;
    }
    if (output_name != NULL) {
        *output_name = directory->names + start;
    }
    if (output_size != NULL) {
        *output_size = size;
    }
}

/* 插入排序：目录子项数的量级是「一帧的指令条数 / 一个数组的元素数」，
 * 而名字块本身已在内存里，比较是纯字节操作。比快排少一份递归与临时区。 */
static void key_directory_sort(runtimec_key_directory_t *directory) {
    for (uint32_t index = 1U; index < directory->count; ++index) {
        const uint32_t current = directory->offsets[index];
        uint32_t position = index;

        const char *current_name = NULL;
        size_t current_size = 0U;
        key_directory_entry(directory, index, &current_name, &current_size);

        while (position > 0U) {
            const char *previous_name = NULL;
            size_t previous_size = 0U;
            key_directory_entry(directory, position - 1U, &previous_name,
                                &previous_size);
            if (runtimec_key_compare_directory_entry(
                    previous_name, previous_size, current_name,
                    current_size) <= 0) {
                break;
            }
            directory->offsets[position] = directory->offsets[position - 1U];
            --position;
        }
        directory->offsets[position] = current;
    }
}

static runtimec_status_t key_directory_grow_names(
    runtimec_key_directory_t *directory, uint32_t required) {
    if (required <= directory->names_capacity && directory->names != NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    char *names = runtimec_allocator_allocate(
        &directory->allocator, (size_t)required,
        RUNTIMEC_KEY_DIRECTORY_NAMES_ALIGNMENT);
    if (names == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (directory->names != NULL) {
        runtimec_allocator_deallocate(&directory->allocator, directory->names,
                                      directory->names_capacity,
                                      RUNTIMEC_KEY_DIRECTORY_NAMES_ALIGNMENT);
    }
    directory->names = names;
    directory->names_capacity = required;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t key_directory_grow_offsets(
    runtimec_key_directory_t *directory, uint32_t required) {
    if (required <= directory->offsets_capacity && directory->offsets != NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    const size_t bytes = (size_t)required * sizeof(uint32_t);
    uint32_t *offsets = runtimec_allocator_allocate(
        &directory->allocator, bytes,
        RUNTIMEC_KEY_DIRECTORY_OFFSETS_ALIGNMENT);
    if (offsets == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (directory->offsets != NULL) {
        runtimec_allocator_deallocate(
            &directory->allocator, directory->offsets,
            (size_t)directory->offsets_capacity * sizeof(uint32_t),
            RUNTIMEC_KEY_DIRECTORY_OFFSETS_ALIGNMENT);
    }
    directory->offsets = offsets;
    directory->offsets_capacity = required;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_key_directory_refresh(
    runtimec_key_directory_t *directory, const char *key) {
    if (directory == NULL || directory->source == NULL || key == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    uint32_t length = 0U;
    uint32_t count = 0U;
    uint64_t generation = 0U;

    /* 先只探测：拿到长度/个数/generation，未变就立即返回。 */
    runtimec_status_t status = directory->source(
        directory->source_context, key, NULL, 0U, &length, &count, &generation);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (directory->valid && generation == directory->generation &&
        count == directory->count) {
        return RUNTIMEC_STATUS_SUCCESS;
    }

    /*
     * 探测可能只报个数、不报字节数（旧 ABI 的 `ListLen` 只给 N，没有 M）。
     * 这种情况下按「至少每项 1 字节 + NUL」估一个下界，剩下的交给取回时的
     * INSUFFICIENT_CAPACITY 重试路径——那条路径按 source 回报的精确长度扩容。
     */
    uint32_t wanted = length != 0U ? length + 1U : count + 1U;
    if (wanted < 2U) {
        wanted = 2U;
    }
    status = key_directory_grow_names(directory, wanted);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    char *names = directory->names;
    status = directory->source(directory->source_context, key, names,
                               directory->names_capacity, &length, &count,
                               &generation);
    /*
     * 容量不足：source 已把**所需**总长度写进 `length`（新 ABI 的 `kvspaceList`
     * 与旧 ABI 适配层都遵守这条）。按它扩容再取一次——这是唯一需要重试的分支，
     * 写成「放弃、等下次 refresh」会让调用方看到一次无谓的失败。
     */
    if (status == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY) {
        if (length <= directory->names_capacity) {
            /* 报的所需长度不比现有容量大，说明不是容量问题而是集合在变：不重试，
             * 交给下一次 refresh（否则这里会死循环）。 */
            directory->valid = false;
            return status;
        }
        status = key_directory_grow_names(directory, length + 1U);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        names = directory->names;
        status = directory->source(directory->source_context, key, names,
                                   directory->names_capacity, &length, &count,
                                   &generation);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        directory->valid = false;
        return status;
    }
    if (length >= directory->names_capacity) {
        /* 取回长度顶到容量：没有空间写结尾 NUL，也无法确定边界。放弃本次。 */
        directory->valid = false;
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    names[length] = '\0';
    directory->names_length = length;

    status = key_directory_grow_offsets(directory, count + 1U);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    directory->count = key_directory_split(directory);
    key_directory_sort(directory);
    directory->generation = generation;
    directory->valid = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_key_directory_at(
    const runtimec_key_directory_t *directory, uint32_t index,
    const char **output_name, uint32_t *output_size) {
    if (directory == NULL || !directory->valid || index >= directory->count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const uint32_t start = directory->offsets[index];
    uint32_t size = 0U;
    while (start + size < directory->names_length &&
           directory->names[start + size] != '\0') {
        ++size;
    }
    if (output_name != NULL) {
        *output_name = directory->names + start;
    }
    if (output_size != NULL) {
        *output_size = size;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_key_directory_find(
    const runtimec_key_directory_t *directory, const char *name,
    uint32_t name_size, uint32_t *output_index) {
    if (directory == NULL || !directory->valid || name == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const char *member = NULL;
    size_t member_size = 0U;
    runtimec_key_member_name(name, name_size, &member, &member_size);
    (void)member;

    uint32_t low = 0U;
    uint32_t high = directory->count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const char *candidate = NULL;
        size_t candidate_size = 0U;
        key_directory_entry(directory, middle, &candidate, &candidate_size);
        const int compared = runtimec_key_compare_directory_entry(
            candidate, candidate_size, member, member_size);
        if (compared == 0) {
            if (output_index != NULL) {
                *output_index = middle;
            }
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (compared < 0) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return RUNTIMEC_STATUS_INVALID;
}
