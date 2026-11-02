/* runtimec 公共 C 接口：把结构化请求翻译成 engine 的调用。 */

#include <string.h>

#include <kvlang/runtimec.h>

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
        request->data_source_name[0] == '\0') {
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
    options.machine_code_capacity = RUNTIMEC_DEFAULT_MACHINE_CODE_CAPACITY;
    options.enable_jit = true;

    runtimec_platform_session_t *platform_session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_create(
        request->data_source_name, &options, &platform_session, &engine_error);
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
    return kvlang_runtime_fail(
        error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
        "runtimec execution is not implemented");
}
