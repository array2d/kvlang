/*
 * 组件：cli/kvlangrun —— runtimec 的命令行驱动
 *
 * 子命令：**vthread 的创建与运行分离**（对照 runtime/ 的
 * `vthread·create(funckey) -> vid` 与 `vthread·run(vid)`）。
 *
 *     KVSPACE=<dsn> kvlangrun create <file.kv> [--no-stdlib]
 *         layout 文件，并创建一个 vthread：帧 `/vthread/<vid>/[1]` 写
 *         `‥lib = /lib/test`，`‥pc = [1, 1]`，`‥status = "init"`。
 *         **不执行任何指令**。stdout 只打印 vid（一行）。
 *
 *     KVSPACE=<dsn> kvlangrun run <vid>
 *         读该 vid 在 kvspace 里持久化的 `‥pc`（`[stackid, s0]`），从该位置
 *         继续执行到结束。**resume 复用旧 vid**：不新建 vthread、不重写入口
 *         位置。`‥status` 非 init/running/wait 时不执行（paused 等待外部恢复，
 *         done/error 是终态）。
 *
 * 为什么按 vid 切开：状态全在 kvspace，create 与 run 之间进程可以退出。这正是
 * crash/stop 恢复需要的形状——崩溃后对同一个 vid 再跑一次 run 即可。
 *
 * ── 三个对调用方的约定 ───────────────────────────────────────────────
 *
 * 1. **DSN 只从 `KVSPACE` 读**，不读默认 6379。DSN 决定状态落在哪个后端；
 *    默认值会把「忘了设」变成「连到了另一个后端」。
 * 2. **run 的 stdout 原样透传**被跑程序的 `println`；驱动自己的诊断一律走
 *    stderr。create 的 stdout 只有 vid。
 * 3. **`--no-stdlib` 接受但当前无作用**：runtimec 这条链不装载 stdlib。
 *
 * ── 退出码 ──────────────────────────────────────────────────────────
 *
 *   0  跑到 DONE，或 vthread 已处于终态 / paused（没有可执行的工作）
 *   1  执行失败（诊断写 stderr，首行可定位）
 *   2  用法错误 / 环境缺失（没有 KVSPACE、文件不存在、layout 不收）
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "contract/checkpoint.h"
#include "contract/kvspace_access.h"
#include "engine/execution_window.h"
#include "engine/frame_driver.h"
#include "engine/session.h"
#include "vmbin/vthread_status.h"

/* 入口函数名固定为 `test`（与 tutorial、benchmark 的约定一致）。
 * 裸名，不带 `/lib/`：本驱动自己补前缀——一个名字只许一种含义。 */
#define KVLANG_RUN_ENTRY_NAME "test"
#define KVLANG_RUN_ENTRY_PATH "/lib/" KVLANG_RUN_ENTRY_NAME

/* 根帧：帧号 1，入口 source pc 1。 */
#define KVLANG_RUN_ROOT_STACK_IDENTIFIER 1
#define KVLANG_RUN_ROOT_SEGMENT "1"
#define KVLANG_RUN_ROOT_SOURCE_PC 1

/* vthread 序号槽：`/vthread/‥seq`。与 runtime/ 的分配点同形。 */
static const char kvlangrun_sequence_key[] = "/vthread/\xE2\x80\xA5" "seq";

/* layout 的 C ABI 签名（`layout/src/capi.rs:77`）。经 dlopen 取，不链接——
 * layout 是可选构建产物，与 runtimec 的构建解耦。 */
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

/* 用法/环境错误：写 stderr 并返回退出码 2。 */
static int usage_failure(const char *message) {
    fprintf(stderr, "kvlangrun: %s\n", message);
    return 2;
}

/*
 * 定位 layout 库。
 *
 * 环境变量 `KVLANG_LAYOUT_LIBRARY` 可以显式指定；否则按「与本二进制同目录」
 * 找 `libkvlanglayout.so` / `.dylib`——repo 的两级布局就是 bin/ 与 lib/，
 * 驱动自己的路径推不出来源，所以由调用方给（run.py 会通过环境变量传）。
 */
static void *open_layout_library(char *output_path, size_t output_capacity) {
    const char *explicit_path = getenv("KVLANG_LAYOUT_LIBRARY");

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
                /* 显式判返回值：路径被截断时 dlopen 会去一个**错误的路径**上
                 * 找，失败信息指向不存在的目录，比「没找到」更难查。 */
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
             "libkvlanglayout.so (set KVLANG_LAYOUT_LIBRARY)");
    return NULL;
}

/*
 * 声明外部能力并 layout 用户的 .kv 文件。
 *
 * 先声明 `println`/`print`：用例里写 `println(...)`，layout 的 `check_call_names`
 * 会把未定义的裸名当拼错的函数拒掉（那条规则不能为驱动放宽）。真实部署里这些
 * 名字由扩展在装载期声明；这里用 layout 自己的 `rwir` 声明语法走同一条路径，
 * 不手工拼 kvspace 字节。临时文件只是 layout 的输入，不是产物。
 */
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
    static const char declarations[] =
        "rwir println(*[]char/utf32 $s...)\n"
        "rwir print(*[]char/utf32 $s...)\n";
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

/*
 * 分配 vid：读 `/vthread/‥seq`，加一，写回；返回十进制文本。
 *
 * 分配是**写**：必须先成功写回再返回 vid，否则两个 create 会拿到同一个号。
 * 序号槽的值是文本 XValue（`[]char/utf8`），与 runtime/ 的分配点同形。
 */
static bool allocate_vthread_identifier(void *kvspace_handle, char *output,
                                        size_t output_capacity) {
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    long long sequence = 0;
    if (kvspaceGet(kvspace_handle, kvlangrun_sequence_key, 1, &wire,
                   &wire_size) == 0 &&
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
    if (kvspaceXvalueHeadNewSlack((const uint8_t *)text,
                                  (uint64_t)text_length, "[]char/utf8",
                                  &new_wire, &new_wire_size) != 0 ||
        new_wire == NULL || new_wire_size > UINT32_MAX) {
        free(new_wire);
        return false;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int set_status = kvspaceSetValue(
        kvspace_handle, kvlangrun_sequence_key, new_wire,
        (uint32_t)new_wire_size, error_text, (uint32_t)sizeof(error_text));
    free(new_wire);
    if (set_status != 0) {
        fprintf(stderr, "kvlangrun: cannot allocate a vthread identifier: %s\n",
                error_text[0] != '\0' ? error_text : "(no message)");
        return false;
    }
    snprintf(output, output_capacity, "%s", text);
    return true;
}

/* create：layout + 建 vthread 状态，不执行。 */
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

/*
 * 读 `/vthread/<vid>/‥status` 决定这一趟要不要执行。
 *
 *   1  继续（init / running / wait；键缺失按 init）
 *   0  没有可执行的工作（paused 等待外部恢复；done 或其它终态串）
 *  -1  终态是 error
 *
 * 键缺失不是错误：`create` 与 checkpoint 之间进程可以退出，此时 pc 已在、
 * status 未写，按 init 处理。坏槽同样按继续——解释器的状态门也是这个口径，
 * 两处必须一致。
 */
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

/*
 * 把 `‥pc` 的 stackid 定位到帧目录。
 *
 * 根帧固定 `/vthread/<vid>/[1]`。子帧目录的段是 `<depth>.<call_site_pc>`
 * （`frame_driver.c` 的帧身份来自调用链），而 `‥pc` 只带 stackid——所以按
 * 「`[<stackid>]` 或 `[<stackid>.`」扫一层，**必须恰好一个**。多个候选说明
 * 同一深度有两个历史帧目录，`‥pc` 不足以区分，这时失败而不是猜一个。
 */
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
    int32_t child_count = 0;
    if (kvspaceListLen(kvspace_handle, prefix, 0, 1, &child_count) != 0) {
        return false;
    }
    char found[128];
    size_t found_length = 0U;
    size_t matches = 0U;
    for (int32_t index = 0; index < child_count; ++index) {
        char name[128];
        uint32_t name_length = 0U;
        if (kvspaceListAt(kvspace_handle, prefix, 0, 1, index,
                          (uint8_t *)name, (uint32_t)sizeof(name),
                          &name_length) != 0 ||
            name_length == 0U || name_length >= sizeof(name) ||
            name[0] != '[') {
            continue;
        }
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

/* run：从旧 vid 的 `‥pc` 继续执行。 */
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
        /* 迁移哨兵（s0 < 0）与无帧位置都不是可执行坐标：读方必须拒绝，
         * 不照着执行。 */
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
    /* 不开 JIT 时机器码容量**必须为 0**：session 把两者不一致判为非法请求。 */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
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

    /*
     * **不重定向 stdout**：被跑程序的 println 直接落到进程 stdout，这正是
     * run.py 解析 `__bench_ns:` / `__bench_input:` 所需。
     */
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
            /* 从子帧返回后要回到调用方，但那一步的坐标只在内存里传过，
             * 没有 `‥returnpc` 落盘（spec 05/01 要求）。这里报出来，不当成功。 */
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
        /* 外部把 `‥status` 写成 paused：停在当前指令之前，位置已落盘。
         * 不是失败；外部恢复后对同一个 vid 再跑 run 即可。 */
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
                "usage: kvlangrun create <file.kv> [--no-stdlib]\n"
                "       kvlangrun run <vid>\n");
        return 2;
    }
    const char *command = arguments[1];
    if (strcmp(command, "create") == 0) {
        const char *file_path = NULL;
        for (int index = 2; index < argument_count; ++index) {
            if (strcmp(arguments[index], "--no-stdlib") == 0) {
                /* runtimec 链不装载 stdlib，本开关今天不改变行为。 */
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
