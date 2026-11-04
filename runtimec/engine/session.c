/*
 * 组件：engine/session 的实现
 * 作用：显式 owner graph 的根，持有 host 能力表副本、vmkv encoder、
 *       BinRWIR Compiler 与 compilation repository。
 * 联系：interface 经 platform_session 调用本组件；route 在 session 上执行。
 * 约束：依赖任一构造失败都原样上报第一个失败状态，并按逆序销毁已建对象；
 *       不用 function-static / global / thread-local 藏状态。
 */

#include "engine/session.h"

#include <string.h>

#include "binrwir_compiler/compiler.h"
#include "engine/compilation_repository.h"
#include "binrwir/registry.h"
#include "vmbin/handlers/host.h"
#ifdef RUNTIMEC_TEMP_PRINTLN
#include "vmbin/temp_println.h"
#endif
#include "vmkv/encoder.h"

/* 编译器修订号：compilation repository 的 key 字段之一，非零且在构建内固定。 */
#define RUNTIMEC_SESSION_COMPILER_REVISION 1U

/* JIT 目标三元组：按宿主架构选，编译期常量，不留运行时探测。 */
#if defined(__x86_64__) && !defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_X86_64
#define RUNTIMEC_SESSION_JIT_TRIPLE "x86_64-unknown-linux-gnu"
#elif defined(__x86_64__) && defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_X86_64
#define RUNTIMEC_SESSION_JIT_TRIPLE "x86_64-apple-darwin"
#elif defined(__aarch64__) && defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_AARCH64
#define RUNTIMEC_SESSION_JIT_TRIPLE "aarch64-apple-darwin"
#else
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_AARCH64
#define RUNTIMEC_SESSION_JIT_TRIPLE "aarch64-unknown-linux-gnu"
#endif

struct runtimec_session_t {
    runtimec_host_services_t host_services;
    runtimec_session_options_t options;
    vmkv_encoder_t *vmkv_encoder;
    binrwir_compiler_t *binrwir_compiler;
    runtimec_compilation_repository_t *compilation_repository;
    /* builtin 注册表本阶段零条目：没有实现，所以没有条目。 */
    binrwir_registry_t builtin_registry;
    /* checkpoint 的单调提交序号，route 经访问器取号。 */
    uint64_t checkpoint_sequence;
    /* kvspace 句柄：调用方的，session 只借用；EventHandler 需要存储时经此取。 */
    void *kvspace_handle;
};

static runtimec_status_t runtimec_session_fail(runtimec_error_t *error,
                                               runtimec_status_t status,
                                               int64_t code,
                                               const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/* 依赖构造失败时点名是哪个组件；失败状态本身原样透传。 */
static runtimec_status_t runtimec_session_forward(
    runtimec_error_t *error, runtimec_status_t status, const char *component) {
    static char message[160];
    const char *prefix = "dependency failed: ";
    size_t length = 0U;
    for (const char *cursor = prefix; *cursor != '\0'; cursor++) {
        message[length++] = *cursor;
    }
    for (const char *cursor = component; *cursor != '\0' &&
                                      length + 1U < sizeof(message);
         cursor++) {
        message[length++] = *cursor;
    }
    message[length] = '\0';
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t runtimec_session_create(
    const runtimec_host_services_t *host_services,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_session != NULL) {
        *output_session = NULL;
    }
    if (!runtimec_host_services_is_valid(host_services) || options == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(&options->allocator) ||
        options->compilation_cache_capacity == 0U ||
        (options->enable_jit && options->machine_code_capacity == 0U) ||
        (!options->enable_jit && options->machine_code_capacity != 0U) ||
        output_session == NULL || error == NULL) {
        return runtimec_session_fail(error, RUNTIMEC_STATUS_INVALID, 1,
                                     "invalid session create request");
    }

    const runtimec_allocator_t allocator = options->allocator;
    runtimec_session_t *session = runtimec_allocator_allocate(
        &allocator, sizeof(*session), _Alignof(runtimec_session_t));
    if (session == NULL) {
        return runtimec_session_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
                                     "session allocation failed");
    }
    memset(session, 0, sizeof(*session));
    session->host_services = *host_services;
    session->options = *options;
    /* 借用调用方的句柄：session 不连接、不关闭，只透传给执行期。 */
    session->kvspace_handle = options->kvspace_handle;

    /*
     * builtin 注册表：置空后按模块追加。零条目是合法状态——不装任何 builtin 的
     * session 依赖它。每个模块在自己的文件里提供静态条目表，本函数只负责追加；
     * 装载期校验（重复 opid、空 handler、超限）都在 append 里。
     */
    runtimec_status_t status = binrwir_registry_initialize(
        &session->builtin_registry);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "builtin registry");
        goto fail;
    }
#ifdef RUNTIMEC_TEMP_PRINTLN
    {
        size_t temp_println_entry_count = 0U;
        const binrwir_registry_entry_t *temp_println_entries =
            runtimec_temp_println_entries(&temp_println_entry_count);
        status = binrwir_registry_append(
            &session->builtin_registry, "temp_println", temp_println_entries,
            temp_println_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
#endif
    /*
     * host 服务模块（C 线）：kvspace / vthread / random 共 21 条。
     * 每个模块一行，由各自文件提供静态条目表；本函数只负责追加。
     * 其余三条线（scalar / container / string）交付后各加一行，不批量改。
     */
    {
        size_t host_entry_count = 0U;
        const binrwir_registry_entry_t *host_entries =
            runtimec_host_handler_entries(&host_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "host",
                                         host_entries, host_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }

    status = vmkv_encoder_create(allocator, &session->vmkv_encoder, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "vmkv encoder");
        goto fail;
    }

    binrwir_compiler_configuration_t configuration;
    memset(&configuration, 0, sizeof(configuration));
    configuration.structure_size = sizeof(configuration);
    configuration.compiler_revision = RUNTIMEC_SESSION_COMPILER_REVISION;
    configuration.enable_jit = options->enable_jit;
    if (options->enable_jit) {
        configuration.jit_machine_code_capacity =
            options->machine_code_capacity;
        configuration.jit_target.architecture =
            RUNTIMEC_SESSION_JIT_ARCHITECTURE;
        configuration.jit_target.triple = runtimec_string_view_from_c_string(
            RUNTIMEC_SESSION_JIT_TRIPLE);
        configuration.jit_target.feature_mask = 0U;
    }
    status = binrwir_compiler_create(&allocator, &configuration,
                                     &session->binrwir_compiler, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "binrwir compiler");
        goto fail;
    }

    status = runtimec_compilation_repository_create(
        allocator, options->compilation_cache_capacity,
        &session->compilation_repository, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status,
                                          "compilation repository");
        goto fail;
    }

    *output_session = session;
    return RUNTIMEC_STATUS_SUCCESS;

fail:
    /* 按依赖的逆序释放已建对象；只释放确实建成的那些。 */
    if (session->compilation_repository != NULL) {
        (void)runtimec_compilation_repository_destroy(
            session->compilation_repository);
    }
    if (session->binrwir_compiler != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compiler_destroy(session->binrwir_compiler, &ignored);
    }
    if (session->vmkv_encoder != NULL) {
        (void)vmkv_encoder_destroy(session->vmkv_encoder);
    }
    (void)runtimec_allocator_deallocate(&allocator, session, sizeof(*session),
                                        _Alignof(runtimec_session_t));
    *output_session = NULL;
    return status;
}

runtimec_status_t runtimec_session_destroy(runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const runtimec_allocator_t allocator = session->options.allocator;
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (session->compilation_repository != NULL) {
        status = runtimec_compilation_repository_destroy(
            session->compilation_repository);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (session->binrwir_compiler != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        status = binrwir_compiler_destroy(session->binrwir_compiler, &ignored);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (session->vmkv_encoder != NULL) {
        status = vmkv_encoder_destroy(session->vmkv_encoder);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    memset(session, 0, sizeof(*session));
    return runtimec_allocator_deallocate(&allocator, session,
                                         sizeof(*session),
                                         _Alignof(runtimec_session_t));
}

runtimec_status_t runtimec_session_get_host_services(
    runtimec_session_t *session,
    const runtimec_host_services_t **output_host_services) {
    if (output_host_services != NULL) {
        *output_host_services = NULL;
    }
    if (session == NULL || output_host_services == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_host_services = &session->host_services;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_get_kvspace_handle(
    runtimec_session_t *session, void **output_kvspace_handle) {
    if (output_kvspace_handle != NULL) {
        *output_kvspace_handle = NULL;
    }
    if (session == NULL || output_kvspace_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* NULL 是合法值（不接存储的用例）：成功但句柄为空，不当作失败。 */
    *output_kvspace_handle = session->kvspace_handle;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (session->compilation_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 派生状态只有 compilation repository：清空即可，kvspace 明文不动。 */
    return runtimec_compilation_repository_clear(
        session->compilation_repository);
}

runtimec_status_t runtimec_session_get_runtime_components(
    runtimec_session_t *session,
    runtimec_session_runtime_components_t *output_components) {
    if (output_components != NULL) {
        *output_components = (runtimec_session_runtime_components_t){0};
    }
    if (session == NULL || output_components == NULL ||
        session->vmkv_encoder == NULL || session->binrwir_compiler == NULL ||
        session->compilation_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_components->vmkv_encoder = session->vmkv_encoder;
    output_components->binrwir_compiler = session->binrwir_compiler;
    output_components->compilation_repository =
        session->compilation_repository;
    output_components->builtin_registry = &session->builtin_registry;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_take_checkpoint_sequence(
    runtimec_session_t *session, uint64_t *output_sequence) {
    if (output_sequence != NULL) {
        *output_sequence = 0U;
    }
    if (session == NULL || output_sequence == NULL ||
        session->checkpoint_sequence == UINT64_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    session->checkpoint_sequence += 1U;
    *output_sequence = session->checkpoint_sequence;
    return RUNTIMEC_STATUS_SUCCESS;
}
