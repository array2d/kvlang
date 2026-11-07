/*
 * binrwir-dump：把 kvlang 语料 layout 进**每个文件独立的临时 kvspace**，
 * 逐函数编码成 canonical BinRWIR Program，再把打印文本落到
 * `<output-root>/<相对路径>.binkv`。
 *
 * 为什么每个文件一个 store：语料的函数名会在不同文件之间重名（每个 tutorial
 * 都写 `main`/`test`）。共用一个 store 会让后 layout 的文件覆盖前一个，
 * 于是「dump 出了什么」取决于遍历顺序——那不是一份可复现的产物。
 *
 * 为什么 opid 必须有：这份产物是给人检查**二进制读写码**的。算子名是便利，
 * opid 才是被检查的对象。所以格式化时**不传名字解析器**（它一旦解析成功就
 * 只打名字、丢掉 opid），改为在文本后附一张 `opid -> 算子名` 图例。
 *
 * 用法：
 *   binrwir-dump <corpus-root> <output-root> <layout-library> <kvspace-library>
 *
 * 退出码：全部成功 0；有任一失败 1（失败明细仍完整落盘）。
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "binrwir/format.h"
#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/snapshot.h"
#include "contract/kvspace_access.h"
#include <kvspace/kvspace.h>
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

#define PATH_CAPACITY 1024
#define NAME_CAPACITY 256
#define STAGE_CAPACITY 32
#define REASON_CAPACITY 256
#define MAX_FUNCTIONS 512

typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry, uint32_t entry_capacity,
                                      char *error, uint32_t error_capacity);

typedef struct failure {
    char source[PATH_CAPACITY];
    char stage[STAGE_CAPACITY];
    char reason[REASON_CAPACITY];
} failure_t;

typedef struct report {
    failure_t *items;
    size_t count;
    size_t capacity;
} report_t;

typedef struct dump_context {
    runtimec_allocator_t allocator;
    layout_file_function_t layout_file;
} dump_context_t;

static void *dump_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void dump_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static runtimec_allocator_t dump_allocator(void) {
    runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = dump_allocate,
        .deallocate = dump_deallocate,
    };
    return allocator;
}

/* 拼接路径；截断是错误，不静默返回半截路径。 */
static bool format_path(char *output, size_t capacity, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    return written >= 0 && (size_t)written < capacity;
}

static void copy_text(char *output, size_t capacity, const char *text) {
    if (capacity == 0U) {
        return;
    }
    (void)snprintf(output, capacity, "%s", text != NULL ? text : "");
}

/* 只取消息首行：多行消息的后续行是上下文，不是根因。 */
static void copy_first_line(char *output, size_t capacity,
                            runtimec_string_view_t message) {
    size_t length = 0U;
    if (capacity == 0U) {
        return;
    }
    while (length < message.size && length + 1U < capacity &&
           message.data[length] != '\n' && message.data[length] != '\r') {
        output[length] = message.data[length];
        ++length;
    }
    output[length] = '\0';
    if (length == 0U) {
        copy_text(output, capacity, "(no message)");
    }
}

static void report_add(report_t *report, const char *source, const char *stage,
                       const char *reason) {
    if (report->count == report->capacity) {
        const size_t capacity =
            report->capacity == 0U ? 64U : report->capacity * 2U;
        failure_t *grown = realloc(report->items, capacity * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        report->items = grown;
        report->capacity = capacity;
    }
    failure_t *item = &report->items[report->count];
    copy_text(item->source, sizeof(item->source), source);
    copy_text(item->stage, sizeof(item->stage), stage);
    copy_text(item->reason, sizeof(item->reason), reason);
    report->count += 1U;
}

static bool make_directory(const char *path) {
    char working[PATH_CAPACITY];
    copy_text(working, sizeof(working), path);
    const size_t length = strlen(working);
    for (size_t index = 1U; index < length; ++index) {
        if (working[index] != '/') {
            continue;
        }
        working[index] = '\0';
        if (mkdir(working, 0755) != 0) {
            struct stat information;
            if (stat(working, &information) != 0 ||
                !S_ISDIR(information.st_mode)) {
                return false;
            }
        }
        working[index] = '/';
    }
    if (mkdir(working, 0755) != 0) {
        struct stat information;
        if (stat(working, &information) != 0 || !S_ISDIR(information.st_mode)) {
            return false;
        }
    }
    return true;
}

/* 递归删除一个临时目录里的普通文件；子目录也删。 */
static void remove_tree(const char *path) {
    DIR *directory = opendir(path);
    if (directory == NULL) {
        (void)remove(path);
        return;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[PATH_CAPACITY];
        if (!format_path(child, sizeof(child), "%s/%s", path, entry->d_name)) {
            continue;
        }
        struct stat information;
        if (stat(child, &information) == 0 && S_ISDIR(information.st_mode)) {
            remove_tree(child);
        } else {
            (void)remove(child);
        }
    }
    closedir(directory);
    (void)rmdir(path);
}

static bool ends_with_kv(const char *name) {
    const size_t length = strlen(name);
    return length > 3U && strcmp(name + length - 3U, ".kv") == 0;
}

/* 目录条目名是 `/lib/` 下的相对名，目录带尾斜杠；`‥` 开头的是元信息。 */
static bool function_name_of(const uint8_t *entry, uint32_t entry_size,
                             char *output, size_t capacity) {
    size_t length = entry_size;
    if (length >= 3U && entry[0] == 0xE2U && entry[1] == 0x80U &&
        entry[2] == 0xA5U) {
        return false;
    }
    if (length > 0U && entry[0] == '.') {
        return false;
    }
    if (length > 0U && entry[length - 1U] == '/') {
        --length;
    }
    if (length == 0U || length + 1U > capacity) {
        return false;
    }
    memcpy(output, entry, length);
    output[length] = '\0';
    return true;
}

static int compare_names(const void *left, const void *right) {
    return strcmp((const char *)left, (const char *)right);
}

/* 返回函数名数量；失败返回 -1。 */
static int list_functions(void *handle, char names[][NAME_CAPACITY]) {
    int32_t count = 0;
    if (kvspaceListLen(handle, "/lib/", 1, 0, &count) !=
        0) {
        return -1;
    }
    int output_count = 0;
    for (int32_t ordinal = 0; ordinal < count; ++ordinal) {
        uint8_t entry[1024];
        uint32_t entry_size = 0U;
        if (kvspaceListAt(handle, "/lib/", 1, 0, ordinal,
                                              entry, (uint32_t)sizeof(entry),
                                              &entry_size) != 0) {
            continue;
        }
        if (output_count >= MAX_FUNCTIONS) {
            return -1;
        }
        if (function_name_of(entry, entry_size, names[output_count],
                             NAME_CAPACITY)) {
            output_count += 1;
        }
    }
    qsort(names, (size_t)output_count, NAME_CAPACITY, compare_names);
    return output_count;
}

static bool append_operation_legend(dump_context_t *context,
                                    const binrwir_program_t *program,
                                    runtimec_bytes_buffer_t *buffer) {
    binrwir_program_view_t view;
    if (binrwir_program_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    binrwir_operation_identifier_t identifiers[1024];
    size_t identifier_count = 0U;
    for (size_t ordinal = 0U; ordinal < view.instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return false;
        }
        const binrwir_operation_identifier_t identifier =
            instruction.instruction->operation_identifier;
        bool seen = false;
        for (size_t index = 0U; index < identifier_count; ++index) {
            if (identifiers[index] == identifier) {
                seen = true;
                break;
            }
        }
        if (seen || identifier_count >= 1024U) {
            continue;
        }
        identifiers[identifier_count] = identifier;
        identifier_count += 1U;
    }
    if (identifier_count == 0U) {
        return true;
    }
    const char *header = "# opid names\n";
    binrwir_format_buffer_sink_context_t sink_context = {
        .allocator = context->allocator,
        .buffer = buffer,
    };
    runtimec_string_view_t accepted = binrwir_format_buffer_sink(
        &sink_context, runtimec_string_view_from_c_string(header));
    if (accepted.size != strlen(header)) {
        return false;
    }
    for (size_t index = 0U; index < identifier_count; ++index) {
        char line[NAME_CAPACITY + 32];
        runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);
        bool formatted = false;
        if (vmkv_rwir_manifest_find_operation_by_identifier(identifiers[index],
                                                            &name) &&
            name.size != 0U) {
            formatted = format_path(line, sizeof(line), "0x%08X %.*s\n",
                                    (unsigned)identifiers[index],
                                    (int)name.size, name.data);
        } else {
            formatted = format_path(line, sizeof(line), "0x%08X (unnamed)\n",
                                    (unsigned)identifiers[index]);
        }
        if (!formatted) {
            return false;
        }
        accepted = binrwir_format_buffer_sink(
            &sink_context, runtimec_string_view_from_c_string(line));
        if (accepted.size != strlen(line)) {
            return false;
        }
    }
    return true;
}

/*
 * 语料里调用的外部能力先声明进 `/lib`，否则 layout 的「未定义裸名」检查会把它们
 * 当成拼错的函数拒掉——那条规则是对的，不能为 dump 放宽。
 *
 * 这里只声明，不实现：编码后它们是 FOREIGN，dump 打印出的 opid 正是 FOREIGN。
 */
static bool declare_capabilities(dump_context_t *context,
                                 const char *data_source,
                                 const char *work_directory, char *stage,
                                 char *reason) {
    static const char declarations[] =
        "rwir println(*[]char/utf32 $s...)\n"
        "rwir print(*[]char/utf32 $s...)\n"
        "rwir printf(*[]char/utf32 $s...)\n"
        "rwir debugger()\n";
    char path[PATH_CAPACITY];
    if (!format_path(path, sizeof(path), "%s/capabilities.kv",
                     work_directory)) {
        copy_text(stage, STAGE_CAPACITY, "layout");
        copy_text(reason, REASON_CAPACITY,
                  "capability declaration path is too long");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        copy_text(stage, STAGE_CAPACITY, "layout");
        copy_text(reason, REASON_CAPACITY,
                  "cannot write capability declaration");
        return false;
    }
    const size_t size = strlen(declarations);
    const bool written = fwrite(declarations, 1U, size, file) == size;
    const bool closed = fclose(file) == 0;
    bool succeeded = written && closed;
    if (succeeded) {
        char entry[NAME_CAPACITY];
        char layout_error[REASON_CAPACITY * 2];
        entry[0] = '\0';
        layout_error[0] = '\0';
        if (context->layout_file(path, data_source, entry,
                                 (uint32_t)sizeof(entry), layout_error,
                                 (uint32_t)sizeof(layout_error)) != 0) {
            succeeded = false;
            copy_text(stage, STAGE_CAPACITY, "layout");
            copy_first_line(reason, REASON_CAPACITY,
                            runtimec_string_view_from_c_string(layout_error));
        }
    } else {
        copy_text(stage, STAGE_CAPACITY, "layout");
        copy_text(reason, REASON_CAPACITY,
                  "cannot write capability declaration");
    }
    (void)remove(path);
    return succeeded;
}

/* 声明的能力没有 `‥ctime`，不是可 dump 的函数；包目录同样被这一条滤掉。 */
static bool function_has_revision(void *handle, const char *function_path) {
    char key[PATH_CAPACITY];
    if (!format_path(key, sizeof(key), "%s/\xE2\x80\xA5" "ctime",
                     function_path)) {
        return false;
    }
    uint8_t *raw = NULL;
    uint32_t raw_size = 0U;
    if (kvspaceGet(handle, key, 1, &raw, &raw_size) != 0) {
        return false;
    }
    return raw != NULL && raw_size != 0U;
}

/* 编码并打印一个函数；失败时写 stage/reason。 */
static bool encode_function(dump_context_t *context,
                            runtimec_kvspace_access_t *services,
                            const char *function_path,
                            runtimec_bytes_buffer_t *buffer, char *stage,
                            char *reason) {
    runtimec_error_t error;
    runtimec_error_reset(&error);
    vmkv_encoder_t *encoder = NULL;
    if (vmkv_encoder_create(context->allocator, &encoder, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        copy_text(stage, STAGE_CAPACITY, "encode");
        copy_first_line(reason, REASON_CAPACITY, error.message);
        return false;
    }
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key = runtimec_key_view_from_c_string(function_path);
    snapshot.source_position_text =
        runtimec_string_view_from_c_string("[1,0]");
    runtimec_status_t status = runtimec_kvspace_access_source_revision(
        services, snapshot.function_key, &snapshot.source_revision, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        copy_text(stage, STAGE_CAPACITY, "encode");
        copy_first_line(reason, REASON_CAPACITY, error.message);
        (void)vmkv_encoder_destroy(encoder);
        return false;
    }
    const vmkv_encode_request_t request = {
        .kvspace_access = services,
        .function_path = runtimec_string_view_from_c_string(function_path),
        .expected_snapshot = snapshot,
    };
    binrwir_program_t *program = NULL;
    status = vmkv_encode(encoder, &request, &program, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        copy_text(stage, STAGE_CAPACITY, "encode");
        copy_first_line(reason, REASON_CAPACITY, error.message);
        (void)vmkv_encoder_destroy(encoder);
        return false;
    }
    binrwir_format_buffer_sink_context_t sink_context = {
        .allocator = context->allocator,
        .buffer = buffer,
    };
    const runtimec_string_view_t function_name =
        runtimec_string_view_make(function_path, strlen(function_path));
    /* 解析器传 NULL：名字一解析成功，格式化就只打名字、不打 opid。 */
    status = binrwir_program_format(program, function_name, NULL, NULL,
                                    binrwir_format_buffer_sink,
                                    &sink_context);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        copy_text(stage, STAGE_CAPACITY, "print");
        copy_text(reason, REASON_CAPACITY, "binrwir_program_format failed");
        (void)binrwir_program_destroy(program);
        (void)vmkv_encoder_destroy(encoder);
        return false;
    }
    const bool legend = append_operation_legend(context, program, buffer);
    (void)binrwir_program_destroy(program);
    (void)vmkv_encoder_destroy(encoder);
    if (!legend) {
        copy_text(stage, STAGE_CAPACITY, "print");
        copy_text(reason, REASON_CAPACITY, "operation legend failed");
        return false;
    }
    return true;
}

static bool write_bytes(const char *path, const runtimec_bytes_buffer_t *buffer) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t written =
        buffer->size == 0U
            ? 0U
            : fwrite(buffer->data, 1U, buffer->size, file);
    const bool closed = fclose(file) == 0;
    return written == buffer->size && closed;
}

/*
 * 处理一个源文件。成功时把内容写进 output_path；失败写 stage/reason 并返回
 * false——但**仍写已得到的部分**由调用方决定，这里只负责报告。
 */
static bool dump_one_file(dump_context_t *context, const char *source_path,
                          const char *output_path, const char *work_directory,
                          char *stage, char *reason) {
    if (!make_directory(work_directory)) {
        copy_text(stage, STAGE_CAPACITY, "write");
        copy_text(reason, REASON_CAPACITY, "cannot create temporary directory");
        return false;
    }
    char data_source[PATH_CAPACITY];
    if (!format_path(data_source, sizeof(data_source), "shm://%s/store.shm",
                     work_directory)) {
        copy_text(stage, STAGE_CAPACITY, "write");
        copy_text(reason, REASON_CAPACITY, "temporary store path is too long");
        remove_tree(work_directory);
        return false;
    }
    void *handle = kvspaceConnect(data_source);
    if (handle == NULL) {
        copy_text(stage, STAGE_CAPACITY, "layout");
        copy_text(reason, REASON_CAPACITY, "cannot connect temporary store");
        remove_tree(work_directory);
        return false;
    }
    if (!declare_capabilities(context, data_source, work_directory, stage,
                              reason)) {
        kvspaceClose(handle);
        remove_tree(work_directory);
        return false;
    }
    char entry[NAME_CAPACITY];
    char layout_error[REASON_CAPACITY * 2];
    entry[0] = '\0';
    layout_error[0] = '\0';
    const int layout_status =
        context->layout_file(source_path, data_source, entry,
                             (uint32_t)sizeof(entry), layout_error,
                             (uint32_t)sizeof(layout_error));
    if (layout_status != 0) {
        copy_text(stage, STAGE_CAPACITY, "layout");
        copy_first_line(reason, REASON_CAPACITY,
                        runtimec_string_view_from_c_string(layout_error));
        kvspaceClose(handle);
        remove_tree(work_directory);
        return false;
    }
    char(*names)[NAME_CAPACITY] = calloc(MAX_FUNCTIONS, NAME_CAPACITY);
    if (names == NULL) {
        copy_text(stage, STAGE_CAPACITY, "encode");
        copy_text(reason, REASON_CAPACITY, "out of memory");
        kvspaceClose(handle);
        remove_tree(work_directory);
        return false;
    }
    const int function_count = list_functions(handle, names);
    if (function_count < 0) {
        copy_text(stage, STAGE_CAPACITY, "list");
        copy_text(reason, REASON_CAPACITY, "cannot enumerate /lib");
        free(names);
        kvspaceClose(handle);
        remove_tree(work_directory);
        return false;
    }
    const runtimec_kvspace_access_request_t adapter_request = {
        .allocator = context->allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_kvspace_access_t *services = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_create(&adapter_request, &services, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        copy_text(stage, STAGE_CAPACITY, "encode");
        copy_first_line(reason, REASON_CAPACITY, error.message);
        free(names);
        kvspaceClose(handle);
        remove_tree(work_directory);
        return false;
    }
    runtimec_bytes_buffer_t buffer = {NULL, 0U, 0U};
    bool succeeded = true;
    for (int ordinal = 0; ordinal < function_count; ++ordinal) {
        char function_path[PATH_CAPACITY];
        if (!format_path(function_path, sizeof(function_path), "/lib/%s",
                         names[ordinal])) {
            copy_text(stage, STAGE_CAPACITY, "encode");
            copy_text(reason, REASON_CAPACITY, "function path is too long");
            succeeded = false;
            break;
        }
        if (!function_has_revision(handle, function_path)) {
            continue;
        }
        if (!encode_function(context, services, function_path,
                             &buffer, stage, reason)) {
            succeeded = false;
            break;
        }
    }
    if (succeeded) {
        char parent[PATH_CAPACITY];
        copy_text(parent, sizeof(parent), output_path);
        char *separator = strrchr(parent, '/');
        if (separator != NULL) {
            *separator = '\0';
            if (!make_directory(parent)) {
                succeeded = false;
                copy_text(stage, STAGE_CAPACITY, "write");
                copy_text(reason, REASON_CAPACITY,
                          "cannot create output directory");
            }
        }
    }
    if (succeeded && !write_bytes(output_path, &buffer)) {
        succeeded = false;
        copy_text(stage, STAGE_CAPACITY, "write");
        copy_text(reason, REASON_CAPACITY, "cannot write output file");
    }
    if (buffer.data != NULL) {
        runtimec_allocator_deallocate(&context->allocator, buffer.data,
                                      buffer.capacity, 1U);
    }
    (void)runtimec_kvspace_access_destroy(services, &error);
    free(names);
    kvspaceClose(handle);
    remove_tree(work_directory);
    return succeeded;
}

/* 递归遍历语料；相对路径随目录层级传递。 */
static void dump_directory(dump_context_t *context, const char *corpus_root,
                           const char *output_root, const char *relative,
                           size_t *total, size_t *succeeded,
                           report_t *report) {
    char directory_path[PATH_CAPACITY];
    if (relative[0] == '\0') {
        copy_text(directory_path, sizeof(directory_path), corpus_root);
    } else if (!format_path(directory_path, sizeof(directory_path), "%s/%s",
                            corpus_root, relative)) {
        report_add(report, relative, "walk", "directory path is too long");
        *total += 1U;
        return;
    }
    DIR *directory = opendir(directory_path);
    if (directory == NULL) {
        report_add(report, relative, "walk", "cannot open directory");
        *total += 1U;
        return;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child_relative[PATH_CAPACITY];
        if (relative[0] == '\0') {
            copy_text(child_relative, sizeof(child_relative), entry->d_name);
        } else if (!format_path(child_relative, sizeof(child_relative),
                                "%s/%s", relative, entry->d_name)) {
            report_add(report, relative, "walk", "entry path is too long");
            continue;
        }
        struct stat information;
        char child_path[PATH_CAPACITY];
        if (!format_path(child_path, sizeof(child_path), "%s/%s", corpus_root,
                         child_relative)) {
            *total += 1U;
            report_add(report, child_relative, "walk", "source path is too long");
            continue;
        }
        if (stat(child_path, &information) != 0) {
            continue;
        }
        if (S_ISDIR(information.st_mode)) {
            dump_directory(context, corpus_root, output_root, child_relative,
                           total, succeeded, report);
            continue;
        }
        if (!ends_with_kv(child_relative)) {
            continue;
        }
        *total += 1U;
        char output_path[PATH_CAPACITY];
        if (!format_path(output_path, sizeof(output_path), "%s/%s.binkv",
                         output_root, child_relative)) {
            report_add(report, child_relative, "write",
                       "output path is too long");
            continue;
        }
        char work_directory[PATH_CAPACITY];
        if (!format_path(work_directory, sizeof(work_directory), "%s/.work/%zu",
                         output_root, *total)) {
            report_add(report, child_relative, "write",
                       "temporary path is too long");
            continue;
        }
        char stage[STAGE_CAPACITY];
        char reason[REASON_CAPACITY];
        stage[0] = '\0';
        reason[0] = '\0';
        if (dump_one_file(context, child_path, output_path, work_directory,
                          stage, reason)) {
            *succeeded += 1U;
        } else {
            report_add(report, child_relative, stage, reason);
        }
    }
    closedir(directory);
}

/* 把连续的单引号段抹成 `'…'`：局部变量名不同但根因相同，分组要按根因。 */
static void root_cause_key(const failure_t *item, char *output,
                           size_t capacity) {
    size_t written = 0U;
    bool inside_quotes = false;
    const char *text = item->reason;
    while (*text != '\0' && written + 1U < capacity) {
        if (*text == '\'') {
            if (!inside_quotes) {
                const char *quote = "'…'";
                for (const char *p = quote; *p != '\0' && written + 1U < capacity;
                     ++p) {
                    output[written++] = *p;
                }
                inside_quotes = true;
            } else {
                inside_quotes = false;
            }
            ++text;
            continue;
        }
        if (inside_quotes) {
            ++text;
            continue;
        }
        output[written++] = *text++;
    }
    output[written] = '\0';
}

typedef struct root_cause_count {
    char key[REASON_CAPACITY];
    size_t count;
} root_cause_count_t;

static bool write_failures(const char *output_root, const report_t *report,
                           size_t total, size_t succeeded) {
    char path[PATH_CAPACITY];
    if (!format_path(path, sizeof(path), "%s/FAILURES.md", output_root)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "# binrwir-dump 失败清单\n\n");
    fprintf(file, "共 %zu：成功 %zu / 失败 %zu\n\n", total, succeeded,
            total - succeeded);
    if (report->count == 0U) {
        fprintf(file, "无失败。\n");
        fclose(file);
        return true;
    }
    fprintf(file, "| 文件 | 阶段 | 首行原因 |\n|---|---|---|\n");
    for (size_t index = 0U; index < report->count; ++index) {
        const failure_t *item = &report->items[index];
        fprintf(file, "| `%s` | %s | %s |\n", item->source, item->stage,
                item->reason);
    }
    /* 按根因分组：局部名不同、根因相同，逐行看会以为失败很多种。 */
    root_cause_count_t *groups =
        calloc(report->count, sizeof(root_cause_count_t));
    size_t group_count = 0U;
    if (groups != NULL) {
        for (size_t index = 0U; index < report->count; ++index) {
            char key[REASON_CAPACITY];
            root_cause_key(&report->items[index], key, sizeof(key));
            size_t position = 0U;
            for (; position < group_count; ++position) {
                if (strcmp(groups[position].key, key) == 0) {
                    break;
                }
            }
            if (position == group_count) {
                copy_text(groups[position].key, sizeof(groups[position].key),
                          key);
                group_count += 1U;
            }
            groups[position].count += 1U;
        }
        /* 计数降序；同数按文本排序，保证两次运行同一输出。 */
        for (size_t left = 1U; left < group_count; ++left) {
            root_cause_count_t value = groups[left];
            size_t position = left;
            while (position > 0U &&
                   (groups[position - 1U].count < value.count ||
                    (groups[position - 1U].count == value.count &&
                     strcmp(groups[position - 1U].key, value.key) > 0))) {
                groups[position] = groups[position - 1U];
                --position;
            }
            groups[position] = value;
        }
        fprintf(file, "\n## 按根因分组\n\n| 条数 | 根因 |\n|---|---|\n");
        for (size_t index = 0U; index < group_count; ++index) {
            fprintf(file, "| %zu | %s |\n", groups[index].count,
                    groups[index].key);
        }
        free(groups);
    }
    fclose(file);
    return true;
}

int main(int argument_count, char **arguments) {
    if (argument_count != 5) {
        fprintf(stderr,
                "usage: %s <corpus-root> <output-root> <layout-library> "
                "<kvspace-library>\n",
                arguments[0]);
        return 2;
    }
    const char *corpus_root = arguments[1];
    const char *output_root = arguments[2];
    const char *layout_path = arguments[3];
    const char *kvspace_path = arguments[4];
    dump_context_t context;
    memset(&context, 0, sizeof(context));
    context.allocator = dump_allocator();
    void *layout_library = dlopen(layout_path, RTLD_NOW | RTLD_LOCAL);
    if (layout_library == NULL) {
        fprintf(stderr, "cannot load layout %s: %s\n", layout_path, dlerror());
        return 2;
    }
    context.layout_file = (layout_file_function_t)(uintptr_t)dlsym(
        layout_library, "kvlangLayoutFile");
    if (context.layout_file == NULL) {
        fprintf(stderr, "layout library has no kvlangLayoutFile\n");
        return 2;
    }
    /* kvspace 是链接期依赖：不再 dlopen、不再解析符号。第 5 个实参保留位置
     * 但不使用——命令行形态不变，免得脚本与文档同时改。 */
    (void)kvspace_path;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (!make_directory(output_root)) {
        fprintf(stderr, "cannot create output root %s\n", output_root);
        return 2;
    }
    report_t report = {NULL, 0U, 0U};
    size_t total = 0U;
    size_t succeeded = 0U;
    dump_directory(&context, corpus_root, output_root, "", &total, &succeeded,
                   &report);
    const bool written = write_failures(output_root, &report, total, succeeded);
    printf("共 %zu：成功 %zu / 失败 %zu\n", total, succeeded,
           total - succeeded);
    if (!written) {
        fprintf(stderr, "cannot write FAILURES.md under %s\n", output_root);
    }
    free(report.items);
    dlclose(layout_library);
    return (report.count == 0U && written) ? 0 : 1;
}
