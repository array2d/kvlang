/*
 * 组件：tests/kv_runner
 * 作用：以 kvlang 语法为单位的用例运行器——把 .kv 源码 layout 进 kvspace、
 *       编成 BinRWIR、执行，并产出三态结论。
 *
 * 为什么不是更多 C 用例：C 用例证明「组件按契约工作」，证明不了「一段 kvlang
 * 程序能被正确执行」。ctest 全绿时 layout 写路径一挂就全挂，因为每个组件都在用
 * 自己的替身数据。kv 用例是唯一以语言语法为单位的判据。
 *
 * 三态（decision-kv-test-corpus.md §4）：
 *   pass     输出与期望逐行相同
 *   fail     跑起来了，输出不符或运行时错误
 *   blocked  执行停在 FOREIGN 标记处——runtimec 没有该能力。
 *            **必须打印 文件名 + pc + 算子名**，否则它就退化成「这条没过」。
 *
 * 第四态：**预期 layout 失败**（语法禁令的负向用例）。文件头写
 *   `// 期望错误: <必须出现在 layout 错误里的子串>`
 * 即表示该用例预期 layout 拒绝，而不是预期跑出某段输出。判定：
 *   layout 失败 且 错误文本含该子串 → **PASS**（计入 pass，并单列 expected_failed 计数）
 *   layout 失败 但 子串不命中        → **FAIL**（贴实际错误首行）
 *   layout 成功                      → **FAIL**（「预期失败却通过了」）
 *
 * 两条硬约束：
 *   1. 子串必须非空。空的 `// 期望错误:` 等于「随便报个错就算过」，会把任意语法错误
 *      都算成通过——那是恒真判据，直接判 FAIL。
 *   2. 计数分开报：`pass N（其中预期失败 M）`。否则「预期失败用例 100% 通过」会被读成
 *      「正向用例也全过」，两种 pass 混在一个数里就没法核。
 *
 * 没有 `// 期望错误:` 的文件行为完全不变（原有三态）。正向用例仍必须有
 * `// 期望输出:`，否则 FAIL——没有期望的用例会永远 pass。
 *
 * `println` 替身放在执行循环**外面**，不进 binrwir_registry：能力表是
 * 「runtimec 能执行什么」的事实源，把只存在于测试的处理器写进去，等于让能力表
 * 说一句不成立的话（routing/README.md §四之二：println 属 byteseek）。
 *
 * 用法：kv_runner <corpus-dir> <layout-lib> <kvspace-lib>
 */

/* 必须写在**任何** include 之前：特性宏先于系统头生效，否则 `<stdio.h>` 已经
 * 按严格 ISO 的可见性展开，`mkstemp`/`fileno` 不可见。
 * 本构建是 `-std=c11`（C_EXTENSIONS OFF），两个函数都是 POSIX，需要这一行。 */
#define _POSIX_C_SOURCE 200809L

#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include "host/kvspace_adapter.h"
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

/* 路径段最长 255（NAME_MAX），相对路径最多 4 层 + 分隔符，1024 富余。 */
#define CASE_PATH_CAPACITY 1024

/* layout 库的 C 入口：读 .kv 文件、布局进 dsn 指向的 kvspace。
 * 六参：路径、DSN、入口名出参、入口名容量、错误出参、错误容量。 */
typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry, size_t entry_capacity,
                                      char *error, size_t error_capacity);

/* ── 三态计数 ─────────────────────────────────────────────────────── */

typedef struct runner_counts {
    size_t passed;
    size_t failed;
    size_t blocked;
    /* pass 之中「预期 layout 失败且子串命中」的条数。单列的原因见文件头约束 2。 */
    size_t expected_failed;
    /* pass 之中**真执行且输出逐行相同**的条数。没有它就分不清「编过了」与
     * 「跑对了」——那是两件事。 */
    size_t executed;
    /* fail 的两个细分，处置不同：
     *  expectation_wrong —— 跑完了但输出与期望不符。要先判是期望写错还是实现错，
     *                        **不得**直接改期望值去凑（那是「测试是镜子」）。
     *  incomplete        —— 实际输出比期望多/少了行，而期望本身可能就没写全。
     *                        这类是**用例不完整**，既不是期望错也不是实现错。 */
    size_t expectation_wrong;
    size_t incomplete;
    /* **观测为空**：期望有内容，实际一条都没输出。与 expectation_wrong 分开，
     * 因为修复方向相反——前者查链路，后者查实现/期望。 */
    size_t unobserved;
    /* 执行期失败：跑起来但报错（status != SUCCESS）。
     * 与 `unobserved` 的区别：这一条**根本没产出输出**且带了错误码；
     * unobserved 是执行「成功」但什么都没输出。两者都查链路，但一个看错误码、
     * 一个看输出接管——不是同一件事。 */
    size_t execution_failed;
    /* 编码期失败：layout 过了但成不了 Program。查编码器，不是查执行。 */
    size_t encode_failed;
} runner_counts_t;

/* blocked 记录：文件 + pc + 算子名。分组后就得到能力缺口表。 */
typedef struct blocked_entry {
    char file[CASE_PATH_CAPACITY];
    uint32_t pc;
    char operation[64];
} blocked_entry_t;

#define MAX_BLOCKED 512
static blocked_entry_t g_blocked[MAX_BLOCKED];
static size_t g_blocked_count;


/* ── 分配器（与 end_to_end 同形：malloc/free 包装） ────────────────── */

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

static runtimec_kvspace_symbol_address_t resolve_symbol(void *context,
                                                        const char *name) {
    return (runtimec_kvspace_symbol_address_t)(uintptr_t)dlsym(context, name);
}

/* ── 运行环境 ─────────────────────────────────────────────────────── */

typedef struct runner_environment {
    void *layout_library;
    layout_file_function_t layout_file;
    void *kvspace_library;
    runtimec_kvspace_binary_interface_t binary_interface;
    void *kvspace_handle;
    runtimec_host_services_t services;
    runtimec_session_t *session;
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
    environment->kvspace_library = dlopen(kvspace_path, RTLD_NOW | RTLD_LOCAL);
    if (environment->kvspace_library == NULL) {
        fprintf(stderr, "SKIP: cannot load kvspace %s: %s\n", kvspace_path,
                dlerror());
        return false;
    }
    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = environment->kvspace_library, .resolve = resolve_symbol};
    runtimec_error_t error;
    if (runtimec_kvspace_binary_interface_bind(&resolver,
                                               &environment->binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: kvspace bind failed\n");
        return false;
    }
    snprintf(environment->directory, sizeof(environment->directory),
             "/tmp/runtimec-kvrun-%d", (int)getpid());
    (void)mkdir(environment->directory, 0700);
    snprintf(environment->data_source, sizeof(environment->data_source),
             "shm://%s/store.shm", environment->directory);
    environment->kvspace_handle =
        environment->binary_interface.connect(environment->data_source);
    if (environment->kvspace_handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", environment->data_source);
        return false;
    }
    runtimec_kvspace_adapter_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = environment->kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    if (runtimec_kvspace_adapter_create_services(
            &request, &environment->binary_interface, &environment->services,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: adapter create failed\n");
        return false;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = (uint32_t)sizeof(options);
    options.allocator.context = NULL;
    options.allocator.allocate = runtimec_allocate;
    options.allocator.deallocate = runtimec_deallocate;
    options.compilation_cache_capacity = 64U;
    /* enable_jit=false 时 machine_code_capacity **必须为 0**：
     * session 把「不开 JIT 却要机器码空间」判为非法请求（session.c:92-93）。
     * 骨架阶段只用解释档。 */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    if (runtimec_session_create(&environment->services, &options,
                                &environment->session,
                                &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: session create failed: status=%d %s\n",
                (int)error.status, error.message.data ? error.message.data : "");
        return false;
    }
    return true;
}

/*
 * 先让外部能力在 /lib 下存在。
 *
 * 用例里写 `println(...)`，而 layout 的 `check_call_names` 会把未定义的裸名当拼错的
 * 函数拒掉——那条规则是对的，不能为了测试放宽。真实部署里这些名字由扩展在装载期
 * 声明；测试用 layout 自己的 `rwir` 声明语法走同一条路径（write_rwir_decl），
 * 不手工拼 kvspace 字节。
 *
 * `println` 在这里只是**声明**，让 layout 放行；真正执行时它不在 runtimec 的
 * myrwircaps 里，编码后会成为 FOREIGN——这正是 runner 要产出 blocked 的地方。
 * 替身放在执行循环外面（裁决 §5），不进 vmkw_builtin_registry。
 */
static bool declare_external_capabilities(runner_environment_t *environment) {
    /* 新参数文法：`name: T` 已废，声明也是 `*T $name`。
     * 变参标记 `...` 跟在**类型**后（`*[]char/utf32 $s...`），落签名行锚点 `[0,0]`
     * 的 dynamic 字节；这样 `println(1, "x")` 的多实参才过得了 layout 的类型检查。 */
    static const char declarations[] =
        "rwir println(*[]char/utf32 $s...)\n"
        "rwir print(*[]char/utf32 $s...)\n";
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


/* 从 RWIR 源槽取算子名：`/lib/<fn>/[pc,0]` 的 body 是 5 字节 counts + 文本。
 *
 * 为什么不用 operation_identifier 反查：FOREIGN（0xFFFE）本来就不在能力表里，
 * 表里没有它就等于没有名字。源槽里才是 layout 实际写下的那个名字，也正是
 * blocked 报告要给人看的东西。读不到就写 "?"，不编。
 */
static void operation_name_at(runner_environment_t *environment,
                              const runtimec_snapshot_t *snapshot,
                              const char *function_path, uint32_t pc,
                              char *out, size_t out_capacity) {
    snprintf(out, out_capacity, "?");
    /* 截断是**错误**，不静默截掉：半个路径会让后续「key 不存在」指向错位置。
     * 用 snprintf 的返回值判，它返回「本该写入的长度」。 */
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
    /* 读回的是**完整 wire**：16 字节 head + body。body 前 5 字节是 counts，
     * 之后才是算子名文本。 */
    uint8_t storage[512];
    runtimec_bytes_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    if (environment->services.read(environment->services.context, snapshot,
                                   runtimec_key_view_from_c_string(key),
                                   &buffer, &error) != RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    /* 长度常量只有一处定义：`kvspace/xvalue_head.h` 的 KVSPACE_XVALUE_HEAD_LEN。
     * 自己再写一份就是第二处真相。 */
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

/* 生命周期收尾与其它组件一致：返回状态而不是 void。清理要继续走完，错误先记下。 */
static runtimec_status_t environment_close(runner_environment_t *environment) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (environment->session != NULL &&
        runtimec_session_destroy(environment->session) !=
            RUNTIMEC_STATUS_SUCCESS) {
        /* 失败继续清理，但状态要记下来——否则「收尾出错」永远看不见。 */
        status = RUNTIMEC_STATUS_ERROR;
    }
    if (environment->kvspace_handle != NULL) {
        runtimec_error_t error;
        (void)runtimec_kvspace_adapter_destroy_services(&environment->services,
                                                        &error);
        environment->binary_interface.close(environment->kvspace_handle);
    }
    if (environment->kvspace_library != NULL) {
        dlclose(environment->kvspace_library);
    }
    if (environment->layout_library != NULL) {
        dlclose(environment->layout_library);
    }
    if (environment->directory[0] != '\0') {
        char store[320];
        snprintf(store, sizeof(store), "%s/store.shm", environment->directory);
        (void)remove(store);
        (void)rmdir(environment->directory);
    }
    return status;
}

/* ── 用例发现 ─────────────────────────────────────────────────────── */

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
        /* `known-limitations/` 是**停靠区**，不是语料：里面的用例阻塞于已知的
         * 能力缺口（如 head.ptr 恒 0 导致指针参数不可编码），跑起来只会贡献
         * 「一条都不验」的噪声，且会把限制误读成回归。
         *
         * 跳过是**显式列举**目录名，不是按路径猜——名单只有一行，加新停靠区
         * 要改这里，看得见。 */
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

/* ── 期望输出解析（与 tutorial/test.py 同约定） ────────────────────── */

typedef struct expectation {
    char lines[16][256];
    size_t count;
    /* `// 期望错误: <子串>`；空串表示「没有这一行」，即正向用例。 */
    char expected_error[256];
} expectation_t;

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
        /* `// 期望错误: xxx`。先于「期望输出」判定，但两者互不干扰：一个文件要么
         * 是正向（只写期望输出）、要么是负向（只写期望错误），同时写属作者错误，
         * 由 main 里的互斥校验拦下，不在这里替它选一个。 */
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
        if (strstr(line, "期望输出") != NULL || strstr(line, "Expected output") != NULL) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *text = colon + 1;
                while (*text == ' ') {
                    text += 1;
                }
                if (*text != '\0' && out->count < 16U) {
                    snprintf(out->lines[out->count], 256, "%s", text);
                    out->count += 1U;
                    in_block = false;
                    continue;
                }
                in_block = true;
                continue;
            }
        }
        if (in_block) {
            if (strncmp(line, "//   ", 5) == 0 || strncmp(line, "// \t", 4) == 0) {
                const char *text = line + (strncmp(line, "//   ", 5) == 0 ? 5 : 4);
                if (*text != '\0' && out->count < 16U) {
                    snprintf(out->lines[out->count], 256, "%s", text);
                    out->count += 1U;
                }
            } else if (strncmp(line, "//", 2) != 0) {
                in_block = false;
            }
        }
    }
    fclose(file);
}

/* 取**第一条 error 行**，没有 error 行再退回第一非空行；写进 output。
 *
 * 为什么不是「第一行」：layout 的 stderr 里 `info:` 行常排在 `error:` 之前
 * （`rwfunc outside lib block — registering under /lib/<name>` 就是典型）。
 * 贴那条 info 行等于什么都没说——它既不是失败原因，也不含任何诊断内容。
 * 报红必须贴的是**真正把 layout 拦下来的那一行**。 */
static void first_error_line(const char *text, char *output, size_t capacity) {
    static const char empty[] = "(空)";

    if (text == NULL || text[0] == '\0') {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    const char *fallback = NULL;
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
            if (fallback == NULL) {
                fallback = cursor;
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
    if (fallback == NULL) {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    const size_t length = strcspn(fallback, "\r\n");
    const size_t copy = length < capacity - 1U ? length : capacity - 1U;
    memcpy(output, fallback, copy);
    output[copy] = '\0';
}

#define FILENO_STDOUT 1
#define FILENO_STDERR 2

/* 把 stream（stdout/stderr）重定向进临时文件，失败返回 false。
 *
 * 两个用途，同一手法：
 *  · stderr —— layout 的 C ABI 只回一条**汇总串**，真正的诊断走 stderr。
 *    负向用例要匹配的是后者；只比对 ABI 返回串的话，任何语法错误都长得一样，
 *    「预期错误」就成了「报错就行」，那是恒真判据。
 *  · stdout —— 用例的 println 输出写进程 stdout，比对必须逐条抓；
 *    不抓就是所有用例共用一条流，前一条的输出会串到下一条。
 *
 * 都在同一进程内，dup 一份即可，不用 fork。 */
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

/* 恢复 stream，把捕获到的内容读进 output（截断到容量）。 */
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

/* ── 输出比对：逐行完全相同 ────────────────────────────────────────── */

typedef enum compare_result {
    COMPARE_PASS = 0,
    /* 跑完了，某一行内容不符 —— 期望错或实现错，要人判。 */
    COMPARE_EXPECTATION_WRONG = 1,
    /* 实际行数多于期望，或期望为空 —— 用例本身没写全。 */
    COMPARE_INCOMPLETE = 2,
    /* **观测为空**：期望有内容，实际一条都没输出。
     *
     * 必须与 `COMPARE_EXPECTATION_WRONG` 分开——两者的修复方向**相反**：
     *   观测为空   → 查链路（算子没执行到 / 输出没接上 / 被 FOREIGN 挡住）
     *   内容不符   → 查实现（算错了）或查期望（写错了）
     * 混在一起会把「println 执行不到」读成「println 算错了」，去改实现，白费。
     *
     * 判据：期望非空而实际为零行。它不是「输出不符」，是「没观测到」。 */
    COMPARE_UNOBSERVED = 3
} compare_result_t;

/* 按行切分，剥掉行尾空白。**行尾空白是唯一的归一化**：编辑器与不同 printf
 * 实现会在行尾留空格，那不是语义差异。除此之外不宽容——不去空行、不排序、
 * 不去重、不折叠内部空白。
 *
 * 与 `tutorial/test.py` 的 `_normalize_output_lines` 同一规则（那边已有反向控制
 * 5 条）：多打印不管、顺序错不管、期望 `1` 还能匹到 `12` 的比对不算比对。 */
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
        /* 剥行尾空白（空格与制表符）。 */
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
    /* **观测为空**要先判，不能并进「行数不等」。
     *
     * 「期望 2 行、实际 0 行」与「期望 2 行、实际 1 行」是两件事：
     * 前者链路没通（算子没执行到），后者执行了但少了一次输出。
     * 修复方向相反——合并会把查链路的人指去改实现。 */
    if (actual_count == 0U && expectation->count != 0U) {
        fprintf(stderr,
                "FAIL %s: 未观测到任何输出（期望 %zu 行）——"
                "查链路，不是查实现\n",
                case_path, expectation->count);
        return COMPARE_UNOBSERVED;
    }
    if (actual_count != expectation->count) {
        /* 行数不等分两种，但要先看是哪边多。
         * 实际比期望多 → 可能是实现多打印（实现错）；也可能是用例只写了部分行
         * （用例不完整）。两者判据不同，报的时候必须把两侧都给出来。 */
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

/* ── 主流程（骨架：尚未接执行） ───────────────────────────────────── */

int main(int argument_count, char **arguments) {
    if (argument_count < 4) {
        fprintf(stderr,
                "usage: kv_runner <corpus-dir> <layout-lib> <kvspace-lib>\n");
        return 2;
    }
    const char *corpus = arguments[1];

    runner_environment_t environment;
    if (!environment_open(&environment, arguments[2], arguments[3])) {
        return SKIP_RETURN_CODE;
    }

    if (!declare_external_capabilities(&environment)) {
        if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "warning: environment close reported an error\n");
        }
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
    for (size_t index = 0U; index < cases.count; ++index) {
        char full[CASE_PATH_CAPACITY * 2U];
        snprintf(full, sizeof(full), "%s/%s", corpus, cases.paths[index]);
        expectation_t expectation;
        parse_expectations(full, &expectation);
        const bool expects_layout_failure = expectation.expected_error[0] != '\0';
        if (expects_layout_failure && expectation.count != 0U) {
            /* 两类期望互斥。同时写说明作者没决定这条用例测什么；替它选一边
             * 会让另一边永远不被检查。 */
            fprintf(stderr,
                    "FAIL %s: 同时写了 `期望输出:` 与 `期望错误:` —— 一条用例只能是"
                    "正向或负向\n",
                    cases.paths[index]);
            counts.failed += 1U;
            continue;
        }
        if (!expects_layout_failure && expectation.count == 0U) {
            /* 没有期望的用例会永远 pass —— 那是恒真判据。这里直接算失败。 */
            fprintf(stderr, "FAIL %s: 缺 `// 期望输出:` 头\n", cases.paths[index]);
            missing_expectation += 1U;
            counts.failed += 1U;
            continue;
        }
        /* layout 到本实例的 store。失败是 fail（跑不起来），不是 blocked。 */
        char entry[256];
        char error_text[512];
        memset(entry, 0, sizeof(entry));
        memset(error_text, 0, sizeof(error_text));
        /* 负向用例要匹配 stderr 里的具体诊断，正向用例不该把 layout 的诊断丢掉：
         * 两种情况都把 stderr 抓下来，正向失败时把它一起打出来。 */
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
            /* 负向用例：只看「拒绝」与「拒绝得对」。
             * 空子串在上面的解析里就被当成「没有这一行」——但作者可能写了
             * `// 期望错误:` 后面什么都没有，那时 expected_error 也是空串，
             * 于是变成正向用例、接着因缺期望输出而 FAIL。这条路径是对的：
             * 空子串不能算「报什么错都行」。 */
            if (layout_result == 0) {
                fprintf(stderr, "FAIL %s: 预期 layout 失败，但它通过了\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            /* 子串在**两条**来源里找：ABI 汇总串与捕获到的 stderr。
             * 汇总串只有一类（`parse: error-level diagnostics …`），具体诊断
             * （`rwfunc parameter must be a single type`）只在 stderr 里。 */
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
                counts.failed += 1U;
                continue;
            }
            counts.passed += 1U;
            counts.expected_failed += 1U;
            continue;
        }
        if (layout_result != 0) {
            /* 正向用例失败：摘要 + stderr 首行。只给汇总串等于没说，
             * 同一条红在语法层和类型层的原因完全不同。 */
            char stderr_line[512];
            first_error_line(captured, stderr_line, sizeof(stderr_line));
            fprintf(stderr, "FAIL %s: layout failed: %s\n  实际 stderr 首行：%s\n",
                    cases.paths[index], error_text, stderr_line);
            counts.failed += 1U;
            continue;
        }
        layout_ok += 1U;
        /* 编码 + FOREIGN 扫描：blocked 的判据就在这里。
         * 不执行也能得到 blocked —— 只要编码出来的指令里有 FOREIGN，
         * 就说明 runtimec 没有该能力，且能报出 pc 与算子名。 */
        {
            runtimec_session_runtime_components_t components;
            if (runtimec_session_get_runtime_components(
                    environment.session, &components) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: cannot get components\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            /* 每条用例的入口恒为顶层 `rwfunc test()`（与 tutorial 同约定）。
             * `entry` 是 layout 返回的**初始化入口**（可能是 `init`），不是它。 */
            char function_path[CASE_PATH_CAPACITY];
            snprintf(function_path, sizeof(function_path), "/lib/test");
            vmkv_encode_request_t encode_request;
            runtimec_error_t encode_error;
            memset(&encode_request, 0, sizeof(encode_request));
            memset(&encode_error, 0, sizeof(encode_error));
            encode_request.host_services = &environment.services;
            encode_request.function_path =
                runtimec_string_view_from_c_string(function_path);
            encode_request.expected_snapshot.contract_version =
                runtimec_contract_version();
            /* revision 必须**从 host 真取**：它由 store 实例与 generation 决定，
             * 自己编一个值会被编码器判为「function source revision differs」
             * ——那是真校验，不该绕。 */
            runtimec_source_revision_reset(
                &encode_request.expected_snapshot.source_revision);
            encode_request.expected_snapshot.function_key =
                runtimec_key_view_from_c_string(function_path);
            /* 快照里的位置文本叫 `source_position_text`（编译缓存键的源码位置），
             * 与执行位置 `runtimec_program_counter_t` 不是同一个概念。 */
            encode_request.expected_snapshot.source_position_text =
                runtimec_string_view_from_c_string("[1,0]");
            if (environment.services.source_revision(
                    environment.services.context,
                    encode_request.expected_snapshot.function_key,
                    &encode_request.expected_snapshot.source_revision,
                    &encode_error) != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: source revision unavailable\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            binrwir_program_t *program = NULL;
            const runtimec_status_t encoded = vmkv_encode(
                components.vmkv_encoder, &encode_request, &program,
                &encode_error);
            if (encoded != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: encode failed: %s\n",
                        cases.paths[index],
                        encode_error.message.data ? encode_error.message.data
                                                  : "");
                counts.failed += 1U;
                counts.encode_failed += 1U;
                continue;
            }
            /* Program 是不透明类型：条数经 get_view，逐条经 get_instruction。
             * 算子名暂缺——operation_identifier 只能回到能力表，而 FOREIGN
             * （0xFFFE）本来就不在表里。下一步从 RWIR 源槽取名字。 */
            binrwir_program_view_t program_view;
            unsigned foreign = 0U;
            if (binrwir_program_get_view(program, &program_view) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: get_view failed\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            /* 指令标识符是稠密序号、从 0 起（Program 私有 framing 不暴露起始值，
             * 但标识符区间由 get_view 的 instruction_count 界定）。 */
            for (size_t ordinal = 0U; ordinal < program_view.instruction_count;
                 ++ordinal) {
                binrwir_instruction_view_t view;
                if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
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
                counts.blocked += 1U;
                continue;
            }
            /* 无 FOREIGN → 可以执行。建帧、绑参、`‥pc` 写入全在
             * execution_window_execute 内部完成，runner 只构造请求。 */
            runtimec_execution_window_request_t window_request;
            memset(&window_request, 0, sizeof(window_request));
            char vthread_identifier[64];
            const int vthread_length = snprintf(
                vthread_identifier, sizeof(vthread_identifier), "%u",
                ++environment.sequence);
            if (vthread_length < 0 ||
                (size_t)vthread_length >= sizeof(vthread_identifier)) {
                fprintf(stderr, "FAIL %s: vthread identifier truncated\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            char frame_path[128];
            const int frame_length =
                snprintf(frame_path, sizeof(frame_path), "/vthread/%s/[1]",
                         vthread_identifier);
            if (frame_length < 0 ||
                (size_t)frame_length >= sizeof(frame_path)) {
                fprintf(stderr, "FAIL %s: frame path truncated\n",
                        cases.paths[index]);
                counts.failed += 1U;
                continue;
            }
            window_request.function_path =
                runtimec_string_view_from_c_string(function_path);
            window_request.vthread_identifier =
                runtimec_string_view_from_c_string(vthread_identifier);
            window_request.frame_path =
                runtimec_string_view_from_c_string(frame_path);
            window_request.source_logical_program_counter = 1U;
            window_request.arguments = NULL;
            window_request.argument_count = 0U;
            runtimec_execution_window_result_t window_result;
            memset(&window_result, 0, sizeof(window_result));
            runtimec_error_t window_error;
            memset(&window_error, 0, sizeof(window_error));
            /* 抓 stdout：println 写进程 stdout，逐条用例必须各自捕获，否则
             * 前一条的输出会串到下一条的比对里。与抓 stderr 同一手法。 */
            char output_path[256];
            output_path[0] = '\0';
            char captured_output[8192];
            captured_output[0] = '\0';
            int saved_stdout = -1;
            const bool capturing_stdout =
                redirect_stream_to(FILENO_STDOUT, output_path,
                                   sizeof(output_path), &saved_stdout);
            const runtimec_status_t executed = runtimec_execution_window_execute(
                environment.session, &window_request, &window_result,
                &window_error);
            if (capturing_stdout) {
                restore_stream(FILENO_STDOUT, saved_stdout, output_path,
                               captured_output, sizeof(captured_output));
            }
            if (executed != RUNTIMEC_STATUS_SUCCESS) {
                /* 只打 `message` 是不够的：它经常是空串（错误由 exit 携带而不是
                 * error 结构），那就成了「execute failed: 」——等于没报。
                 * status 与 exit 的 pc/detail 才是能定位的部分。 */
                fprintf(stderr,
                        "FAIL %s: execute failed status=%d"
                        "（exit.kind=%u status=%d detail=%u pc=%u）: %s\n",
                        cases.paths[index], (int)executed,
                        window_result.execution_exit.kind,
                        (int)window_result.execution_exit.status,
                        window_result.execution_exit.detail_code,
                        window_result.execution_exit.logical_program_counter,
                        window_error.message.data ? window_error.message.data
                                                  : "(无消息)");
                counts.failed += 1U;
                counts.execution_failed += 1U;
                continue;
            }
            if (window_result.execution_exit.kind ==
                BINRWIR_EXECUTION_EXIT_HANDOFF) {
                /* 执行到 handoff = 撞上了本 runtime 没有的能力。
                 * **必须带原因串**：不带的话「blocked 21」与「blocked 3」
                 * 看不出区别，blocked 就退化成「这条没过」。 */
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = window_result.execution_exit
                                   .logical_program_counter;
                    snprintf(slot->operation, sizeof(slot->operation), "%s",
                             "handoff");
                    g_blocked_count += 1U;
                }
                counts.blocked += 1U;
                continue;
            }
            if (window_result.execution_exit.kind ==
                BINRWIR_EXECUTION_EXIT_ERROR) {
                /* 跑起来了但运行时错：是 fail，不是 blocked。
                 * 两者分开是因为处置不同——blocked 等能力补齐，fail 是缺陷。 */
                char stderr_line[512];
                first_error_line(captured, stderr_line, sizeof(stderr_line));
                fprintf(stderr,
                        "FAIL %s: 执行报错 status=%d detail=%u pc=%u\n"
                        "  stderr：%s\n",
                        cases.paths[index],
                        (int)window_result.execution_exit.status,
                        window_result.execution_exit.detail_code,
                        window_result.execution_exit.logical_program_counter,
                        stderr_line);
                counts.failed += 1U;
                continue;
            }
            /* 真跑完了：逐行比对。 */
            switch (compare_output(&expectation, captured_output, cases.paths[index])) {
                case COMPARE_PASS:
                    counts.passed += 1U;
                    counts.executed += 1U;
                    break;
                case COMPARE_EXPECTATION_WRONG:
                    counts.failed += 1U;
                    counts.expectation_wrong += 1U;
                    break;
                case COMPARE_INCOMPLETE:
                    counts.failed += 1U;
                    counts.incomplete += 1U;
                    break;
                case COMPARE_UNOBSERVED:
                    /* 与「输出不符」分开计。混在一起会让「链路没通」看起来像
                     * 「实现算错」，把人指向相反的修复方向。 */
                    counts.failed += 1U;
                    counts.unobserved += 1U;
                    break;
                default:
                    counts.failed += 1U;
                    break;
            }
        }
    }

    /*
     * TODO(骨架)：layout → 编码 → 执行 → 抓输出。
     * 现在只做「发现用例 + 校验期望格式」，**不产出任何 pass 数**——
     * 一个不执行的 runner 若报 PASS，就是又一个恒真判据。
     */
    /* 汇总首行：`executed` 与 `blocked` **必须同时出现**。
     *
     * 只报 executed 的话「5/25」看起来像全过；只报 blocked 的话「20」看不出
     * 底下有 5 条真跑过。两个数挨着摆，第一眼就知道「大部分没验」。
     *
     * 顺序按「可信度」排：executed（跑对）→ blocked（没验）→ fail（红了）→
     * 仅编码（只编过）。抄数字的人从左往右读，先读到的是可信的。 */
    printf("用例 %zu：executed %zu / blocked %zu / fail %zu / pass(仅编码) %zu\n",
           cases.count, counts.executed, counts.blocked, counts.failed,
           counts.passed - counts.executed);
    /* fail 的细分按**修复方向**排，不按发生顺序：
     *   未观测到    → 查链路（输出没接上）
     *   执行期失败  → 查链路（带错误码，看 status/detail）
     *   编码期失败  → 查编码器
     *   期望不符    → 查实现或期望
     *   用例不完整  → 补用例
     *   其中预期失败 → 负向用例正常工作，不是失败 */
    printf("  细分：未观测到 %zu、执行期失败 %zu、编码期失败 %zu、"
           "期望不符 %zu、用例不完整 %zu、其中预期失败 %zu\n",
           counts.unobserved, counts.execution_failed, counts.encode_failed,
           counts.expectation_wrong, counts.incomplete, counts.expected_failed);
    /* 「未观测到」必须单列并带解释：它与「期望不符」的修复方向相反。
     * 不解释的话，看到 21 条 fail 的人会去查实现，而实际该查链路。 */
    if (counts.unobserved != 0U) {
        printf("  注：`未观测到` = 期望有内容但实际零行输出 —— 查**链路**"
               "（算子没执行到/输出没接上），不是查实现。\n");
    }
    /* 覆盖率**单独一行**，给人看、不给退出码看。
     *
     * 分母是 executed + blocked：这两个数合起来才是「本该能跑的正向用例」。
     * 这一行是 A 方案防掩盖的核心——ctest 的绿只承诺「已执行的都相符」，
     * 而这一行说的是「已执行的有多少」。缺了它，绿就覆盖了整批。 */
    {
        const size_t denom = counts.executed + counts.blocked;
        if (denom == 0U) {
            printf("本层已验 0/0 —— 本次没有可执行的用例\n");
        } else {
            printf("本层已验 %zu/%zu = %zu%%（executed / executed+blocked）\n",
                   counts.executed, denom, counts.executed * 100U / denom);
        }
    }
    if (g_blocked_count > 0U) {
        /* 每一条 blocked 都带原因串。不带的「blocked 20」与「blocked 3」
         * 看不出区别，blocked 就退化成「这条没过」。 */
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
    /* 退出码三态（架构师裁决）：
     *
     *   有失败            → 1
     *   executed > 0 且全相符 → 0
     *   executed == 0     → 77（CTest 的 SKIP）
     *
     * 为什么 `executed == 0` 不是 0：那时一条行为都没验（全是 blocked 或只编过），
     * 报 0 就是「永远绿的 runner」——这个文件第 226 行警告过的那个东西。
     * 为什么也不是 1：它没坏，只是今天没验到，制造长期红是另一种噪声。
     * 77 是 CTest 的跳过约定（`runtimec.vmkv.end_to_end` 缺库时也返 77），
     * 它诚实表达「这条用例今天什么都没验」，且**会在 println 接上后自动转成 0/1**——
     * 转换那一刻就是行为层真正开始工作的时刻，谁都看得见。
     *
     * `missing_expectation`（缺 `// 期望输出:` 的用例）算失败：没有期望的用例会永远 pass，
     * 那是恒真判据。 */
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
