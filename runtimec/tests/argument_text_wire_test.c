
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvlang/runtimec.h>
#include <kvlang/xvalue.h>

#include "binrwir/argument.h"
#include "binrwir/binfunc.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "contract/allocator.h"
#include "contract/extension_interface.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static runtimec_allocator_t test_allocator(void) {
    const runtimec_allocator_t allocator = {NULL, test_allocate,
                                            test_deallocate};
    return allocator;
}

typedef struct fixture {
    binrwir_binfunc_t *program;
    binrwir_instruction_view_t instruction;
    binrwir_frame_bindings_t *bindings;
    binrwir_binvthread_frame_t frame;
    runtimec_allocator_t allocator;
} fixture_t;

static int fixture_build(fixture_t *fixture, const uint8_t *wire,
                         size_t wire_size, const char *langtype) {
    const runtimec_allocator_t allocator = test_allocator();
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    uint32_t literal_index = 0U;
    uint32_t instruction_identifier = 0U;
    uint32_t parameter_index = 0U;
    binrwir_argument_t argument;
    binrwir_instruction_build_request_t request;

    memset(fixture, 0, sizeof(*fixture));
    fixture->allocator = allocator;

    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;

    if (binrwir_binfunc_builder_create(&allocator, &identity, &builder) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    if (binrwir_binfunc_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ, 0U,
            runtimec_string_view_from_c_string(langtype),
            &parameter_index) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    if (binrwir_binfunc_builder_add_literal(
            builder, runtimec_bytes_view_make(wire, wire_size),
            runtimec_string_view_from_c_string(langtype),
            &literal_index) != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    if (binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U,
                                literal_index, &argument) !=
        RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = 1U;
    request.operation_identifier = UINT16_C(0x7001);
    request.read_arguments = (binrwir_argument_span_t){&argument, 1U};
    request.write_arguments = (binrwir_argument_span_t){NULL, 0U};
    if (binrwir_binfunc_builder_add_instruction(builder, &request,
                                                &instruction_identifier) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_builder_finalize(builder, &fixture->program) !=
            RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_binfunc_builder_destroy(builder);
        return 0;
    }
    (void)binrwir_binfunc_builder_destroy(builder);

    if (binrwir_binfunc_get_instruction(fixture->program, 0U,
                                        &fixture->instruction) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    if (binrwir_frame_bindings_create(&allocator, 1U, &fixture->bindings) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return 0;
    }
    memset(&fixture->frame, 0, sizeof(fixture->frame));
    fixture->frame.program = fixture->program;
    fixture->frame.bindings = fixture->bindings;
    fixture->frame.snapshot_generation = 1U;
    fixture->frame.program_generation = 1U;
    return 1;
}

static void fixture_release(fixture_t *fixture) {
    if (fixture->bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(fixture->bindings);
    }
    if (fixture->program != NULL) {
        (void)binrwir_binfunc_destroy(fixture->program);
    }
}

static void check_round_trip(const uint8_t *body, size_t body_size,
                             const char *label) {
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    fixture_t fixture;
    char output[256];
    kvlang_runtime_text_buffer_t buffer;
    kvlang_runtime_error_t error;
    char message[256];

    if (kvlangXvalueHeadNewSlack(body, (uint64_t)body_size, "[]char/utf8",
                                 &wire, &wire_size) != 0 ||
        wire == NULL) {
        printf("[FAIL] %s: cannot encode the wire\n", label);
        ++g_failures;
        return;
    }
    if (!fixture_build(&fixture, wire, (size_t)wire_size, "[]char/utf8")) {
        printf("[FAIL] %s: cannot build the fixture\n", label);
        ++g_failures;
        free(wire);
        return;
    }
    memset(output, 0, sizeof(output));
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = output;
    buffer.capacity = sizeof(output);
    memset(&error, 0, sizeof(error));
    memset(message, 0, sizeof(message));
    error.message.data = message;
    error.message.capacity = sizeof(message);

    const runtimec_status_t status = kvlang_runtime_read_argument_text(
        &fixture.frame, &fixture.instruction, 0U, &buffer, &error);
    snprintf(message, sizeof(message),
             "%s: read_argument_text status=%d (expected 0)", label,
             (int)status);
    check(status == RUNTIMEC_STATUS_SUCCESS, message);
    snprintf(message, sizeof(message),
             "%s: length=%zu (expected %zu)", label, buffer.length, body_size);
    check(buffer.length == body_size, message);
    snprintf(message, sizeof(message),
             "%s: bytes differ from the stored text", label);
    check(buffer.length == body_size &&
              memcmp(output, body, body_size) == 0,
          message);
    if (buffer.length == body_size && memcmp(output, body, body_size) != 0) {
        printf("        read  : ");
        for (size_t at = 0U; at < buffer.length && at < 32U; ++at) {
            printf("%02x ", (unsigned char)output[at]);
        }
        printf("\n        stored: ");
        for (size_t at = 0U; at < body_size && at < 32U; ++at) {
            printf("%02x ", body[at]);
        }
        printf("\n");
    }

    fixture_release(&fixture);
    free(wire);
}

static void check_integer_round_trip(void) {
    static const uint8_t body[] = {0x08, 0x07, 0x06, 0x05,
                                   0x04, 0x03, 0x02, 0x01};
    const int64_t expected = INT64_C(0x0102030405060708);
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    fixture_t fixture;
    kvlang_runtime_error_t error;
    char message[128];
    int64_t value = 0;

    if (kvlangXvalueHeadNewSlack(body, sizeof(body), "int64", &wire,
                                 &wire_size) != 0 ||
        wire == NULL) {
        printf("[FAIL] integer: cannot encode the wire\n");
        ++g_failures;
        return;
    }
    if (!fixture_build(&fixture, wire, (size_t)wire_size, "int64")) {
        printf("[FAIL] integer: cannot build the fixture\n");
        ++g_failures;
        free(wire);
        return;
    }
    memset(&error, 0, sizeof(error));
    memset(message, 0, sizeof(message));
    error.message.data = message;
    error.message.capacity = sizeof(message);

    const runtimec_status_t status = kvlang_runtime_read_argument_integer(
        &fixture.frame, &fixture.instruction, 0U, &value, &error);
    snprintf(message, sizeof(message),
             "integer: status=%d (expected 0)", (int)status);
    check(status == RUNTIMEC_STATUS_SUCCESS, message);
    snprintf(message, sizeof(message),
             "integer: value=%lld (expected %lld)",
             (long long)value, (long long)expected);
    check(value == expected, message);

    fixture_release(&fixture);
    free(wire);
}

int main(void) {
    
    check_round_trip((const uint8_t *)"hello", 5U, "ascii text");
    
    {
        static const uint8_t leading_nul[] = {0x00, 'a', 'b', 0x00, 'c'};
        check_round_trip(leading_nul, sizeof(leading_nul), "leading NUL");
    }
    
    {
        static const uint8_t chinese[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
        check_round_trip(chinese, sizeof(chinese), "non-ASCII text");
    }
    
    check_round_trip((const uint8_t *)"", 0U, "empty text");
    
    check_integer_round_trip();

    if (g_failures == 0) {
        printf("argument_text_wire_test: OK\n");
        return 0;
    }
    printf("argument_text_wire_test: %d failure(s)\n", g_failures);
    return 1;
}
