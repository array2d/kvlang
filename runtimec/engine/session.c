/*
 * 组件：engine/session 的实现
 * 作用：显式 owner graph 的根，持有 host 能力表副本、vmkv encoder、
 *       BinRWIR Compiler 与 compilation repository。
 * 联系：interface 经 platform_session 调用本组件；route 在 session 上执行。
 * 约束：依赖任一构造失败都原样上报第一个失败状态，并按逆序销毁已建对象；
 *       不用 function-static / global / thread-local 藏状态。
 */

#include "engine/session.h"

#include <stdio.h>
#include <string.h>

#include "binrwir_compiler/compiler.h"
#include "engine/compilation_repository.h"
#include "engine/frame_state.h"
#include "binrwir/registry.h"
#include "vmbin/handlers/container.h"
#include "vmbin/handlers/host.h"
#include "vmbin/handlers/langtype.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_generated.h"
#include "vmbin/handlers/scatter.h"
#include "vmbin/handlers/time.h"
#include "vmbin/handlers/string.h"
#ifdef RUNTIMEC_TEMP_OUTPUT
#include "vmbin/temp_output.h"
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
    /* 借用：所有权在 platform_session，session 只读不销毁。 */
    runtimec_kvspace_access_t *kvspace_access;
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
    /*
     * **跨 execute 存活的帧状态表**（`engine/frame_state.h`）。
     *
     * 窗口一次 execute 的生命周期 ≠ 一帧从建立到 DONE 的生命周期。驱动靠「对同一
     * frame_path 再 execute 一次」来 resume，若每次 execute 都重建绑定表，父帧在
     * 指令里绑过的局部槽就随旧表一起消失（`strings.kv` 的 `0x4501`）。
     *
     * 表按值持有在这里，条目与绑定表归 `engine/frame_state` 管理。
     */
    runtimec_frame_state_table_t frames;
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
    runtimec_kvspace_access_t *kvspace_access,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_session != NULL) {
        *output_session = NULL;
    }
    if (kvspace_access == NULL || options == NULL ||
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
    session->kvspace_access = kvspace_access;
    session->options = *options;
    /* 借用调用方的句柄：session 不连接、不关闭，只透传给执行期。 */
    session->kvspace_handle = options->kvspace_handle;
    /* 帧状态表只置空，不分配：没有执行就没有帧。 */
    runtimec_frame_state_table_initialize(&session->frames, allocator);

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
#ifdef RUNTIMEC_TEMP_OUTPUT
    {
        size_t temp_output_entry_count = 0U;
        const binrwir_registry_entry_t *temp_output_entries =
            runtimec_temp_output_entries(&temp_output_entry_count);
        status = binrwir_registry_append(
            &session->builtin_registry, "temp_output", temp_output_entries,
            temp_output_entry_count, error);
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
    /* container 模块（B 线）：compactarray / scatterarray / map·new，8 条。 */
    {
        size_t container_entry_count = 0U;
        const binrwir_registry_entry_t *container_entries =
            runtimec_container_handler_entries(&container_entry_count);

        status = binrwir_registry_append(
            &session->builtin_registry, "container", container_entries,
            container_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    /* scalar 模块（A 线）：算术 / 位运算 / 比较，35 条，区间 0x0002–0x002B。 */
    {
        size_t scalar_entry_count = 0U;
        const binrwir_registry_entry_t *scalar_entries =
            runtimec_scalar_handler_entries(&scalar_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "scalar",
                                         scalar_entries, scalar_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    /*
     * scalar_generated 模块：特化轮次产出的 opid 的 handler（1836 条）。
     *
     * 只生成不注册，等于没做：特化把 opid 改成 0x8000+ 之后，注册表查不到就是
     * 全线 INVALID_INSTRUCTION。必须在任何一次特化之前装好——session 创建期
     * 天然早于编译，顺序由生命周期保证，不靠调用顺序自觉。
     */
    {
        status = binrwir_registry_append(
            &session->builtin_registry, "scalar_generated",
            scalar_generated_entries, scalar_generated_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    /* string 模块（B 线）：13 条。行为验证由 coding4 另行补，接线只验条目与去重。 */
    {
        size_t string_entry_count = 0U;
        const binrwir_registry_entry_t *string_entries =
            runtimec_string_handler_entries(&string_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "string",
                                         string_entries, string_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    /* langtype 模块：`parselangtype` 两条（0x010D compactarray / 0x0136 xv）。
     * 此前它导出了条目表却没人装配——编得出、装不进，执行到即 INVALID。 */
    {
        size_t langtype_entry_count = 0U;
        const binrwir_registry_entry_t *langtype_entries =
            runtimec_langtype_handler_entries(&langtype_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "langtype",
                                         langtype_entries, langtype_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    /* scatter 模块（B 线）：散 key 变长数组，8 条。同 string，接线先于行为验证。 */
    {
        size_t scatter_entry_count = 0U;
        const binrwir_registry_entry_t *scatter_entries =
            runtimec_scatter_handler_entries(&scatter_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "scatter",
                                         scatter_entries, scatter_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }

    /*
     * time 模块：`time·` 与 `time/duration·` 两族，19 条。
     *
     * 它们**不是** kvspace 能力，也不是标量运算——是内建表里独立的一族
     * （spec `05-runtime语义/05-runtimec内建.kv` 的「`·` 限定内建族」把它列成
     * 单独一行）。所以有自己的模块与自己的注册行，不并入 host 或 scalar。
     */
    {
        size_t time_entry_count = 0U;
        const binrwir_registry_entry_t *time_entries =
            runtimec_time_handler_entries(&time_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "time",
                                         time_entries, time_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }

    /*
     * 扩展算子：由调用方（byteseek）通过 options 传入。
     *
     * 装载在**所有内建模块之后**：`binrwir_registry_append` 对重复 opid 拒绝而
     * 不覆盖，所以内建先到先得——扩展撞了内建 opid 会被具名拒绝，不会被内建悄悄
     * 顶掉，也不会顶掉内建。顺序即优先级，不需要额外的冲突表。
     *
     * 校验（`structure_size`、opid 合法性、handler 非空）在公共接口 `create` 里
     * 做，这里只把已校验的表转成内部条目形状——校验只有一处，不在这里重复。
     */
    if (options->extension_modules != NULL && options->extension_count != 0U) {
        /*
         * **不 include 公共头**（`kvlang/runtimec.h`）：那一层是给外部调用方的，
         * engine 依赖它会让分层边反向。这里只按**字段顺序**读同一块内存——
         * 公共接口已经校验过 `structure_size`，布局对不上在那一层就失败了。
         */
        const runtimec_extension_module_view_t *modules =
            (const runtimec_extension_module_view_t *)options->extension_modules;
        for (size_t module = 0U; module < options->extension_count; ++module) {
            const runtimec_extension_module_view_t *entry_module = &modules[module];
            for (size_t item = 0U; item < entry_module->entry_count; ++item) {
                const runtimec_extension_entry_view_t *source =
                    &entry_module->entries[item];
                const binrwir_registry_entry_t entry = {
                    .operation_identifier = source->operation_identifier,
                    .flags = source->flags,
                    .handler = (binrwir_registry_handler_t)source->handler,
                };
                status = binrwir_registry_append(
                    &session->builtin_registry, entry_module->module_name,
                    &entry, 1U, error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    /*
                     * **补一句具名诊断**：registry 的常量消息只说「重复了」，
                     * 十个扩展一起装载时那等于没定位。模块名与 opid 在这里补，
                     * 因为消息缓冲必须在失败帧之后仍然有效——在 registry 里拼
                     * 栈缓冲会悬空（实测出过乱码）。
                     */
                    static char extension_error[256];
                    (void)snprintf(
                        extension_error, sizeof(extension_error),
                        "extension module \"%s\" registers opid 0x%04X which is "
                        "already taken",
                        entry_module->module_name,
                        (unsigned)entry.operation_identifier);
                    runtimec_error_reset(error);
                    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                                       1,
                                       runtimec_string_view_from_c_string(
                                           extension_error),
                                       RUNTIMEC_PROGRAM_COUNTER_NONE);
                    goto fail;
                }
            }
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
    /*
     * 帧状态兜底：DONE/ERROR 时会话已经关过每一帧，这里只剩异常路径的残留。
     * **先清帧再拆依赖**：绑定表里存的是 kvspace 地址，拆 kvspace 之前让它归零。
     */
    runtimec_frame_state_table_release(&session->frames);
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

runtimec_status_t runtimec_session_get_kvspace_access(
    runtimec_session_t *session,
    runtimec_kvspace_access_t **output_kvspace_access) {
    if (output_kvspace_access != NULL) {
        *output_kvspace_access = NULL;
    }
    if (session == NULL || output_kvspace_access == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_kvspace_access = session->kvspace_access;
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

runtimec_status_t runtimec_session_get_frame_state(
    runtimec_session_t *session,
    runtimec_frame_state_table_t **output_frame_state) {
    if (output_frame_state != NULL) {
        *output_frame_state = NULL;
    }
    if (session == NULL || output_frame_state == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_frame_state = &session->frames;
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
