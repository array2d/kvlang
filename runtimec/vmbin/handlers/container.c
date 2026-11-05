/*
 * 组件：vmbin/handlers/container 的实现
 * 作用：compact 数组算子的 handler——元素计数、下标读、下标写。
 * 联系：条目由 session 装进 `binrwir_registry_t`；值经 `vmbin/value_access`
 *       按地址读写，与 core 规则共用同一条数据面。
 * 约束：
 *   - **不按元素 kind 分叉**。每个参数是一个具体类型，元素宽从声明 langtype
 *     的基种类算出一个数，下标算术对任何 kind 都是同一条 `基址 + i × 宽`。
 *     需要按 kind 走的只有「运算语义不同」的地方，这三条里一处都没有。
 *   - 不缓存 V.body：只在本次访问内借用，处理完即弃。
 *   - 越界按**长度**判，与类型无关（行为矩阵 array-element-access-by-kind）。
 *
 * 本批实现（opid / 名字 / catalog 行）：
 *   0x0102 compactarray·new     rwir_catalog.def:366  array-element-access-by-kind
 *   0x0104 compactarray·numel   rwir_catalog.def:380  array-element-access-by-kind
 *   0x0107 compactarray·at      rwir_catalog.def:401  array-element-access-by-kind
 *   0x0108 compactarray·set     rwir_catalog.def:408  array-element-access-by-kind
 *
 * **已知限制（有意留到下一批，不是遗忘）**：
 *   - `new` 的 body 打包用定长栈缓冲 `CONTAINER_NEW_BODY_CAPACITY`（512 字节，
 *     no-malloc 基线）。超过即失败，细节码
 *     `RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER`——**那是缓冲上限，不是内存不足**。
 *     正解是流式写或让存储层分配，属下一批。
 *   - `at` / `set` 只做 **1 维**。多维要解析 langtype 的 dims 做行主序扁平化，
 *     超过本模块的简洁判据（一条 handler ≈15 行），单独立一条。
 */

#include "vmbin/handlers/container.h"

#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"

/* 帧内槽 key 的上限：与 host 侧同量级，够放 `/vthread/<vid>/[d]/<符号名>`。 */
#define CONTAINER_KEY_CAPACITY 512U

/* `new` 的 body 打包缓冲。no-malloc：定长栈缓冲，超限失败封闭而不是静默截断。 */
#define CONTAINER_NEW_BODY_CAPACITY 512U

/* 下标参数与本批元素宽度都是标量：8 字节足够放下任何一个整数 kind。 */
#define CONTAINER_SCALAR_CAPACITY 8U

/* 类型与解析函数由 container.h 导出（scatter 也用同一份宽度表）。 */

/*
 * 声明 langtype → 元素宽与维数。形状是 `[a][b]…kind`：连续维段在方括号里，
 * 基种类在最后一段之后。与 `vmkv/parameter_type.c` 同一条形状规则，但那条
 * 只判类别不带宽度，租不到；宽度表在 runtimec 里只有这一份的下沉副本。
 *
 * 不认识的基种类返回 UNSUPPORTED：不猜宽度，也不按 body 长度反推。
 */
runtimec_status_t runtimec_container_langtype_parse(
    runtimec_string_view_t langtype, runtimec_container_langtype_t *output) {
    static const struct {
        const char *text;
        uint32_t size;
    } widths[] = {
        {"int8", 1U},    {"uint8", 1U},   {"bool", 1U},
        {"int16", 2U},   {"uint16", 2U},  {"int32", 4U},
        {"uint32", 4U},  {"float32", 4U}, {"int64", 8U},
        {"uint64", 8U},  {"float64", 8U}, {"time", 8U},
        {"time/duration", 8U},
        {"char/utf8", 1U}, {"char/ascii", 1U}, {"char/utf32", 4U},
    };
    size_t offset = 0U;
    int32_t dimension_count = 0;

    output->base = runtimec_string_view_make(NULL, 0U);
    output->element_size = 0U;
    output->dimension_count = 0;
    if (langtype.size == 0U) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    while (offset < langtype.size && langtype.data[offset] == '[') {
        const char *closing = memchr(langtype.data + offset, ']',
                                     langtype.size - offset);
        if (closing == NULL || closing == langtype.data + offset) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        offset = (size_t)(closing - langtype.data) + 1U;
        ++dimension_count;
    }
    if (dimension_count == 0 || offset >= langtype.size) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    output->base = runtimec_string_view_make(langtype.data + offset,
                                             langtype.size - offset);
    output->dimension_count = dimension_count;
    for (size_t ordinal = 0U; ordinal < sizeof(widths) / sizeof(widths[0]);
         ++ordinal) {
        const size_t size = strlen(widths[ordinal].text);
        if (output->base.size == size &&
            memcmp(output->base.data, widths[ordinal].text, size) == 0) {
            output->element_size = widths[ordinal].size;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_UNSUPPORTED;
}

/* 统一的失败出口：写结构化 ERROR，指令未提交。 */
static runtimec_status_t container_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

static runtimec_status_t container_fail_with_detail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter, runtimec_container_diagnostic_t detail) {
    runtimec_status_t returned =
        container_fail(output_exit, status, source_program_counter);

    output_exit->detail_code = (uint32_t)detail;
    return returned;
}

/*
 * 把声明 langtype 拷成 NUL 结尾的 C 串（`container_publish` 收的是 C 串）。
 * 放不下返回 NULL——调用方据此失败封闭，不截断：截断的 langtype 会造出一条
 * 类型错误的 wire，比失败难查得多。
 */
static const char *container_langtype_text(runtimec_string_view_t langtype,
                                           char *buffer, size_t capacity) {
    if (langtype.size == 0U || langtype.size + 1U > capacity) {
        return NULL;
    }
    memcpy(buffer, langtype.data, langtype.size);
    buffer[langtype.size] = '\0';
    return buffer;
}


/*
 * 五条创建型算子的公共尾段：它们只在「body 从哪来、head 的 langtype 是什么」上
 * 不同，那部分各自算完再进来。
 *
 * **组 wire 与写存储只有一份实现**（`vmbin_handler_write_new_value`），本函数只是
 * 把 langtype 文本换成视图再转调——避免 container 与 string 各留一份。
 */
static runtimec_status_t container_publish(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *body,
    size_t body_size, binrwir_execution_exit_t *output_exit) {
    return vmbin_handler_write_new_value(frame, instruction, body, body_size,
                                         output_exit);
}

/*
 * 打包：遍历全部读参，逐个核宽后拼进 body。返回拼好的字节数，失败返回 -1。
 *
 * **这一层就是「打包」的全部**——遍历、核宽、拼装是同一件事的三个步骤，不再往下拆。
 * 核宽的理由写在 `container_new_build` 的注释里：body 布局由声明类型决定，任何与
 * 声明不符的输入都不许拼进去。
 */
static int64_t container_pack_elements(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t element_size,
    uint8_t *body, size_t capacity,
    runtimec_container_diagnostic_t *output_detail) {
    size_t body_size = 0U;
    runtimec_error_t error;

    *output_detail = RUNTIMEC_CONTAINER_DIAG_NONE;
    runtimec_error_reset(&error);
    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        vmbin_value_location_t element;

        if (vmbin_value_access_locate_body(
                frame, &instruction->read_arguments.data[ordinal], &element,
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE;
            return -1;
        }
        if (element.size != element_size) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH;
            return -1;
        }
        if (body_size + element.size > capacity) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER;
            return -1;
        }
        memcpy(body + body_size, element.read_bytes, element.size);
        body_size += element.size;
    }
    return (int64_t)body_size;
}

/*
 * 从读参造一个新 compact 值并发布：解析声明类型、按元素宽打包、写目标 key。
 *
 * 一句话目的：**把「N 个读参」变成一个 compact 值**。内部的打包细节归
 * `container_pack_elements`，写存储归 `vmbin_handler_write_new_value`——本函数
 * 只负责「解析类型 + 把两件事接起来」。
 *
 * body 的字节布局完全由写参的声明类型决定：每个读参的宽必须等于声明宽，多了会
 * 覆盖下一个元素，少了会留空洞，而 head 仍自称合法——「自洽但语义错误」。
 */
static void container_create_from_elements(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    runtimec_container_langtype_t declared_type;
    runtimec_container_diagnostic_t detail;
    int64_t body_size;

    if (runtimec_container_langtype_parse(result_langtype, &declared_type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        declared_type.element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    body_size = container_pack_elements(frame, instruction,
                                        declared_type.element_size, body,
                                        sizeof(body), &detail);
    if (body_size < 0) {
        vmbin_handler_fail(output_exit,
                           detail == RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER
                               ? RUNTIMEC_STATUS_OUT_OF_MEMORY
                               : RUNTIMEC_STATUS_INVALID,
                           detail, output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, body, (size_t)body_size,
                            output_exit);
}

/* 逻辑函数：一句话——从读参造一个新数组并发布。 */
static inline void container_new_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    container_create_from_elements(frame, instruction, result_langtype, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_new_handler, container_new_build)


/*
 * 切片边界：给定源 body 长度与元素宽，判 `[lo, hi)` 是否合法，合法则给出
 * 起始字节偏移与字节长度。返回 -1 表示越界。
 *
 * 「判边界」是独立语义（将来 `scatterarray·slice` 要同一套判据），所以单列。
 */
static int64_t container_slice_bounds(size_t body_size, size_t element_size,
                                      int64_t lo, int64_t hi,
                                      size_t *output_byte_length) {
    *output_byte_length = 0U;
    if (element_size == 0U || lo < 0 || hi < lo ||
        (uint64_t)hi > body_size / element_size) {
        return -1;
    }
    *output_byte_length = (size_t)(hi - lo) * element_size;
    return lo * (int64_t)element_size;
}

/*
 * 按边界切一段并发布：取源 body 与元素宽、取两个边界、判越界、写目标 key。
 *
 * 一句话目的：**把源的一段元素变成一个新 compact 值**。边界判据在
 * `container_slice_bounds` 里，本函数只负责取参数与接起来。
 *
 * 新值的 langtype 由**写槽声明**给出，不从源抄——源的 dims 是旧长度，抄过来就错。
 */
static void container_create_from_slice(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    vmbin_value_location_t bounds[2];
    runtimec_container_langtype_t type;
    runtimec_string_view_t source_langtype;
    int64_t lo = 0;
    int64_t hi = 0;
    size_t byte_length = 0U;
    int64_t offset;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &source, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        runtimec_container_langtype_parse(source_langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &bounds[0], &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[2], &bounds[1], &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        bounds[0].size != sizeof(lo) || bounds[1].size != sizeof(hi)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    offset = container_slice_bounds(source.size, type.element_size, lo, hi,
                                    &byte_length);
    if (offset < 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                           output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, source.read_bytes + offset,
                            byte_length, output_exit);
}

/* 逻辑函数：一句话——按边界切一段并发布。 */
static inline void container_newslice_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    container_create_from_slice(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_TERNARY(container_newslice_handler, container_newslice_build)


/*
 * 一个形状段 `[d1,d2,…]` 的元素总数；形状非法返回 -1。
 *
 * 只解析**首个**方括号段：本批只支持一维定长（`[N]K`），多维留到单独立条时再扩。
 * 不认识的写法一律返回 -1，不猜。
 */
static int64_t container_shape_numel(runtimec_string_view_t langtype) {
    int64_t total = 1;
    size_t offset = 1U;

    if (langtype.size < 3U || langtype.data[0] != '[') {
        return -1;
    }
    while (offset < langtype.size && langtype.data[offset] != ']') {
        int64_t value = 0;

        if (langtype.data[offset] < '0' || langtype.data[offset] > '9') {
            return -1;
        }
        while (offset < langtype.size && langtype.data[offset] >= '0' &&
               langtype.data[offset] <= '9') {
            value = value * 10 + (langtype.data[offset] - '0');
            ++offset;
        }
        total *= value;
        if (offset < langtype.size && langtype.data[offset] == ',') {
            ++offset;
        }
    }
    if (offset >= langtype.size || langtype.data[offset] != ']') {
        return -1;
    }
    /* 形状段之后必须还有基种类，否则 `[N]` 后面没类型。 */
    return offset + 1U < langtype.size ? total : -1;
}

/*
 * 按 langtype 算「总字节数」：形状段乘积 × 元素宽。返回 -1 表示算不出。
 *
 * 与 `container_shape_numel` 的区别：那个只看形状段乘积（元素个数），这个乘上
 * 元素宽给字节数——reshape 的守恒判据要的是字节数对得上。
 */
static int64_t container_langtype_total_bytes(runtimec_string_view_t langtype) {
    runtimec_container_langtype_t type;
    const int64_t element_count = container_shape_numel(langtype);

    if (element_count < 0 ||
        runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        return -1;
    }
    return element_count * (int64_t)type.element_size;
}

/*
 * `compactarray·newreshape(a, dims...) -> b` 与
 * `compactarray·newreinterpret(a, langtype) -> b`：**body 字节原样，只换 head 的
 * langtype**。
 *
 * 两者是同一条实现的两种入口，差别只在「新 langtype 从哪来」：
 *   newreshape      → 由写槽声明给出（layout 已经把 dims 编进 `[d1,d2]K`）
 *   newreinterpret  → 由第二个读参给出（那个读参的值就是 langtype 串）
 *
 * 为什么这一个能超过 15 行：它要覆盖两种来源，而两种来源的取值方式确实不同
 * （一个来自签名、一个来自值）。但**仍然没有按 langtype 分叉**——拿到新类型后
 * 走的都是同一句「body 原样 + 新 langtype 组 wire」。
 */
static runtimec_status_t container_replace_langtype(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int langtype_from_value,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_string_view_t replacement;
    runtimec_error_t error;
    runtimec_status_t status;
    char langtype_text[CONTAINER_KEY_CAPACITY];
    const char *text;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &source, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    if (langtype_from_value != 0) {
        /*
         * `newreinterpret` 的新类型由**第二个读参的值**给出（那个参数本身是一个
         * char 串）。值的 langtype 是 `[N]char/...`，不是我们要的「新数组类型」，
         * 所以这里取的是 body 文本，不是 `_langtype`。
         */
        vmbin_value_location_t text_location;

        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &text_location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        replacement = runtimec_string_view_make(
            (const char *)text_location.read_bytes, text_location.size);
    } else {
        /* `newreshape` 的新类型由**写槽声明**给出：layout 已把 dims 编进 `[d1,d2]K`。 */
        status = vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &replacement, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
    }
    text = container_langtype_text(replacement, langtype_text,
                                   sizeof(langtype_text));
    if (text == NULL) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    /*
     * 守恒判据（只对 `newreshape`；`newreinterpret` 本来就允许变总元素数）：
     * **两边的总字节数都必须等于 body 长度**。
     *
     * 只核「新类型对得上」会漏两种：
     *   - `[4]int64 → [3,3]int64`：9 × 8 = 72 装不进 32 字节的 body；
     *   - 源本身畸形（声明 `[4]int64` 应 32 字节、body 实际 36）：新类型 `[9]int32`
     *     恰好也是 36 字节，于是 reshape 把一条畸形值**洗白**成合法值。
     *
     * reshape 是「同一块字节换一种切法」，**两边都得对得上这块字节**；只对一边
     * 就等于接受畸形输入。源侧的声明同样从参数类型表取，不是从 body 反推。
     */
    if (langtype_from_value == 0) {
        runtimec_string_view_t source_langtype;
        int64_t new_bytes;
        int64_t old_bytes;

        status = vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        new_bytes = container_langtype_total_bytes(replacement);
        old_bytes = container_langtype_total_bytes(source_langtype);
        if (new_bytes < 0 || old_bytes < 0 ||
            (size_t)new_bytes != source.size || (size_t)old_bytes != source.size) {
            return container_fail_with_detail(
                output_exit, RUNTIMEC_STATUS_INVALID, counter,
                RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH);
        }
    } else {
        /* `newreinterpret`：换解释允许变总元素数，只要求元素宽整除 body。 */
        runtimec_container_langtype_t new_type;

        if (runtimec_container_langtype_parse(replacement, &new_type) !=
                RUNTIMEC_STATUS_SUCCESS ||
            new_type.element_size == 0U ||
            source.size % new_type.element_size != 0U) {
            return container_fail_with_detail(
                output_exit, RUNTIMEC_STATUS_INVALID, counter,
                RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH);
        }
    }
    return container_publish(frame, instruction, source.read_bytes, source.size,
                             output_exit);
}

/*
 * 填充体：按声明类型一次成型，每元素填同一个值。
 *
 * 三件事合成一层（它们就是「按类型造一个填满的 body」这一个语义）：
 * 解析形状段得到元素个数、核填充值宽度、按元素宽重复填充。
 */
static int64_t container_fill_body(runtimec_string_view_t langtype,
                                   const uint8_t *filler, size_t filler_size,
                                   int has_filler, uint8_t *body,
                                   size_t capacity,
                                   runtimec_container_diagnostic_t *output_detail) {
    runtimec_container_langtype_t type;
    const int64_t element_count = container_shape_numel(langtype);
    uint8_t element[CONTAINER_SCALAR_CAPACITY];

    *output_detail = RUNTIMEC_CONTAINER_DIAG_NONE;
    if (element_count < 0 ||
        runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE;
        return -1;
    }
    if (has_filler != 0 && filler_size != type.element_size) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH;
        return -1;
    }
    if ((uint64_t)element_count * type.element_size > capacity) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER;
        return -1;
    }
    memset(element, 0, sizeof(element));
    if (has_filler != 0) {
        memcpy(element, filler, type.element_size);
    }
    for (int64_t ordinal = 0; ordinal < element_count; ++ordinal) {
        memcpy(body + (size_t)ordinal * type.element_size, element,
               type.element_size);
    }
    return element_count * (int64_t)type.element_size;
}

/*
 * 按声明类型造一个填满的 compact 值并发布：取类型文本读参、取可选填充值、
 * 成型、写目标 key。
 *
 * 一句话目的：**造一个「N 个相同值」的 compact 值**。成型细节在
 * `container_fill_body` 里，本函数只负责取参数与接起来。
 *
 * 这是核心 runtime 唯一的初始化模式（spec：只认 `T·new` + `T·fill`）。
 */
static void container_create_from_fill(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t langtype_value;
    vmbin_value_location_t filler;
    runtimec_container_diagnostic_t detail;
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    int64_t body_size;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &langtype_value,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    memset(&filler, 0, sizeof(filler));
    if (instruction->read_arguments.count == 2U &&
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &filler,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    body_size = container_fill_body(
        runtimec_string_view_make((const char *)langtype_value.read_bytes,
                                  langtype_value.size),
        filler.read_bytes, filler.size,
        instruction->read_arguments.count == 2U ? 1 : 0, body, sizeof(body),
        &detail);
    if (body_size < 0) {
        vmbin_handler_fail(output_exit,
                           detail == RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER
                               ? RUNTIMEC_STATUS_OUT_OF_MEMORY
                               : RUNTIMEC_STATUS_INVALID,
                           detail, output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, body, (size_t)body_size,
                            output_exit);
}

/* 逻辑函数：一句话——造一个「N 个相同值」的数组并发布。 */
static inline void container_fill_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype; /* fill 的类型来自读参文本，不是写槽声明。 */
    container_create_from_fill(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_fill_handler, container_fill_build)

/*
 * `compactarray·numel(a) -> n`：元素总数 = 值体长度 / 元素宽。
 *
 * spec（03-类型系统/13-ndarray元数据算子.kv）说 numel 读 head 的 array_len；
 * 本运行时的 head 只有 bodylen 与 langtype（dims 在 langtype 里），**没有独立的
 * array_len 字段**，所以这个数由除法得出——**这是当前唯一可得的数，不是定义**。
 * 两者在合法值上恒等（body 是元素连续打包的）；将来 head 加了 array_len，就应
 * 改读它，而不是继续在这里除。
 */
/*
 * `compactarray·numel(a) -> n`：元素总数 = 值体长度 / 元素宽。
 *
 * spec（03-类型系统/13-ndarray元数据算子.kv）说 numel 读 head 的 array_len；
 * 本运行时的 head 只有 bodylen 与 langtype（dims 在 langtype 里），**没有独立的
 * array_len 字段**，所以这个数由除法得出——**这是当前唯一可得的数，不是定义**。
 * 两者在合法值上恒等（body 是元素连续打包的）；将来 head 加了 array_len，就应
 * 改读它，而不是继续在这里除。
 */
/*
 * 元素总数：值体长度 / 元素宽。**成功才写 `*out`，失败只写 `exit`**——哨兵不出
 * 这个函数，调用方因此不需要「把 -1 翻成失败」那一步（那一步曾让 numel 把 -1
 * 当结果写进输出槽）。
 *
 * spec（03-类型系统/13-ndarray元数据算子.kv）说 numel 读 head 的 array_len；
 * 本运行时的 head 只有 bodylen 与 langtype（dims 在 langtype 里），**没有独立的
 * array_len 字段**，所以这个数由除法得出——**这是当前唯一可得的数，不是定义**。
 * 将来 head 加了 array_len，就应改读它。
 */
static void container_element_count(size_t body_size,
                                    runtimec_string_view_t langtype,
                                    int64_t *out,
                                    binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    if (runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    *out = (int64_t)(body_size / type.element_size);
}

/* 逻辑函数：一行。 */
static inline void container_numel_compute(const uint8_t *body, size_t size,
                                           runtimec_string_view_t langtype,
                                           int64_t *out,
                                           binrwir_execution_exit_t *output_exit) {
    (void)body;
    container_element_count(size, langtype, out, output_exit);
}

VMBIN_HANDLER_UNARY_ARRAY(container_numel_handler, container_numel_compute,
                           int64_t)

/*
 * 元素读：`基址 + i × 宽`。**成功才写 `*element` 与 `*output_element_size`，
 * 失败只写 `exit`**——哨兵不出这个函数，调用方不需要翻译。
 *
 * 越界与负下标都失败（行为矩阵的负向判据）——判据是**长度**，与类型无关。
 */
static void container_read_element(const uint8_t *body, size_t body_size,
                                   runtimec_string_view_t langtype, int64_t index,
                                   uint8_t *element, size_t element_capacity,
                                   size_t *output_element_size,
                                   binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    if (runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U || index < 0 ||
        (uint64_t)index >= body_size / type.element_size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                           output_exit->logical_program_counter);
        return;
    }
    if (type.element_size > element_capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    *output_element_size = type.element_size;
    memcpy(element, body + (size_t)index * type.element_size, type.element_size);
}

/* 逻辑函数：一行。 */
static inline void container_at_compute(const uint8_t *body, size_t body_size,
                                        runtimec_string_view_t langtype,
                                        int64_t index, uint8_t *element,
                                        size_t element_capacity,
                                        binrwir_execution_exit_t *output_exit) {
    size_t element_size = 0U;
    container_read_element(body, body_size, langtype, index, element, element_capacity, &element_size, output_exit);
}

VMBIN_HANDLER_ARRAY_INDEX(container_at_handler, container_at_compute,
                             int64_t)

/*
 * 元素写：**仅同槽 patch**——改的是 body 里的一段字节，head 与 attr 一个字节不动。
 * **成功不写哨兵，失败只写 `exit`**。
 *
 * 判序有讲究：**先判值宽（形状问题，与下标无关），再判下标**。两者都失败时报宽度
 * ——形状不对时「下标是否越界」没有意义，报它会把读者引向错误的第二因。
 */
static void container_write_element(const uint8_t *body, size_t body_size,
                                    runtimec_string_view_t langtype,
                                    int64_t index, const uint8_t *value,
                                    size_t value_size, uint8_t *target,
                                    binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    (void)body; /* 同槽 patch 只写不读。 */
    if (runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    if (value_size != type.element_size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    if (index < 0 || (uint64_t)index >= body_size / type.element_size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(target + (size_t)index * type.element_size, value, value_size);
}

/* 逻辑函数：一行。 */
static inline void container_set_compute(const uint8_t *body, size_t body_size,
                                         runtimec_string_view_t langtype,
                                         int64_t index, const uint8_t *value,
                                         size_t value_size, uint8_t *target,
                                         binrwir_execution_exit_t *output_exit) {
    container_write_element(body, body_size, langtype, index, value, value_size, target, output_exit);
}

VMBIN_HANDLER_ARRAY_INDEX_WRITE(container_set_handler, container_set_compute,
                                  int64_t)




static void container_newreshape_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    const vmbin_value_location_t *source,
    const runtimec_string_view_t *source_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)source;
    (void)source_langtype;
    (void)container_replace_langtype(frame, instruction, 0, output_exit);
}
VMBIN_HANDLER_CREATE_ONE(container_newreshape_handler, container_newreshape_build)

static void container_newreinterpret_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    const vmbin_value_location_t *source,
    const runtimec_string_view_t *source_langtype,
    const vmbin_value_location_t *langtype_value,
    binrwir_execution_exit_t *output_exit) {
    (void)source;
    (void)source_langtype;
    (void)langtype_value;
    (void)container_replace_langtype(frame, instruction, 1, output_exit);
}
VMBIN_HANDLER_CREATE_TWO(container_newreinterpret_handler,
                         container_newreinterpret_build)

static const binrwir_registry_entry_t runtimec_container_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NUMEL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_numel_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_AT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_at_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_SET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_set_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_FILL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_fill_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_SLICE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newslice_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_RESHAPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newreshape_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_REINTERPRET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newreinterpret_handler},
};

const binrwir_registry_entry_t *runtimec_container_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_container_entry_table) /
                    sizeof(runtimec_container_entry_table[0]);
    return runtimec_container_entry_table;
}
