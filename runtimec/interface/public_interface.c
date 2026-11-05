/* runtimec 公共 C 接口：把结构化请求翻译成 engine 的调用。 */

#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "engine/execution_window.h"
#include "engine/platform_session.h"

/* 派生状态的容量上限：不表达任何正确性开关，只是缓存规模。 */
#define RUNTIMEC_DEFAULT_COMPILATION_CACHE_CAPACITY 256U
#define RUNTIMEC_DEFAULT_MACHINE_CODE_CAPACITY (1U << 20)

static int kvlang_runtime_text_buffer_is_valid(
    const kvlang_runtime_text_buffer_t *buffer) {
    return buffer != NULL &&
           (buffer->capacity == 0U || buffer->data != NULL);
}

static void kvlang_runtime_text_buffer_clear(
    kvlang_runtime_text_buffer_t *buffer) {
    if (buffer != NULL) {
        buffer->length = 0U;
        buffer->required_length = 0U;
        if (buffer->data != NULL && buffer->capacity != 0U) {
            buffer->data[0] = '\0';
        }
    }
}

static runtimec_status_t kvlang_runtime_fail(
    kvlang_runtime_error_t *error, runtimec_status_t status,
    const char *message) {
    if (error == NULL) {
        return status;
    }
    error->status = status;
    error->operation_status = status;
    error->code = 0;
    if (!kvlang_runtime_text_buffer_is_valid(&error->message)) {
        error->status = RUNTIMEC_STATUS_INVALID;
        error->operation_status = RUNTIMEC_STATUS_INVALID;
        error->message.length = 0U;
        error->message.required_length = 0U;
        return RUNTIMEC_STATUS_INVALID;
    }
    kvlang_runtime_text_buffer_clear(&error->message);
    if (message == NULL) {
        return status;
    }
    size_t message_length = strlen(message);
    error->message.required_length = message_length;
    if (error->message.capacity <= message_length) {
        error->status = RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(error->message.data, message, message_length + 1U);
    error->message.length = message_length;
    return status;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_create(
    const kvlang_runtime_create_request_t *request,
    kvlang_runtime_t **output, kvlang_runtime_error_t *error) {
    if (output != NULL) {
        *output = NULL;
    }
    if (request == NULL || output == NULL || error == NULL ||
        !kvlang_runtime_text_buffer_is_valid(&error->message) ||
        request->data_source_name == NULL ||
        request->data_source_name[0] == '\0' ||
        request->instance_identifier == 0U) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec create request");
    }
    runtimec_session_options_t options;
    /* 结构体一次填满：engine 会校验 structure_size，缺省路径不留未初始化字节。 */
    options.structure_size = sizeof(options);
    options.allocator = runtimec_default_allocator();
    /* 编译缓存与机器码容量是派生状态的容量上限，不表达正确性开关。 */
    options.compilation_cache_capacity = RUNTIMEC_DEFAULT_COMPILATION_CACHE_CAPACITY;
    /*
     * phase 2 的阶段选择：BinRWIR Compiler 只有 VMBIN 档，这里关掉 JIT。
     * 不是缺省值——`enable_jit=true` 时编译器会明确返回 NOT_IMPLEMENTED（配置
     * 自洽不等于能力存在），公共入口就会必然失败。JIT 落地时把这两行改回
     * 计划值：machine_code_capacity 恢复 RUNTIMEC_DEFAULT_MACHINE_CODE_CAPACITY、
     * enable_jit 置真。
     */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;

    runtimec_platform_session_t *platform_session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_create(
        request->data_source_name, request->instance_identifier, &options,
        &platform_session, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /* engine 的错误文本是借用视图：在本帧内转成公共错误结构。 */
        if (engine_error.message.size != 0U &&
            engine_error.message.data != NULL) {
            return kvlang_runtime_fail(error, status,
                                       engine_error.message.data);
        }
        return kvlang_runtime_fail(error, status,
                                   runtimec_status_name(status));
    }
    *output = (kvlang_runtime_t *)platform_session;
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_destroy(
    kvlang_runtime_t *runtime, kvlang_runtime_error_t *error) {
    if (runtime == NULL || error == NULL ||
        !kvlang_runtime_text_buffer_is_valid(&error->message)) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec destroy request");
    }
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_destroy(
        (runtimec_platform_session_t *)runtime, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            engine_error.message.data != NULL ? engine_error.message.data
                                              : runtimec_status_name(status));
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_execute(
    kvlang_runtime_t *runtime,
    const kvlang_runtime_execute_request_t *request,
    kvlang_runtime_execute_result_t *output,
    kvlang_runtime_error_t *error) {
    if (output != NULL) {
        kvlang_runtime_text_buffer_clear(&output->value);
        kvlang_runtime_text_buffer_clear(&output->program_counter);
    }
    if (runtime == NULL || request == NULL || output == NULL || error == NULL ||
        !kvlang_runtime_text_buffer_is_valid(&error->message) ||
        !kvlang_runtime_text_buffer_is_valid(&output->value) ||
        !kvlang_runtime_text_buffer_is_valid(&output->program_counter) ||
        request->entry_kind < KVLANG_RUNTIME_ENTRY_FUNCTION ||
        request->entry_kind > KVLANG_RUNTIME_ENTRY_BOOTSTRAP ||
        request->entry_name == NULL || request->entry_name[0] == '\0' ||
        (request->argument_count != 0U && request->arguments == NULL)) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec execute request");
    }
    /*
     * 本版只实现 `FUNCTION`：给一个函数路径，跑到第一个退出边界为止。
     *
     * 其余三档**显式 NOT_IMPLEMENTED**，不静默放过：
     * `PROGRAM_COUNTER` 与 `VTHREAD` 要读持久化游标（`/vthread/<vid>/‥pc`），
     * `BOOTSTRAP` 要建 vthread——三者都需要先有「从存储恢复」的入口，
     * 那是下一步的事。用「先当 FUNCTION 跑」代劳，会让调用方以为恢复生效了。
     */
    if (request->entry_kind != KVLANG_RUNTIME_ENTRY_FUNCTION) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "only ENTRY_FUNCTION is implemented; PROGRAM_COUNTER / VTHREAD / "
            "BOOTSTRAP need a storage-recovery entry");
    }

    runtimec_session_t *session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    runtimec_status_t status = runtimec_platform_session_get_session(
        (const runtimec_platform_session_t *)runtime, &session, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            engine_error.message.data != NULL ? engine_error.message.data
                                              : runtimec_status_name(status));
    }

    /*
     * 帧路径与 vthread 标识由本层定：调用方给的是「函数名」，不是帧位置。
     *
     * 每次 execute 用同一组固定值（`runtimec-execute` / `[1]`）：本入口是
     * **一次调用一个函数**，不承载续跑。要续跑得用 `VTHREAD` 档，
     * 而那一档明确未实现——所以这里不需要唯一化，也不需要保存。
     */
    /* vthread 标识必须是**数字字符串**：`execution_window` 用它算
     * checkpoint 的落点，非数字直接判请求非法（实测 "runtimec-execute" 被拒）。
     * 0 是合法的固定值——本入口不承载续跑，不需要唯一化。 */
    static const char entry_vthread[] = "0";
    static const char entry_frame[] = "/vthread/0/[1]";
    char function_path[512];
    const int function_length =
        snprintf(function_path, sizeof(function_path), "/lib/%s",
                 request->entry_name);
    if (function_length < 0 ||
        (size_t)function_length >= sizeof(function_path)) {
        return kvlang_runtime_fail(error, RUNTIMEC_STATUS_INVALID,
                                   "entry name is too long");
    }

    runtimec_execution_window_request_t window_request;
    memset(&window_request, 0, sizeof(window_request));
    window_request.function_path =
        runtimec_string_view_from_c_string(function_path);
    window_request.vthread_identifier =
        runtimec_string_view_from_c_string(entry_vthread);
    window_request.frame_path =
        runtimec_string_view_from_c_string(entry_frame);
    window_request.stack_identifier = 1;
    window_request.source_logical_program_counter = 1U;
    window_request.arguments = NULL;
    window_request.argument_count = 0U;

    runtimec_execution_window_result_t window_result;
    memset(&window_result, 0, sizeof(window_result));
    runtimec_error_t window_error;
    runtimec_error_reset(&window_error);
    status = runtimec_execution_window_execute(session, &window_request,
                                               &window_result, &window_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            window_error.message.data != NULL ? window_error.message.data
                                              : runtimec_status_name(status));
    }
    /* 只有 DONE 才是「跑完了」。CALL / HANDOFF / YIELD 都表示**停在半路**，
     * 报成功会让调用方以为函数返回了——那正是「以看起来合理的值失败」。 */
    if (window_result.execution_exit.kind != BINRWIR_EXECUTION_EXIT_DONE) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "execution stopped before DONE: this entry does not drive CALL / "
            "HANDOFF / YIELD yet");
    }

    /* `value`：函数返回值（DONE 时可能有）；没有就留空串，不编。 */
    if (window_result.return_value.size != 0U) {
        if (output->value.capacity < window_result.return_value.size + 1U) {
            output->value.required_length = window_result.return_value.size;
            return kvlang_runtime_fail(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                                       "value buffer is too small");
        }
        memcpy(output->value.data, window_result.return_value.data,
               window_result.return_value.size);
        output->value.data[window_result.return_value.size] = '\0';
        output->value.length = window_result.return_value.size;
    }
    /* `program_counter`：停在哪个 source pc（十进制文本）。 */
    {
        char counter_text[32];
        const int counter_length =
            snprintf(counter_text, sizeof(counter_text), "%u",
                     window_result.execution_exit.logical_program_counter);
        if (counter_length < 0 ||
            output->program_counter.capacity < (size_t)counter_length + 1U) {
            return kvlang_runtime_fail(error,
                                       RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                                       "program_counter buffer is too small");
        }
        memcpy(output->program_counter.data, counter_text,
               (size_t)counter_length + 1U);
        output->program_counter.length = (size_t)counter_length;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
