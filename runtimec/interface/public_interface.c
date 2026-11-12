
#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "engine/execution_window.h"
#include "engine/platform_session.h"

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

static void kvlang_runtime_report(kvlang_runtime_error_t *error,
                                  runtimec_status_t status,
                                  const char *message) {
    if (error == NULL) {
        return;
    }
    error->status = status;
    error->operation_status = status;
    error->code = 0;
    kvlang_runtime_text_buffer_clear(&error->message);
    if (message == NULL || !kvlang_runtime_text_buffer_is_valid(&error->message)) {
        return;
    }
    const size_t message_length = strlen(message);
    
    error->message.required_length = message_length;
    if (error->message.capacity <= message_length) {
        return;
    }
    memcpy(error->message.data, message, message_length + 1U);
    error->message.length = message_length;
}

static runtimec_status_t kvlang_runtime_fail(
    kvlang_runtime_error_t *error, runtimec_status_t status,
    const char *message) {
    kvlang_runtime_report(error, status, message);
    return status;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_create(
    const kvlang_runtime_create_request_t *request,
    kvlang_runtime_t **output, kvlang_runtime_error_t *error) {
    if (output != NULL) {
        *output = NULL;
    }
    
    if (request == NULL || output == NULL ||
        request->data_source_name == NULL ||
        request->data_source_name[0] == '\0' ||
        request->instance_identifier == 0U) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec create request");
    }
    
    if (request->structure_size != sizeof(kvlang_runtime_create_request_t)) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "runtimec create request has an unexpected structure_size");
    }
    
    for (size_t module = 0U; module < request->extension_count; ++module) {
        const kvlang_runtime_extension_module_t *entry_module =
            &request->extensions[module];
        if (entry_module->module_name == NULL ||
            entry_module->module_name[0] == '\0' ||
            entry_module->entries == NULL || entry_module->entry_count == 0U) {
            return kvlang_runtime_fail(
                error, RUNTIMEC_STATUS_INVALID,
                "runtimec extension module is incomplete");
        }
        for (size_t item = 0U; item < entry_module->entry_count; ++item) {
            const kvlang_runtime_extension_entry_t *entry =
                &entry_module->entries[item];
            
            if (entry->structure_size != sizeof(kvlang_runtime_extension_entry_t)) {
                return kvlang_runtime_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    "runtimec extension entry has an unexpected structure_size");
            }
            if (entry->handler == NULL) {
                return kvlang_runtime_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    "runtimec extension entry has no handler");
            }
        }
    }
    if (request->extension_count != 0U && request->extensions == NULL) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "runtimec extension table is missing");
    }
    runtimec_session_options_t options;
    
    options.structure_size = sizeof(options);
    options.allocator = runtimec_default_allocator();
    
    options.compilation_cache_capacity = RUNTIMEC_DEFAULT_COMPILATION_CACHE_CAPACITY;
    
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    
    options.extension_modules = request->extensions;
    options.extension_count = request->extension_count;

    runtimec_platform_session_t *platform_session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_create(
        request->data_source_name, request->instance_identifier, &options,
        &platform_session, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        
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
    
    if (runtime == NULL) {
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
    
    if (runtime == NULL || request == NULL || output == NULL ||
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

    static const char entry_vthread[] = "0";
    static const char entry_frame[] = "/vthread/0/[1]";
    
    if (request->entry_name[0] == '/') {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "entry name must be a bare function name, e.g. \"test\"; "
            "got a key starting with '/' (this entry adds the /lib/ prefix "
            "itself)");
    }
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
    
    if (window_result.execution_exit.kind != BINRWIR_EXECUTION_EXIT_DONE) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "execution stopped before DONE: this entry does not drive CALL / "
            "HANDOFF / YIELD yet");
    }

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
