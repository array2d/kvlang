/* The unimplemented public interface must fail without producing output. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,       \
                    __LINE__, #condition);                                         \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static kvlang_runtime_error_t prepare_error(char *message, size_t capacity) {
    kvlang_runtime_error_t error = {0};
    error.message.data = message;
    error.message.capacity = capacity;
    return error;
}

int main(void) {
    char error_message[128] = "dirty";
    char value[16] = "dirty";
    char program_counter[16] = "dirty";
    kvlang_runtime_error_t error = prepare_error(
        error_message, sizeof(error_message));
    kvlang_runtime_t *runtime = (kvlang_runtime_t *)(uintptr_t)1U;
    kvlang_runtime_t *created_runtime = runtime;
    kvlang_runtime_create_request_t create_request = {"shared-memory://test"};
    const char *arguments[] = {"value"};
    kvlang_runtime_execute_request_t execute_request = {
        KVLANG_RUNTIME_ENTRY_FUNCTION, "example", arguments, 1U,
    };
    kvlang_runtime_execute_result_t result = {
        {value, sizeof(value), 5U, 5U},
        {program_counter, sizeof(program_counter), 5U, 5U},
    };

    RUNTIMEC_EXPECT(kvlang_runtime_create(
        &create_request, &created_runtime, &error) ==
        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(created_runtime == NULL);
    RUNTIMEC_EXPECT(error.status == RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(error.message.length != 0U);

    {
        char small_error_message[4] = {'x', 'y', 'z', '\0'};
        kvlang_runtime_error_t small_error = prepare_error(
            small_error_message, sizeof(small_error_message));
        created_runtime = runtime;
        RUNTIMEC_EXPECT(kvlang_runtime_create(
            &create_request, &created_runtime, &small_error) ==
            RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
        RUNTIMEC_EXPECT(created_runtime == NULL);
        RUNTIMEC_EXPECT(small_error.status ==
                        RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
        RUNTIMEC_EXPECT(small_error.operation_status ==
                        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
        RUNTIMEC_EXPECT(small_error.message.length == 0U);
        RUNTIMEC_EXPECT(small_error.message.required_length >=
                        sizeof(small_error_message));
        RUNTIMEC_EXPECT(small_error_message[0] == '\0');
        RUNTIMEC_EXPECT(small_error_message[1] == 'y');
        RUNTIMEC_EXPECT(small_error_message[2] == 'z');
    }

    error = prepare_error(error_message, sizeof(error_message));
    strcpy(value, "dirty");
    strcpy(program_counter, "dirty");
    result.value.length = 5U;
    result.value.required_length = 5U;
    result.program_counter.length = 5U;
    result.program_counter.required_length = 5U;
    RUNTIMEC_EXPECT(kvlang_runtime_execute(
        runtime, &execute_request, &result, &error) ==
        RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(result.value.length == 0U);
    RUNTIMEC_EXPECT(result.value.required_length == 0U);
    RUNTIMEC_EXPECT(value[0] == '\0');
    RUNTIMEC_EXPECT(result.program_counter.length == 0U);
    RUNTIMEC_EXPECT(result.program_counter.required_length == 0U);
    RUNTIMEC_EXPECT(program_counter[0] == '\0');

    error = prepare_error(error_message, sizeof(error_message));
    RUNTIMEC_EXPECT(kvlang_runtime_destroy(runtime, &error) ==
                    RUNTIMEC_STATUS_NOT_IMPLEMENTED);
    RUNTIMEC_EXPECT(error.status == RUNTIMEC_STATUS_NOT_IMPLEMENTED);

    if (failures != 0) {
        fprintf(stderr, "stub_fail_closed_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
