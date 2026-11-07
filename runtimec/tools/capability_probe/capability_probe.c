/*
 * 组件：tools/capability_probe
 * 作用：给一条能力行跑一次执行，输出 TSV 三列事实：exit.kind / detail_code / 首行输出。
 *
 * **它是分类器，不是测试**：没有期望值、不判通过失败。判定口径在
 * `coding1/builtin-registry-contract.md`（DONE=已实现；HANDOFF+UNSUPPORTED=未实现；
 * ERROR+detail_code!=0=实现了但错了）。把分类写进本工具，等于把「当前状态」固化成
 * 断言——明天某行实现了，工具开始说谎。
 *
 * 输出一行 TSV：`<行标识>\t<exit.kind>\t<detail_code>\t<首行输出>`
 *
 * 用法：
 *   capability_probe <dsn> <行标识> [源码文件] \
 *       [--kvspace <lib>] [--layout <lib>]
 * 库路径也可由 RUNTIMEC_KVSPACE_LIBRARY / KVLANG_LAYOUT_LIBRARY 给出。
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "contract/kvspace_access.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include <kvspace/kvspace.h>

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

/* exit.kind 的数值 + 名字。名字只服务可读性，不参与判定。 */
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

/* 读一段文件到静态缓冲；调用方给容量。失败返回 false。 */
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

/* 截到首个换行为止：TSV 一行不能含换行。 */
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
    const char *kvspace_library = getenv("RUNTIMEC_KVSPACE_LIBRARY");
    const char *layout_library = getenv("KVLANG_LAYOUT_LIBRARY");
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
                "[--kvspace <lib>] [--layout <lib>]\n");
        return 2;
    }
    if (kvspace_library == NULL || layout_library == NULL) {
        fprintf(stderr, "missing library path (--kvspace/--layout or env)\n");
        return 2;
    }

    /*
     * 源码：给了文件就用它。
     *
     * **内置模板只覆盖「能直接写进 kv 源码的算子」**——`copy` / `call` / `goto`
     * 这类是执行器内部语义名，源码里不存在对应写法（`copy` 就是 `=`、`call` 是
     * 函数调用、`goto` 是块跳转）。给它们拼 `copy -> *r` 必然 layout 或编码失败，
     * 而那个失败**不是能力行状态**，把两者混在一起工具就说谎了。
     *
     * 所以：不给源码且算子在模板白名单外时，直接报「需要源码」，不猜。
     */
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
            /* 能直接在源码里写的：二元算术、字面量赋值、块内跳转。 */
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
        /*
         * 模板**不带形参**：探针只回答「这条能力行能不能跑」，带形参就得同时
         * 提供实参与帧槽，那是另一件事（也未实现），会把「能力未实现」和
         * 「形参没绑上」混成一个失败。写槽用局部符号，由执行窗口物化。
         */
        snprintf(template_buffer, sizeof(template_buffer),
                 "rwfunc probe() -> () {\n\tx:int64 = 0\n\t%s\n}\n", body);
    }
    const char *source =
        source_path != NULL ? source_buffer : template_buffer;

    /* ── 段 1：装载 layout（kvspace 已是链接期依赖）──────────────────── */
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

    /* ── 段 2：layout 源码进 store ─────────────────────────────────── */
    entry[0] = '\0';
    layout_error[0] = '\0';
    if (layout_code_function(source, dsn, entry, (uint32_t)sizeof(entry),
                             layout_error, (uint32_t)sizeof(layout_error)) !=
        0) {
        /*
         * layout 就失败也是事实：这条能力行连编都编不出来（未注册 / 签名不符）。
         * 照实报，不退出——那是本工具要回答的问题之一。
         */
        printf("%s\tLAYOUT_FAILED\t0\t%s\n", capability, layout_error);
        return 0;
    }

    /* ── 段 3：adapter + session ──────────────────────────────────── */
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
    /*
     * 诊断：直接问一次 source revision，把执行窗口内部那一步的事实摊开。
     * 它失败会让窗口报 detail=3（SNAPSHOT_FAILED）而不带消息，只能在这里看。
     */
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

    /* ── 段 4：执行一次，捕获 stdout 首行 ──────────────────────────── */
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
    /* vid 必须是纯数字：执行窗口按整数解析 vthread 身份。 */
    request.vthread_identifier = runtimec_string_view_from_c_string("2");
    request.frame_path = runtimec_string_view_from_c_string("/vthread/2/[1]");
    request.stack_identifier = 1;
    request.source_logical_program_counter = 1U;
    /* 紧邻窗口调用再问一次：两次结果不同就说明中间有状态变化。 */
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
    /*
     * 准备期失败与能力状态必须分开：本次实测里所有行都因为「读不到 source
     * revision」在编码前失败，那会统一显示成 ERROR —— 读者会以为每条能力都
     * 「实现了但错了」，那是工具在说谎。
     *
     * 判据：ERROR 且 logical_program_counter 为 NONE。真正的执行期 ERROR 一定
     * 带着出错那条指令的位置；准备期失败（快照 / 编码 / 编译）没有位置。
     */
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
