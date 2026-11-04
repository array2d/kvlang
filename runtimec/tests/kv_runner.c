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
 * `println` 替身放在执行循环**外面**，不进 binrwir_registry：能力表是
 * 「runtimec 能执行什么」的事实源，把只存在于测试的处理器写进去，等于让能力表
 * 说一句不成立的话（routing/README.md §四之二：println 属 byteseek）。
 *
 * 用法：kv_runner <corpus-dir> <layout-lib> <kvspace-lib>
 */

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
    static const char declarations[] =
        "rwir println(s: )\n"
        "rwir print(s: )\n";
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
 * 为什么不用 operation_identifier 反查：FOREIGN（0xFFFFFFFE）本来就不在能力表里，
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
        if (expectation.count == 0U) {
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
        if (environment.layout_file(full, environment.data_source, entry,
                                    sizeof(entry), error_text,
                                    sizeof(error_text)) != 0) {
            fprintf(stderr, "FAIL %s: layout failed: %s\n", cases.paths[index],
                    error_text);
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
                continue;
            }
            /* Program 是不透明类型：条数经 get_view，逐条经 get_instruction。
             * 算子名暂缺——operation_identifier 只能回到能力表，而 FOREIGN
             * （0xFFFFFFFE）本来就不在表里。下一步从 RWIR 源槽取名字。 */
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
            const runtimec_status_t executed = runtimec_execution_window_execute(
                environment.session, &window_request, &window_result,
                &window_error);
            if (executed != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: execute failed: %s\n",
                        cases.paths[index],
                        window_error.message.data ? window_error.message.data
                                                  : "");
                counts.failed += 1U;
                continue;
            }
            if (window_result.execution_exit.kind ==
                BINRWIR_EXECUTION_EXIT_HANDOFF) {
                /* 执行到 handoff = 撞上了本 runtime 没有的能力。 */
                counts.blocked += 1U;
                continue;
            }
            counts.passed += 1U;
        }
    }

    /*
     * TODO(骨架)：layout → 编码 → 执行 → 抓输出。
     * 现在只做「发现用例 + 校验期望格式」，**不产出任何 pass 数**——
     * 一个不执行的 runner 若报 PASS，就是又一个恒真判据。
     */
    /* `pass(encoded)` 而不是裸 `pass`：这个数字只表示「编得过」，不表示
     * 「输出对」。标记写在数字旁边，抄汇总表的人不会把它当通过率。 */
    printf("用例 %zu：pass(encoded) %zu / fail %zu / blocked %zu\n",
           cases.count, counts.passed, counts.failed, counts.blocked);
    if (g_blocked_count > 0U) {
        printf("blocked 明细（文件 + source pc + 算子名）：\n");
        for (size_t index = 0U; index < g_blocked_count; ++index) {
            printf("  blocked  %s  pc=%u  op=%s\n", g_blocked[index].file,
                   g_blocked[index].pc, g_blocked[index].operation);
        }
    }
    /* 尚未接入执行：pass 只表示「编码成功且无 FOREIGN」，**不表示输出正确**。
     * 这条区分必须写死在输出里，否则 pass 会被当成「用例过了」。 */
    printf("注：pass(encoded) 当前含义 = 编码成功且无 FOREIGN；输出比对尚未启用。\n");
    (void)layout_ok;

    if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "warning: environment close reported an error\n");
    }
    return missing_expectation == 0U ? 0 : 1;
}
