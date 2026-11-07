/*
 * 组件：engine/frame_state 的实现
 * 作用：跨 `execution_window_execute` 存活的帧状态表——`frame_path` 是键，
 *       值是 `{bindings, function_path, generation}`。
 * 联系：`session.c` 按值持有表并在销毁时兜底；`execution_window.c` 借出表，
 *       进入时 `open`、DONE/ERROR 退出时 `close`。
 * 约束：线性扫（单次运行的存活帧数是个位数，不做哈希）；同路径只许绑一个函数；
 *       分配失败不破坏既有表（先分配成功再插入）。不藏 function-static 状态。
 */

#include "engine/frame_state.h"

#include <string.h>

static runtimec_status_t runtimec_frame_state_fail(runtimec_error_t *error,
                                                   runtimec_status_t status,
                                                   int64_t code,
                                                   const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/* 两个视图逐字节相等。先比长度：长度不同时不该去 memcmp 越界区间。 */
static bool runtimec_frame_state_view_equals(const char *left, size_t left_size,
                                             const char *right,
                                             size_t right_size) {
    if (left_size != right_size) {
        return false;
    }
    if (left_size == 0U) {
        return true;
    }
    return memcmp(left, right, left_size) == 0;
}

static bool runtimec_frame_state_matches(const runtimec_frame_state_t *state,
                                         runtimec_key_view_t frame_path) {
    return runtimec_frame_state_view_equals(state->frame_path_text,
                                            state->frame_path.size,
                                            frame_path.data, frame_path.size);
}

static ptrdiff_t runtimec_frame_state_find(
    const runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < table->count; ++ordinal) {
        if (runtimec_frame_state_matches(table->entries[ordinal], frame_path)) {
            return (ptrdiff_t)ordinal;
        }
    }
    return -1;
}

/* 销毁一个条目：bindings、路径副本、条目自己。顺序无关，但都归表所有。 */
static void runtimec_frame_state_discard(
    runtimec_frame_state_t *state, const runtimec_allocator_t *allocator) {
    if (state->bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(state->bindings);
        state->bindings = NULL;
    }
    if (state->frame_path_text != NULL) {
        (void)runtimec_allocator_deallocate(allocator, state->frame_path_text,
                                            state->frame_path.size + 1U, 1U);
        state->frame_path_text = NULL;
    }
    (void)runtimec_allocator_deallocate(allocator, state, sizeof(*state),
                                        _Alignof(runtimec_frame_state_t));
}

/* 从表里摘掉第 ordinal 项：尾巴前移，容量不变。 */
static void runtimec_frame_state_remove(runtimec_frame_state_table_t *table,
                                        size_t ordinal) {
    size_t index;

    for (index = ordinal + 1U; index < table->count; ++index) {
        table->entries[index - 1U] = table->entries[index];
    }
    table->count -= 1U;
}

/*
 * 保证还能再插一项。**先扩容、再插入**：失败时表保持原样（`count` 不变），
 * 调用方手上那份新分配还能自己销毁。
 */
static runtimec_status_t runtimec_frame_state_reserve(
    runtimec_frame_state_table_t *table, runtimec_error_t *error) {
    size_t capacity;
    runtimec_frame_state_t **grown;

    if (table->count < table->capacity) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    capacity = table->capacity == 0U ? 8U : table->capacity * 2U;
    grown = (runtimec_frame_state_t **)runtimec_allocator_allocate(
        &table->allocator, capacity * sizeof(*grown),
        _Alignof(runtimec_frame_state_t *));
    if (grown == NULL) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 1,
            "frame state table allocation failed");
    }
    if (table->count != 0U) {
        memcpy(grown, table->entries, table->count * sizeof(*grown));
    }
    if (table->entries != NULL) {
        (void)runtimec_allocator_deallocate(
            &table->allocator, table->entries,
            table->capacity * sizeof(*grown),
            _Alignof(runtimec_frame_state_t *));
    }
    table->entries = grown;
    table->capacity = capacity;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 建一张新绑定表并放进表里。路径文本由表拥有：请求里的字节本次调用后就失效。 */
static runtimec_status_t runtimec_frame_state_insert(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_error_t *error) {
    runtimec_frame_state_t *state;
    runtimec_status_t status;
    char *text;

    text = (char *)runtimec_allocator_allocate(&table->allocator,
                                              frame_path.size + 1U, 1U);
    if (text == NULL) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
            "frame state path copy allocation failed");
    }
    memcpy(text, frame_path.data, frame_path.size);
    text[frame_path.size] = '\0';

    state = (runtimec_frame_state_t *)runtimec_allocator_allocate(
        &table->allocator, sizeof(*state), _Alignof(runtimec_frame_state_t));
    if (state == NULL) {
        (void)runtimec_allocator_deallocate(&table->allocator, text,
                                            frame_path.size + 1U, 1U);
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 3,
            "frame state allocation failed");
    }
    memset(state, 0, sizeof(*state));
    state->frame_path = runtimec_key_view_make(text, frame_path.size);
    state->frame_path_text = text;
    state->function_path = function_path;
    state->generation = storage_generation;

    if (binrwir_frame_bindings_create(
            &table->allocator,
            binding_capacity == 0U ? 1U : binding_capacity,
            &state->bindings) != RUNTIMEC_STATUS_SUCCESS) {
        state->bindings = NULL;
        runtimec_frame_state_discard(state, &table->allocator);
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 4,
            "frame bindings allocation failed");
    }

    status = runtimec_frame_state_reserve(table, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_frame_state_discard(state, &table->allocator);
        return status;
    }
    table->entries[table->count++] = state;
    *output_state = state;
    return RUNTIMEC_STATUS_SUCCESS;
}

void runtimec_frame_state_table_initialize(runtimec_frame_state_table_t *table,
                                           runtimec_allocator_t allocator) {
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
    table->allocator = allocator;
}

runtimec_status_t runtimec_frame_state_open(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_frame_state_result_t *output_result, runtimec_error_t *error) {
    ptrdiff_t ordinal;
    /*
     * **初始化为 NULL，不是「兜底」。**
     *
     * 每条控制流都写了 `state`：`ordinal < 0` 时经 `&state` 由
     * `runtimec_frame_state_insert` 在**唯一成功出口**写入，`ordinal >= 0` 时
     * 直接取 `table->entries[ordinal]`；`insert` 失败则在本行之前 return。
     *
     * 但 `insert` 是另一个翻译单元里的 `static` 函数，`-O2` 的跨函数数据流
     * 追不到「它的成功路径一定写 `*output_state`」，于是 `-Werror=
     * maybe-uninitialized` 在 `state->function_path` 处误报——**Debug（-O0）
     * 不报、Release（-O2）报**，正是这条误报的形态。
     *
     * 补初值让编译器的保守假设成立，**不改控制流、不加失败分支**。
     */
    runtimec_frame_state_t *state = NULL;
    runtimec_status_t status;

    if (output_state != NULL) {
        *output_state = NULL;
    }
    if (output_result != NULL) {
        *output_result = RUNTIMEC_FRAME_STATE_INVALID;
    }
    if (table == NULL || error == NULL || output_state == NULL ||
        output_result == NULL || !runtimec_allocator_is_valid(&table->allocator) ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U ||
        !runtimec_string_view_is_valid(function_path) ||
        function_path.size == 0U ||
        storage_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 5,
            "invalid frame state open request");
    }
    ordinal = runtimec_frame_state_find(table, frame_path);
    if (ordinal < 0) {
        status = runtimec_frame_state_insert(table, frame_path, function_path,
                                             storage_generation,
                                             binding_capacity, &state, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        *output_state = state;
        *output_result = RUNTIMEC_FRAME_STATE_CREATED;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    state = table->entries[(size_t)ordinal];
    /*
     * 同帧只能绑一个函数。判据与 `ensure_frame` 是同一句话（那边拦「帧上已有
     * 另一个 `‥lib`」），这里拦内存里那张表——同一句话两处执行，不是两份规则。
     */
    if (!runtimec_frame_state_view_equals(
            state->function_path.data, state->function_path.size,
            function_path.data, function_path.size)) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 6,
            "frame path is already bound to another function");
    }
    if (state->generation == storage_generation) {
        *output_state = state;
        *output_result = RUNTIMEC_FRAME_STATE_REUSED;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * 代次变了：旧绑定指向的存储已被换掉，整张表作废重建。
     *
     * **旧表先留下**，新表建成功才换——分配失败时旧表原样可用，不制造「表没了
     * 但帧还在」的半状态。函数路径一致（上面已判），新条目沿用同一个文本缓冲。
     */
    {
        binrwir_frame_bindings_t *rebuilt = NULL;
        if (binrwir_frame_bindings_create(
                &table->allocator,
                binding_capacity == 0U ? 1U : binding_capacity,
                &rebuilt) != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_frame_state_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 7,
                "frame bindings allocation failed");
        }
        (void)binrwir_frame_bindings_destroy(state->bindings);
        state->bindings = rebuilt;
        state->function_path = function_path;
        state->generation = storage_generation;
    }
    *output_state = state;
    *output_result = RUNTIMEC_FRAME_STATE_REBUILT;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_frame_state_close(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_error_t *error) {
    ptrdiff_t ordinal;

    if (table == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (!runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return runtimec_frame_state_fail(
            error, RUNTIMEC_STATUS_INVALID, 8,
            "invalid frame state close request");
    }
    ordinal = runtimec_frame_state_find(table, frame_path);
    if (ordinal < 0) {
        /* 不在表里 = 已经不存在，与「我刚让它不存在」同义，不是错误。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    runtimec_frame_state_discard(table->entries[(size_t)ordinal],
                                       &table->allocator);
    runtimec_frame_state_remove(table, (size_t)ordinal);
    return RUNTIMEC_STATUS_SUCCESS;
}

size_t runtimec_frame_state_count(const runtimec_frame_state_table_t *table) {
    return table != NULL ? table->count : 0U;
}

void runtimec_frame_state_table_release(runtimec_frame_state_table_t *table) {
    if (table == NULL) {
        return;
    }
    while (table->count != 0U) {
        table->count -= 1U;
        runtimec_frame_state_discard(table->entries[table->count],
                                          &table->allocator);
    }
    if (table->entries != NULL) {
        (void)runtimec_allocator_deallocate(
            &table->allocator, table->entries,
            table->capacity * sizeof(*table->entries),
            _Alignof(runtimec_frame_state_t *));
    }
    table->entries = NULL;
    table->capacity = 0U;
}
