/*
 * 组件：tools/kvlangrun —— runtimec 的命令行驱动
 *
 * 作用：跑一个 .kv 文件的入口函数，把**被跑程序自己的输出原样送到 stdout**。
 *       它是 benchmark 的 kvlang 列所调用的那个二进制（`benchmark/run.py` 的
 *       `--kvlang-bin`）。
 *
 * 用法：
 *     KVSPACE=<dsn> kvlangrun <file.kv> [--no-stdlib]
 *
 * ── 三个约定，都是对 `benchmark/run.py` 的既有行为 ──────────────────
 *
 * 1. **DSN 只从环境变量 `KVSPACE` 读**，不读默认 6379。run.py 每次采样前重置
 *    该后端并设好这个变量（`run.py:177`），驱动跟着它走，不自己编一个地址。
 *
 * 2. **stdout 原样透传**。被跑程序的 `println` 由 runtimec 的打印层直接写进程
 *    stdout；本驱动**不重定向、不包装、不加前缀**——run.py 用
 *    `__bench_ns:\s*(\d+)` 与 `__bench_input:` 逐行解析，任何包装都会让那些行
 *    匹配不上。驱动自己的诊断一律走 **stderr**。
 *
 * 3. **`--no-stdlib` 接受但当前无作用**。runtimec 这条链**不装载 stdlib**
 *    （`kv_runner.c` 的同一裁决：`09-debugger` 依赖的 `debugger` 由 byteseek 的
 *    stdlib 提供，本链不装）。所以这个开关今天不改变任何行为。
 *    **若将来本链开始装载 stdlib，这个开关必须变成真开关**——否则它就是个
 *    看起来实现了、实际没有的参数。
 *
 * ── 为什么用内部 execution_window API 而不是公共接口 ────────────────
 *
 * `interface/public_interface.c` 明确拒绝 CALL/HANDOFF/YIELD：
 *
 *     if (window_result.execution_exit.kind != BINRWIR_EXECUTION_EXIT_DONE) {
 *         return kvlang_runtime_fail(error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
 *             "execution stopped before DONE: this entry does not drive CALL / "
 *             "HANDOFF / YIELD yet");
 *     }
 *
 * 而 benchmark 的用例几乎都有函数调用（`fib` 本身就是递归）。所以本驱动**必须**
 * 自带调用帧推进——那套逻辑在 `engine/frame_driver`，与语料运行器共用同一份实现。
 *
 * ── 退出码 ─────────────────────────────────────────────────────────
 *
 *   0  跑到 DONE
 *   1  执行失败（诊断已写 stderr，首行是可定位的那句）
 *   2  用法错误 / 环境缺失（没有 KVSPACE、文件不存在、layout 不收）
 *
 * 失败时**必须**把首行写到 stderr：run.py 在非零退出时取
 * `r.stderr.strip().splitlines()[-1]` 作为失败原因，只写一个状态码等于没报。
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include <kvspace/kvspace.h>

#include "contract/kvspace_access.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include "engine/frame_driver.h"

/* 入口函数名固定为 `test`（与 tutorial、benchmark 的约定一致）。
 * 裸名，不带 `/lib/`：本驱动自己补前缀——一个名字只许一种含义。 */
#define KVLANG_RUN_ENTRY_NAME "test"
#define KVLANG_RUN_ENTRY_PATH "/lib/" KVLANG_RUN_ENTRY_NAME

#define KVLANG_RUN_VTHREAD "1"
#define KVLANG_RUN_ROOT_DEPTH 1
#define KVLANG_RUN_ROOT_SEGMENT "1"
#define KVLANG_RUN_ROOT_SOURCE_PC 1U

/* layout 的 C ABI 签名（`layout/src/capi.rs:77`）。经 dlopen 取，不链接——
 * layout 是可选构建产物，与 runtimec 的构建解耦。 */
typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry_out, uint32_t entry_capacity,
                                      char *error_out, uint32_t error_capacity);

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
    /*
     * 未指定时按「本二进制所在目录的上级 lib/」推断：repo 布局是
     * `bin/kvlangrun` + `lib/libkvlanglayout.so`。仍然找不到就失败并把
     * 试过的路径写进 output_path，让调用方看得见找了哪。
     */
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

int main(int argument_count, char **arguments) {
    const char *file_path = NULL;
    int index;

    for (index = 1; index < argument_count; ++index) {
        if (strcmp(arguments[index], "--no-stdlib") == 0) {
            /* 见文件头第 3 条：runtimec 链不装载 stdlib，本开关今天不改变行为。 */
            continue;
        }
        if (file_path == NULL) {
            file_path = arguments[index];
            continue;
        }
        return usage_failure("unexpected extra argument (usage: kvlangrun <file.kv> [--no-stdlib])");
    }
    if (file_path == NULL) {
        return usage_failure("missing <file.kv> (usage: kvlangrun <file.kv> [--no-stdlib])");
    }
    const char *dsn = getenv("KVSPACE");
    if (dsn == NULL || dsn[0] == '\0') {
        /* **不读默认 6379**：默认值会把「忘了设」变成「连到了另一个后端」，
         * 而那个后端的数字会被当成这个后端的。 */
        return usage_failure("KVSPACE is not set (it must name the data source to use)");
    }

    /* ── 1. layout 该文件 ─────────────────────────────────────────── */
    char layout_path[4096];
    void *layout_library = open_layout_library(layout_path, sizeof(layout_path));
    if (layout_library == NULL) {
        fprintf(stderr, "kvlangrun: cannot load layout library: %s\n",
                dlerror() != NULL ? dlerror() : layout_path);
        return 2;
    }
    layout_file_function_t layout_file = (layout_file_function_t)(uintptr_t)
        dlsym(layout_library, "kvlangLayoutFile");
    if (layout_file == NULL) {
        fprintf(stderr, "kvlangrun: layout library has no kvlangLayoutFile\n");
        return 2;
    }
    /*
     * 先声明外部能力，否则**一个 benchmark 用例都跑不了**：用例里写
     * `println(...)`，而 layout 的 `check_call_names` 会把未定义的裸名当拼错的
     * 函数拒掉（那条规则是对的，不能为驱动放宽）。
     *
     * 真实部署里这些名字由扩展在装载期声明；这里用 layout 自己的 `rwir` 声明
     * 语法走同一条路径，**不手工拼 kvspace 字节**。变参标记 `...` 跟在类型后，
     * 那样 `println("a", 1)` 的多实参才过得了类型检查。
     *
     * 声明成功后删掉临时文件：它只是给 layout 读的输入，不是产物。
     */
    {
        char declarations_path[256];
        snprintf(declarations_path, sizeof(declarations_path),
                 "/tmp/kvlangrun-capabilities-%d.kv", (int)getpid());
        static const char declarations[] =
            "rwir println(*[]char/utf32 $s...)\n"
            "rwir print(*[]char/utf32 $s...)\n";
        FILE *declaration_file = fopen(declarations_path, "wb");
        if (declaration_file == NULL) {
            fprintf(stderr, "kvlangrun: cannot write capability declarations\n");
            return 2;
        }
        const size_t declaration_size = strlen(declarations);
        const bool written = fwrite(declarations, 1U, declaration_size,
                                    declaration_file) == declaration_size;
        fclose(declaration_file);
        if (!written) {
            (void)remove(declarations_path);
            fprintf(stderr, "kvlangrun: cannot write capability declarations\n");
            return 2;
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
            return 2;
        }
    }
    {
        char entry[256];
        char layout_error[2048];
        memset(entry, 0, sizeof(entry));
        memset(layout_error, 0, sizeof(layout_error));
        if (layout_file(file_path, dsn, entry, sizeof(entry), layout_error,
                        sizeof(layout_error)) != 0) {
            fprintf(stderr, "kvlangrun: layout failed: %s\n",
                    layout_error[0] != '\0' ? layout_error : "(no message)");
            return 2;
        }
    }

    /* ── 2. 建 store/session/驱动 ─────────────────────────────────── */
    void *kvspace_handle = kvspaceConnect(dsn);
    if (kvspace_handle == NULL) {
        fprintf(stderr, "kvlangrun: cannot connect %s\n", dsn);
        return 2;
    }
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = program_allocate,
        .deallocate = program_deallocate,
    };
    const runtimec_kvspace_access_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_kvspace_access_t *kvspace_access = NULL;
    runtimec_error_t error;
    runtimec_error_reset(&error);
    if (runtimec_kvspace_access_create(&adapter_request, &kvspace_access,
                                       &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: kvspace adapter create failed: %s\n",
                error.message.data != NULL ? error.message.data : "(no message)");
        return 2;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = (uint32_t)sizeof(options);
    options.allocator = allocator;
    options.kvspace_handle = kvspace_handle;
    options.compilation_cache_capacity = 64U;
    /* 不开 JIT 时机器码容量**必须为 0**：session 把两者不一致判为非法请求。 */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    if (runtimec_session_create(kvspace_access, &options, &session, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: session create failed: %s\n",
                error.message.data != NULL ? error.message.data : "(no message)");
        return 2;
    }
    frame_driver_t driver;
    if (frame_driver_initialize(&driver, session) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "kvlangrun: frame driver initialize failed\n");
        return 2;
    }

    /* ── 3. 执行入口 ──────────────────────────────────────────────── */
    /*
     * **不重定向 stdout**：被跑程序的 println 直接落到进程 stdout，这正是
     * run.py 解析 `__bench_ns:` / `__bench_input:` 所需。任何缓冲都会破坏时序。
     */
    binrwir_execution_exit_t exit_state;
    memset(&exit_state, 0, sizeof(exit_state));
    char driver_message[512];
    driver_message[0] = '\0';
    const frame_driver_outcome_t outcome = frame_driver_execute(
        &driver, KVLANG_RUN_ENTRY_PATH, KVLANG_RUN_ROOT_SOURCE_PC,
        KVLANG_RUN_ROOT_DEPTH, KVLANG_RUN_ROOT_SEGMENT, KVLANG_RUN_VTHREAD,
        NULL, 0U, &exit_state, driver_message, sizeof(driver_message));
    fflush(stdout);

    int result = 0;
    switch (outcome) {
    case FRAME_DRIVER_OUTCOME_DONE:
        result = 0;
        break;
    case FRAME_DRIVER_OUTCOME_EXECUTE_FAILED:
        /* status 与 exit 的 pc/detail 才是能定位的部分；message 常为空串。 */
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
        /* HANDOFF 是「交给外部 rwir」，不是失败也不是 DONE。benchmark 用例
         * 不该停在它上面；停了说明这条链还缺那个能力，如实报不装成功。 */
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
    case FRAME_DRIVER_OUTCOME_INVALID:
    default:
        fprintf(stderr, "kvlangrun: execution outcome is invalid\n");
        result = 1;
        break;
    }
    return result;
}
