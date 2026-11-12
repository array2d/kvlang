
#define _POSIX_C_SOURCE 200809L

#include "contract/kvspace_interface.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>

#include "tools/driver_output.h"

#include "vmbin/directory_walk.h"

#include "contract/checkpoint.h"
#include "contract/kvspace_access.h"
#include "engine/execution_window.h"
#include "engine/frame_driver.h"
#include "engine/session.h"
#include "vmbin/vthread_status.h"

#define KVLANG_RUN_ENTRY_NAME "test"
#define KVLANG_RUN_ENTRY_PATH "/lib/" KVLANG_RUN_ENTRY_NAME

#define KVLANG_RUN_ROOT_STACK_IDENTIFIER 1
#define KVLANG_RUN_ROOT_SEGMENT "1"
#define KVLANG_RUN_ROOT_SOURCE_PC 1

static const char kvlangrun_sequence_key[] = "/vthread/\xE2\x80\xA5" "seq";

typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry_out, uint32_t entry_capacity,
                                      char *error_out, uint32_t error_capacity);

typedef struct kvlangrun_environment {
    void *kvspace_handle;
    runtimec_kvspace_access_t *kvspace_access;
} kvlangrun_environment_t;

static void *program_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void program_deallocate(void *context, void *allocation, size_t size,
                               size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static int usage_failure(const char *message) {
    fprintf(stderr, "kvlangrun: %s\n", message);
    return 2;
}

static const char *g_runner_operators_argument = NULL;

static const char *g_layout_library_argument = NULL;

static void *open_layout_library(char *output_path, size_t output_capacity) {
    const char *explicit_path = g_layout_library_argument;

    if (explicit_path != NULL && explicit_path[0] != '\0') {
        snprintf(output_path, output_capacity, "%s", explicit_path);
        return dlopen(explicit_path, RTLD_NOW | RTLD_LOCAL);
    }
    char binary_path[4096];
    const ssize_t length = readlink("/proc/self/exe", binary_path,
                                    sizeof(binary_path) - 1U);

    if (length > 0) {
        binary_path[length] = '\0';
        char *slash = strrchr(binary_path, '/');

        if (slash != NULL) {
            *slash = '\0';
            const char *const names[] = {"libkvlanglayout.so",
                                         "libkvlanglayout.dylib"};
            for (size_t ordinal = 0U;
                 ordinal < sizeof(names) / sizeof(names[0]); ++ordinal) {
                
                const int written = snprintf(output_path, output_capacity,
                                             "%s/../lib/%s", binary_path,
                                             names[ordinal]);

                if (written < 0 || (size_t)written >= output_capacity) {
                    continue;
                }
                void *library = dlopen(output_path, RTLD_NOW | RTLD_LOCAL);

                if (library != NULL) {
                    return library;
                }
            }
        }
    }
    snprintf(output_path, output_capacity,
             "libkvlanglayout.so (pass --layout-lib=<path>)");
    return NULL;
}

static bool layout_capabilities_and_file(const char *dsn,
                                         const char *file_path) {
    char layout_path[4096];
    void *layout_library = open_layout_library(layout_path, sizeof(layout_path));
    if (layout_library == NULL) {
        fprintf(stderr, "kvlangrun: cannot load layout library: %s\n",
                dlerror() != NULL ? dlerror() : layout_path);
        return false;
    }
    layout_file_function_t layout_file = (layout_file_function_t)(uintptr_t)
        dlsym(layout_library, "kvlangLayoutFile");
    if (layout_file == NULL) {
        fprintf(stderr, "kvlangrun: layout library has no kvlangLayoutFile\n");
        return false;
    }

    char declarations_path[256];
    snprintf(declarations_path, sizeof(declarations_path),
             "/tmp/kvlangrun-capabilities-%d.kv", (int)getpid());
    
    char declarations[512];
    declarations[0] = '\0';
    if (g_runner_operators_argument != NULL) {
        const char *cursor = g_runner_operators_argument;
        while (*cursor != '\0') {
            const char *comma = strchr(cursor, ',');
            const size_t size =
                comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
            const size_t used = strlen(declarations);
            if (size != 0U && used < sizeof(declarations) &&
                snprintf(declarations + used, sizeof(declarations) - used,
                         "rwir %.*s(*[]char/utf32 #s...)\n", (int)size,
                         cursor) <= 0) {
                fprintf(stderr, "kvlangrun: cannot format capability declarations\n");
                return false;
            }
            if (comma == NULL) {
                break;
            }
            cursor = comma + 1;
        }
    }
    FILE *declaration_file = fopen(declarations_path, "wb");
    if (declaration_file == NULL) {
        fprintf(stderr, "kvlangrun: cannot write capability declarations\n");
        return false;
    }
    const size_t declaration_size = strlen(declarations);
    const bool written = fwrite(declarations, 1U, declaration_size,
                                declaration_file) == declaration_size;
    fclose(declaration_file);
    if (!written) {
        (void)remove(declarations_path);
        fprintf(stderr, "kvlangrun: cannot write capability declarations\n");
        return false;
    }
    char entry[256];
    char layout_error[2048];
    memset(entry, 0, sizeof(entry));
    memset(layout_error, 0, sizeof(layout_error));
    const int declaration_result =
        layout_file(declarations_path, dsn, entry, sizeof(entry),
                    layout_error, sizeof(layout_error));
    (void)remove(declarations_path);
    if (declaration_result != 0) {
        fprintf(stderr, "kvlangrun: cannot declare external capabilities: %s\n",
                layout_error[0] != '\0' ? layout_error : "(no message)");
        return false;
    }

    memset(entry, 0, sizeof(entry));
    memset(layout_error, 0, sizeof(layout_error));
    if (layout_file(file_path, dsn, entry, sizeof(entry), layout_error,
                    sizeof(layout_error)) != 0) {
        fprintf(stderr, "kvlangrun: layout failed: %s\n",
                layout_error[0] != '\0' ? layout_error : "(no message)");
        return false;
    }
    return true;
}

static bool environment_open(kvlangrun_environment_t *environment,
                             const char *dsn) {
    memset(environment, 0, sizeof(*environment));
    environment->kvspace_handle = kvspaceConnect(dsn);
    if (environment->kvspace_handle == NULL) {
        fprintf(stderr, "kvlangrun: cannot connect %s\n", dsn);
        return false;
    }
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = program_allocate,
        .deallocate = program_deallocate,
    };
    const runtimec_kvspace_access_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = environment->kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_create(&adapter_request,
                                       &environment->kvspace_access, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: kvspace adapter create failed: %s\n",
                error.message.data != NULL ? error.message.data
                                           : "(no message)");
        kvspaceClose(environment->kvspace_handle);
        environment->kvspace_handle = NULL;
        return false;
    }
    return true;
}

static void environment_close(kvlangrun_environment_t *environment) {
    if (environment->kvspace_access != NULL) {
        runtimec_error_t error;
        (void)runtimec_kvspace_access_destroy(environment->kvspace_access,
                                              &error);
        environment->kvspace_access = NULL;
    }
    if (environment->kvspace_handle != NULL) {
        kvspaceClose(environment->kvspace_handle);
        environment->kvspace_handle = NULL;
    }
}

static bool allocate_vthread_identifier(void *kvspace_handle, char *output,
                                        size_t output_capacity) {
    const uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    long long sequence = 0;
    if (runtimec_kvspace_read_wire(kvspace_handle, kvlangrun_sequence_key,
                                   &wire, &wire_size) &&
        wire != NULL && wire_size >= KVSPACE_XVALUE_HEAD_LEN) {
        kvspaceXvalueHead head;
        memcpy(&head, wire, sizeof(head));
        const uint64_t body_size = kvspaceXvalueHeadLen(&head);
        if (body_size != 0U && body_size < 32U &&
            KVSPACE_XVALUE_HEAD_LEN + body_size <= wire_size) {
            char text[32];
            memcpy(text, wire + KVSPACE_XVALUE_HEAD_LEN, (size_t)body_size);
            text[body_size] = '\0';
            sequence = strtoll(text, NULL, 10);
        }
    }
    sequence += 1;
    char text[32];
    const int text_length = snprintf(text, sizeof(text), "%lld", sequence);
    if (text_length <= 0 || (size_t)text_length >= sizeof(text)) {
        return false;
    }
    uint8_t *new_wire = NULL;
    uint64_t new_wire_size = 0U;
    if (kvlangXvalueHeadNewSlack((const uint8_t *)text,
                                  (uint64_t)text_length, "[]char/utf8",
                                  &new_wire, &new_wire_size) != 0 ||
        new_wire == NULL || new_wire_size > UINT32_MAX) {
        free(new_wire);
        return false;
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        kvspace_handle, kvlangrun_sequence_key, new_wire,
        (uint32_t)new_wire_size, error_text, (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(new_wire);
    if (set_status != 0) {
        fprintf(stderr, "kvlangrun: cannot allocate a vthread identifier: %s\n",
                error_text[0] != '\0' ? error_text : "(no message)");
        return false;
    }
    snprintf(output, output_capacity, "%s", text);
    return true;
}

static int command_create(const char *file_path) {
    const char *dsn = getenv("KVSPACE");
    if (dsn == NULL || dsn[0] == '\0') {
        return usage_failure(
            "KVSPACE is not set (it must name the data source to use)");
    }
    if (!layout_capabilities_and_file(dsn, file_path)) {
        return 2;
    }
    kvlangrun_environment_t environment;
    if (!environment_open(&environment, dsn)) {
        return 2;
    }
    int result = 2;
    char vthread_identifier[32];
    if (!allocate_vthread_identifier(environment.kvspace_handle,
                                     vthread_identifier,
                                     sizeof(vthread_identifier))) {
        goto done;
    }
    char frame_path[128];
    const int frame_length = snprintf(frame_path, sizeof(frame_path),
                                      "/vthread/%s/[%s]", vthread_identifier,
                                      KVLANG_RUN_ROOT_SEGMENT);
    if (frame_length <= 0 || (size_t)frame_length >= sizeof(frame_path)) {
        fprintf(stderr, "kvlangrun: root frame path is too long\n");
        goto done;
    }
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_ensure_frame(
            environment.kvspace_access, NULL,
            runtimec_key_view_make(frame_path, strlen(frame_path)),
            runtimec_string_view_from_c_string(KVLANG_RUN_ENTRY_PATH),
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: cannot establish the root frame: %s\n",
                error.message.data != NULL ? error.message.data
                                           : "(no message)");
        goto done;
    }
    runtimec_checkpoint_t checkpoint;
    runtimec_checkpoint_reset(&checkpoint);
    checkpoint.vthread_identifier =
        runtimec_string_view_from_c_string(vthread_identifier);
    checkpoint.frame_identifier =
        runtimec_string_view_from_c_string(frame_path);
    checkpoint.logical_program_counter = (runtimec_program_counter_t){
        KVLANG_RUN_ROOT_STACK_IDENTIFIER, KVLANG_RUN_ROOT_SOURCE_PC};
    checkpoint.persistent_status =
        runtimec_string_view_from_c_string("init");
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_checkpoint(environment.kvspace_access,
                                           &checkpoint, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: cannot persist the new vthread: %s\n",
                error.message.data != NULL ? error.message.data
                                           : "(no message)");
        goto done;
    }
    printf("%s\n", vthread_identifier);
    result = 0;
done:
    environment_close(&environment);
    return result;
}

static int vthread_status_executable(const char *vthread_identifier,
                                     void *kvspace_handle) {
    char status_key[128];
    const int written = snprintf(status_key, sizeof(status_key),
                                 "/vthread/%s/\xE2\x80\xA5" "status",
                                 vthread_identifier);
    if (written <= 0 || (size_t)written >= sizeof(status_key)) {
        return -1;
    }
    runtimec_string_view_t text;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_vthread_status_read(kvspace_handle, status_key, &text,
                                     &error) != RUNTIMEC_STATUS_SUCCESS) {
        return 1;
    }
    if ((text.size == 4U && memcmp(text.data, "init", 4U) == 0) ||
        (text.size == 7U && memcmp(text.data, "running", 7U) == 0) ||
        (text.size == 4U && memcmp(text.data, "wait", 4U) == 0)) {
        return 1;
    }
    if (text.size == 6U && memcmp(text.data, "paused", 6U) == 0) {
        fprintf(stderr, "kvlangrun: vthread %s is paused\n",
                vthread_identifier);
        return 0;
    }
    if (text.size == 5U && memcmp(text.data, "error", 5U) == 0) {
        fprintf(stderr, "kvlangrun: vthread %s is in error\n",
                vthread_identifier);
        return -1;
    }
    fprintf(stderr, "kvlangrun: vthread %s is already %.*s\n",
            vthread_identifier, (int)text.size, text.data);
    return 0;
}

static bool locate_frame_path(void *kvspace_handle,
                              const char *vthread_identifier,
                              int32_t stack_identifier, char *frame_path,
                              size_t frame_capacity, char *segment,
                              size_t segment_capacity) {
    if (stack_identifier == KVLANG_RUN_ROOT_STACK_IDENTIFIER) {
        const int length = snprintf(frame_path, frame_capacity,
                                    "/vthread/%s/[%s]", vthread_identifier,
                                    KVLANG_RUN_ROOT_SEGMENT);
        if (length <= 0 || (size_t)length >= frame_capacity) {
            return false;
        }
        snprintf(segment, segment_capacity, "%s", KVLANG_RUN_ROOT_SEGMENT);
        return true;
    }
    char prefix[128];
    const int prefix_length = snprintf(prefix, sizeof(prefix),
                                       "/vthread/%s/", vthread_identifier);
    if (prefix_length <= 0 || (size_t)prefix_length >= sizeof(prefix)) {
        return false;
    }
    int64_t child_count = 0;
    runtimec_directory_walk_t walk;
    if (!runtimec_directory_walk_open(&walk, NULL, kvspace_handle, prefix,
                                      true,
                                      false) ||
        !runtimec_directory_walk_count(&walk, &child_count)) {
        (void)runtimec_directory_walk_close(&walk);
        return false;
    }
    char found[128];
    size_t found_length = 0U;
    size_t matches = 0U;
    for (int64_t index = 0; index < child_count; ++index) {
        const char *entry_name = NULL;
        uint32_t entry_length = 0U;
        if (!runtimec_directory_walk_at(&walk, index, &entry_name,
                                        &entry_length) ||
            entry_name == NULL || entry_length == 0U ||
            entry_length >= sizeof(found) || entry_name[0] != '[') {
            continue;
        }
        char name[128];
        memcpy(name, entry_name, entry_length);
        const uint32_t name_length = entry_length;
        size_t cursor = 1U;
        int32_t depth = 0;
        bool has_digits = false;
        while (cursor < name_length && name[cursor] >= '0' &&
               name[cursor] <= '9') {
            depth = depth * 10 + (name[cursor] - '0');
            cursor += 1U;
            has_digits = true;
        }
        if (!has_digits || cursor >= name_length ||
            (name[cursor] != ']' && name[cursor] != '.') ||
            depth != stack_identifier) {
            continue;
        }
        matches += 1U;
        if (matches == 1U) {
            memcpy(found, name, name_length);
            found_length = name_length;
        }
    }
    (void)runtimec_directory_walk_close(&walk);
    if (matches != 1U) {
        fprintf(stderr,
                "kvlangrun: cannot locate the frame for stack %d "
                "(%zu candidates)\n",
                (int)stack_identifier, matches);
        return false;
    }
    const int path_length = snprintf(frame_path, frame_capacity,
                                     "/vthread/%s/%.*s", vthread_identifier,
                                     (int)found_length, found);
    if (path_length <= 0 || (size_t)path_length >= frame_capacity ||
        found_length < 3U) {
        return false;
    }
    snprintf(segment, segment_capacity, "%.*s", (int)(found_length - 2U),
             found + 1);
    return true;
}

static int command_run(const char *vthread_identifier) {
    const char *dsn = getenv("KVSPACE");
    if (dsn == NULL || dsn[0] == '\0') {
        return usage_failure(
            "KVSPACE is not set (it must name the data source to use)");
    }
    kvlangrun_environment_t environment;
    if (!environment_open(&environment, dsn)) {
        return 2;
    }
    int result = 1;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    runtimec_program_counter_t counter;
    bool counter_present = false;
    if (runtimec_kvspace_access_read_program_counter(
            environment.kvspace_access,
            runtimec_string_view_from_c_string(vthread_identifier), &counter,
            &counter_present, &error) != RUNTIMEC_STATUS_SUCCESS ||
        !counter_present) {
        fprintf(stderr, "kvlangrun: vthread %s has no ‥pc\n",
                vthread_identifier);
        goto done;
    }
    const int executable = vthread_status_executable(
        vthread_identifier, environment.kvspace_handle);
    if (executable <= 0) {
        result = executable == 0 ? 0 : 1;
        goto done;
    }
    if (counter.stack_identifier < KVLANG_RUN_ROOT_STACK_IDENTIFIER ||
        counter.source_instruction_ordinal < 0) {
        
        fprintf(stderr, "kvlangrun: vthread %s has an unusable ‥pc [%d,%d]\n",
                vthread_identifier, (int)counter.stack_identifier,
                (int)counter.source_instruction_ordinal);
        goto done;
    }
    char frame_path[256];
    char segment[128];
    if (!locate_frame_path(environment.kvspace_handle, vthread_identifier,
                           counter.stack_identifier, frame_path,
                           sizeof(frame_path), segment, sizeof(segment))) {
        goto done;
    }
    char function_path[256];
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_read_frame_library(
            environment.kvspace_access,
            runtimec_key_view_make(frame_path, strlen(frame_path)),
            function_path, sizeof(function_path), &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: frame %s has no ‥lib: %s\n", frame_path,
                error.message.data != NULL ? error.message.data
                                           : "(no message)");
        goto done;
    }

    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = program_allocate,
        .deallocate = program_deallocate,
    };
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = (uint32_t)sizeof(options);
    options.allocator = allocator;
    options.kvspace_handle = environment.kvspace_handle;
    options.compilation_cache_capacity = 64U;
    
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    
    {
        size_t driver_output_entry_count = 0U;
        const binrwir_registry_entry_t *driver_entries =
            runtimec_driver_output_entries(&driver_output_entry_count);
        static runtimec_extension_entry_view_t entry_views[3];
        static runtimec_extension_module_view_t output_module;
        if (driver_output_entry_count <= 3U) {
            for (size_t at = 0U; at < driver_output_entry_count; ++at) {
                entry_views[at].structure_size =
                    (uint32_t)sizeof(runtimec_extension_entry_view_t);
                entry_views[at].operation_identifier =
                    driver_entries[at].operation_identifier;
                entry_views[at].flags = driver_entries[at].flags;
                entry_views[at].handler = driver_entries[at].handler;
            }
            output_module.module_name = "driver_output";
            output_module.entries = entry_views;
            output_module.entry_count = driver_output_entry_count;
            options.extension_modules = &output_module;
            options.extension_count = 1U;
        }
    }
    runtimec_session_t *session = NULL;
    runtimec_error_reset(&error);
    if (runtimec_session_create(environment.kvspace_access, &options, &session,
                                &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: session create failed: %s\n",
                error.message.data != NULL ? error.message.data
                                           : "(no message)");
        goto done;
    }
    frame_driver_t driver;
    if (frame_driver_initialize(&driver, session) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: frame driver initialize failed\n");
        (void)runtimec_session_destroy(session);
        goto done;
    }

    binrwir_execution_exit_t exit_state;
    memset(&exit_state, 0, sizeof(exit_state));
    char driver_message[512];
    driver_message[0] = '\0';
    const frame_driver_outcome_t outcome = frame_driver_execute(
        &driver, function_path,
        (uint32_t)counter.source_instruction_ordinal,
        counter.stack_identifier, segment, vthread_identifier, NULL, 0U,
        &exit_state, driver_message, sizeof(driver_message));
    fflush(stdout);

    switch (outcome) {
    case FRAME_DRIVER_OUTCOME_DONE:
        if (counter.stack_identifier != KVLANG_RUN_ROOT_STACK_IDENTIFIER) {
            
            fprintf(stderr,
                    "kvlangrun: frame %s finished but ‥returnpc is not "
                    "persisted; cannot continue the caller\n",
                    frame_path);
            result = 1;
        } else {
            result = 0;
        }
        break;
    case FRAME_DRIVER_OUTCOME_EXECUTE_FAILED:
        fprintf(stderr,
                "kvlangrun: execute failed status=%d（exit.kind=%u status=%d "
                "detail=%u pc=%u）%s%s\n",
                (int)exit_state.status, (unsigned)exit_state.kind,
                (int)exit_state.status, (unsigned)exit_state.detail_code,
                (unsigned)exit_state.logical_program_counter,
                driver_message[0] != '\0' ? " " : "", driver_message);
        result = 1;
        break;
    case FRAME_DRIVER_OUTCOME_HANDOFF:
        fprintf(stderr,
                "kvlangrun: execution stopped at HANDOFF before DONE "
                "(pc=%u, detail=%u)\n",
                (unsigned)exit_state.logical_program_counter,
                (unsigned)exit_state.detail_code);
        result = 1;
        break;
    case FRAME_DRIVER_OUTCOME_ERROR:
        fprintf(stderr,
                "kvlangrun: execution failed at pc=%u detail=%u（调用返回链或"
                "帧构造出错）\n",
                (unsigned)exit_state.logical_program_counter,
                (unsigned)exit_state.detail_code);
        result = 1;
        break;
    case FRAME_DRIVER_OUTCOME_YIELD:
        
        fprintf(stderr, "kvlangrun: vthread %s paused at pc=%u\n",
                vthread_identifier,
                (unsigned)exit_state.logical_program_counter);
        result = 0;
        break;
    case FRAME_DRIVER_OUTCOME_INVALID:
    default:
        fprintf(stderr, "kvlangrun: execution outcome is invalid\n");
        result = 1;
        break;
    }
    (void)runtimec_session_destroy(session);
done:
    environment_close(&environment);
    return result;
}

int main(int argument_count, char **arguments) {
    if (argument_count < 3) {
        fprintf(stderr,
                "usage: kvlangrun create <file.kv> [--no-stdlib] --operators=a,b "
                "[--layout-lib=<path>]\n"
                "       kvlangrun run <vid> --operators=a,b [--layout-lib=<path>]\n");
        return 2;
    }
    
    char *filtered[64];
    int filtered_count = 0;
    for (int at = 0; at < argument_count && filtered_count < 64; ++at) {
        if (at > 0 && strncmp(arguments[at], "--layout-lib=", 13) == 0) {
            g_layout_library_argument = arguments[at] + 13;
            continue;
        }
        if (at > 0 && strncmp(arguments[at], "--operators=", 12) == 0) {
            g_runner_operators_argument = arguments[at] + 12;
            continue;
        }
        filtered[filtered_count] = arguments[at];
        ++filtered_count;
    }
    argument_count = filtered_count;
    arguments = filtered;
    const char *command = arguments[1];
    if (strcmp(command, "create") == 0) {
        const char *file_path = NULL;
        for (int index = 2; index < argument_count; ++index) {
            if (strcmp(arguments[index], "--no-stdlib") == 0) {
                
                continue;
            }
            if (file_path != NULL) {
                return usage_failure(
                    "unexpected extra argument (usage: kvlangrun create "
                    "<file.kv> [--no-stdlib])");
            }
            file_path = arguments[index];
        }
        if (file_path == NULL) {
            return usage_failure(
                "missing <file.kv> (usage: kvlangrun create <file.kv> "
                "[--no-stdlib])");
        }
        return command_create(file_path);
    }
    if (strcmp(command, "run") == 0) {
        if (argument_count != 3) {
            return usage_failure(
                "usage: kvlangrun run <vid> (exactly one identifier)");
        }
        return command_run(arguments[2]);
    }
    return usage_failure(
        "unknown command (usage: kvlangrun create <file.kv> | run <vid>)");
}
