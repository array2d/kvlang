/*
 * 组件：engine/frame_driver 的实现
 * 作用：见 `frame_driver.h`——调用帧驱动，把 `EXIT_CALL` 变成「在新帧里再执行
 *       一次」，并在返回链断裂时如实报错。
 * 联系：`kv_runner`（语料）与 `kvlangrun`（benchmark 驱动）共用本实现；
 *       两者都只提供「调谁、怎么比」，不解释 CALL。
 * 约束：**无文件级可变状态**——全部跨调用状态在 `frame_driver_t` 里，调用方持有。
 *       本文件只做「按既有语义搬过来」的机械转换，控制流与消息文本一字未改。
 */

#include "engine/frame_driver.h"

#include <stdio.h>
#include <stdio.h>
#include <string.h>


#include "binrwir/binvthread_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "engine/execution_window.h"


/* 记一条；同 (帧路径, 槽) 覆盖（递归同帧号不会重入，覆盖即正确）。 */
static void effective_key_record(frame_driver_t *driver,
                                 const char *frame_path, uint16_t slot,
                                 const char *key) {
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (driver->effective_keys[ordinal].parameter_slot == slot &&
            strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            snprintf(driver->effective_keys[ordinal].key,
                     sizeof(driver->effective_keys[ordinal].key), "%s", key);
            return;
        }
    }
    if (driver->effective_key_count >= FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY) {
        return;
    }
    frame_driver_effective_key_t *entry = &driver->effective_keys[driver->effective_key_count++];
    snprintf(entry->frame_path, sizeof(entry->frame_path), "%s", frame_path);
    entry->parameter_slot = slot;
    snprintf(entry->key, sizeof(entry->key), "%s", key);
}

/* 查一条；没有则返回 NULL。 */
static const char *effective_key_lookup(const frame_driver_t *driver,
                                        const char *frame_path,
                                        uint16_t slot) {
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (driver->effective_keys[ordinal].parameter_slot == slot &&
            strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            return driver->effective_keys[ordinal].key;
        }
    }
    return NULL;
}

/* 帧弹出：把该帧的所有记录整段清掉（同一个 frame_path 前缀只属于它）。 */
static void effective_key_forget_frame(frame_driver_t *driver,
                                       const char *frame_path) {
    size_t kept = 0U;
    for (size_t ordinal = 0U; ordinal < driver->effective_key_count; ++ordinal) {
        if (strcmp(driver->effective_keys[ordinal].frame_path, frame_path) == 0) {
            continue;
        }
        if (kept != ordinal) {
            driver->effective_keys[kept] = driver->effective_keys[ordinal];
        }
        kept += 1U;
    }
    driver->effective_key_count = kept;
}


runtimec_status_t frame_driver_initialize(frame_driver_t *driver,
                                          runtimec_session_t *session) {
    if (driver == NULL || session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(driver, 0, sizeof(*driver));
    driver->session = session;
    return RUNTIMEC_STATUS_SUCCESS;
}

frame_driver_outcome_t frame_driver_execute(
    frame_driver_t *driver, const char *function_path, uint32_t source_pc,
    int32_t depth, const char *frame_segment,
    const char *vthread_identifier,
    const runtimec_argument_description_t *arguments, size_t argument_count,
    binrwir_execution_exit_t *out_exit, char *output_message,
    size_t output_message_capacity) {
    /*
     * ── 为什么是循环，不是「同一个函数的尾递归」────────────────────────
     *
     * 本函数在 `EXIT_CALL`（子帧）与「续跑同一帧」两处都要再执行一次。
     * 子帧那次**必须**递归（子帧有它自己的 depth / 帧路径 / 实参，回来后
     * 还要清它的 effective key 记录），保留。**同一帧的续跑改成循环**——
     * 它只是换一个 `source_pc`，帧自身（函数、depth、段、实参）全不变。
     *
     * **这不是优化，是正确性**：C **不保证**尾调用消除，`-O0` 下必然累积
     * 栈帧。实测（helper 的 gdb 回栈，N=900 崩）：`/lib/test` `depth=1`
     * `pc=1` 在 `frame_driver.c:566` 自递归，`#9..#39` 全是同一个调用点，
     * 深度 **O(步数)** —— 约 900 步就把 8MB 栈耗尽。任何超过约 900 步的
     * 程序都会崩，那不是边界情况，是主路径。
     *
     * 循环版把「续跑」变成改 `source_pc` 后 `continue`，栈深度恒为 1。
     */
    char frame_path[128];
    /*
     * 目录名带唯一序号：`/vthread/{vid}/[{depth}.#{serial}]`。
     * `depth` 保留在最前面，读的人仍能一眼看出层级；序号保证同深度多次调用
     * 不共用目录（见 `g_frame_serial` 的注释）。
     */
    const int frame_length =
        snprintf(frame_path, sizeof(frame_path), "/vthread/%s/[%s]",
                 vthread_identifier, frame_segment);
    if (frame_length < 0 || (size_t)frame_length >= sizeof(frame_path)) {
        return FRAME_DRIVER_OUTCOME_INVALID;
    }
    /*
     * 续跑循环：**只有 `source_pc` 会变**，帧路径与服务对象都不变。
     * 每一次迭代对应「同一帧再执行一步」。
     */
    for (;;) {
    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string(function_path);
    request.vthread_identifier =
        runtimec_string_view_from_c_string(vthread_identifier);
    request.frame_path = runtimec_string_view_from_c_string(frame_path);
    request.stack_identifier = depth;
    request.source_logical_program_counter = source_pc;
    request.arguments = arguments;
    request.argument_count = argument_count;

    runtimec_execution_window_result_t result;
    memset(&result, 0, sizeof(result));
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    /* 窗口的 message 是**线程局部静态缓冲**（`execution_window.c:922`），
     * 下一次失败会覆盖它。所以这里立刻拷走，之后任何再失败的调用都会冲掉原话。 */
    char message_copy[256];
    message_copy[0] = '\0';
    if (runtimec_execution_window_execute(driver->session, &request,
                                          &result, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *out_exit = result.execution_exit;
        if (error.message.data != NULL && error.message.size != 0U) {
            const size_t copy = error.message.size < sizeof(message_copy) - 1U
                                    ? error.message.size
                                    : sizeof(message_copy) - 1U;
            memcpy(message_copy, error.message.data, copy);
            message_copy[copy] = '\0';
        }
        /* 把窗口给的消息带出来：`detail` 是分类码，`message` 才是能定位的那句。
         *
         * `detail_code` 一个字段承载**两个枚举**：窗口自己的诊断码
         * （`execution_window.c:33-50`，6=COMPILE_FAILED）与解释器的诊断码
         * （`interpreter.h:43-51`，6=UNRESOLVED_TARGET）。
         *
         * 分辨办法：**窗口的 fail 路径会把 error 填上**（`execution_window.c:903-933`
         * 那条会写 "instruction execution failed (execution exit detail=…)"），
         * 而窗口自己提前返回的路径不会带这句。所以 `message` 空 = 窗口层失败。
         * 这也是 `execution_window.c:905` 注释里说的「读的人会把两者读错」。
         */
        if (output_message != NULL && output_message_capacity != 0U) {
            snprintf(output_message, output_message_capacity, "%s",
                     message_copy);
        }
        return FRAME_DRIVER_OUTCOME_EXECUTE_FAILED;
    }
    *out_exit = result.execution_exit;
    /* 本帧实际执行的 Program。递归时每帧不同，所以**从结果里取**，
     * 不能存在驱动里当全局——那就是「一个函数只有一个活动帧」的错误假设。 */
    const binrwir_binfunc_t *frame_program = result.program;
    switch (result.execution_exit.kind) {
    case BINRWIR_EXECUTION_EXIT_DONE:
        return FRAME_DRIVER_OUTCOME_DONE;
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return FRAME_DRIVER_OUTCOME_HANDOFF;
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return FRAME_DRIVER_OUTCOME_ERROR;
    case BINRWIR_EXECUTION_EXIT_YIELD:
        /* `‥status` 不是继续态：停在当前指令之前，位置已提交。不是失败，
         * 也不是 DONE——调用方可以退出，之后按同一 vid 再进入。 */
        return FRAME_DRIVER_OUTCOME_YIELD;
    case BINRWIR_EXECUTION_EXIT_CALL: {
        /* 返回链断裂：调用方没给 resume 点。spec 要求报错并保留帧，
         * **不许**当成「跑完了」。 */
        if (result.execution_exit.resume_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /* 被调函数名在符号池里（不含 `/lib/`，见 encoder.c:1222-1226），
         * 补前缀才是它的绝对路径。名字可能已带包前缀（`outer/inner·add1`），
         * 补的是 `/lib/` 这一段，名字原样。 */
        runtimec_string_view_t callee_name =
            runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t callee_langtype =
            runtimec_string_view_make(NULL, 0U);
        /* `binrwir_binfunc_get_symbol` 返回 **runtimec_status_t**，不是 bool。
         * 写成 `!get_symbol(...)` 会把 SUCCESS(0) 取反成 true，于是每次解析都
         * 判失败——两套返回值语义混用，而且不报错，只是走错分支。 */
        if (binrwir_binfunc_get_symbol(
                frame_program,
                (uint16_t)result.execution_exit.target_identifier,
                &callee_name, &callee_langtype) != RUNTIMEC_STATUS_SUCCESS ||
            callee_name.size == 0U) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /* **必须用 `%.*s` + size**：`runtimec_string_view_t` 是长度视图，
         * 不保证 NUL 终止。用 `%s` 会把视图后面的字节读进来——实测拼出
         * `/lib/add1rwfuncvint64int64`，于是 source revision 查不到，
         * 而错误只报「不可用」，看起来像 store 坏了。 */
        char callee_path[FRAME_DRIVER_PATH_CAPACITY];
        const int callee_length =
            snprintf(callee_path, sizeof(callee_path), "/lib/%.*s",
                     (int)callee_name.size, callee_name.data);
        if (callee_length < 0 ||
            (size_t)callee_length >= sizeof(callee_path)) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /* ── 物化常量实参到被调帧的 `._lit<i>` ─────────────────────
         *
         * spec `04-layout语义/03-函数:15-17`：字面量实参**在调用绑定期**物化为
         * 被调帧的 `._lit<i>` KV 值，再按地址传给函数。
         * （与 `05-runtime语义/04-函数调用:127-133` 不冲突——那条管同一个 Program
         * 内的普通 rwir，常量内联为 LITERAL 引用、不落临时 key。两个域。）
         *
         * `add1(41)` 是跨帧 rwfunc 调用，`*int64 $x` 是指针绑定需要一个 key，
         * 所以 `41` 必须物化。落点是新帧目录下，所以这里先建目录。
         */
        /*
         * **不在这里预建子帧目录**：帧路径由 `execute_frame_until_done` 自己算
         * （含唯一序号 `g_frame_serial`），执行窗口的 `ensure_frame` 会建它。
         *
         * 曾经在这里按 `/[depth+1]` 先 mkindex：那时两处算的是同一个名字，所以
         * 看不出问题；加了序号之后两处算法不一致，预建的目录和实际用的对不上，
         * 而 `ensure_frame` 又会因「帧上已有另一个函数」被拒——正是它要修的那个
         * 症状。**同一件事两个地方算，就是这个下场。**
         */
        /* 取出 call 指令的读参项（跳过最前的 FUNCTION 那个）。 */
        uint32_t call_instruction_identifier = 0U;
        binrwir_instruction_view_t call_instruction;
        memset(&call_instruction, 0, sizeof(call_instruction));
        if (binrwir_binfunc_find_instruction(
                frame_program,
                result.execution_exit.logical_program_counter,
                &call_instruction_identifier) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_get_instruction(frame_program,
                                            call_instruction_identifier,
                                            &call_instruction) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /* 驱动只给**描述**，物化归 `execution_window`。
         *
         * 早先这里自己拼 `._lit<i>` 并 `set_value`——那是让缺少 snapshot 的层
         * 去造 key：绕开来会破坏 revision（实测 `function source revision differs`），
         * 硬造又等于抄一份窗口的内部状态。两种都是第二份真相。 */
        runtimec_argument_description_t callee_arguments[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES];
        /*
         * 三个 key 缓冲：**容量 64，不是 128**。
         *
         * 实测（探针扫过 tutorial 206 条 + tests/kv 40 条）：
         *   读参局部 key  最大 24 字节  （`/vthread/26/[10.16]/acc1`）
         *   写参 key      最大 19 字节  （`/vthread/55/[1]/ans`）
         * 64 有 2.7 倍余量。**不足时 `binrwir_frame_local_key` /
         * `binrwir_frame_parameter_key` 返回 SIZE_MAX 而不截断**，上层据此
         * 具名失败（见下方两处 `== SIZE_MAX` 检查）——不会静默写错槽。
         *
         * 这减的是**每层递归的栈占用**：3 × 16 × (128-64) = 3,072 字节/层。
         *
         * 实测：每层 9,488 → **6,432 字节**；递归上限 840 → **1200 层**（改前
         * 840 通过/900 崩，改后 1200 通过/1300 崩）。**本函数里还留着一个
         * 未定位的 4 KB 对象**（`sub $0x1000`），砍掉可望到 2000+ 层——
         * 那是记在 `workspace/622/coding3/two-hop-status.md` 的技术债，未做。
         */
        char read_local_keys[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        char write_target_keys[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        char write_target_langtypes[FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES][64];
        size_t callee_argument_count = 0U;
        memset(callee_arguments, 0, sizeof(callee_arguments));
        /*
         * **实参个数超限是具名失败，不是静默截断。**
         *
         * 没有这一趟预检查时，下面的循环会在数组满时**直接丢掉**多余条目、
         * 不报错；随后窗口按签名核对才发现个数不符，报 `0x4008 argument count`
         * ——一个与真实成因（本数组满）无关的错误码，读日志的人会去查签名，
         * 而签名是对的。那是「以看起来合理的方式失败」。
         *
         * **计数口径**定义在 `frame_driver.h` 的宏注释里（条目数，写参也占一格），
         * 这里只按那个口径数，不再复述一遍。
         */
        {
            size_t actual_argument_count = 0U;
            bool exceeds_limit = false;

            /* 读参与写参都会被写进 `callee_arguments`，两者都要数。 */
            for (size_t ordinal = 0U;
                 ordinal < call_instruction.read_arguments.count; ++ordinal) {
                if (call_instruction.read_arguments.data[ordinal]
                        .reference_kind == BINRWIR_REFERENCE_FUNCTION) {
                    continue;
                }
                actual_argument_count += 1U;
            }
            actual_argument_count += call_instruction.write_arguments.count;
            if (actual_argument_count > (size_t)FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES) {
                exceeds_limit = true;
            }
            if (exceeds_limit) {
                (void)snprintf(
                    output_message, output_message_capacity,
                    "call passes %zu arguments but the driver supports at most "
                    "%u; arguments are never silently dropped",
                    actual_argument_count,
                    (unsigned)FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES);
                return FRAME_DRIVER_OUTCOME_INVALID;
            }
        }
        for (size_t ordinal = 0U;
             ordinal < call_instruction.read_arguments.count &&
             callee_argument_count < FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.read_arguments.data[ordinal];
            if (argument->reference_kind == BINRWIR_REFERENCE_FUNCTION) {
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
                runtimec_bytes_view_t wire;
                runtimec_string_view_t literal_langtype;
                memset(&wire, 0, sizeof(wire));
                if (binrwir_binfunc_get_literal(frame_program,
                                                argument->payload, &wire,
                                                &literal_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_LITERAL;
                callee_arguments[callee_argument_count].literal_bytes = wire;
                callee_arguments[callee_argument_count].literal_langtype =
                    literal_langtype;
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
                /* 变量实参（`fib(a)` 的 `a`）：它**已经存在**于调用方帧，
                 * 所以是 EXISTING_KEY，窗口不创建。
                 *
                 * 这正是递归与单层调用的实质差别：单层的实参多是字面量，
                 * 递归的实参是上一层算出来的局部。 */
                runtimec_string_view_t local_name =
                    runtimec_string_view_make(NULL, 0U);
                runtimec_string_view_t local_langtype =
                    runtimec_string_view_make(NULL, 0U);
                if (binrwir_binfunc_get_symbol(frame_program,
                                               (uint16_t)argument->slot,
                                               &local_name, &local_langtype) !=
                        RUNTIMEC_STATUS_SUCCESS ||
                    local_name.size == 0U) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                /*
                 * key 用**本帧自己的 `frame_path`**，不用 `depth` 重算。
                 *
                 * `depth` 是帧号，`frame_path` 才是地址——引入子帧段
                 * `<depth+1>.<call_site_pc>`（`[2.1]` 这种）之后，两者
                 * **不再是同一个东西**。此处曾按 depth 重算 ``/<vid>/[depth]/<name>``，
                 * 于是 key 指向 `[2]/n1` 而真实是 `[1]/n1`——一个不存在的帧，
                 * `bind_slot` 解析失败（`ARGUMENT_UNRESOLVED`），所有带局部
                 * 实参的调用全挂。
                 */
                if (binrwir_frame_local_key(
                        runtimec_string_view_from_c_string(frame_path),
                        runtimec_string_view_make(local_name.data,
                                                  local_name.size),
                        read_local_keys[callee_argument_count],
                        sizeof(read_local_keys[callee_argument_count])) ==
                    SIZE_MAX) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        read_local_keys[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            /* 其它 kind（STATIC / PARAMETER_*）本阶段失败封闭。 */
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /* 写参：指向调用方帧的槽。此刻可能还不存在（`add1(41) -> v` 的 `v`），
         * 所以描述成 WRITE_TARGET——**要不要创建由窗口判**，驱动不做这个判断。 */
        for (size_t ordinal = 0U;
             ordinal < call_instruction.write_arguments.count &&
             callee_argument_count < FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.write_arguments.data[ordinal];
            /*
             * 写槽可以是**局部**（`add1(41) -> v`），也可以是**本帧自己的形参写位置**
             * （`fib(n1) -> (*a, *b)`）。后者是递归的形态：写回的不是新变量，
             * 而是调用方签名里那个 `*T $name`。
             *
             * 两者的 key **不同形**（见 `binrwir_frame_bindings.h`）：
             *   局部  `/vthread/<vid>/[<depth>]/<name>`
             *   形参  `/vthread/<vid>/[<depth>]/[0,+<ordinal+1>]`
             * 所以这里按 kind 分派，但 key 一律由 `binrwir_frame_*_key` 拼——
             * 拼法只有一处，本驱动不自己拼。
             */
            if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE) {
                /*
                 * 形参写位置：key 由 helper 拼，本驱动不自己拼。
                 * `slot` 是**该方向内的序号**（argidx），从 0 起。
                 *
                 * ── 用**本帧**的 `frame_path`，不是被调方那段 ──────────────
                 *
                 * 写参 `*o` 是**调用方签名里的槽**：被调方写它，等于写调用方的
                 * `/vthread/<vid>/[<本帧段>]/[0,+N]`。用它自己的 `callee` 段去拼
                 * ——那是被调帧的槽——值就写进了子帧，调用方永远读不到（症状：
                 * `inner(*n) -> *o` 之后 `*o` 恒为 0，而单层 `inner(3) -> r` 正常，
                 * 因为那一层调用方就是根帧、恰好同段）。
                 *
                 * **LOCAL 实参那一支（上面）已经用同一条判据修过**，注释原文
                 * 「`depth` 是帧号，`frame_path` 才是地址」。这里用的是同一个
                 * `frame_path` 局部量——两处取帧路径只有这一个来源。
                 *
                 * **契约（对正式驱动同样成立）**：驱动负责把写参槽绑到**调用者
                 * key**；runtime 只认绑定，不自己推导「写参该落在哪」。
                 */
                /*
                 * **先查本帧该写参的有效 key**：本帧自己被调进来时，这个写参
                 * 已经被绑到某个最终接收者。查不到（根帧、或该层没经由本驱动
                 * 传递）才用本帧自己的槽 key。
                 */
                const char *effective =
                    effective_key_lookup(driver, frame_path, argument->slot);
                size_t parameter_key_size = SIZE_MAX;
                if (effective != NULL) {
                    const size_t effective_size = strlen(effective);
                    if (effective_size + 1U <=
                        sizeof(write_target_keys[callee_argument_count])) {
                        memcpy(write_target_keys[callee_argument_count],
                               effective, effective_size + 1U);
                        parameter_key_size = effective_size;
                    }
                }
                if (parameter_key_size == SIZE_MAX) {
                    parameter_key_size = binrwir_frame_parameter_key(
                        runtimec_string_view_from_c_string(frame_path),
                        argument->slot, true,
                        write_target_keys[callee_argument_count],
                        sizeof(write_target_keys[callee_argument_count]));
                }
                if (parameter_key_size == SIZE_MAX) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                /*
                 * 形参的 langtype 只能从**调用方 Program 的签名**取：
                 * `slot` 是该方向内的序号，签名表里就是它。
                 */
                runtimec_string_view_t parameter_type =
                    runtimec_string_view_make(NULL, 0U);
                {
                    binrwir_binfunc_view_t view;
                    if (binrwir_binfunc_get_view(frame_program, &view) !=
                            RUNTIMEC_STATUS_SUCCESS ||
                        (size_t)argument->slot >=
                            view.signature.write_parameter_count) {
                        return FRAME_DRIVER_OUTCOME_ERROR;
                    }
                    parameter_type =
                        view.signature.write_parameter_types[argument->slot];
                }
                const int type_written = snprintf(
                    write_target_langtypes[callee_argument_count],
                    sizeof(write_target_langtypes[callee_argument_count]),
                    "%.*s", (int)parameter_type.size, parameter_type.data);
                if (type_written < 0 ||
                    (size_t)type_written >=
                        sizeof(write_target_langtypes[callee_argument_count])) {
                    return FRAME_DRIVER_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        write_target_keys[callee_argument_count]);
                callee_arguments[callee_argument_count].literal_langtype =
                    runtimec_string_view_from_c_string(
                        write_target_langtypes[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            runtimec_string_view_t local_name =
                runtimec_string_view_make(NULL, 0U);
            runtimec_string_view_t local_langtype =
                runtimec_string_view_make(NULL, 0U);
            if (binrwir_binfunc_get_symbol(frame_program,
                                           (uint16_t)argument->slot,
                                           &local_name, &local_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                local_name.size == 0U) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            /* 同读参：key 用本帧 `frame_path`，不用 `depth` 重算。 */
            if (binrwir_frame_local_key(
                    runtimec_string_view_from_c_string(frame_path),
                    runtimec_string_view_make(local_name.data,
                                              local_name.size),
                    write_target_keys[callee_argument_count],
                    sizeof(write_target_keys[callee_argument_count])) ==
                SIZE_MAX) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            const int langtype_written = snprintf(
                write_target_langtypes[callee_argument_count],
                sizeof(write_target_langtypes[callee_argument_count]), "%.*s",
                (int)local_langtype.size, local_langtype.data);
            if (langtype_written < 0 ||
                (size_t)langtype_written >=
                    sizeof(write_target_langtypes[callee_argument_count])) {
                return FRAME_DRIVER_OUTCOME_ERROR;
            }
            callee_arguments[callee_argument_count].source =
                RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
            callee_arguments[callee_argument_count].key =
                runtimec_string_view_from_c_string(
                    write_target_keys[callee_argument_count]);
            callee_arguments[callee_argument_count].literal_langtype =
                runtimec_string_view_from_c_string(
                    write_target_langtypes[callee_argument_count]);
            callee_argument_count += 1U;
        }
        /* 递归进入被调帧。**深度递增**——复用同一帧号会让子帧覆盖调用方帧，
         * 那是「能跑一会儿但算错」的形态。 */
        /* 被调函数的 Program 由窗口内部 encode，驱动这里传 NULL：
         * `execute` 会用自己的 Program，并把结果里的 `program` 交回来。
         * **不自己 encode**——两个编码入口是这条链上最贵的错误。 */
        /* 被调帧用自己的 exit 变量：共用一个的话，被调在产出 exit 之前失败时
         * 调用方会读到**上一次**的 exit —— 那是「以看起来合理的值失败」。 */
        binrwir_execution_exit_t callee_exit;
        memset(&callee_exit, 0, sizeof(callee_exit));
        /*
         * 子帧段 = `<depth+1>.<call_site_pc>`。
         *
         * 帧身份必须来自**调用链**，不是计数器：
         *   - 同一调用点重复调用 ⇒ 同一段 ⇒ 复用同一帧（不增长）；
         *   - 不同调用点 ⇒ call_site_pc 不同 ⇒ 区分得开；
         *   - 递归 ⇒ depth 不同 ⇒ 区分得开；
         *   - 跑两遍 ⇒ 完全一样（帧路径可寻址，崩溃恢复靠的就是它）。
         *
         * 曾经只用 `<depth>`：同一深度先后调两个不同函数会撞同一帧路径，
         * `ensure_frame` 因「帧上已有另一个函数」拒绝（detail=13）。改用全局
         * 计数器是错的——不可确定、无法复用、无限增长。
         */
        char callee_frame_segment[64];
        const int callee_segment_length = snprintf(
            callee_frame_segment, sizeof(callee_frame_segment), "%d.%u",
            (int)depth + 1,
            (unsigned)result.execution_exit.logical_program_counter);
        if (callee_segment_length < 0 ||
            (size_t)callee_segment_length >= sizeof(callee_frame_segment)) {
            return FRAME_DRIVER_OUTCOME_ERROR;
        }
        /*
         * **登记本次调用给出的写参 key**（按被调方帧路径 + 槽号）。
         *
         * 被调方自己的 `*o` 写参若是 PARAMETER_WRITE，它要用的是**这里给它的那个
         * key**，不是它自己帧里的槽 key。反过来不记，就是让它写到一个没人读的地方。
         */
        char callee_frame_path[128];
        const int callee_path_length = snprintf(
            callee_frame_path, sizeof(callee_frame_path), "/vthread/%s/[%s]",
            vthread_identifier, callee_frame_segment);
        if (callee_path_length > 0 &&
            (size_t)callee_path_length < sizeof(callee_frame_path)) {
            /* **槽号是写参方向内的序号**（`argument->slot`），不是 `callee_arguments`
             * 里的位置——后者把读参与写参混在一个数组里，直接用 ordinal 会错位。 */
            uint16_t write_ordinal = 0U;
            for (size_t ordinal = 0U; ordinal < callee_argument_count;
                 ++ordinal) {
                const runtimec_argument_description_t *description =
                    &callee_arguments[ordinal];
                if (description->source != RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET) {
                    continue;
                }
                char key_text[128];
                const size_t key_size = description->key.size;
                if (key_size == 0U || key_size + 1U > sizeof(key_text)) {
                    write_ordinal += 1U;
                    continue;
                }
                memcpy(key_text, description->key.data, key_size);
                key_text[key_size] = '\0';
                effective_key_record(driver, callee_frame_path, write_ordinal, key_text);
                write_ordinal += 1U;
            }
        }
        const frame_driver_outcome_t callee_outcome = frame_driver_execute(
            driver, callee_path, FRAME_DRIVER_CALLEE_ENTRY_SOURCE_PC,
            depth + 1, callee_frame_segment, vthread_identifier,
            callee_arguments, callee_argument_count, &callee_exit,
            output_message, output_message_capacity);
        /* 子帧结束：它那一段的记录全部作废，否则同深度复用目录会串 key。 */
        if ((size_t)callee_path_length < sizeof(callee_frame_path)) {
            effective_key_forget_frame(driver, callee_frame_path);
        }
        if (callee_outcome != FRAME_DRIVER_OUTCOME_DONE) {
            /*
             * 子帧没跑完（ERROR / HANDOFF / INVALID）：**必须把它的 exit 交出去**。
             *
             * 曾经这里直接 `return callee_outcome;`——`out_exit` 一个字节都没写，
             * 于是调用方拿到的是**本帧自己那条 CALL 的 exit**（`kind=2 status=0`），
             * 子帧的失败被整个吞掉。症状看起来像「跑到一条 call 就没人接了」，
             * 实际是「子帧失败了，但从没被报告上去」。
             *
             * 实测（2026-11-05）：`fib` 第三层报 `kind=6 status=3 detail=9`，
             * 父帧拿到的却是 `kind=2 status=0`。
             */
            *out_exit = callee_exit;
            return callee_outcome;
        }
        /* 被调回来了：回到调用方的 resume 点。
         * 写参结果**不用搬运**——`*T $name` 是「指向调用方 key，读写沿链直达」
         * （spec 05/04-函数调用 的形态表）。 */
        /* 恢复调用方时**不能**再传本次的实参：`arguments` 是「调用被调方时用的」，
         * 不是调用方自己的。传错会让调用方的帧绑定被重新绑到被调的实参上——
         * 那是「以奇怪的方式跑一会儿然后算错」。
         *
         * 调用方的实参是它被调进来时那份，这里原样传 `arguments` 恰恰是错的：
         * 递归时 `fib(a)` 的 `a` 属于**被调那次**，恢复 fib 自身执行时，
         * 它要用的仍是自己被调时绑的那份。 */
        /* 恢复执行时**必须传本帧自己的实参**（形参 `arguments`）。
         *
         * 曾经传 NULL，理由是「`arguments` 是调用被调方时用的」——那是把两件事
         * 混了：`callee_arguments` 才是「给被调方的」；本帧的 `arguments` 是
         * **它自己被调进来时收到的那份**，恢复续跑必须原样再给一次，
         * 否则 `bind_parameters` 会因实参个数不足直接拒（detail=8）。 */
        /*
         * **续跑同一帧：改 pc 后 continue，不递归。**
         *
         * 帧身份（`function_path` / `depth` / `frame_segment` / `arguments`）
         * 全部不变——变的只有「执行到哪」。
         *
         * 实参的归属**照旧**：`arguments` 是**本帧自己被调进来时**收到的那份，
         * 续跑必须原样再给一次。上面那两段注释记的教训在这里一字未改——
         * 改循环只是去掉了 C 栈帧，没有改动「传哪份实参」这件事。
         */
        source_pc = result.execution_exit.resume_logical_program_counter;
        continue;
    }
    default:
        return FRAME_DRIVER_OUTCOME_INVALID;
    }
    }  /* for (;;) 续跑循环 */
}
