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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/char_codec.h"
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
 * **把一个「langtype 文本实参」解成 ASCII 文本。**
 *
 * 为什么需要：`compactarray·fill("...")` 与 `compactarray·newreinterpret(x, "...")`
 * 的第 1/2 个实参都是**字符串字面量**，而 layout 把字符串字面量存成 `char/utf32`
 * （每字符 4 字节，后跟 3 个 NUL）。`container_langtype_parse` 只认 ASCII 形态的
 * `[dims]kind`——直接把 UTF-32 的字节喂进去，它读到 `'['` 之后的 `0x00` 就判
 * 「不是数字」并返回失败。实测症状：
 *
 *   fill("[4]int32")              → detail 0x4302（UNPARSEABLE_LANGTYPE）
 *   newreinterpret(a, "[]uint8")  → detail 0x4304（VALUE_WIDTH_MISMATCH）
 *
 * **一条规则一个实现**：编码分派只在这里做，`fill` 与 `newreinterpret` 都调它。
 * 解码本身走 `contract/char_codec` 的唯一实现，本文件不写第二个解码器。
 *
 * 判据是**读参自己声明的 langtype**，不是数据长什么样：
 *   - `char/utf32`（含 `[N]char/utf32`）⇒ 解码成 UTF-8；
 *   - `char/utf8` / `char/ascii` ⇒ 已经是 ASCII 形态，原样取用；
 *   - 其它种类 ⇒ 失败。**不猜**：一个 `int64` 实参不可能是 langtype 文本。
 *
 * 输出写进调用方的 `buffer`（NUL 结尾），返回 `buffer`；失败返回 NULL。
 */
static const char *container_langtype_text_from_argument(
    const vmbin_value_location_t *value, runtimec_string_view_t value_langtype,
    const uint8_t *bytes, size_t size, char *buffer, size_t capacity) {
    /* 实参的 langtype 带维度前缀（`[8]char/utf32`），与 `container_langtype_parse`
     * 剥 `[dims]` 的做法一致：**先剥维度段再看基种类**。用「整串等于
     * `char/utf32`」会漏掉所有带维度的形态，而 layout 写的字面量恒带维度。 */
    static const char utf32_kind[] = "char/utf32";
    const char *base = value_langtype.data;
    size_t base_size = value_langtype.size;
    if (base != NULL && base_size != 0U && base[0] == '[') {
        while (base_size != 0U && base[0] != ']') {
            ++base;
            --base_size;
        }
        if (base_size != 0U) {
            ++base;
            --base_size;
        }
    }
    const bool is_utf32 = base_size == sizeof(utf32_kind) - 1U &&
                          memcmp(base, utf32_kind, sizeof(utf32_kind) - 1U) == 0;
    (void)value;

    if (size == 0U) {
        return NULL;
    }
    if (is_utf32) {
        size_t used = 0U;
        /* 编码器要求 `capacity > 输出长度`，调用方按「文本缓冲留一个位置」分配。 */
        if (!runtimec_char_utf32_to_utf8(bytes, size, buffer, capacity, &used)) {
            return NULL;
        }
        if (used + 1U > capacity) {
            return NULL;
        }
        buffer[used] = '\0';
        return buffer;
    }
    return container_langtype_text(
        runtimec_string_view_make((const char *)bytes, size), buffer, capacity);
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
int64_t runtimec_container_shape_numel(runtimec_string_view_t langtype) {
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
    const int64_t element_count = runtimec_container_shape_numel(langtype);

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
        runtimec_string_view_t argument_type;

        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &text_location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        /* 与 `fill` 同一条规则：这个实参是 langtype **文本**，按它自己的编码
         * 解码（layout 把字符串字面量存成 `char/utf32`，每字符后跟三个 NUL）。 */
        if (vmbin_value_access_langtype(
                frame, &instruction->read_arguments.data[1], &argument_type,
                &error) != RUNTIMEC_STATUS_SUCCESS ||
            container_langtype_text_from_argument(
                &text_location, argument_type, text_location.read_bytes,
                text_location.size, langtype_text,
                sizeof(langtype_text)) == NULL) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        replacement = runtimec_string_view_make(langtype_text,
                                                strlen(langtype_text));
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
    const int64_t element_count = runtimec_container_shape_numel(langtype);
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
    runtimec_string_view_t langtype_argument_type;
    runtimec_container_diagnostic_t detail;
    char langtype_text[CONTAINER_KEY_CAPACITY];
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
    /* 第 1 个实参是 langtype **文本**：按它自己的编码解码成 ASCII 再解析。
     * 直接拿 body 字节喂 `container_fill_body` 会把 UTF-32 的 `5b 00 00 00`
     * 读成「`[` 后面跟一个非数字」，报 UNPARSEABLE_LANGTYPE。 */
    if (vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &langtype_argument_type,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        container_langtype_text_from_argument(
            &langtype_value, langtype_argument_type, langtype_value.read_bytes,
            langtype_value.size, langtype_text, sizeof(langtype_text)) == NULL) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    body_size = container_fill_body(
        runtimec_string_view_make(langtype_text, strlen(langtype_text)),
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
 * `compactarray·dim(a) -> d`：维数。形状段 `[a][b]…` 的个数——**就在 langtype 里**，
 * 不读 head（`xvalue_head.h:146`：shape lives in leading `[dims]` of langtype）。
 */
/* 与 numel 共用同一段「解析失败即报」的样板：解析只有一条判据。 */
static inline bool container_parse_or_fail(runtimec_string_view_t langtype,
                                           runtimec_container_langtype_t *type,
                                           binrwir_execution_exit_t *output_exit) {
    if (runtimec_container_langtype_parse(langtype, type) ==
        RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                       output_exit->logical_program_counter);
    return false;
}

static inline void container_dim_compute(const uint8_t *body, size_t size,
                                         runtimec_string_view_t langtype,
                                         int64_t *out,
                                         binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    (void)body;
    (void)size;
    if (container_parse_or_fail(langtype, &type, output_exit)) {
        *out = (int64_t)type.dimension_count;
    }
}

VMBIN_HANDLER_UNARY_ARRAY(container_dim_handler, container_dim_compute, int64_t)

/*
 * `compactarray·bodylen(a) -> n`：body 字节数。**直接用读出来的 body 长度**，
 * 不重新推算——那是本层唯一直接可测的量。
 */
static inline void container_bodylen_compute(const uint8_t *body, size_t size,
                                             runtimec_string_view_t langtype,
                                             int64_t *out,
                                             binrwir_execution_exit_t *output_exit) {
    (void)body;
    (void)langtype;
    (void)output_exit;
    *out = (int64_t)size;
}

VMBIN_HANDLER_UNARY_ARRAY(container_bodylen_handler, container_bodylen_compute,
                           int64_t)

/*
 * `compactarray·langtype(a) -> ka`：返回 head 的 langtype 串（如 `[4]int64`）。
 *
 * 与 `parselangtype` 的区别：那个把串**拆成**基名与维数两个输出，这个**整串返回**。
 * 两者都读同一个事实源（值的 langtype），只是一个不拆。
 *
 * 写用 `vmbin_value_access_write_body`：写参槽的长度在绑定期已定，
 * 长度不符由它裁定——本函数不自己判，那是第二套判据。
 */
/*
 * 统一失败出口：把一步的 status 翻成结构化 exit。**只此一处**——容器 handler
 * 里凡是「一步失败即整条失败」的，都走它，避免每个 handler 各写一份同样的
 * `if (status != SUCCESS) { fail(...); return ERROR; }`。
 */
static runtimec_status_t container_finish(runtimec_status_t status,
                                          binrwir_execution_exit_t *output_exit,
                                          uint32_t source_program_counter) {
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    vmbin_handler_fail(output_exit, status,
                       RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                       source_program_counter);
    return RUNTIMEC_STATUS_ERROR;
}

/* 「读 langtype → 原样写回写参」。两步各自一行，失败合流到同一出口。 */
static runtimec_status_t container_langtype_transfer(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction) {
    runtimec_string_view_t langtype;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    const runtimec_status_t status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &langtype, &error);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? vmbin_value_access_write_body(
                     frame, &instruction->write_arguments.data[0],
                     runtimec_bytes_view_make(langtype.data, langtype.size),
                     &error)
               : status;
}

static bool container_unary_arity(const binrwir_instruction_view_t *instruction,
                                  binrwir_execution_exit_t *output_exit) {
    if (instruction->read_arguments.count == 1U &&
        instruction->write_arguments.count == 1U) {
        return true;
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,
                       instruction->source_logical_program_counter);
    return false;
}

/*
 * 写目标的 key：**一条规则一个实现**——派生逻辑全在
 * `vmbin_value_access_argument_key`（LOCAL → 帧槽、STATIC → 绝对路径本身），
 * 本函数只做 error → exit 的翻译，不自己拼 key。
 *
 * 为什么要翻译而不是丢：helper 分得出「缓冲区不够」与「引用种类不对」两种失败，
 * 一律折成 `INVALID` 就把那个区分抹掉了——这正是今晚反复消灭的那类「统一出口
 * 掩盖具体原因」。
 */
static bool container_target_key(binrwir_execution_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 char *buffer, size_t capacity,
                                 binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_argument_key(
            frame, &instruction->write_arguments.data[0], buffer, capacity,
            &error) == RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    /*
     * **建 key 失败 ≠ langtype 解析失败**，用各自具名的码。
     *
     * 曾经这里复用 `UNPARSEABLE_LANGTYPE`（0x4302）：`map·new` 的写槽是绝对路径
     * （`static[N]`）而当时的 helper 只接 LOCAL，于是建 key 失败，报出来却是
     * 「langtype 解析不出元素宽」——**判据指向完全错的地方**。四个人
     * （`062`/`056`/`038`/`abs_dynamic_key`，全部 pc=1）都被这个错码误导过。
     */
    vmbin_handler_fail(output_exit,
                       error.status != RUNTIMEC_STATUS_SUCCESS
                           ? error.status
                           : RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_TARGET_KEY_UNRESOLVED,
                       instruction->source_logical_program_counter);
    return false;
}

static runtimec_status_t container_langtype_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    if (!container_unary_arity(instruction, output_exit)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return container_finish(container_langtype_transfer(frame, instruction),
                            output_exit,
                            instruction->source_logical_program_counter);
}

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

/*
 * `compactarray·copy(a) -> b`：**同形副本**。
 *
 * spec `03-类型系统/10-compact形态:59` 说明它的用途：`compactarray·set` 只做同槽
 * patch，异槽写法非法，所以「改一个元素」必须先 copy 再在同槽 set。
 *
 * 实现就是「body 原样 + langtype 原样」。**不做跨形态转换**（spec 09-数组形态
 * 「创建铁律」：compact 与散列无隐式转换）。
 */
/* 「把 source 的 body 原样写进写参」这一段的唯一实现；失败口径也在这里。 */
static void container_write_or_fail(binrwir_execution_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    const vmbin_value_location_t *source,
                                    binrwir_execution_exit_t *output_exit,
                                    runtimec_error_t *error) {
    const runtimec_status_t status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(source->read_bytes, source->size), error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                           instruction->source_logical_program_counter);
    }
}

static void container_copy_build(binrwir_execution_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 const vmbin_value_location_t *source,
                                 const runtimec_string_view_t *source_langtype,
                                 binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    (void)source_langtype;

    runtimec_error_reset(&error);
    /* **写参的 langtype 不在这里写**：它在绑定期由 layout 编好的写槽声明给出。
     * 本层只复制 body——写 langtype 是第二套事实源，会与声明漂移。 */
    container_write_or_fail(frame, instruction, source, output_exit, &error);
}


/*
 * ── map·new / struct·new ─────────────────────────────────────────────
 *
 * 参考实现：`runtime/src/rwir_map.c:142`（kvlangBuiltinMap）与
 * `runtime/src/rwir_struct.c:20`（kvlangBuiltinStructNew）。**不另发明 wire**。
 *
 * 共同点：目标槽先写一个**空 body 的容器 marker**（langtype = 声明类型），
 * 再逐成员写。marker 用 `vmbin_handler_write_new_value(frame, …, NULL, 0)`
 * 正好等价于前辈的 `kvlangXvalueNew(out, langtype, "", 0, 1)`。
 */

/*
 * 成员访问分隔符 `·`（U+00B7 中点号，`\xC2\xB7`）——**不是上点号 `‥`（U+2025）**。
 *
 * 两者差一个字节，长得很像，但它们承担的事实完全不同：
 *   U+00B7 `·`   **成员访问**（`p·x`、`outer/inner·add1`），layout 的
 *                `keytree::MEMBER_SEP` 就是它，`scatter.c` 的
 *                `SCATTER_MEMBER_SEPARATOR` 也是它；
 *   U+2025 `‥`   **运行期系统成员**（`‥pc`、`‥status`、`‥lib`、`‥ctime`），
 *                只有 kvspace 的运行期成员用。
 *
 * 写错这一格的后果不是报错，是**静默读到 0**：`struct·new` 把成员写到
 * `p‥x`，而 layout 编出来的读槽是 `p·x`——两个不同的 key，读不到就是零值域，
 * 用例照样「成功」地打印 0。**成功但值错，比报错严重一个量级。**
 *
 * 判据不是「哪个符号看起来对」，是**「谁是同一事实的另一个写者」**：
 * layout 编 key、scatter 组 key、本文件组 key 必须用同一份字节。
 */
static const char container_member_separator[] = "\xC2\xB7";

static bool container_member_key(const char *slot_key, const char *suffix,
                                 size_t suffix_size, char *buffer,
                                 size_t capacity) {
    const size_t slot_size = strlen(slot_key);
    const size_t separator_size = sizeof(container_member_separator) - 1U;

    if (slot_size + separator_size + suffix_size + 1U > capacity) {
        return false;
    }
    memcpy(buffer, slot_key, slot_size);
    memcpy(buffer + slot_size, container_member_separator, separator_size);
    if (suffix_size != 0U) {
        memcpy(buffer + slot_size + separator_size, suffix, suffix_size);
    }
    buffer[slot_size + separator_size + suffix_size] = '\0';
    return true;
}

/*
 * 具名 map 的键来自**字符串**字面量；位置型的读参是值本身（int64 等）。
 * 区分只看「字面量的 langtype 是不是字符序列」，不看引用种类——两者都是 LITERAL。
 */
static bool container_argument_is_name(binrwir_execution_frame_t *frame,
                                       const binrwir_argument_t *argument) {
    runtimec_string_view_t declared;
    runtimec_error_t error;

    if (argument->reference_kind != BINRWIR_REFERENCE_LITERAL) {
        return false;
    }
    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(frame, argument, &declared, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    for (size_t ordinal = 0U; ordinal + 5U <= declared.size; ++ordinal) {
        if (memcmp(declared.data + ordinal, "char", 4U) == 0) {
            return true;
        }
    }
    return false;
}

/* 把一个值写成 `<slot>‥<suffix>`：body + 它自己的声明 langtype 组 wire。 */
static runtimec_status_t container_write_member(
    binrwir_execution_frame_t *frame, const char *slot_key,
    const char *suffix, size_t suffix_size,
    const binrwir_argument_t *value_argument, runtimec_error_t *error) {
    vmbin_value_location_t element;
    runtimec_string_view_t declared;
    char langtype_text[256];
    char member_key[VMBIN_HANDLER_KEY_CAPACITY];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    if (!container_member_key(slot_key, suffix, suffix_size, member_key,
                              sizeof(member_key))) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_value_access_locate_body(frame, value_argument, &element,
                                             error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_langtype(frame, value_argument, &declared,
                                          error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    if (kvspaceXvalueHeadNewSlack(element.read_bytes, (uint64_t)element.size,
                                  (uint64_t)element.size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    const int set_status = kvspaceSetValue(
        frame->kvspace_handle, member_key, wire, (uint32_t)wire_size, message,
        (uint32_t)sizeof(message));
    free(wire);
    return set_status == 0 ? RUNTIMEC_STATUS_SUCCESS : RUNTIMEC_STATUS_ERROR;
}

/*
 * 位置型成员写入：每个读参一个元素，键 `<slot>‥[i]`。
 *
 * **具名型必须在这里被拒**：首参是字符串字面量时走 map 的命名键，必须先解
 * 字面量文本——本函数只接位置型，遇到具名型失败封闭。宁可报「没实现」，
 * 也不要用错的键写出一份看起来正常的容器。
 */
/*
 * create 一族的统一开场：取写参槽 key，然后写空 body 的 marker。
 * 两步任一失败即置 exit 并返回 false——调用方只需 `if (!ok) return;`。
 */
/*
 * **成员索引**：给容器 base 建目录，之后成员才能被写与枚举。
 *
 * 这是「容器创建」维度里所有容器**共有**的那一半，与「值」分开：
 *   - `map·new`：写空 body 的 marker（值）+ 本函数（索引）；
 *   - `struct·new`：克隆原型子树（值，含默认字段）+ 本函数（索引）。
 *
 * 两者「值」的来源不同（marker vs 克隆），但**索引这一半完全一样**，
 * 所以它只有这一处实现。`scatter.c` 的 `scatter_container_create` 是同一判据。
 */
static bool container_index_container(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, char *buffer,
    size_t capacity, binrwir_execution_exit_t *output_exit) {
    char message[256];
    const size_t key_length = strlen(buffer);


    /* 目录键以 '/' 结尾：kvspace 的目录枚举要求如此。 */
    if (key_length + 2U > capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           instruction->source_logical_program_counter);
        return false;
    }
    buffer[key_length] = '/';
    buffer[key_length + 1U] = '\0';
    message[0] = '\0';
    const int mkindex_status = kvspaceMkindex(frame->kvspace_handle, buffer, 0U,
                                              message, (uint32_t)sizeof(message));
    buffer[key_length] = '\0';
    if (mkindex_status != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return false;
    }
    return true;
}

/*
 * ── 容器的两个正交维度 ────────────────────────────────────────────────
 *
 * **「容器的创建」与「成员的写法」是两件不同的事，不许合并。**
 *
 *   创建：值是值、成员索引是成员索引——这对**所有**容器都一样
 *         ⇒ **只有一处**（本函数 + `container_create_variadic` / `container_struct_create`
 *           共用的那一段）。
 *   成员写法：`map·new` 是**位置型**（`<slot>·[i]`，每个读参一个元素）；
 *            `struct·new` 是**具名型**（`<slot>·<name>`，名字从 UTF-32 字面量解）。
 *         ⇒ **两种事实，必须分开**。
 *
 * 下一个人会想「顺手统一两条成员循环」——**不要**。那次统一会把具名与位置
 * 合并成一种，然后两者都不对。
 *
 * ── 建一个空容器要两件事，缺一不可 ──────────────────────────────────
 * （与 `scatter.c` 的 `scatter_container_create` 同一判据——那是最早写对的地方，
 * 照它做，不发明第三种。）
 *
 *   1. base key 写一条**空 body 的 container marker**（langtype 取自写槽声明）；
 *   2. `kvspaceMkindex` 给 base 建目录 —— **成员索引**。
 *
 * 只做 1：base 上有类型、没有成员索引，`p·x` 写不进去——kvspace 报
 *         「memhead 不存在，禁止写 memitem」；
 * 只做 2：base 上没有类型。
 *
 * ⇒ **「值」与「成员索引」是容器的两样东西，写值不等于声明容器。**
 */
bool runtimec_container_open_marker(binrwir_execution_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    char *buffer, size_t capacity,
                                    binrwir_execution_exit_t *output_exit) {
    char message[256];
    size_t key_length;

    if (!container_target_key(frame, instruction, buffer, capacity,
                              output_exit) ||
        vmbin_handler_write_new_value(frame, instruction, NULL, 0U,
                                      output_exit) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    (void)message;
    (void)key_length;
    return container_index_container(frame, instruction, buffer, capacity,
                                     output_exit);
}

static void container_write_positional_members(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        char suffix[24];
        const int suffix_size = snprintf(suffix, sizeof(suffix), "[%zu]", ordinal);

        runtimec_error_reset(&error);
        if (container_argument_is_name(
                frame, &instruction->read_arguments.data[ordinal]) ||
            suffix_size <= 0 || (size_t)suffix_size >= sizeof(suffix) ||
            container_write_member(
                frame, slot_key, suffix, (size_t)suffix_size,
                &instruction->read_arguments.data[ordinal],
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit,
                               suffix_size <= 0
                                   ? RUNTIMEC_STATUS_INVALID
                                   : RUNTIMEC_STATUS_UNSUPPORTED,
                               RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                               instruction->source_logical_program_counter);
            return;
        }
    }
}

/* 变参 create 的完整开场：marker + 位置型成员。map·new 与 struct·new 共用。 */
static void container_create_variadic(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char slot_key[VMBIN_HANDLER_KEY_CAPACITY];

    if (runtimec_container_open_marker(frame, instruction, slot_key, sizeof(slot_key),
                              output_exit)) {
        container_write_positional_members(frame, instruction, slot_key,
                                           output_exit);
    }
}

/* 「一个读参、一个写参」的 arity 检查；不满足即置 exit。 */

/* 写参槽的完整 key；取不到即置 exit 并返回 false。容器 create 一族共用。 */


static void container_map_new_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype;
    /* marker 后逐成员写位置型元素；具名型在 members 里被拒。 */
    container_create_variadic(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_map_new_handler, container_map_new_build)

/* 把 `/lib/Name` 的 UTF-32 字面量解成 ASCII 路径。非 UTF-32 形态直接复制。 */
static bool container_literal_path(binrwir_execution_frame_t *frame,
                                   const binrwir_argument_t *argument,
                                   char *buffer, size_t capacity,
                                   size_t *output_size) {
    /*
     * **必须 `= {0}`**：`runtimec_bytes_buffer_reset` 的契约是「只把已用长度
     * 归零，不释放 data、不改变 capacity」（`contract/view.h:61`）——它**不**初始化。
     * 未初始化的 `data` / `capacity` 是栈垃圾，而 `read_body` 的判据
     * `data == NULL || capacity < size` 会被「垃圾非 NULL 且垃圾 ≥ size」
     * 蒙混过去，于是 memcpy 直接写进野指针。
     *
     * 这是 p0 那条「二义状态不许存在」的同一类：**「清零」与「清内容」是两件事，
     * 名字必须对得上**。reset 的名字说的是后者，就不能指望它做前者。
     */
    runtimec_bytes_buffer_t bytes = {0};
    runtimec_string_view_t declared;
    runtimec_error_t error;
    bool ok = false;

    /*
     * **所有权必须是一个可判定的状态，不是一段推理。**
     *
     * 原来的写法是：先零容量读一次，断言「必然先拿 INSUFFICIENT_CAPACITY」，
     * 于是第二遍用 malloc 的缓冲；然后所有出口无条件 `free(bytes.data)`。
     * 那个断言**不是契约**——`vmbin_value_access_read_body` 只在 `data == NULL`
     * 或容量不足时返回 INSUFFICIENT_CAPACITY，零容量 + 零长度 body 时直接
     * SUCCESS，`bytes.data` 仍是刚 reset 的 NULL……**但下一句 `malloc` 分支
     * 被跳过，`bytes.data` 保持 NULL 之外的任何一种历史值都可能发生**。
     * Release 下 `constprop` 把这个假设优化得更彻底，于是 free 到了一个
     * 不是 `malloc` 来的指针 ⇒ `arena_for_chunk` 段错误（Debug 不崩，是 glibc
     * 宽容度的差别，不是两种构建行为不同）。
     *
     * ⇒ 改成：**每个出口只看 `owned`**。`owned` 只有一处被置真，
     * 只有一处被 free（本函数末尾与两个失败出口共用同一个判据）。
     */
    bool owned = false;

    runtimec_bytes_buffer_reset(&bytes);
    runtimec_error_reset(&error);
    if (vmbin_value_access_read_body(frame, argument, &bytes, &error) ==
        RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY) {
        bytes.data = (uint8_t *)malloc(bytes.size != 0U ? bytes.size : 1U);
        if (bytes.data == NULL) {
            return false;
        }
        owned = true;
        bytes.capacity = bytes.size;
        bytes.size = 0U;
        runtimec_error_reset(&error);
        if (vmbin_value_access_read_body(frame, argument, &bytes, &error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            free(bytes.data); /* owned 在上方置真，此处是它自己的出口 */
            return false;
        }
    }
    if (!runtimec_bytes_buffer_is_valid(&bytes) ||
        vmbin_value_access_langtype(frame, argument, &declared, &error) !=
            RUNTIMEC_STATUS_SUCCESS) {
        if (owned) {
            free(bytes.data);
        }
        return false;
    }
    if (bytes.size != 0U) {
        bool utf32 = false;
        for (size_t ordinal = 1U; ordinal + 1U < declared.size; ++ordinal) {
            if (memcmp(declared.data + ordinal, "char/utf32", 10U) == 0) {
                utf32 = true;
                break;
            }
        }
        if (utf32) {
            const size_t characters = bytes.size / 4U;
            if (characters + 1U <= capacity) {
                for (size_t ordinal = 0U; ordinal < characters; ++ordinal) {
                    buffer[ordinal] = (char)bytes.data[ordinal * 4U];
                }
                buffer[characters] = '\0';
                *output_size = characters;
                ok = true;
            }
        } else if (bytes.size + 1U <= capacity) {
            memcpy(buffer, bytes.data, bytes.size);
            buffer[bytes.size] = '\0';
            *output_size = bytes.size;
            ok = true;
        }
    }
    if (owned) {
        free(bytes.data);
    }
    return ok;
}

/*
 * struct·new 的三段：解原型路径 → 克隆子树（失败即回滚）→ 写实例基值 → 成对写字段。
 *
 * `kvspaceCpTree` **不是原子的**：它先删目的子树再逐层复制，中途失败只返回非零、
 * 不回滚。所以失败后目的槽可能是空的、也可能是半个实例——半个实例正是 p0 禁止的
 * 状态。这里自己回滚成「那里什么都没有」；回滚失败也走同一出口，调用方看到的是
 * 失败，不是成功。
 */
static bool container_struct_clone(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    char prototype_path[512];
    size_t prototype_size = 0U;
    char message[256];

    message[0] = '\0';
    if (!container_literal_path(frame, &instruction->read_arguments.data[0],
                                prototype_path, sizeof(prototype_path),
                                &prototype_size) ||
        prototype_size == 0U ||
        kvspaceCpTree(frame->kvspace_handle, prototype_path, slot_key, message,
                      (uint32_t)sizeof(message)) == 0) {
        return prototype_size != 0U;
    }
    char rollback[256];
    rollback[0] = '\0';
    (void)kvspaceDelTree(frame->kvspace_handle, slot_key, rollback,
                         (uint32_t)sizeof(rollback));
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                       instruction->source_logical_program_counter);
    return false;
}

/* 成对 (字段名, 值)：字段名同样是 UTF-32 字面量，值直接写成员槽。 */
static bool container_struct_fields(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    for (size_t ordinal = 1U; ordinal + 1U < instruction->read_arguments.count;
         ordinal += 2U) {
        char field_name[128];
        size_t field_size = 0U;
        runtimec_error_t error;

        runtimec_error_reset(&error);
        if (!container_literal_path(
                frame, &instruction->read_arguments.data[ordinal], field_name,
                sizeof(field_name), &field_size) ||
            field_size == 0U ||
            container_write_member(
                frame, slot_key, field_name, field_size,
                &instruction->read_arguments.data[ordinal + 1U],
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                               instruction->source_logical_program_counter);
            return false;
        }
    }
    return true;
}

/* struct·new 的全流程：取槽 → 克隆原型 → 写基值 → 成对写字段。 */
static void container_struct_create(binrwir_execution_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    binrwir_execution_exit_t *output_exit) {
    char slot_key[VMBIN_HANDLER_KEY_CAPACITY];

    /*
     * **创建维度**：克隆原型（值，含默认字段）+ 建成员索引（共用那一段）。
     * **成员维度**：自己的具名循环（`container_struct_fields`）。
     *
     * 不调 `container_create_variadic`：那条的成员是位置型的，对 struct 是错的事实；
     * 也不用 marker 顶替克隆——marker 是空 body，会把原型里的默认字段丢掉。
     */
    if (instruction->read_arguments.count >= 1U &&
        container_target_key(frame, instruction, slot_key, sizeof(slot_key),
                             output_exit) &&
        container_struct_clone(frame, instruction, slot_key, output_exit) &&
        container_index_container(frame, instruction, slot_key, sizeof(slot_key),
                                  output_exit)) {
        (void)container_struct_fields(frame, instruction, slot_key, output_exit);
    }
}

static void container_struct_new_build(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype;
    container_struct_create(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_struct_new_handler,
                              container_struct_new_build)

VMBIN_HANDLER_CREATE_ONE(container_copy_handler, container_copy_build)

static const binrwir_registry_entry_t runtimec_container_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_MAP_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_map_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_STRUCT_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_struct_new_handler},
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
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_COPY,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_copy_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_DIMENSION,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_dim_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_langtype_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_BODY_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_bodylen_handler},
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
