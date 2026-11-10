/* runtimec 公共 C 接口：把结构化请求翻译成 engine 的调用。 */

#include <stdio.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "engine/execution_window.h"
#include "engine/platform_session.h"

/* 派生状态的容量上限：不表达任何正确性开关，只是缓存规模。 */
#define RUNTIMEC_DEFAULT_COMPILATION_CACHE_CAPACITY 256U
#define RUNTIMEC_DEFAULT_MACHINE_CODE_CAPACITY (1U << 20)

static int kvlang_runtime_text_buffer_is_valid(
    const kvlang_runtime_text_buffer_t *buffer) {
    return buffer != NULL &&
           (buffer->capacity == 0U || buffer->data != NULL);
}

static void kvlang_runtime_text_buffer_clear(
    kvlang_runtime_text_buffer_t *buffer) {
    if (buffer != NULL) {
        buffer->length = 0U;
        buffer->required_length = 0U;
        if (buffer->data != NULL && buffer->capacity != 0U) {
            buffer->data[0] = '\0';
        }
    }
}

/*
 * 把一次失败的诊断记进调用方的错误对象。**返回值永远由调用方决定**，不由本函数
 * 决定——这就是「状态码为主，错误文本为辅」的唯一落地点（TASK-071）。
 *
 * 曾经这里是三条改返回值的路：
 *   - `error == NULL` ⇒ 报 INVALID。于是**不想要错误文本的调用方**拿到的码与
 *     真正的失败无关：连不上数据源（ERROR）也会读成 INVALID；
 *   - 缓冲放不下 ⇒ 报 INSUFFICIENT_CAPACITY，并覆盖 `error->status`。于是
 *     「调用方给多大缓冲」成了操作结论的一部分；
 *   - 缓冲畸形（capacity 非零而 data 为空）⇒ 报 INVALID，同上。
 *
 * 三者都是同一个错误：**把旁路资源的状况写进了主结论**。文本是辅助物：写不下
 * 就**不写**（不写半条），`required_length` 报出完整长度让调用方扩容重试；缓冲
 * 畸形就**只当没有文本**，不动状态码。
 */
static void kvlang_runtime_report(kvlang_runtime_error_t *error,
                                  runtimec_status_t status,
                                  const char *message) {
    if (error == NULL) {
        return;
    }
    error->status = status;
    error->operation_status = status;
    error->code = 0;
    kvlang_runtime_text_buffer_clear(&error->message);
    if (message == NULL || !kvlang_runtime_text_buffer_is_valid(&error->message)) {
        return;
    }
    const size_t message_length = strlen(message);
    /* 容量不足也照样报出完整长度：那是调用方扩容的唯一依据。 */
    error->message.required_length = message_length;
    if (error->message.capacity <= message_length) {
        return;
    }
    memcpy(error->message.data, message, message_length + 1U);
    error->message.length = message_length;
}

/* 失败 = 记诊断 + **原样返回操作结论**。 */
static runtimec_status_t kvlang_runtime_fail(
    kvlang_runtime_error_t *error, runtimec_status_t status,
    const char *message) {
    kvlang_runtime_report(error, status, message);
    return status;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_create(
    const kvlang_runtime_create_request_t *request,
    kvlang_runtime_t **output, kvlang_runtime_error_t *error) {
    if (output != NULL) {
        *output = NULL;
    }
    /* `error` 是可选的：不要诊断的调用方传 NULL，拿到的仍是同一个状态码。 */
    if (request == NULL || output == NULL ||
        request->data_source_name == NULL ||
        request->data_source_name[0] == '\0' ||
        request->instance_identifier == 0U) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec create request");
    }
    /*
     * **尺寸先于任何其它读取**：它就是为了「在按任何偏移读之前知道对方按哪个
     * 布局填的」。所以这条放在最前，且不向后兼容——按 p5，老调用方按旧尺寸
     * 传入直接拒绝，不猜字段位置。
     */
    if (request->structure_size != sizeof(kvlang_runtime_create_request_t)) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "runtimec create request has an unexpected structure_size");
    }
    /* 扩展表逐项校验：尺寸、opid 合法性、handler 非空。 */
    for (size_t module = 0U; module < request->extension_count; ++module) {
        const kvlang_runtime_extension_module_t *entry_module =
            &request->extensions[module];
        if (entry_module->module_name == NULL ||
            entry_module->module_name[0] == '\0' ||
            entry_module->entries == NULL || entry_module->entry_count == 0U) {
            return kvlang_runtime_fail(
                error, RUNTIMEC_STATUS_INVALID,
                "runtimec extension module is incomplete");
        }
        for (size_t item = 0U; item < entry_module->entry_count; ++item) {
            const kvlang_runtime_extension_entry_t *entry =
                &entry_module->entries[item];
            /* 结构漂移由 size 拦截，不靠约定。 */
            if (entry->structure_size != sizeof(kvlang_runtime_extension_entry_t)) {
                return kvlang_runtime_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    "runtimec extension entry has an unexpected structure_size");
            }
            if (entry->handler == NULL) {
                return kvlang_runtime_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    "runtimec extension entry has no handler");
            }
        }
    }
    if (request->extension_count != 0U && request->extensions == NULL) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "runtimec extension table is missing");
    }
    runtimec_session_options_t options;
    /* 结构体一次填满：engine 会校验 structure_size，缺省路径不留未初始化字节。 */
    options.structure_size = sizeof(options);
    options.allocator = runtimec_default_allocator();
    /* 编译缓存与机器码容量是派生状态的容量上限，不表达正确性开关。 */
    options.compilation_cache_capacity = RUNTIMEC_DEFAULT_COMPILATION_CACHE_CAPACITY;
    /*
     * phase 2 的阶段选择：BinRWIR Compiler 只有 VMBIN 档，这里关掉 JIT。
     * 不是缺省值——`enable_jit=true` 时编译器会明确返回 NOT_IMPLEMENTED（配置
     * 自洽不等于能力存在），公共入口就会必然失败。JIT 落地时把这两行改回
     * 计划值：machine_code_capacity 恢复 RUNTIMEC_DEFAULT_MACHINE_CODE_CAPACITY、
     * enable_jit 置真。
     */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    /* 扩展表原样透传给 session；它不认识「扩展」这个概念，只负责交给 registry。 */
    options.extension_modules = request->extensions;
    options.extension_count = request->extension_count;

    runtimec_platform_session_t *platform_session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_create(
        request->data_source_name, request->instance_identifier, &options,
        &platform_session, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /* engine 的错误文本是借用视图：在本帧内转成公共错误结构。 */
        if (engine_error.message.size != 0U &&
            engine_error.message.data != NULL) {
            return kvlang_runtime_fail(error, status,
                                       engine_error.message.data);
        }
        return kvlang_runtime_fail(error, status,
                                   runtimec_status_name(status));
    }
    *output = (kvlang_runtime_t *)platform_session;
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_destroy(
    kvlang_runtime_t *runtime, kvlang_runtime_error_t *error) {
    /* `error` 可选，理由同 create。 */
    if (runtime == NULL) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec destroy request");
    }
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    const runtimec_status_t status = runtimec_platform_session_destroy(
        (runtimec_platform_session_t *)runtime, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            engine_error.message.data != NULL ? engine_error.message.data
                                              : runtimec_status_name(status));
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_execute(
    kvlang_runtime_t *runtime,
    const kvlang_runtime_execute_request_t *request,
    kvlang_runtime_execute_result_t *output,
    kvlang_runtime_error_t *error) {
    if (output != NULL) {
        kvlang_runtime_text_buffer_clear(&output->value);
        kvlang_runtime_text_buffer_clear(&output->program_counter);
    }
    /* `error` 可选；`output` 的两个缓冲不是——它们是**结果**，必须收得下。 */
    if (runtime == NULL || request == NULL || output == NULL ||
        !kvlang_runtime_text_buffer_is_valid(&output->value) ||
        !kvlang_runtime_text_buffer_is_valid(&output->program_counter) ||
        request->entry_kind < KVLANG_RUNTIME_ENTRY_FUNCTION ||
        request->entry_kind > KVLANG_RUNTIME_ENTRY_BOOTSTRAP ||
        request->entry_name == NULL || request->entry_name[0] == '\0' ||
        (request->argument_count != 0U && request->arguments == NULL)) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid runtimec execute request");
    }
    /*
     * 本版只实现 `FUNCTION`：给一个函数路径，跑到第一个退出边界为止。
     *
     * 其余三档**显式 NOT_IMPLEMENTED**，不静默放过：
     * `PROGRAM_COUNTER` 与 `VTHREAD` 要读持久化游标（`/vthread/<vid>/‥pc`），
     * `BOOTSTRAP` 要建 vthread——三者都需要先有「从存储恢复」的入口，
     * 那是下一步的事。用「先当 FUNCTION 跑」代劳，会让调用方以为恢复生效了。
     */
    if (request->entry_kind != KVLANG_RUNTIME_ENTRY_FUNCTION) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "only ENTRY_FUNCTION is implemented; PROGRAM_COUNTER / VTHREAD / "
            "BOOTSTRAP need a storage-recovery entry");
    }

    runtimec_session_t *session = NULL;
    runtimec_error_t engine_error;
    runtimec_error_reset(&engine_error);
    runtimec_status_t status = runtimec_platform_session_get_session(
        (const runtimec_platform_session_t *)runtime, &session, &engine_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            engine_error.message.data != NULL ? engine_error.message.data
                                              : runtimec_status_name(status));
    }

    /*
     * 帧路径与 vthread 标识由本层定：调用方给的是「函数名」，不是帧位置。
     *
     * 每次 execute 用同一组固定值（`runtimec-execute` / `[1]`）：本入口是
     * **一次调用一个函数**，不承载续跑。要续跑得用 `VTHREAD` 档，
     * 而那一档明确未实现——所以这里不需要唯一化，也不需要保存。
     */
    /* vthread 标识必须是**数字字符串**：`execution_window` 用它算
     * checkpoint 的落点，非数字直接判请求非法（实测 "runtimec-execute" 被拒）。
     * 0 是合法的固定值——本入口不承载续跑，不需要唯一化。 */
    static const char entry_vthread[] = "0";
    static const char entry_frame[] = "/vthread/0/[1]";
    /*
     * `entry_name` 是**裸函数名**（`"test"`），不是 key。
     *
     * 本层无条件补 `/lib/` 前缀。若放一个已经带前缀的名字进来，拼出来就是
     * `/lib//lib/test`——**双前缀**。而 kvspace 对缺失 key 返回
     * `rc=0 + out=NULL`，**不报错**，于是这个错只表现为下游那句
     * 「function has no readable source revision key」，真因被埋掉。
     *
     * ⇒ **一个名字只许一种含义**（p0）：以 `/` 开头一律 INVALID，不走前缀。
     * 不提供「两种形态都收」的兼容——那正是「一个名字两种含义」。
     */
    if (request->entry_name[0] == '/') {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "entry name must be a bare function name, e.g. \"test\"; "
            "got a key starting with '/' (this entry adds the /lib/ prefix "
            "itself)");
    }
    char function_path[512];
    const int function_length =
        snprintf(function_path, sizeof(function_path), "/lib/%s",
                 request->entry_name);
    if (function_length < 0 ||
        (size_t)function_length >= sizeof(function_path)) {
        return kvlang_runtime_fail(error, RUNTIMEC_STATUS_INVALID,
                                   "entry name is too long");
    }

    runtimec_execution_window_request_t window_request;
    memset(&window_request, 0, sizeof(window_request));
    window_request.function_path =
        runtimec_string_view_from_c_string(function_path);
    window_request.vthread_identifier =
        runtimec_string_view_from_c_string(entry_vthread);
    window_request.frame_path =
        runtimec_string_view_from_c_string(entry_frame);
    window_request.stack_identifier = 1;
    window_request.source_logical_program_counter = 1U;
    window_request.arguments = NULL;
    window_request.argument_count = 0U;

    runtimec_execution_window_result_t window_result;
    memset(&window_result, 0, sizeof(window_result));
    runtimec_error_t window_error;
    runtimec_error_reset(&window_error);
    status = runtimec_execution_window_execute(session, &window_request,
                                               &window_result, &window_error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return kvlang_runtime_fail(
            error, status,
            window_error.message.data != NULL ? window_error.message.data
                                              : runtimec_status_name(status));
    }
    /* 只有 DONE 才是「跑完了」。CALL / HANDOFF / YIELD 都表示**停在半路**，
     * 报成功会让调用方以为函数返回了——那正是「以看起来合理的值失败」。 */
    if (window_result.execution_exit.kind != BINRWIR_EXECUTION_EXIT_DONE) {
        return kvlang_runtime_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "execution stopped before DONE: this entry does not drive CALL / "
            "HANDOFF / YIELD yet");
    }

    /* `value`：函数返回值（DONE 时可能有）；没有就留空串，不编。 */
    if (window_result.return_value.size != 0U) {
        if (output->value.capacity < window_result.return_value.size + 1U) {
            output->value.required_length = window_result.return_value.size;
            return kvlang_runtime_fail(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                                       "value buffer is too small");
        }
        memcpy(output->value.data, window_result.return_value.data,
               window_result.return_value.size);
        output->value.data[window_result.return_value.size] = '\0';
        output->value.length = window_result.return_value.size;
    }
    /* `program_counter`：停在哪个 source pc（十进制文本）。 */
    {
        char counter_text[32];
        const int counter_length =
            snprintf(counter_text, sizeof(counter_text), "%u",
                     window_result.execution_exit.logical_program_counter);
        if (counter_length < 0 ||
            output->program_counter.capacity < (size_t)counter_length + 1U) {
            return kvlang_runtime_fail(error,
                                       RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                                       "program_counter buffer is too small");
        }
        memcpy(output->program_counter.data, counter_text,
               (size_t)counter_length + 1U);
        output->program_counter.length = (size_t)counter_length;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
