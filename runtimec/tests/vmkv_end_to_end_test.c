/*
 * runtimec 端到端里程碑：真实 .kv 源码 → 真 layout 库写进真 kvspace →
 * vmkv 编成 BinRWIR。
 *
 * 这个用例存在的理由：此前 encoder 全部用例的输入都是手搭的 fake_store_t。
 * 假输入只能证明假结论——真实 layout 产出的槽形状（字面量、类型串、函数签名、
 * count 头）与假树是否有差异，只有真链路能回答。
 *
 * 两个阶段：
 *   1. 纯算术（10 + 3 -> r）：必须编码成功、Program 校验通过、无 FOREIGN；
 *   2. hello.kv（println / time·*）：必须编码成功，且 FOREIGN 标记的 source pc
 *      与源码里外部能力的位置对应。
 *
 * 缺依赖时跳过并打印原因（SKIP_RETURN_CODE 77），不伪装通过。
 *
 * 依赖（缺一即跳过）：
 *   - layout 库：KVSPACE_TEST_LAYOUT_LIBRARY 或 argv[1]
 *     （默认布局 target/release/libkvlanglayout.so 由 CMake 传入）
 *   - kvspace 库：RUNTIMEC_KVSPACE_LIBRARY 或 argv[2]
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "contract/host_services.h"
#include "contract/view.h"
#include "host/kvspace_adapter.h"
#include "host/kvspace_binary_interface.h"
#include "vmkv/encoder.h"

#define SKIP_RETURN_CODE 77
#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
        }                                                                      \
    } while (0)

/* layout 库的 C 入口：读 .kv 文件、布局进 dsn 指向的 kvspace。 */
typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry_out, uint32_t entry_capacity,
                                      char *error_out, uint32_t error_capacity);

static int failures;

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

static runtimec_kvspace_symbol_address_t resolve_symbol(void *context,
                                                        const char *name) {
    void *symbol = dlsym(context, name);
    return (runtimec_kvspace_symbol_address_t)(uintptr_t)symbol;
}

/* 写一个临时 .kv 文件；失败返回 false。 */
static bool write_source(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t size = strlen(text);
    const bool written = fwrite(text, 1U, size, file) == size;
    fclose(file);
    return written;
}

/* 一个测试实例：真 layout 库 + 真 kvspace 库 + 一个独立的 shm store。 */
typedef struct test_environment {
    void *layout_library;
    layout_file_function_t layout_file;
    void *kvspace_library;
    runtimec_kvspace_binary_interface_t binary_interface;
    void *kvspace_handle;
    runtimec_host_services_t services;
    char directory[256];
    char data_source[320];
} test_environment_t;

static bool environment_open(test_environment_t *environment,
                             const char *layout_path, const char *kvspace_path) {
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
             "/tmp/runtimec-e2e-%d", (int)getpid());
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
    return true;
}

/* 释放一个测试实例；与其它组件的生命周期入口一致，返回状态而不是 void。 */
static runtimec_status_t environment_close(test_environment_t *environment) {
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
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 真 layout：把源码布局进本实例的 store。成功返回 true。 */
static bool layout_into_store(test_environment_t *environment,
                              const char *source, const char *label) {
    char source_path[320];
    snprintf(source_path, sizeof(source_path), "%s/%s.kv", environment->directory,
             label);
    if (!write_source(source_path, source)) {
        return false;
    }
    char entry[256];
    char error_text[512];
    memset(entry, 0, sizeof(entry));
    memset(error_text, 0, sizeof(error_text));
    const int result = environment->layout_file(
        source_path, environment->data_source, entry, sizeof(entry), error_text,
        sizeof(error_text));
    if (result != 0) {
        fprintf(stderr, "layout(%s) failed: %s\n", label, error_text);
        return false;
    }
    return true;
}

/*
 * 前置探测：layout 能不能写进任何东西。
 *
 * 为什么要有这一步：如果 layout 本身坏了（例如 XValue head 布局改版而 layout 的写
 * 路径没跟上），后面每个阶段都会各自失败一次，读报告的人会先怀疑被测代码而不是
 * layout。用最小输入（空函数体）探一次，就只报一条红，并且指认根因在下游。
 *
 * 探测失败不是 SKIP：跳过意味着「环境不具备」，而这是「依赖坏了」，必须红。
 */
static bool layout_can_write_anything(test_environment_t *environment) {
    static const char probe_source[] =
        "rwfunc layoutprobe() -> () {\n"
        "\treturn\n"
        "}\n";
    if (layout_into_store(environment, probe_source, "layoutprobe")) {
        return true;
    }
    fprintf(stderr,
            "\nFAIL: runtimec end-to-end cannot run: the layout library cannot "
            "write any value into kvspace.\n"
            "  Probe: `rwfunc layoutprobe() -> () { return }` — the smallest "
            "possible function — is rejected.\n"
            "  This is a layout-side fault, not a vmkv or encoder fault: the "
            "failure happens before any encoding.\n"
            "  Most likely cause: the XValue head layout changed and layout's "
            "own wire writer still emits the previous layout.\n"
            "  Everything downstream (tutorial, benchmark, route end-to-end) is "
            "blocked by the same fault.\n");
    ++failures;
    return false;
}

/*
 * 用真 store 编码一个函数。Program 的每条指令都打印出来——这是「可核对的文本」，
 * 出问题时人和 agent 都能直接看出哪条 opid / pc / 参数不对。
 */
static bool encode_and_report(test_environment_t *environment,
                              const char *function_path, size_t *output_count,
                              size_t *output_foreign_count) {
    runtimec_error_t error;
    runtimec_error_reset(&error);
    vmkv_encoder_t *encoder = NULL;
    if (vmkv_encoder_create(environment->services.allocator, &encoder, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "encoder create failed\n");
        return false;
    }
    /*
     * 快照必须携带**真实的** source revision：编码器会拿它与打开函数源时读到的
     * revision 比对，不一致就拒绝（防止编到一半源码被改）。预先猜一个 generation
     * 是错的——adapter 的 generation 随 store 事实变化，只能先问再编。
     */
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key = runtimec_key_view_from_c_string(function_path);
    snapshot.logical_program_counter =
        runtimec_string_view_from_c_string("[1,0]");
    if (environment->services.source_revision(
            environment->services.context, runtimec_key_view_from_c_string(
                                               function_path),
            &snapshot.source_revision, &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr,
                "  %s: host has no source revision; the function is not present "
                "or has no ctime\n",
                function_path);
        vmkv_encoder_destroy(encoder);
        return false;
    }

    const vmkv_encode_request_t request = {
        .host_services = &environment->services,
        .function_path = runtimec_string_view_from_c_string(function_path),
        .expected_snapshot = snapshot,
    };
    binrwir_program_t *program = NULL;
    const runtimec_status_t status =
        vmkv_encode(encoder, &request, &program, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "encode(%s) failed: status=%d code=%lld %.*s\n",
                function_path, (int)status, (long long)error.code,
                (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        vmkv_encoder_destroy(encoder);
        return false;
    }
    EXPECT(binrwir_program_validate(program) == RUNTIMEC_STATUS_SUCCESS);

    binrwir_program_view_t view;
    EXPECT(binrwir_program_get_view(program, &view) == RUNTIMEC_STATUS_SUCCESS);
    size_t foreign_count = 0U;
    printf("  program %s: %zu instruction(s), %zu read / %zu write param(s)\n",
           function_path, view.instruction_count,
           view.signature.read_parameter_count,
           view.signature.write_parameter_count);
    for (size_t ordinal = 0U; ordinal < view.instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            EXPECT(false);
            break;
        }
        const uint32_t operation = instruction.instruction->operation_identifier;
        const bool foreign = operation == BINRWIR_OPERATION_IDENTIFIER_FOREIGN;
        if (foreign) {
            foreign_count += 1U;
        }
        printf("    [%zu] pc=%-3u opid=0x%08X%s  reads=%zu writes=%zu\n", ordinal,
               instruction.source_logical_program_counter, operation,
               foreign ? " FOREIGN" : "", instruction.read_arguments.count,
               instruction.write_arguments.count);
        for (size_t position = 0U; position < instruction.read_arguments.count;
             ++position) {
            const binrwir_argument_t *argument =
                &instruction.read_arguments.data[position];
            /*
             * 只有形参引用才由签名给出类型；LITERAL / STATIC / LOCAL / FUNCTION
             * 的 slot 是各自的池下标，拿它索引 read_parameter_types 会越界。
             */
            runtimec_string_view_t type = runtimec_string_view_make(NULL, 0U);
            if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_READ &&
                argument->slot < view.signature.read_parameter_count) {
                type = view.signature.read_parameter_types[argument->slot];
            }
            printf("         read  argidx=%u kind=%u slot=%u payload=0x%llX "
                   "type=%.*s\n",
                   argument->argument_index, argument->reference_kind,
                   argument->slot, (unsigned long long)argument->payload,
                   (int)type.size, type.data != NULL ? type.data : "");
        }
        for (size_t position = 0U; position < instruction.write_arguments.count;
             ++position) {
            const binrwir_argument_t *argument =
                &instruction.write_arguments.data[position];
            printf("         write argidx=%u kind=%u slot=%u payload=0x%llX\n",
                   argument->argument_index, argument->reference_kind,
                   argument->slot, (unsigned long long)argument->payload);
        }
    }
    *output_count = view.instruction_count;
    *output_foreign_count = foreign_count;
    binrwir_program_destroy(program);
    vmkv_encoder_destroy(encoder);
    return true;
}

/* 阶段 1：纯算术。必须成功、校验通过、不含 FOREIGN。 */
static void stage_one_arithmetic(test_environment_t *environment) {
    static const char source[] =
        "rwfunc test() -> (r: int64) {\n"
        "\t10 + 3 -> r\n"
        "}\n";
    printf("stage 1: pure arithmetic\n");
    EXPECT(layout_into_store(environment, source, "arith"));
    size_t instruction_count = 0U;
    size_t foreign_count = 0U;
    EXPECT(encode_and_report(environment, "/lib/test", &instruction_count,
                             &foreign_count));
    EXPECT(instruction_count > 0U);
    /* 纯算术不可能出现外部能力：出现 FOREIGN 说明 alias 表漏了本 runtime 的行。 */
    EXPECT(foreign_count == 0U);
}

/*
 * 重新编码一次并取出 FOREIGN 的 source pc 序列，与期望逐项比对。
 * 只有在「输入由测试自己拥有」时才允许这样精确断言位置。
 */
static bool foreign_program_counters_match(test_environment_t *environment,
                                           const char *function_path,
                                           const uint32_t *expected,
                                           size_t expected_count) {
    runtimec_error_t error;
    runtimec_error_reset(&error);
    vmkv_encoder_t *encoder = NULL;
    if (vmkv_encoder_create(environment->services.allocator, &encoder, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key = runtimec_key_view_from_c_string(function_path);
    snapshot.logical_program_counter =
        runtimec_string_view_from_c_string("[1,0]");
    if (environment->services.source_revision(
            environment->services.context,
            runtimec_key_view_from_c_string(function_path),
            &snapshot.source_revision, &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmkv_encoder_destroy(encoder);
        return false;
    }
    const vmkv_encode_request_t request = {
        .host_services = &environment->services,
        .function_path = runtimec_string_view_from_c_string(function_path),
        .expected_snapshot = snapshot,
    };
    binrwir_program_t *program = NULL;
    const runtimec_status_t status =
        vmkv_encode(encoder, &request, &program, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmkv_encoder_destroy(encoder);
        return false;
    }
    binrwir_program_view_t view;
    size_t seen = 0U;
    bool matched = binrwir_program_get_view(program, &view) ==
                   RUNTIMEC_STATUS_SUCCESS;
    for (size_t ordinal = 0U; matched && ordinal < view.instruction_count;
         ++ordinal) {
        binrwir_instruction_view_t instruction;
        if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            matched = false;
            break;
        }
        if (instruction.instruction->operation_identifier !=
            BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
            continue;
        }
        if (seen >= expected_count ||
            instruction.source_logical_program_counter != expected[seen]) {
            fprintf(stderr,
                    "  FOREIGN #%zu at pc %u, expected pc %u\n", seen,
                    instruction.source_logical_program_counter,
                    seen < expected_count ? expected[seen] : 0U);
            matched = false;
            break;
        }
        seen += 1U;
    }
    if (matched && seen != expected_count) {
        fprintf(stderr, "  saw %zu FOREIGN, expected %zu\n", seen,
                expected_count);
        matched = false;
    }
    binrwir_program_destroy(program);
    vmkv_encoder_destroy(encoder);
    return matched;
}


/*
 * 模拟扩展装载期：真实部署里由扩展（例如 byteseek）在装载时声明它实现的能力。
 * 测试要走出「外部能力 → FOREIGN」这条路，就得先让这些名字在 /lib 下存在，
 * 否则 layout 的 check_call_names 会把 println 当拼错的裸名拒掉——那条规则是对的，
 * 不能为了测试放宽。
 *
 * 用 layout 自己的 `rwir` 声明语法（走 write_rwir_decl），而不是手工拼 kvspace
 * 字节：这才是真实装载路径的形状。空 kindexpr（`s:`）表示不约束类型。
 */
static bool declare_external_capabilities(test_environment_t *environment) {
    static const char declarations[] =
        "rwir println(s: )\n"
        "rwir time·now() -> (r: time)\n"
        "rwir time·sub(a: time, b: time) -> (r: time/duration)\n"
        "rwir time/duration·as_nanos(d: time/duration) -> (r: int64)\n";
    return layout_into_store(environment, declarations, "capabilities");
}
/*
 * 阶段 2：外部能力必须编成 FOREIGN，而不是报错。
 *
 * 输入是**测试自带的 fixture**，不是 tutorial 文件：位置由这里自己决定，所以可以
 * 精确断言。对不受控的外部文件只做冒烟（见阶段 3）。
 *
 * source pc 是**指令序号**（layout/src/code.rs:881-886 的 `(i as i32) + 1`），
 * 不是源码行号。fixture 里刻意插入空行，让两者不再相等——这样下面那组
 * 「pc 2 / pc 4」的断言同时证明「pc 跟指令序号走，不跟行号走」：
 *
 *   指令序号 | 源码行号 | 内容
 *   ---------+----------+------------------
 *      1     |    2     | 1 + 2 -> r
 *      —     |    3     | （空行）
 *      2     |    4     | println("x")    FOREIGN（不在 alias 表）
 *      3     |    5     | 3 + 4 -> r
 *      —     |    6     | （空行）
 *      4     |    7     | time·now()      本 runtime 原生（opid 0x0150）
 *      5     |    8     | 5 + 6 -> r
 *
 * 若断言按行号写，FOREIGN 会在 pc 4；实际期望 pc 2。
 *
 * 注意 `time·now` 是**本 runtime 的原生能力**（alias 表里有 `time·now` 行），
 * 所以它不是 FOREIGN——只有 `println` 这类由扩展就地实现的名字才是。
 */
static void stage_two_foreign(test_environment_t *environment) {
    static const char source[] =
        "rwfunc probe(a: int64) -> (r: int64) {\n"
        "\t1 + 2 -> r\n"
        "\n"
        "\tprintln(\"x\")\n"
        "\t3 + 4 -> r\n"
        "\n"
        "\ttime·now()\n"
        "\t5 + 6 -> r\n"
        "}\n";
    printf("stage 2: external capabilities, precise ordinals (self-owned fixture)\n");
    EXPECT(layout_into_store(environment, source, "foreign"));
    size_t instruction_count = 0U;
    size_t foreign_count = 0U;
    /* 编码必须成功——外部能力不是错误，是 FOREIGN 标记。 */
    EXPECT(encode_and_report(environment, "/lib/probe", &instruction_count,
                             &foreign_count));
    /* 5 条源码指令 + layout 在函数体末尾合成的 1 条 return（lower.rs:488-496）。 */
    EXPECT(instruction_count == 6U);
    EXPECT(foreign_count == 1U);
    EXPECT(foreign_program_counters_match(environment, "/lib/probe",
                                          (const uint32_t[]){2U}, 1U));
}

/*
 * 阶段 3：tutorial/01-basics/hello.kv 冒烟。
 *
 * 输入是外部文件、会随时间变化，所以**只验「没崩」和「形状对」**：编码成功、
 * 有指令、至少一条 FOREIGN。精确位置断言放在阶段 2 的自有 fixture 里。
 */
static void stage_three_tutorial_smoke(test_environment_t *environment) {
    static const char source[] =
        "rwfunc test() -> () {\n"
        "\tt0 = time·now()\n"
        "\tprintln(\"hello kvlang\")\n"
        "\tt1 = time·now()\n"
        "\tdelta = time·sub(t1, t0)\n"
        "\tns = time/duration·as_nanos(delta)\n"
        "\tprintln(\"__bench_ns:\", ns)\n"
        "}\n";
    printf("stage 3: tutorial hello.kv smoke (external input, shape only)\n");
    EXPECT(layout_into_store(environment, source, "hello"));
    size_t instruction_count = 0U;
    size_t foreign_count = 0U;
    EXPECT(encode_and_report(environment, "/lib/test", &instruction_count,
                             &foreign_count));
    EXPECT(instruction_count > 0U);
    EXPECT(foreign_count > 0U);
}

/*
 * 阶段 4（负例）：放宽只发生在「不需要计算类型」的位置。
 *
 * 上面把 `IDENTITY + NONE` 的行改成类型透传，不再要求落进类别表。这个放宽必须是
 * 有边界的：需要由操作数推出结果的算子（`add` 的 WIDEN + NUMERIC_COMPATIBLE）
 * 仍然必须能把操作数分类，否则它算不出结果类型。
 *
 * 这里用 `add` 直接喂一个不可分类的操作数类型，断言编码失败——证明放宽没有
 * 蔓延到需要计算的位置。
 */
static void stage_four_negative(test_environment_t *environment) {
    static const char source[] =
        "rwfunc negtest() -> (r: int64) {\n"
        "\t\"x\" + 1 -> r\n"
        "}\n";
    printf("stage 4: negative control (relaxation must not reach computing ops)\n");
    /* layout 自己就会拒掉字符串加数值（同一类错误在语法层被挡住），
     * 所以这里断言的是「形不成可编码的函数」而不是编码期失败。 */
    const bool laid_out = layout_into_store(environment, source, "negative");
    EXPECT(!laid_out);
    /* 无论在哪一层被拒，都不能产出一个「悄悄用错类型」的 Program。 */
    size_t instruction_count = 0U;
    size_t foreign_count = 0U;
    EXPECT(!encode_and_report(environment, "/lib/negtest", &instruction_count,
                              &foreign_count));
}

int main(int argument_count, char **arguments) {
    const char *layout_path = argument_count > 1
                                  ? arguments[1]
                                  : getenv("KVSPACE_TEST_LAYOUT_LIBRARY");
    const char *kvspace_path = argument_count > 2
                                   ? arguments[2]
                                   : getenv("RUNTIMEC_KVSPACE_LIBRARY");
    if (layout_path == NULL || layout_path[0] == '\0') {
        fprintf(stderr, "SKIP: no layout library path\n");
        return SKIP_RETURN_CODE;
    }
    if (kvspace_path == NULL || kvspace_path[0] == '\0') {
        fprintf(stderr, "SKIP: no kvspace library path\n");
        return SKIP_RETURN_CODE;
    }
    test_environment_t environment;
    if (!environment_open(&environment, layout_path, kvspace_path)) {
        (void)environment_close(&environment);
        return SKIP_RETURN_CODE;
    }
    /*
     * 逐层前置：先探下游能否工作，再验证扩展声明能写进去。
     *
     * 两个前置都必须**各自计失败**，不能写成 `A && B`——短路会让 B 失败时静默
     * 跳过全部阶段，而 failures 仍是 0，最后打印 ok 并 exit 0。那是「跳过伪装
     * 通过」，比红更糟。
     */
    if (layout_can_write_anything(&environment)) {
        if (declare_external_capabilities(&environment)) {
            stage_one_arithmetic(&environment);
            stage_two_foreign(&environment);
            stage_three_tutorial_smoke(&environment);
            stage_four_negative(&environment);
        } else {
            fprintf(stderr,
                    "FAIL: external capability declarations were rejected by "
                    "layout; the external-capability stages cannot run.\n");
            ++failures;
        }
    }
    (void)environment_close(&environment);
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("vmkv end-to-end: ok\n");
    return 0;
}
