
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvlang/runtimec.h>

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

static runtimec_status_t test_extension_handler(void *frame, const void *instruction,
                                                void *output_exit) {
    (void)frame;
    (void)instruction;
    (void)output_exit;
    return RUNTIMEC_STATUS_SUCCESS;
}

#define TEST_FREE_OPERATION_IDENTIFIER UINT32_C(0x7001)

#define TEST_TAKEN_OPERATION_IDENTIFIER UINT32_C(0x0177)

static kvlang_runtime_extension_entry_t make_entry(uint32_t identifier,
                                                   uint32_t structure_size,
                                                   kvlang_runtime_extension_handler_t handler) {
    kvlang_runtime_extension_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.structure_size = structure_size;
    entry.operation_identifier = identifier;
    entry.flags = 0U;
    entry.handler = handler;
    return entry;
}

static kvlang_runtime_create_request_t make_request(
    const kvlang_runtime_extension_module_t *modules, size_t module_count,
    size_t structure_size) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = structure_size;
    request.data_source_name = "shm:///tmp/runtimec-extension-test";
    request.instance_identifier = 1U;
    request.extensions = modules;
    request.extension_count = module_count;
    return request;
}

static runtimec_status_t create_with(const kvlang_runtime_create_request_t *request,
                                     char *message, size_t message_capacity) {
    kvlang_runtime_t *runtime = NULL;
    kvlang_runtime_error_t error;
    memset(&error, 0, sizeof(error));
    error.message.data = message;
    error.message.capacity = message_capacity;
    error.message.length = 0U;
    error.message.required_length = 0U;
    if (message_capacity != 0U) {
        message[0] = '\0';
    }
    const runtimec_status_t status = kvlang_runtime_create(request, &runtime, &error);
    if (runtime != NULL) {
        kvlang_runtime_error_t destroy_error;
        memset(&destroy_error, 0, sizeof(destroy_error));
        char destroy_message[128];
        destroy_message[0] = '\0';
        destroy_error.message.data = destroy_message;
        destroy_error.message.capacity = sizeof(destroy_message);
        (void)kvlang_runtime_destroy(runtime, &destroy_error);
    }
    if (message_capacity != 0U && error.message.length < message_capacity) {
        message[error.message.length] = '\0';
    }
    return status;
}

static void test_valid_extension_is_accepted(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "test-extension",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    const runtimec_status_t status = create_with(&request, message, sizeof(message));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        printf("[FAIL] a valid extension must be accepted, status=%d message=%s\n",
               (int)status, message);
        ++g_failures;
    }
}

static void test_duplicate_identifier_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_TAKEN_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "duplicate-module",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    const runtimec_status_t status = create_with(&request, message, sizeof(message));
    check(status != RUNTIMEC_STATUS_SUCCESS,
          "an extension reusing a builtin opid must be rejected");
    check(strstr(message, "duplicate-module") != NULL,
          "the rejection must name the offending module");
}

static void test_missing_handler_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t),
        NULL);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "handlerless",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "an extension entry without a handler must be rejected");
}

static void test_wrong_request_size_is_rejected(void) {
    const kvlang_runtime_create_request_t request = make_request(
        NULL, 0U, sizeof(kvlang_runtime_create_request_t) - 1U);
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "a create request with a wrong structure_size must be rejected");
}

static void test_wrong_entry_size_is_rejected(void) {
    const kvlang_runtime_extension_entry_t entry = make_entry(
        TEST_FREE_OPERATION_IDENTIFIER, sizeof(kvlang_runtime_extension_entry_t) - 1U,
        test_extension_handler);
    const kvlang_runtime_extension_module_t module = {
        .module_name = "wrong-size",
        .entries = &entry,
        .entry_count = 1U,
    };
    const kvlang_runtime_create_request_t request = make_request(
        &module, 1U, sizeof(kvlang_runtime_create_request_t));
    char message[256];
    check(create_with(&request, message, sizeof(message)) != RUNTIMEC_STATUS_SUCCESS,
          "an extension entry with a wrong structure_size must be rejected");
}

int main(void) {
    test_valid_extension_is_accepted();
    test_duplicate_identifier_is_rejected();
    test_missing_handler_is_rejected();
    test_wrong_request_size_is_rejected();
    test_wrong_entry_size_is_rejected();

    if (g_failures != 0) {
        printf("extension_registration: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("extension_registration: OK "
           "(valid accepted; duplicate/missing-handler/wrong-size rejected)\n");
    return 0;
}
