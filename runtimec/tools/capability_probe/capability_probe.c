
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "contract/kvspace_interface.h"
#include "contract/kvspace_access.h"
#include "engine/execution_window.h"
#include "engine/session.h"

#define CAPABILITY_PROBE_TEMPLATE_CAPACITY 512U
#define CAPABILITY_PROBE_CAPTURE_CAPACITY 512U

typedef int (*layout_code_function_t)(const char *, const char *, char *,
                                      uint32_t, char *, uint32_t);

static void *probe_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void probe_deallocate(void *context, void *allocation, size_t size,
                             size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static const char *probe_exit_kind_name(uint16_t kind) {
    switch (kind) {
    case BINRWIR_EXECUTION_EXIT_NONE:
        return "NONE";
    case BINRWIR_EXECUTION_EXIT_DONE:
        return "DONE";
    case BINRWIR_EXECUTION_EXIT_CALL:
        return "CALL";
    case BINRWIR_EXECUTION_EXIT_BUILTIN:
        return "BUILTIN";
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return "HANDOFF";
    case BINRWIR_EXECUTION_EXIT_YIELD:
        return "YIELD";
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static bool probe_read_file(const char *path, char *buffer, size_t capacity) {
    FILE *file = fopen(path, "r");
    size_t size;

    if (file == NULL) {
        return false;
    }
    size = fread(buffer, 1U, capacity - 1U, file);
    fclose(file);
    buffer[size] = '\0';
    return true;
}

static void probe_first_line(const char *text, char *output, size_t capacity) {
    size_t length = 0U;

    while (text[length] != '\0' && text[length] != '\n' &&
           length + 1U < capacity) {
        output[length] = text[length];
        ++length;
    }
    output[length] = '\0';
}

int main(int argument_count, char **arguments) {
    const char *dsn = NULL;
    const char *capability = NULL;
    const char *source_path = NULL;
    
    const char *kvspace_library = NULL;
    const char *layout_library = NULL;
    char template_buffer[CAPABILITY_PROBE_TEMPLATE_CAPACITY];
    char source_buffer[CAPABILITY_PROBE_TEMPLATE_CAPACITY];
    char entry[128];
    char layout_error[512];
    char capture_path[256];
    char captured[CAPABILITY_PROBE_CAPTURE_CAPACITY];
    size_t captured_size = 0U;
    int ordinal = 1;

    while (ordinal < argument_count) {
        if (strcmp(arguments[ordinal], "--kvspace") == 0 &&
            ordinal + 1 < argument_count) {
            kvspace_library = arguments[ordinal + 1];
            ordinal += 2;
        } else if (strcmp(arguments[ordinal], "--layout") == 0 &&
                   ordinal + 1 < argument_count) {
            layout_library = arguments[ordinal + 1];
            ordinal += 2;
        } else if (dsn == NULL) {
            dsn = arguments[ordinal++];
        } else if (capability == NULL) {
            capability = arguments[ordinal++];
        } else {
            source_path = arguments[ordinal++];
        }
    }
    if (dsn == NULL || capability == NULL) {
        fprintf(stderr,
                "usage: capability_probe <dsn> <capability> [source.kv] "
                "--kvspace <lib> --layout <lib>\n");
        return 2;
    }
    if (kvspace_library == NULL || layout_library == NULL) {
        fprintf(stderr, "missing library path: --kvspace and --layout are required\n");
        return 2;
    }

    if (source_path != NULL) {
        if (!probe_read_file(source_path, source_buffer, sizeof(source_buffer))) {
            fprintf(stderr, "cannot read %s\n", source_path);
            return 2;
        }
    } else {
        static const struct {
            const char *capability;
            const char *body;
        } templates[] = {
            
            {"add", "1 + 2 -> x"},
            {"sub", "5 - 2 -> x"},
            {"mul", "3 × 4 -> x"},
            {"div", "8 ÷ 2 -> x"},
            {"eq", "1 == 1 -> x"},
            {"copy", "1 -> x"},
            {"return", "return"},
        };
        const char *body = NULL;
        for (size_t ordinal = 0;
             ordinal < sizeof(templates) / sizeof(templates[0]); ++ordinal) {
            if (strcmp(templates[ordinal].capability, capability) == 0) {
                body = templates[ordinal].body;
                break;
            }
        }
        if (body == NULL) {
            fprintf(stderr,
                    "no built-in template for '%s'; pass a source file\n",
                    capability);
            printf("%s\tNO_TEMPLATE\t0\tneed source file\n", capability);
            return 0;
        }
        
        snprintf(template_buffer, sizeof(template_buffer),
                 "rwfunc probe() -> () {\n\tx:int64 = 0\n\t%s\n}\n", body);
    }
    const char *source =
        source_path != NULL ? source_buffer : template_buffer;

    (void)kvspace_library;
    void *layout_handle_library = dlopen(layout_library, RTLD_NOW | RTLD_LOCAL);
    layout_code_function_t layout_code_function = NULL;

    if (layout_handle_library == NULL) {
        fprintf(stderr, "cannot load layout library\n");
        return 2;
    }
    {
        void *symbol = dlsym(layout_handle_library, "kvlangLayoutCode");
        memcpy(&layout_code_function, &symbol, sizeof(layout_code_function));
    }
    if (layout_code_function == NULL) {
        fprintf(stderr, "layout library lacks kvlangLayoutCode\n");
        return 2;
    }
    runtimec_error_t error;
    runtimec_error_reset(&error);

    entry[0] = '\0';
    layout_error[0] = '\0';
    if (layout_code_function(source, dsn, entry, (uint32_t)sizeof(entry),
                             layout_error, (uint32_t)sizeof(layout_error)) !=
        0) {
        
        printf("%s\tLAYOUT_FAILED\t0\t%s\n", capability, layout_error);
        return 0;
    }

    void *kvspace_handle = kvspaceConnect(dsn);
    if (kvspace_handle == NULL) {
        fprintf(stderr, "cannot connect %s\n", dsn);
        return 2;
    }
    const runtimec_kvspace_access_request_t adapter_request = {
        .allocator = {.context = NULL,
                      .allocate = probe_allocate,
                      .deallocate = probe_deallocate},
        .borrowed_kvspace_handle = kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_kvspace_access_t *kvspace_access = NULL;
    if (runtimec_kvspace_access_create(&adapter_request, &kvspace_access, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "adapter failed\n");
        return 2;
    }
    
    {
        runtimec_snapshot_t probe_snapshot;
        runtimec_snapshot_reset(&probe_snapshot);
        probe_snapshot.contract_version = runtimec_contract_version();
        probe_snapshot.function_key =
            runtimec_key_view_from_c_string("/lib/probe");
        runtimec_error_t probe_error;
        runtimec_error_reset(&probe_error);
        const runtimec_status_t probe_status =
            runtimec_kvspace_access_source_revision(
                kvspace_access, probe_snapshot.function_key,
            &probe_snapshot.source_revision, &probe_error);
        fprintf(stderr,
                "probe: source_revision status=%d gen=%llu instance=%llu "
                "code=%lld msg='%.*s'\n",
                (int)probe_status,
                (unsigned long long)probe_snapshot.source_revision.generation,
                (unsigned long long)
                    probe_snapshot.source_revision.instance_identifier,
                (long long)probe_error.code, (int)probe_error.message.size,
                probe_error.message.data != NULL ? probe_error.message.data
                                                 : "");
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = adapter_request.allocator;
    options.compilation_cache_capacity = 4U;
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    if (runtimec_session_create(kvspace_access, &options, &session, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "session failed: %.*s\n", (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        return 2;
    }

    snprintf(capture_path, sizeof(capture_path), "/tmp/capability-probe-%d.txt",
             (int)getpid());
    fflush(stdout);
    const int saved_stdout = dup(fileno(stdout));
    FILE *capture = fopen(capture_path, "w");
    if (capture == NULL) {
        fprintf(stderr, "cannot capture stdout\n");
        return 2;
    }
    dup2(fileno(capture), fileno(stdout));

    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string("/lib/probe");
    
    request.vthread_identifier = runtimec_string_view_from_c_string("2");
    request.frame_path = runtimec_string_view_from_c_string("/vthread/2/[1]");
    request.stack_identifier = 1;
    request.source_logical_program_counter = 1U;
    
    {
        runtimec_kvspace_access_t *session_host = NULL;
        if (runtimec_session_get_kvspace_access(session, &session_host) ==
                RUNTIMEC_STATUS_SUCCESS && session_host != NULL) {
            runtimec_snapshot_t before_snapshot;
            runtimec_snapshot_reset(&before_snapshot);
            before_snapshot.contract_version = runtimec_contract_version();
            before_snapshot.function_key =
                runtimec_key_view_from_c_string("/lib/probe");
            runtimec_error_t before_error;
            runtimec_error_reset(&before_error);
            const runtimec_status_t before_status =
                runtimec_kvspace_access_source_revision(
                    session_host, before_snapshot.function_key,
                &before_snapshot.source_revision, &before_error);
            fprintf(stderr,
                    "probe: before-execute source_revision status=%d gen=%llu "
                    "instance=%llu code=%lld msg='%.*s'\n",
                    (int)before_status,
                    (unsigned long long)before_snapshot.source_revision.generation,
                    (unsigned long long)
                        before_snapshot.source_revision.instance_identifier,
                    (long long)before_error.code,
                    (int)before_error.message.size,
                    before_error.message.data != NULL
                        ? before_error.message.data : "");
        }
    }
    fprintf(stderr,
            "probe: path='%.*s'(%zu) vtid='%.*s'(%zu) frame='%.*s'(%zu) "
            "args=%zu/%p stack=%d\n",
            (int)request.function_path.size, request.function_path.data,
            request.function_path.size,
            (int)request.vthread_identifier.size,
            request.vthread_identifier.data,
            request.vthread_identifier.size,
            (int)request.frame_path.size, request.frame_path.data,
            request.frame_path.size, request.argument_count,
            (const void *)request.arguments, (int)request.stack_identifier);
    runtimec_execution_window_result_t result;
    const runtimec_status_t execute_status = runtimec_execution_window_execute(
        session, &request, &result, &error);
    if (execute_status != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr,
                "probe: execute_status=%d result.status=%d detail=%u "
                "exit_kind=%u message=%.*s\n",
                (int)execute_status, (int)result.status,
                (unsigned)result.execution_exit.detail_code,
                (unsigned)result.execution_exit.kind,
                (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
    }

    fflush(stdout);
    dup2(saved_stdout, fileno(stdout));
    fclose(capture);
    close(saved_stdout);

    capture = fopen(capture_path, "r");
    if (capture != NULL) {
        captured_size = fread(captured, 1U, sizeof(captured) - 1U, capture);
        fclose(capture);
    }
    captured[captured_size] = '\0';
    (void)remove(capture_path);

    char first_line[CAPABILITY_PROBE_CAPTURE_CAPACITY];
    probe_first_line(captured, first_line, sizeof(first_line));
    
    const bool preparation_failed =
        result.execution_exit.kind == BINRWIR_EXECUTION_EXIT_ERROR &&
        result.execution_exit.logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    if (preparation_failed) {
        printf("%s\tSTAGE_FAILED\t%u\t%s\n", capability,
               (unsigned)result.execution_exit.detail_code,
               first_line[0] != '\0' ? first_line
                                     : "failed before any instruction");
    } else {
        printf("%s\t%u(%s)\t%u\t%s\n", capability,
               (unsigned)result.execution_exit.kind,
               probe_exit_kind_name(result.execution_exit.kind),
               (unsigned)result.execution_exit.detail_code, first_line);
    }

    runtimec_session_destroy(session);
    runtimec_kvspace_access_destroy(kvspace_access, &error);
    kvspaceClose(kvspace_handle);
    return 0;
}
