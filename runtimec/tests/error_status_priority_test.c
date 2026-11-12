
#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

static kvlang_runtime_create_request_t invalid_request(void) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.data_source_name = NULL; 
    request.instance_identifier = 1U;
    return request;
}

static kvlang_runtime_create_request_t unconnectable_request(void) {
    kvlang_runtime_create_request_t request;
    memset(&request, 0, sizeof(request));
    request.structure_size = sizeof(request);
    request.data_source_name = "no-such-scheme://runtimec-error-status-test";
    request.instance_identifier = 1U;
    return request;
}

typedef struct observation {
    runtimec_status_t returned;
    runtimec_status_t error_status;
    size_t length;
    size_t required_length;
} observation_t;

static observation_t observe(const kvlang_runtime_create_request_t *request,
                             char *data, size_t capacity) {
    observation_t seen;
    kvlang_runtime_t *runtime = NULL;

    memset(&seen, 0, sizeof(seen));
    if (data == NULL) {
        seen.returned = kvlang_runtime_create(request, &runtime, NULL);
        return seen;
    }
    kvlang_runtime_error_t error;
    memset(&error, 0, sizeof(error));
    error.message.data = data;
    error.message.capacity = capacity;
    if (capacity != 0U) {
        memset(data, 0, capacity);
    }
    seen.returned = kvlang_runtime_create(request, &runtime, &error);
    seen.error_status = error.status;
    seen.length = error.message.length;
    seen.required_length = error.message.required_length;
    if (runtime != NULL) {
        char destroy_message[128];
        kvlang_runtime_error_t destroy_error;
        memset(&destroy_error, 0, sizeof(destroy_error));
        destroy_message[0] = '\0';
        destroy_error.message.data = destroy_message;
        destroy_error.message.capacity = sizeof(destroy_message);
        (void)kvlang_runtime_destroy(runtime, &destroy_error);
    }
    return seen;
}

static void check_status_is_independent_of_the_message_buffer(
    const kvlang_runtime_create_request_t *request,
    runtimec_status_t expected, const char *label) {
    char ample_text[512];
    char one_byte[1];
    char message[256];

    const observation_t without_error = observe(request, NULL, 0U);
    const observation_t with_zero_capacity = observe(request, ample_text, 0U);
    const observation_t with_one_byte = observe(request, one_byte, 1U);
    const observation_t with_ample_capacity =
        observe(request, ample_text, sizeof(ample_text));

    snprintf(message, sizeof(message),
             "%s: err=NULL returned %d (expected %d)", label,
             (int)without_error.returned, (int)expected);
    check(without_error.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: cap=0 returned %d (expected %d)", label,
             (int)with_zero_capacity.returned, (int)expected);
    check(with_zero_capacity.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: cap=1 returned %d (expected %d)", label,
             (int)with_one_byte.returned, (int)expected);
    check(with_one_byte.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: ample capacity returned %d (expected %d)", label,
             (int)with_ample_capacity.returned, (int)expected);
    check(with_ample_capacity.returned == expected, message);

    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status", label,
             (int)with_zero_capacity.error_status);
    check(with_zero_capacity.error_status == with_zero_capacity.returned,
          message);
    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status (cap=1)",
             label, (int)with_one_byte.error_status);
    check(with_one_byte.error_status == with_one_byte.returned, message);
    snprintf(message, sizeof(message),
             "%s: error.status=%d differs from the returned status (ample)",
             label, (int)with_ample_capacity.error_status);
    check(with_ample_capacity.error_status == with_ample_capacity.returned,
          message);

    snprintf(message, sizeof(message),
             "%s: cap=0 wrote %zu byte(s); a partial message is not allowed",
             label, with_zero_capacity.length);
    check(with_zero_capacity.length == 0U, message);
    snprintf(message, sizeof(message),
             "%s: cap=1 wrote %zu byte(s); a partial message is not allowed",
             label, with_one_byte.length);
    check(with_one_byte.length == 0U, message);

    snprintf(message, sizeof(message),
             "%s: required_length differs between cap=0 (%zu) and cap=1 (%zu)",
             label, with_zero_capacity.required_length,
             with_one_byte.required_length);
    check(with_zero_capacity.required_length ==
              with_one_byte.required_length,
          message);
    snprintf(message, sizeof(message),
             "%s: required_length=%zu but the ample run wrote %zu byte(s)",
             label, with_ample_capacity.required_length,
             with_ample_capacity.length);
    check(with_ample_capacity.required_length == with_ample_capacity.length,
          message);
    
    snprintf(message, sizeof(message),
             "%s: ample capacity wrote nothing; the message is lost", label);
    check(with_ample_capacity.length != 0U, message);

    {
        kvlang_runtime_t *runtime = NULL;
        kvlang_runtime_error_t error;
        memset(&error, 0, sizeof(error));
        error.message.data = NULL;
        error.message.capacity = 64U; 
        const runtimec_status_t returned =
            kvlang_runtime_create(request, &runtime, &error);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer returned %d (expected %d)", label,
                 (int)returned, (int)expected);
        check(returned == expected, message);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer changed error.status to %d", label,
                 (int)error.status);
        check(error.status == expected, message);
        snprintf(message, sizeof(message),
                 "%s: malformed error buffer wrote %zu byte(s)", label,
                 error.message.length);
        check(error.message.length == 0U, message);
        if (runtime != NULL) {
            (void)kvlang_runtime_destroy(runtime, NULL);
        }
    }
}

int main(void) {
    
    const kvlang_runtime_create_request_t invalid = invalid_request();
    const kvlang_runtime_create_request_t unconnectable = unconnectable_request();

    check_status_is_independent_of_the_message_buffer(
        &invalid, RUNTIMEC_STATUS_INVALID, "invalid request");
    check_status_is_independent_of_the_message_buffer(
        &unconnectable, RUNTIMEC_STATUS_ERROR, "unconnectable data source");

    check(observe(&invalid, NULL, 0U).returned !=
              observe(&unconnectable, NULL, 0U).returned,
          "two different failures collapsed to the same status");

    if (g_failures == 0) {
        printf("error_status_priority_test: OK\n");
        return 0;
    }
    printf("error_status_priority_test: %d failure(s)\n", g_failures);
    return 1;
}
