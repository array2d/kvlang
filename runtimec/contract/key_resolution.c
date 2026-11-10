#include "contract/key_resolution.h"

#include <string.h>

/* 把 `source`（长 `length`）抄进 `output`，含结尾 NUL。放不下返回 false，不截断
 * ——截断会把「路径太长」变成一个指向别处的合法路径。 */
static bool copy_key(char *output, size_t output_capacity, const char *source,
                     size_t length) {
    if (length + 1U > output_capacity) {
        return false;
    }
    memcpy(output, source, length);
    output[length] = '\0';
    return true;
}

runtimec_status_t runtimec_key_resolve_prefix(
    const runtimec_key_reader_t reader, void *reader_context, const char *key,
    char *output_key, size_t output_capacity) {
    if (reader == NULL || key == NULL || output_key == NULL ||
        output_capacity == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const size_t key_length = strlen(key);
    if (!copy_key(output_key, output_capacity, key, key_length)) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }

    for (unsigned hop = 0U; hop < RUNTIMEC_KEY_RESOLUTION_HOP_LIMIT; ++hop) {
        const char *langtype = NULL;
        const char *body = NULL;
        size_t langtype_size = 0U;
        size_t body_size = 0U;

        const runtimec_status_t status = reader(
            reader_context, output_key, &langtype, &langtype_size, &body,
            &body_size);
        if (status == RUNTIMEC_STATUS_INVALID) {
            /* 键不存在：后端 `resolve_fetch` 在这里 `return h`（空），调用方拿到的
             * 是**当前这一跳的路径**——列举一个不存在的目录得到空列表，不是错误。 */
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }

        /* 只有 langtype 首字符是 `*` 才算指针。head 没有 ptr 位；`**T` / `(*T)` 同样
         * 以 `*` 开头，与 `runtimec_key_is_pointer` 的判据保持同一句话。 */
        const bool is_pointer = langtype != NULL && langtype_size != 0U &&
                                langtype[0] == '*';
        if (!is_pointer) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (body == NULL) {
            /* 指针却没有 body：写读双方对格式理解不一致，不猜。 */
            return RUNTIMEC_STATUS_INVALID;
        }
        if (!copy_key(output_key, output_capacity, body, body_size)) {
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
    }
    /* 跳满上限仍未收敛：后端同样判失败（返回 NULL）。不返回「最后那个路径」——
     * 那会把环状的指针链当成正常数据。 */
    return RUNTIMEC_STATUS_ERROR;
}
