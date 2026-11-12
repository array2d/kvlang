
#define _POSIX_C_SOURCE 200809L

#include "contract/kvspace_interface.h"
#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/binfunc.h"
#include "binrwir_compiler/compiler.h"
#include "binrwir/specialize.h"
#include "engine/execution_window.h"
#include "tools/driver_output.h"
#include "engine/session.h"
#include "engine/frame_driver.h"

#include "contract/kvspace_access.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SKIP_RETURN_CODE 77

#define CASE_PATH_CAPACITY 1024

typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry, size_t entry_capacity,
                                      char *error, size_t error_capacity);

typedef enum case_outcome {
    CASE_OUTCOME_UNRECORDED = 0, 
    CASE_OUTCOME_EXECUTED,       
    CASE_OUTCOME_BLOCKED,        
    CASE_OUTCOME_EXPECTED_FAILED,
    CASE_OUTCOME_ENCODE_FAILED,
    CASE_OUTCOME_WINDOW_FAILED,
    CASE_OUTCOME_EXECUTION_FAILED,
    CASE_OUTCOME_EXPECTATION_WRONG,
    CASE_OUTCOME_INCOMPLETE,
    CASE_OUTCOME_UNOBSERVED,
    CASE_OUTCOME_MISSING_EXPECTATION,
    
    CASE_OUTCOME_NOT_A_CASE,
    CASE_OUTCOME_COUNT
} case_outcome_t;

static const char *case_outcome_name(case_outcome_t outcome) {
    switch (outcome) {
    case CASE_OUTCOME_UNRECORDED: return "未记账";
    case CASE_OUTCOME_EXECUTED: return "executed";
    case CASE_OUTCOME_BLOCKED: return "blocked";
    case CASE_OUTCOME_EXPECTED_FAILED: return "预期失败";
    case CASE_OUTCOME_ENCODE_FAILED: return "编译期失败";
    case CASE_OUTCOME_WINDOW_FAILED: return "窗口期失败";
    case CASE_OUTCOME_EXECUTION_FAILED: return "执行期失败";
    case CASE_OUTCOME_EXPECTATION_WRONG: return "期望不符";
    case CASE_OUTCOME_INCOMPLETE: return "用例不完整";
    case CASE_OUTCOME_UNOBSERVED: return "未观测到";
    case CASE_OUTCOME_MISSING_EXPECTATION: return "缺期望头";
    case CASE_OUTCOME_NOT_A_CASE: return "非用例(无 test 入口)";
    case CASE_OUTCOME_COUNT: break;
    }
    return "非法归属";
}

static bool case_outcome_is_failure(case_outcome_t outcome) {
    switch (outcome) {
    case CASE_OUTCOME_ENCODE_FAILED:
    case CASE_OUTCOME_WINDOW_FAILED:
    case CASE_OUTCOME_EXECUTION_FAILED:
    case CASE_OUTCOME_EXPECTATION_WRONG:
    case CASE_OUTCOME_INCOMPLETE:
    case CASE_OUTCOME_UNOBSERVED:
    case CASE_OUTCOME_MISSING_EXPECTATION:
        return true;
    default:
        return false;
    }
}

typedef struct runner_counts {
    size_t passed;
    size_t failed;
    size_t blocked;
    
    size_t expected_failed;
    
    size_t executed;
    
    size_t expectation_wrong;
    size_t incomplete;
    
    size_t unobserved;
    
    size_t execution_failed;
    
    size_t encode_failed;
    
    size_t window_failed;
} runner_counts_t;

typedef struct blocked_entry {
    char file[CASE_PATH_CAPACITY];
    uint32_t pc;
    char operation[64];
} blocked_entry_t;

#define MAX_BLOCKED 512

#define MAX_CASES 256U

typedef struct case_outcome_record {
    char path[CASE_PATH_CAPACITY];
    case_outcome_t outcome;
} case_outcome_record_t;

static case_outcome_record_t g_outcomes[MAX_CASES];

static void record_outcome(case_outcome_record_t *records, size_t index,
                           const char *path, case_outcome_t outcome) {
    if (index >= MAX_CASES) {
        return;
    }
    if (records[index].outcome != CASE_OUTCOME_UNRECORDED) {
        fprintf(stderr,
                "runner 缺陷：%s 被记了两遍归属（先 %s，又 %s）\n", path,
                case_outcome_name(records[index].outcome),
                case_outcome_name(outcome));
        return;
    }
    snprintf(records[index].path, sizeof(records[index].path), "%s", path);
    records[index].outcome = outcome;
}

static void tally_outcome(runner_counts_t *counts, case_outcome_t outcome) {
    if (case_outcome_is_failure(outcome)) {
        counts->failed += 1U;
    }
    switch (outcome) {
    case CASE_OUTCOME_EXECUTED: counts->passed += 1U; counts->executed += 1U; break;
    case CASE_OUTCOME_BLOCKED: counts->blocked += 1U; break;
    case CASE_OUTCOME_EXPECTED_FAILED:
        counts->passed += 1U;
        counts->expected_failed += 1U;
        break;
    case CASE_OUTCOME_ENCODE_FAILED: counts->encode_failed += 1U; break;
    case CASE_OUTCOME_WINDOW_FAILED: counts->window_failed += 1U; break;
    case CASE_OUTCOME_EXECUTION_FAILED: counts->execution_failed += 1U; break;
    case CASE_OUTCOME_EXPECTATION_WRONG: counts->expectation_wrong += 1U; break;
    case CASE_OUTCOME_INCOMPLETE: counts->incomplete += 1U; break;
    case CASE_OUTCOME_UNOBSERVED: counts->unobserved += 1U; break;
    
    case CASE_OUTCOME_MISSING_EXPECTATION: break;
    
    case CASE_OUTCOME_NOT_A_CASE:
    case CASE_OUTCOME_UNRECORDED:
    case CASE_OUTCOME_COUNT:
        break;
    }
}

static void close_case(case_outcome_record_t *records, size_t index,
                       const char *path, runner_counts_t *counts,
                       case_outcome_t outcome) {
    record_outcome(records, index, path, outcome);
    tally_outcome(counts, outcome);
}

typedef struct encoder_gap_code {
    int64_t code;   
    const char *reason;
} encoder_gap_code_t;

static const encoder_gap_code_t kEncoderGapCodes[] = {
    {29, "dynamic member key: layout emits a `base·*k` form this runtime cannot "
         "encode yet (issue #634)"},
    {25, "pointer/extension parameter type is not representable from RWIR text "
         "yet"},
};

static const char *encoder_gap_reason(int64_t code) {
    for (size_t ordinal = 0U;
         ordinal < sizeof(kEncoderGapCodes) / sizeof(kEncoderGapCodes[0]);
         ++ordinal) {
        if (kEncoderGapCodes[ordinal].code == code) {
            return kEncoderGapCodes[ordinal].reason;
        }
    }
    return NULL;
}
static blocked_entry_t g_blocked[MAX_BLOCKED];
static size_t g_blocked_count;

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

static void *runtimec_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return calloc(1U, size);
}

static void runtimec_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

#define KVRUN_CALLEE_ENTRY_SOURCE_PC 1U

#define KVRUN_MAX_ARGUMENTS 16U

typedef frame_driver_outcome_t frame_outcome_t;
#define FRAME_OUTCOME_INVALID FRAME_DRIVER_OUTCOME_INVALID
#define FRAME_OUTCOME_DONE FRAME_DRIVER_OUTCOME_DONE
#define FRAME_OUTCOME_HANDOFF FRAME_DRIVER_OUTCOME_HANDOFF
#define FRAME_OUTCOME_ERROR FRAME_DRIVER_OUTCOME_ERROR
#define FRAME_OUTCOME_EXECUTE_FAILED FRAME_DRIVER_OUTCOME_EXECUTE_FAILED

typedef struct runner_environment {
    void *layout_library;
    layout_file_function_t layout_file;
    
    void *kvspace_handle;
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_session_t *session;
    
    frame_driver_t frame_driver;
    char directory[256];
    char data_source[320];
    unsigned sequence;
} runner_environment_t;

static bool environment_open(runner_environment_t *environment,
                             const char *layout_path,
                             const char *kvspace_path) {
    memset(environment, 0, sizeof(*environment));
    environment->layout_library = dlopen(layout_path, RTLD_NOW | RTLD_LOCAL);
    if (environment->layout_library == NULL) {
        fprintf(stderr, "SKIP: cannot load layout %s: %s\n", layout_path,
                dlerror());
        return false;
    }
    environment->layout_file = (layout_file_function_t)(uintptr_t)dlsym(
        environment->layout_library, "kvlangLayoutFile");
    if (environment->layout_file == NULL) {
        fprintf(stderr, "SKIP: layout library has no kvlangLayoutFile\n");
        return false;
    }
    (void)kvspace_path;
    return true;
}

static void remove_case_store_files(const char *directory) {
    static const char *const files[] = {
        "store.shm", "store.shm.garb", "store.shm.gen",
        "store.shm.sbo.data", "store.shm.sbo.head",
        "capabilities.kv",
    };
    char path[320];
    for (size_t ordinal = 0U; ordinal < sizeof(files) / sizeof(files[0]);
         ++ordinal) {
        snprintf(path, sizeof(path), "%s/%s", directory, files[ordinal]);
        (void)remove(path);
    }
    (void)rmdir(directory);
}

static bool case_store_open(runner_environment_t *environment,
                            unsigned case_ordinal) {
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    snprintf(environment->directory, sizeof(environment->directory),
             "/tmp/runtimec-kvrun-%d-%u", (int)getpid(), case_ordinal);
    
    remove_case_store_files(environment->directory);
    if (mkdir(environment->directory, 0700) != 0) {
        fprintf(stderr, "case store: cannot mkdir %s\n", environment->directory);
        return false;
    }
    snprintf(environment->data_source, sizeof(environment->data_source),
             "shm://%s/store.shm", environment->directory);
    environment->kvspace_handle = kvspaceConnect(environment->data_source);
    if (environment->kvspace_handle == NULL) {
        fprintf(stderr, "case store: cannot connect %s\n",
                environment->data_source);
        return false;
    }
    runtimec_kvspace_access_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = environment->kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    if (runtimec_kvspace_access_create(&request,
                                       &environment->kvspace_access,
                                       &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "case store: adapter create failed\n");
        return false;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = (uint32_t)sizeof(options);
    options.allocator.context = NULL;
    options.allocator.allocate = runtimec_allocate;
    options.allocator.deallocate = runtimec_deallocate;
    
    options.kvspace_handle = environment->kvspace_handle;
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
    if (runtimec_session_create(environment->kvspace_access, &options,
                                &environment->session,
                                &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "case store: session create failed: status=%d %s\n",
                (int)error.status, error.message.data ? error.message.data : "");
        return false;
    }
    
    if (frame_driver_initialize(&environment->frame_driver,
                                environment->session) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "case store: frame driver initialize failed\n");
        return false;
    }
    return true;
}

static runtimec_status_t case_store_close(runner_environment_t *environment) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (environment->session != NULL) {
        if (runtimec_session_destroy(environment->session) !=
            RUNTIMEC_STATUS_SUCCESS) {
            status = RUNTIMEC_STATUS_ERROR;
        }
        environment->session = NULL;
    }
    if (environment->kvspace_handle != NULL) {
        runtimec_error_t error;
        
        if (environment->kvspace_access != NULL) {
            if (runtimec_kvspace_access_destroy(environment->kvspace_access,
                                                &error) !=
                RUNTIMEC_STATUS_SUCCESS) {
                status = RUNTIMEC_STATUS_ERROR;
            }
            environment->kvspace_access = NULL;
        }
        kvspaceClose(environment->kvspace_handle);
        environment->kvspace_handle = NULL;
    }
    if (environment->directory[0] != '\0') {
        remove_case_store_files(environment->directory);
        environment->directory[0] = '\0';
    }
    return status;
}

static const char *g_runner_operators_argument = NULL;

static bool g_list_outcomes = false;

static size_t runner_operator_names(const char *output[], size_t capacity) {
    const char *list = g_runner_operators_argument;
    if (list == NULL || list[0] == '\0') {
        return 0U;
    }
    size_t count = 0U;
    const char *cursor = list;
    while (*cursor != '\0' && count < capacity) {
        const char *comma = strchr(cursor, ',');
        const size_t size = comma != NULL ? (size_t)(comma - cursor) : strlen(cursor);
        if (size != 0U) {
            output[count] = cursor;
            ++count;
        }
        if (comma == NULL) {
            break;
        }
        cursor = comma + 1;
    }
    return count;
}

static bool declare_external_capabilities(runner_environment_t *environment) {
    
    const char *operator_names[8];
    const size_t operator_count =
        runner_operator_names(operator_names, sizeof(operator_names) /
                                                 sizeof(operator_names[0]));
    char declarations[512];
    declarations[0] = '\0';
    for (size_t at = 0U; at < operator_count; ++at) {
        const size_t used = strlen(declarations);
        if (snprintf(declarations + used, sizeof(declarations) - used,
                     "rwir %.*s(*[]char/utf32 #s...)\n",
                     (int)strcspn(operator_names[at], ","),
                     operator_names[at]) <= 0) {
            return false;
        }
    }
    char path[CASE_PATH_CAPACITY];
    snprintf(path, sizeof(path), "%s/capabilities.kv", environment->directory);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t size = strlen(declarations);
    const bool written = fwrite(declarations, 1U, size, file) == size;
    fclose(file);
    if (!written) {
        return false;
    }
    char entry[256];
    char error_text[512];
    memset(entry, 0, sizeof(entry));
    memset(error_text, 0, sizeof(error_text));
    const int result = environment->layout_file(
        path, environment->data_source, entry, sizeof(entry), error_text,
        sizeof(error_text));
    (void)remove(path);
    if (result != 0) {
        fprintf(stderr, "SKIP: cannot declare external capabilities: %s\n",
                error_text);
        return false;
    }
    return true;
}

static void operation_name_at(runner_environment_t *environment,
                              const runtimec_snapshot_t *snapshot,
                              const char *function_path, uint32_t pc,
                              char *out, size_t out_capacity) {
    snprintf(out, out_capacity, "?");
    
    char key[512];
    const int key_length = snprintf(key, sizeof(key), "%s/[%u,0]",
                                    function_path, pc);
    if (key_length < 0 || (size_t)key_length >= sizeof(key)) {
        fprintf(stderr,
                "FAIL: operation key truncated (%d bytes needed, %zu available): "
                "%s/[%u,0]\n",
                key_length, sizeof(key), function_path, pc);
        return;
    }
    
    uint8_t storage[512];
    runtimec_bytes_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    if (runtimec_kvspace_access_read(environment->kvspace_access, snapshot,
                                   runtimec_key_view_from_c_string(key),
                                   &buffer, &error) != RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    
    const size_t head = KVSPACE_XVALUE_HEAD_LEN;
    if (buffer.size <= head + 5U) {
        return;
    }
    const uint8_t *text = buffer.data + head + 5U;
    const size_t text_size = buffer.size - head - 5U;
    size_t usable = text_size;
    for (size_t index = 0U; index < text_size; ++index) {
        if (text[index] == '\0') {
            usable = index;
            break;
        }
    }
    if (usable == 0U || usable >= out_capacity) {
        return;
    }
    memcpy(out, text, usable);
    out[usable] = '\0';
}

static runtimec_status_t environment_close(runner_environment_t *environment) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (environment->layout_library != NULL) {
        dlclose(environment->layout_library);
    }
    return status;
}

typedef struct case_list {
    char paths[256][CASE_PATH_CAPACITY];
    size_t count;
} case_list_t;

static bool ends_with_kv(const char *name) {
    const size_t length = strlen(name);
    return length > 3U && strcmp(name + length - 3U, ".kv") == 0;
}

static void collect_cases(const char *root, const char *relative,
                          case_list_t *list) {
    char directory[CASE_PATH_CAPACITY];
    if (relative[0] == '\0') {
        snprintf(directory, sizeof(directory), "%s", root);
    } else {
        snprintf(directory, sizeof(directory), "%s/%s", root, relative);
    }
    DIR *handle = opendir(directory);
    if (handle == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        if (strcmp(entry->d_name, "known-limitations") == 0) {
            continue;
        }
        char next[CASE_PATH_CAPACITY];
        if (relative[0] == '\0') {
            snprintf(next, sizeof(next), "%s", entry->d_name);
        } else {
            snprintf(next, sizeof(next), "%s/%s", relative, entry->d_name);
        }
        char full[CASE_PATH_CAPACITY * 2U];
        snprintf(full, sizeof(full), "%s/%s", root, next);
        struct stat info;
        if (stat(full, &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            collect_cases(root, next, list);
        } else if (ends_with_kv(entry->d_name) && list->count < 256U) {
            snprintf(list->paths[list->count], CASE_PATH_CAPACITY, "%s", next);
            list->count += 1U;
        }
    }
    closedir(handle);
}

#define KVRUN_EXPECTATION_LINE_CAPACITY 64U
#define KVRUN_EXPECTATION_LINE_SIZE 256U

typedef struct expectation {
    char lines[KVRUN_EXPECTATION_LINE_CAPACITY][KVRUN_EXPECTATION_LINE_SIZE];
    size_t count;
    
    bool overflow;
    
    bool line_overflow;
    size_t line_overflow_index;
    
    char expected_error[256];
    
    char expected_block[256];
} expectation_t;

static bool has_test_entry(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bool found = false;
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL) {
        
        const char *cursor = line;
        while ((cursor = strstr(cursor, "rwfunc")) != NULL) {
            const char *after = cursor + strlen("rwfunc");
            while (*after == ' ' || *after == '\t') {
                after += 1;
            }
            if (strncmp(after, "test", 4U) == 0) {
                const char *tail = after + 4U;
                while (*tail == ' ' || *tail == '\t') {
                    tail += 1;
                }
                if (*tail == '(') {
                    found = true;
                    break;
                }
            }
            cursor = after;
        }
        if (found) {
            break;
        }
    }
    fclose(file);
    return found;
}

static bool file_is_case(const char *path, bool declares_expected_error) {
    return has_test_entry(path) || declares_expected_error;
}

static void parse_expectations(const char *path, expectation_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    char line[512];
    bool in_block = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        
        if (strstr(line, "期望阻塞") != NULL) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *text = colon + 1;
                while (*text == ' ') {
                    text += 1;
                }
                snprintf(out->expected_block, sizeof(out->expected_block), "%s",
                         text);
                continue;
            }
        }
        if (strstr(line, "期望错误") != NULL) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *text = colon + 1;
                while (*text == ' ') {
                    text += 1;
                }
                snprintf(out->expected_error, sizeof(out->expected_error), "%s",
                         text);
                continue;
            }
        }
        if (strstr(line, "期望输出") != NULL ||
            strstr(line, "Expected output") != NULL) {
            
            const char *prefix_end = strstr(line, "期望输出");
            size_t prefix_size = strlen("期望输出");
            if (prefix_end == NULL) {
                prefix_end = strstr(line, "Expected output");
                prefix_size = strlen("Expected output");
            }
            const char *inline_text = prefix_end + prefix_size;
            while (*inline_text == ' ') {
                inline_text += 1;
            }
            if (*inline_text == ':') {
                inline_text += 1;
                while (*inline_text == ' ') {
                    inline_text += 1;
                }
            }
            if (*inline_text != '\0') {
                if (out->count >= KVRUN_EXPECTATION_LINE_CAPACITY) {
                    out->overflow = true;
                } else if (strlen(inline_text) >= KVRUN_EXPECTATION_LINE_SIZE) {
                    
                    out->line_overflow = true;
                    out->line_overflow_index = out->count;
                } else {
                    memcpy(out->lines[out->count], inline_text,
                           strlen(inline_text) + 1U);
                    out->count += 1U;
                }
                in_block = false;
                continue;
            }
            in_block = true;
            continue;
        }
        if (in_block) {
            if (strncmp(line, "//   ", 5) == 0 || strncmp(line, "// \t", 4) == 0) {
                const char *text = line + (strncmp(line, "//   ", 5) == 0 ? 5 : 4);
                if (*text != '\0') {
                    if (out->count >= KVRUN_EXPECTATION_LINE_CAPACITY) {
                        out->overflow = true;
                    } else if (strlen(text) >= KVRUN_EXPECTATION_LINE_SIZE) {
                        
                        out->line_overflow = true;
                        out->line_overflow_index = out->count;
                    } else {
                        memcpy(out->lines[out->count], text, strlen(text) + 1U);
                        out->count += 1U;
                    }
                }
            } else if (strncmp(line, "//", 2) != 0) {
                in_block = false;
            }
        }
    }
    fclose(file);
}

static void first_error_line(const char *text, char *output, size_t capacity) {
    static const char empty[] = "(空)";

    if (text == NULL || text[0] == '\0') {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    
    const char *first_nonempty = NULL;
    const char *cursor = text;
    while (*cursor != '\0') {
        const size_t length = strcspn(cursor, "\r\n");
        if (length > 0U) {
            if (strncmp(cursor, "error", 5U) == 0) {
                const size_t copy = length < capacity - 1U ? length : capacity - 1U;
                memcpy(output, cursor, copy);
                output[copy] = '\0';
                return;
            }
            if (first_nonempty == NULL) {
                first_nonempty = cursor;
            }
        }
        cursor += length;
        if (*cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
    }
    if (first_nonempty == NULL) {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    const size_t length = strcspn(first_nonempty, "\r\n");
    const size_t copy = length < capacity - 1U ? length : capacity - 1U;
    memcpy(output, first_nonempty, copy);
    output[copy] = '\0';
}

#define FILENO_STDOUT 1
#define FILENO_STDERR 2

static bool redirect_stream_to(int stream_fd, char *path, size_t capacity,
                               int *saved_fd) {
    snprintf(path, capacity, "/tmp/kv-runner-XXXXXX");
    const int fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }
    fflush(stream_fd == FILENO_STDERR ? stderr : stdout);
    *saved_fd = dup(stream_fd);
    if (*saved_fd < 0) {
        close(fd);
        remove(path);
        return false;
    }
    if (dup2(fd, stream_fd) < 0) {
        close(fd);
        close(*saved_fd);
        remove(path);
        return false;
    }
    close(fd);
    return true;
}

static void restore_stream(int stream_fd, int saved_fd, const char *path,
                           char *output, size_t capacity) {
    fflush(stdout);
    fflush(stderr);
    dup2(saved_fd, stream_fd);
    close(saved_fd);
    output[0] = '\0';
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        size_t read_size = fread(output, 1U, capacity - 1U, file);
        output[read_size] = '\0';
        fclose(file);
    }
    remove(path);
}

typedef enum compare_result {
    COMPARE_PASS = 0,
    
    COMPARE_EXPECTATION_WRONG = 1,
    
    COMPARE_INCOMPLETE = 2,
    
    COMPARE_UNOBSERVED = 3
} compare_result_t;

static int line_is_bench_noise(const char *line) {
    static const char nanosecond_prefix[] = "__bench_ns:";
    static const char microsecond_prefix[] = "__bench_us:";

    return strncmp(line, nanosecond_prefix, sizeof(nanosecond_prefix) - 1U) == 0 ||
           strncmp(line, microsecond_prefix, sizeof(microsecond_prefix) - 1U) == 0;
}

static size_t split_lines(const char *text, char (*lines)[512],
                          size_t capacity) {
    size_t count = 0U;
    const char *cursor = text;
    while (*cursor != '\0' && count < capacity) {
        size_t length = strcspn(cursor, "\r\n");
        if (length >= sizeof(lines[0])) {
            length = sizeof(lines[0]) - 1U;
        }
        memcpy(lines[count], cursor, length);
        lines[count][length] = '\0';
        
        if (line_is_bench_noise(lines[count])) {
            cursor += strcspn(cursor, "\r\n");
            if (*cursor == '\r') {
                ++cursor;
            }
            if (*cursor == '\n') {
                ++cursor;
            }
            continue;
        }
        
        while (length > 0U &&
               (lines[count][length - 1U] == ' ' ||
                lines[count][length - 1U] == '\t')) {
            lines[count][--length] = '\0';
        }
        count += 1U;
        cursor += strcspn(cursor, "\r\n");
        if (*cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
    }
    return count;
}

static compare_result_t compare_output(const expectation_t *expectation,
                                       const char *actual,
                                       const char *case_path) {
    char actual_lines[64][512];
    const size_t actual_count = split_lines(actual, actual_lines, 64U);
    
    if (actual_count == 0U && expectation->count != 0U) {
        fprintf(stderr,
                "FAIL %s: 未观测到任何输出（期望 %zu 行）——"
                "查链路，不是查实现\n",
                case_path, expectation->count);
        return COMPARE_UNOBSERVED;
    }
    if (actual_count != expectation->count) {
        
        fprintf(stderr,
                "FAIL %s: 行数不等（实际 %zu / 期望 %zu）\n",
                case_path, actual_count, expectation->count);
        for (size_t i = 0U; i < actual_count && i < 4U; ++i) {
            fprintf(stderr, "  实际[%zu]: %s\n", i, actual_lines[i]);
        }
        for (size_t i = 0U; i < expectation->count && i < 4U; ++i) {
            fprintf(stderr, "  期望[%zu]: %s\n", i, expectation->lines[i]);
        }
        return actual_count > expectation->count ? COMPARE_INCOMPLETE
                                                 : COMPARE_EXPECTATION_WRONG;
    }
    for (size_t i = 0U; i < expectation->count; ++i) {
        if (strcmp(expectation->lines[i], actual_lines[i]) != 0) {
            fprintf(stderr, "FAIL %s: 第 %zu 行不同\n  期望: %s\n  实际: %s\n",
                    case_path, i + 1U, expectation->lines[i], actual_lines[i]);
            return COMPARE_EXPECTATION_WRONG;
        }
    }
    return COMPARE_PASS;
}

int main(int argument_count, char **arguments) {
    
    const char *positional[3];
    size_t positional_count = 0U;
    for (int at = 1; at < argument_count; ++at) {
        const char *argument = arguments[at];
        if (strncmp(argument, "--operators=", 12) == 0) {
            g_runner_operators_argument = argument + 12;
            continue;
        }
        if (strcmp(argument, "--list-outcomes") == 0) {
            g_list_outcomes = true;
            continue;
        }
        if (positional_count < 3U) {
            positional[positional_count] = argument;
        }
        ++positional_count;
    }
    if (positional_count != 3U) {
        fprintf(stderr,
                "usage: kv_runner [--operators=a,b] [--list-outcomes] "
                "<corpus-dir> <layout-lib> <kvspace-lib>\n");
        return 2;
    }
    const char *corpus = positional[0];

    runner_environment_t environment;
    if (!environment_open(&environment, positional[1], positional[2])) {
        return SKIP_RETURN_CODE;
    }

    case_list_t cases;
    memset(&cases, 0, sizeof(cases));
    collect_cases(corpus, "", &cases);
    if (cases.count == 0U) {
        fprintf(stderr, "no .kv cases under %s\n", corpus);
        if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "warning: environment close reported an error\n");
        }
        return 1;
    }

    runner_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    size_t missing_expectation = 0U;
    size_t layout_ok = 0U;
    memset(g_outcomes, 0, sizeof(g_outcomes));
    
    for (size_t index = 0U; index < cases.count; ++index) {
        char full[CASE_PATH_CAPACITY * 2U];
        snprintf(full, sizeof(full), "%s/%s", corpus, cases.paths[index]);
        expectation_t expectation;
        parse_expectations(full, &expectation);
        const bool expects_layout_failure = expectation.expected_error[0] != '\0';
        
        if (!case_store_open(&environment, (unsigned)index)) {
            fprintf(stderr, "FAIL %s: 无法建立 per-case store\n",
                    cases.paths[index]);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_WINDOW_FAILED);
            (void)case_store_close(&environment);
            continue;
        }
        
        if (!declare_external_capabilities(&environment)) {
            fprintf(stderr, "FAIL %s: 无法声明外部能力\n", cases.paths[index]);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_WINDOW_FAILED);
            (void)case_store_close(&environment);
            continue;
        }
        
        if (expectation.expected_block[0] == '\0' &&
            expectation.expected_error[0] != '\0' &&
            expectation.count != 0U) {
            fprintf(stderr,
                    "FAIL %s: `期望输出` 与 `期望错误` 互斥——一条用例只能是正向"
                    "或负向\n",
                    cases.paths[index]);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_INCOMPLETE);
            (void)case_store_close(&environment);
            continue;
        }
        
        if (expectation.expected_block[0] != '\0') {
            if (g_blocked_count < MAX_BLOCKED) {
                blocked_entry_t *slot = &g_blocked[g_blocked_count];
                snprintf(slot->file, sizeof(slot->file), "%s",
                         cases.paths[index]);
                slot->pc = 0U;
                snprintf(slot->operation, sizeof(slot->operation), "declared");
                g_blocked_count += 1U;
            }
            fprintf(stderr, "BLOCKED %s: declared — %s\n", cases.paths[index],
                    expectation.expected_block);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_BLOCKED);
            (void)case_store_close(&environment);
            continue;
        }
        if (!file_is_case(full, expects_layout_failure)) {
            
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_NOT_A_CASE);
            (void)case_store_close(&environment);
            continue;
        }
        if (!expects_layout_failure && expectation.count == 0U) {
            
            fprintf(stderr, "FAIL %s: 缺 `// 期望输出:` 头\n", cases.paths[index]);
            missing_expectation += 1U;
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_MISSING_EXPECTATION);
            (void)case_store_close(&environment);
            continue;
        }
        
        if (expectation.overflow) {
            fprintf(stderr,
                    "FAIL %s: 期望输出超过 runner 容量 %u 行\n", cases.paths[index],
                    (unsigned)KVRUN_EXPECTATION_LINE_CAPACITY);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_INCOMPLETE);
            (void)case_store_close(&environment);
            continue;
        }
        if (expectation.line_overflow) {
            fprintf(stderr,
                    "FAIL %s: 期望输出第 %zu 行超过 runner 单行容量 %u 字节\n",
                    cases.paths[index], expectation.line_overflow_index + 1U,
                    (unsigned)KVRUN_EXPECTATION_LINE_SIZE);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_INCOMPLETE);
            (void)case_store_close(&environment);
            continue;
        }
        
        char entry[256];
        char error_text[512];
        memset(entry, 0, sizeof(entry));
        memset(error_text, 0, sizeof(error_text));
        
        char captured[4096];
        char capture_path[256];
        capture_path[0] = '\0';
        captured[0] = '\0';
        int saved_stderr = -1;
        const bool capturing =
            redirect_stream_to(FILENO_STDERR, capture_path,
                               sizeof(capture_path), &saved_stderr);
        const int layout_result = environment.layout_file(
            full, environment.data_source, entry, sizeof(entry), error_text,
            sizeof(error_text));
        if (capturing) {
            restore_stream(FILENO_STDERR, saved_stderr, capture_path, captured,
                           sizeof(captured));
        }
        if (expects_layout_failure) {
            
            if (layout_result == 0) {
                fprintf(stderr, "FAIL %s: 预期 layout 失败，但它通过了\n",
                        cases.paths[index]);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_ENCODE_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            
            if (strstr(error_text, expectation.expected_error) == NULL &&
                strstr(captured, expectation.expected_error) == NULL) {
                char stderr_line[512];
                first_error_line(captured, stderr_line, sizeof(stderr_line));
                fprintf(stderr,
                        "FAIL %s: layout 失败了，但错误不含 %s\n  实际 stderr 首行：%s\n"
                        "  实际 ABI 串：%s\n",
                        cases.paths[index], expectation.expected_error,
                        stderr_line,
                        error_text[0] != '\0' ? error_text : "(空)");
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_ENCODE_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_EXPECTED_FAILED);
            (void)case_store_close(&environment);
            continue;
        }
        if (layout_result != 0) {
            
            static const char capability_prefix[] = "[capability:";
            const char *capability = strstr(error_text, capability_prefix);
            if (capability != NULL) {
                
                const char *reason =
                    capability + sizeof(capability_prefix) - 1U;
                const char *closing = strchr(reason, ']');
                if (closing != NULL) {
                    reason = closing + 1;
                    while (*reason == ' ') {
                        reason += 1;
                    }
                }
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = 0U;
                    snprintf(slot->operation, sizeof(slot->operation),
                             "layout");
                    g_blocked_count += 1U;
                }
                fprintf(stderr, "BLOCKED %s: layout — %s\n", cases.paths[index],
                        reason);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_BLOCKED);
                (void)case_store_close(&environment);
                continue;
            }
            
            char stderr_line[512];
            first_error_line(captured, stderr_line, sizeof(stderr_line));
            fprintf(stderr, "FAIL %s: layout failed: %s\n  实际 stderr 首行：%s\n",
                    cases.paths[index], error_text, stderr_line);
            close_case(g_outcomes, index, cases.paths[index], &counts,
                       CASE_OUTCOME_ENCODE_FAILED);
            (void)case_store_close(&environment);
            continue;
        }
        layout_ok += 1U;
        
        {
            runtimec_session_runtime_components_t components;
            if (runtimec_session_get_runtime_components(
                    environment.session, &components) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: cannot get components\n",
                        cases.paths[index]);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_WINDOW_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            
            char function_path[CASE_PATH_CAPACITY];
            snprintf(function_path, sizeof(function_path), "/lib/test");
            vmkv_encode_request_t encode_request;
            runtimec_error_t encode_error;
            memset(&encode_request, 0, sizeof(encode_request));
            memset(&encode_error, 0, sizeof(encode_error));
            encode_request.kvspace_access = environment.kvspace_access;
            encode_request.function_path =
                runtimec_string_view_from_c_string(function_path);
            encode_request.expected_snapshot.contract_version =
                runtimec_contract_version();
            
            runtimec_source_revision_reset(
                &encode_request.expected_snapshot.source_revision);
            encode_request.expected_snapshot.function_key =
                runtimec_key_view_from_c_string(function_path);
            
            encode_request.expected_snapshot.source_position_text =
                runtimec_string_view_from_c_string("[1,0]");
            if (runtimec_kvspace_access_source_revision(
                    environment.kvspace_access,
                    encode_request.expected_snapshot.function_key,
                    &encode_request.expected_snapshot.source_revision,
                    &encode_error) != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: source revision unavailable\n",
                        cases.paths[index]);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_WINDOW_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            binrwir_binfunc_t *program = NULL;
            const runtimec_status_t encoded = vmkv_encode(
                components.vmkv_encoder, &encode_request, &program,
                &encode_error);
            if (encoded != RUNTIMEC_STATUS_SUCCESS) {
                
                const char *gap_reason =
                    encoded == RUNTIMEC_STATUS_UNSUPPORTED
                        ? encoder_gap_reason(encode_error.code)
                        : NULL;

                if (gap_reason != NULL) {
                    if (g_blocked_count < MAX_BLOCKED) {
                        blocked_entry_t *slot = &g_blocked[g_blocked_count];
                        snprintf(slot->file, sizeof(slot->file), "%s",
                                 cases.paths[index]);
                        slot->pc = 0U;
                        snprintf(slot->operation, sizeof(slot->operation),
                                 "encode:code=%lld",
                                 (long long)encode_error.code);
                        g_blocked_count += 1U;
                    }
                    fprintf(stderr,
                            "BLOCKED %s: encode gap code=%lld — %s\n"
                            "  encoder message: %s\n",
                            cases.paths[index], (long long)encode_error.code,
                            gap_reason,
                            encode_error.message.data
                                ? encode_error.message.data
                                : "");
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_BLOCKED);
                    (void)case_store_close(&environment);
                    continue;
                }
                fprintf(stderr, "FAIL %s: encode failed (code=%lld): %s\n",
                        cases.paths[index], (long long)encode_error.code,
                        encode_error.message.data ? encode_error.message.data
                                                  : "");
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_ENCODE_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            
            binrwir_binfunc_view_t program_view;
            
            unsigned foreign = 0U;
            if (binrwir_binfunc_get_view(program, &program_view) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: get_view failed\n",
                        cases.paths[index]);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_WINDOW_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            
            for (size_t ordinal = 0U; ordinal < program_view.instruction_count;
                 ++ordinal) {
                binrwir_instruction_view_t view;
                if (binrwir_binfunc_get_instruction(program, (uint32_t)ordinal,
                                                    &view) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                    
                    continue;
                }
                if (view.instruction->operation_identifier !=
                    BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
                    
                    continue;
                }
                foreign += 1U;
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = view.source_logical_program_counter;
                    operation_name_at(&environment,
                                      &encode_request.expected_snapshot,
                                      function_path, slot->pc, slot->operation,
                                      sizeof(slot->operation));
                    g_blocked_count += 1U;
                }
            }
            if (foreign > 0U) {
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_BLOCKED);
                (void)case_store_close(&environment);
                continue;
            }
            
            char vthread_identifier[64];
            const int vthread_length = snprintf(
                vthread_identifier, sizeof(vthread_identifier), "%u",
                ++environment.sequence);
            if (vthread_length < 0 ||
                (size_t)vthread_length >= sizeof(vthread_identifier)) {
                fprintf(stderr, "FAIL %s: vthread identifier truncated\n",
                        cases.paths[index]);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_WINDOW_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            binrwir_execution_exit_t frame_exit;
            memset(&frame_exit, 0, sizeof(frame_exit));
            
            char output_path[256];
            output_path[0] = '\0';
            char captured_output[8192];
            captured_output[0] = '\0';
            int saved_stdout = -1;
            const bool capturing_stdout =
                redirect_stream_to(FILENO_STDOUT, output_path,
                                   sizeof(output_path), &saved_stdout);
            char driver_message[256];
            const frame_outcome_t frame_outcome = frame_driver_execute(
                &environment.frame_driver, function_path, 1U, 1, "1",
                vthread_identifier, NULL, 0U, &frame_exit, driver_message,
                sizeof(driver_message));
            if (capturing_stdout) {
                restore_stream(FILENO_STDOUT, saved_stdout, output_path,
                               captured_output, sizeof(captured_output));
            }
            if (frame_outcome == FRAME_OUTCOME_EXECUTE_FAILED) {
                
                fprintf(stderr,
                        "FAIL %s: execute failed status=%d"
                        "（exit.kind=%u status=%d detail=%u pc=%u）\n",
                        cases.paths[index], (int)frame_exit.status,
                        frame_exit.kind, (int)frame_exit.status,
                        frame_exit.detail_code,
                        frame_exit.logical_program_counter);
                
                switch (frame_exit.detail_code) {
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_ENCODE_FAILED);
                    break;
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY:
                case RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_WINDOW_FAILED);
                    break;
                default:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_EXECUTION_FAILED);
                    break;
                }
                (void)case_store_close(&environment);
                continue;
            }
            if (frame_outcome == FRAME_OUTCOME_HANDOFF) {
                
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = frame_exit.logical_program_counter;
                    snprintf(slot->operation, sizeof(slot->operation), "%s",
                             "handoff");
                    g_blocked_count += 1U;
                }
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_BLOCKED);
                (void)case_store_close(&environment);
                continue;
            }
            if (frame_outcome == FRAME_OUTCOME_ERROR) {
                
                char stderr_line[512];
                first_error_line(captured, stderr_line, sizeof(stderr_line));
                fprintf(stderr,
                        "FAIL %s: 执行报错 status=%d detail=%u pc=%u\n"
                        "  stderr：%s\n",
                        cases.paths[index],
                        (int)frame_exit.status,
                        frame_exit.detail_code,
                        frame_exit.logical_program_counter,
                        stderr_line);
                close_case(g_outcomes, index, cases.paths[index], &counts,
                           CASE_OUTCOME_EXECUTION_FAILED);
                (void)case_store_close(&environment);
                continue;
            }
            
            switch (compare_output(&expectation, captured_output, cases.paths[index])) {
                case COMPARE_PASS:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_EXECUTED);
                    break;
                case COMPARE_EXPECTATION_WRONG:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_EXPECTATION_WRONG);
                    break;
                case COMPARE_INCOMPLETE:
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_INCOMPLETE);
                    break;
                case COMPARE_UNOBSERVED:
                    
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_UNOBSERVED);
                    {
                        const binrwir_execution_exit_t *exit_information =
                            &frame_exit;
                        unsigned instruction_count = 0U;
                        if (binrwir_binfunc_get_view(
                                program, &program_view) ==
                            RUNTIMEC_STATUS_SUCCESS) {
                            instruction_count =
                                (unsigned)program_view.instruction_count;
                        }
                        fprintf(stderr,
                                "  停止位置 %s：exit.kind=%u status=%d "
                                "detail=%u pc=%d 指令总数=%u\n",
                                cases.paths[index], exit_information->kind,
                                (int)exit_information->status,
                                exit_information->detail_code,
                                (int)exit_information->logical_program_counter,
                                instruction_count);
                    }
                    break;
                default:
                    
                    close_case(g_outcomes, index, cases.paths[index], &counts,
                               CASE_OUTCOME_INCOMPLETE);
                    break;
            }
        }
        
        (void)case_store_close(&environment);
    }

    {
        
        const bool list_each = g_list_outcomes;
        bool matches = true;
        for (size_t index = 0U; index < cases.count; ++index) {
            const case_outcome_t outcome = g_outcomes[index].outcome;
            if (outcome == CASE_OUTCOME_UNRECORDED) {
                matches = false;
            }
            if (list_each) {
                printf("  归属  %-24s %s\n", case_outcome_name(outcome),
                       cases.paths[index]);
            }
        }
        if (!matches) {
            fprintf(stderr, "每条用例的最终归属（未记账项）：\n");
            for (size_t index = 0U; index < cases.count; ++index) {
                if (g_outcomes[index].outcome == CASE_OUTCOME_UNRECORDED) {
                    fprintf(stderr, "  未记账  %s\n", cases.paths[index]);
                }
            }
        }
    }
    
    {
        size_t bucketed = counts.executed + counts.blocked + counts.failed +
                          counts.expected_failed;
        
        size_t not_a_case = 0U;
        for (size_t index = 0U; index < cases.count; ++index) {
            if (g_outcomes[index].outcome == CASE_OUTCOME_NOT_A_CASE) {
                not_a_case += 1U;
            }
        }
        const size_t accounted = bucketed + not_a_case;
        if (accounted != cases.count) {
            fprintf(stderr,
                    "ASSIGNMENT GAP: 各桶之和 %zu != 用例总数 %zu"
                    "（executed %zu + blocked %zu + fail %zu + 预期失败 %zu"
                    " + 非用例 %zu）\n",
                    accounted, cases.count, counts.executed, counts.blocked,
                    counts.failed, counts.expected_failed, not_a_case);
        }
        printf("归属核对：各桶之和 %zu / 用例总数 %zu"
               "（其中非用例 %zu：无 test 入口或无期望头）——%s\n",
               accounted, cases.count, not_a_case,
               accounted == cases.count ? "齐" : "**不齐**");
    }
    
    printf("用例 %zu：executed %zu / blocked %zu / fail %zu / pass(仅编码) %zu\n",
           cases.count, counts.executed, counts.blocked, counts.failed,
           counts.passed - counts.expected_failed - counts.executed);
    
    printf("  细分：未观测到 %zu、执行期失败 %zu、编译期失败 %zu、"
           "期望不符 %zu、用例不完整 %zu、其中预期失败 %zu\n",
           counts.unobserved, counts.execution_failed, counts.encode_failed,
           counts.expectation_wrong, counts.incomplete, counts.expected_failed);
    
    if (counts.unobserved != 0U) {
        printf("  注：`未观测到` = 期望有内容但实际零行输出 —— 查**链路**"
               "（算子没执行到/输出没接上），不是查实现。\n");
    }
    
    {
        
        const size_t classified = counts.encode_failed + counts.window_failed +
                                  counts.execution_failed +
                                  counts.expectation_wrong + counts.incomplete +
                                  counts.unobserved + missing_expectation;
        
        printf("CLASSIFICATION GAP: %zu failures are unclassified "
               "(failed %zu, classified %zu)\n",
               counts.failed - classified, counts.failed, classified);
    }
    printf("通过 executed %zu / 总数 %zu 条用例\n", counts.executed, cases.count);
    
    printf("  按阶段：编译期失败 %zu、窗口期失败 %zu、执行期失败 %zu、未观测到 %zu、"
           "期望不符 %zu、用例不完整 %zu、仅编码通过 %zu、预期失败 %zu\n",
           counts.encode_failed, counts.window_failed, counts.execution_failed,
           counts.unobserved,
           counts.expectation_wrong, counts.incomplete,
           counts.passed - counts.expected_failed - counts.executed,
           counts.expected_failed);
    if (g_blocked_count > 0U) {
        
        printf("blocked 明细（文件 + source pc + 算子名）：\n");
        for (size_t index = 0U; index < g_blocked_count; ++index) {
            printf("  blocked  %s  pc=%u  op=%s\n", g_blocked[index].file,
                   g_blocked[index].pc, g_blocked[index].operation);
        }
    }
    printf("注：`executed` = 真跑且输出逐行相同；`pass(仅编码)` = 只证明编得过，"
           "**不表示输出对**。\n");
    printf("注：`blocked` 是**有名字的能力缺口**，不是失败、也不是跳过——"
           "明细见上，退出码不受它影响。\n");
    printf("注：`其中预期失败` = layout 按文件头 `// 期望错误:` 被正确拒绝的条数，"
           "与「编得过」不是同一类。\n");
    (void)layout_ok;

    if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "warning: environment close reported an error\n");
    }
    
    if (missing_expectation != 0U || counts.failed != 0U) {
        return 1;
    }
    if (counts.executed == 0U) {
        fprintf(stderr,
                "SKIP: executed=0 —— 本次没有任何用例真跑到结束并比对输出"
                "（blocked %zu、仅编码 %zu）\n",
                counts.blocked, counts.passed - counts.expected_failed);
        return SKIP_RETURN_CODE;
    }
    return 0;
}
