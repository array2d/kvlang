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
#include "vmbin/handlers/handler_target_key.h"
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
 * 读主参数的数组视图：声明类型 + 值体地址 + 值体长度，一次定位。
 *
 * 值体长度来自 `locate`（它走合同的 `runtimec_value_body_length`），元素宽来自
 * 声明类型——两者相除就是元素个数。空值体（None）长度 0，元素数就是 0，
 * 不是坏地址：地址的有效性由 locate 成功证明。
 */
static runtimec_status_t container_view(
    const binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_container_langtype_t *output_type,
    runtimec_error_t *error) {
    runtimec_string_view_t langtype;
    runtimec_status_t status;

    status = vmbin_value_access_langtype(frame, argument, &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_container_langtype_parse(langtype, output_type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return vmbin_value_access_locate_body(frame, argument, output_location,
                                          error);
}

/* 一个下标参数 → 非负扁平序号；越界与负值都由调用方按长度判。 */
static runtimec_status_t container_index(
    const binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    int64_t *output_index, runtimec_error_t *error) {
    vmbin_value_location_t location;
    uint64_t raw = 0U;
    runtimec_status_t status = vmbin_value_access_locate_body(
        frame, argument, &location, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    switch (location.size) {
    case 1U:
        *output_index = (int64_t)(int8_t)location.read_bytes[0];
        return RUNTIMEC_STATUS_SUCCESS;
    case 2U: {
        int16_t value = 0;
        memcpy(&value, location.read_bytes, sizeof(value));
        *output_index = (int64_t)value;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case 4U: {
        int32_t value = 0;
        memcpy(&value, location.read_bytes, sizeof(value));
        *output_index = (int64_t)value;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case 8U:
        memcpy(&raw, location.read_bytes, sizeof(raw));
        *output_index = (int64_t)raw;
        return RUNTIMEC_STATUS_SUCCESS;
    default:
        return RUNTIMEC_STATUS_INVALID;
    }
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
 * 造 wire 并整条写进写参的目标 key——五条创建型算子（`new` / `newslice` /
 * `fill` / `newreshape` / `newreinterpret`）的公共尾段。
 *
 * 它们只在「body 从哪来、head 的 langtype 是什么」上不同，那部分各自算完再进来；
 * 组 wire 与写存储一件事，只写一遍。**分配归存储层**：`kvspaceSetValue` 收完整
 * wire，容量它说了算。本函数只用 `kvspaceXvalueHeadNewSlack` 临时产一条 wire，
 * 传完即还。
 */
static runtimec_status_t container_publish(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *body,
    size_t body_size, const char *langtype_text,
    binrwir_execution_exit_t *output_exit) {
    char key[CONTAINER_KEY_CAPACITY];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    const uint32_t counter = instruction->source_logical_program_counter;
    runtimec_status_t status;

    if (kvspaceXvalueHeadNewSlack(body, (uint64_t)body_size,
                                  (uint64_t)body_size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return container_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    status = runtimec_handler_target_key(frame, &instruction->write_arguments.data[0],
                                  key, sizeof(key));
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        message[0] = '\0';
        if (kvspaceSetValue(frame->kvspace_handle, key, wire,
                            (uint32_t)wire_size, message, sizeof(message)) != 0) {
            status = RUNTIMEC_STATUS_ERROR;
        }
    }
    free(wire);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : container_fail(output_exit, status, counter);
}

/*
 * `compactarray·new(els...) -> a`：把读参的 body 原样连续打包成一条新 XValue，
 * 整条写进目标 key——**写入即创建**，不依赖预先存在的槽。
 *
 * head 的 langtype 取自**写槽的声明类型**（`[N]K`），不按元素类型重编码：
 * 读参 body 已经是按元素宽连续的字节，拼起来就是结果 body。元素宽与声明类型
 * 必须自洽（总字节数 = 元素数 × 宽），不一致就是编译期与执行期对不上，失败封闭。
 *
 * 分配交给存储层：`kvspaceSetValue` 收完整 wire，容量由它决定（扩容只有存储层
 * 能做）。本 handler 只用 `kvspaceXvalueHeadNewSlack` 组一条 wire，用完即还。
 */
static inline runtimec_status_t container_new(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    size_t body_size = 0U;
    char langtype_text[CONTAINER_KEY_CAPACITY];
    runtimec_string_view_t declared;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_langtype(
        frame, &instruction->write_arguments.data[0], &declared, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        return container_fail(output_exit,
                              status == RUNTIMEC_STATUS_SUCCESS
                                  ? RUNTIMEC_STATUS_INVALID
                                  : status,
                              counter);
    }
    /* 各读参的 body 依次拼起来：不按元素类型分支，元素宽差异已体现在字节里。 */
    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        vmbin_value_location_t element;

        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[ordinal], &element,
            &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        if (body_size + element.size > sizeof(body)) {
            /*
             * 这是**定长缓冲**的限制（`CONTAINER_NEW_BODY_CAPACITY`），不是内存
             * 不足。结构化退出没有文本通道，所以可读的说明在这里：
             * 看到 `RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER` 就该去查缓冲上限，
             * 而不是分配器。解法是流式写或让存储层分配，属下一批。
             */
            return container_fail_with_detail(
                output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY, counter,
                RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER);
        }
        memcpy(body + body_size, element.read_bytes, element.size);
        body_size += element.size;
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    return container_publish(frame, instruction, body, body_size, langtype_text,
                             output_exit);
}

/*
 * `compactarray·newslice(a, lo, hi) -> b`：把 `a` 的 `[lo, hi)` 元素段造成新的
 * compact 值，写进目标 key。
 *
 * 边界按**元素个数**判（`body 长度 / 元素宽`），越界返回 INVALID——与 `at` 同一条
 * 判据，与类型无关。新值的 langtype 由写槽声明给出，不从源抄：源的 dims 是旧长度，
 * 抄过来就是错的。
 */
static inline runtimec_status_t container_newslice(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_container_langtype_t type;
    runtimec_string_view_t declared;
    int64_t bounds[2] = {0, 0};
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U ||
        instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = container_view(frame, &instruction->read_arguments.data[0],
                            &source, &type, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        for (size_t ordinal = 0U; ordinal < 2U; ++ordinal) {
            status = container_index(
                frame, &instruction->read_arguments.data[ordinal + 1U],
                &bounds[ordinal], &error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                break;
            }
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &declared, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    {
        const int64_t element_count = (int64_t)(source.size / type.element_size);
        char langtype_text[CONTAINER_KEY_CAPACITY];
        const char *text;

        if (bounds[0] < 0 || bounds[1] < bounds[0] ||
            bounds[1] > element_count) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        text = container_langtype_text(declared, langtype_text,
                                       sizeof(langtype_text));
        if (text == NULL) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        return container_publish(
            frame, instruction,
            source.read_bytes + (size_t)bounds[0] * type.element_size,
            (size_t)(bounds[1] - bounds[0]) * type.element_size, text,
            output_exit);
    }
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
    return container_publish(frame, instruction, source.read_bytes, source.size,
                             text, output_exit);
}

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
 * `compactarray·fill(langtype[, v]) -> a`：按**声明类型**一次成型，每元素填 `v`
 * （省略即全零）。
 *
 * 唯一的初始化模式（spec：核心 runtime 只认 `T·new` + `T·fill`）。`langtype` 这个
 * 读参的值是类型文本，形状段给出元素个数；填充值逐个拷贝到每个元素位置——
 * **只拷 `element_size` 字节，不看是什么 kind**，所以没有按类型分叉。
 */
static runtimec_status_t container_fill(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t langtype_value;
    vmbin_value_location_t filler;
    runtimec_container_langtype_t type;
    int64_t element_count;
    runtimec_string_view_t declared;
    runtimec_error_t error;
    runtimec_status_t status;
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    uint8_t element[CONTAINER_SCALAR_CAPACITY];
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count < 1U ||
        instruction->read_arguments.count > 2U ||
        instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &langtype_value, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    declared = runtimec_string_view_make(
        (const char *)langtype_value.read_bytes, langtype_value.size);
    status = runtimec_container_langtype_parse(declared, &type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    element_count = container_shape_numel(declared);
    if (element_count < 0 ||
        (uint64_t)element_count * type.element_size > sizeof(body)) {
        return container_fail_with_detail(
            output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY, counter,
            RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER);
    }
    /* 填充值：省略即全零；给了就必须正好一个元素的宽。 */
    memset(element, 0, sizeof(element));
    if (instruction->read_arguments.count == 2U) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &filler, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        if (filler.size != type.element_size) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        memcpy(element, filler.read_bytes, type.element_size);
    }
    for (int64_t ordinal = 0; ordinal < element_count; ++ordinal) {
        memcpy(body + (size_t)ordinal * type.element_size, element,
               type.element_size);
    }
    {
        char langtype_text[CONTAINER_KEY_CAPACITY];
        const char *text =
            container_langtype_text(declared, langtype_text,
                                    sizeof(langtype_text));

        if (text == NULL) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        return container_publish(frame, instruction, body,
                                 (size_t)element_count * type.element_size, text,
                                 output_exit);
    }
}

/*
 * `compactarray·numel(a) -> n`：元素总数 = 值体长度 / 元素宽。
 *
 * spec（03-类型系统/13-ndarray元数据算子.kv）说 numel 读 head 的 array_len；
 * 本运行时的 head 只有 bodylen 与 langtype（dims 在 langtype 里），**没有独立的
 * array_len 字段**，所以这个数由除法得出——**这是当前唯一可得的数，不是定义**。
 * 两者在合法值上恒等（body 是元素连续打包的）；将来 head 加了 array_len，就应
 * 改读它，而不是继续在这里除。
 */
static inline runtimec_status_t container_numel(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t location;
    runtimec_container_langtype_t type;
    int64_t element_count;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = container_view(frame, &instruction->read_arguments.data[0],
                            &location, &type, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    element_count = (int64_t)(location.size / type.element_size);
    /* 结果槽是调用方声明的具体类型，宽度由它自己决定；本 handler 只保证数是这个。 */
    {
        vmbin_value_location_t destination;
        uint8_t payload[CONTAINER_SCALAR_CAPACITY];

        status = vmbin_value_access_locate_body(
            frame, &instruction->write_arguments.data[0], &destination, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        if (destination.write_bytes == NULL ||
            destination.size > sizeof(payload)) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        memset(payload, 0, sizeof(payload));
        memcpy(payload, &element_count,
               destination.size < sizeof(element_count) ? destination.size
                                                        : sizeof(element_count));
        status = vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(payload, destination.size), &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
}

/*
 * `compactarray·at(a, i) -> e`：元素读。本体是一句 `基址 + i × 宽`。
 *
 * 越界与负下标都返回 INVALID（行为矩阵的负向判据），判据是**长度**，与类型无关。
 */
static inline runtimec_status_t container_at(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t location;
    runtimec_container_langtype_t type;
    int64_t index = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = container_view(frame, &instruction->read_arguments.data[0],
                            &location, &type, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = container_index(frame, &instruction->read_arguments.data[1],
                                 &index, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    if (index < 0 || (uint64_t)index >= location.size / type.element_size) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    return vmbin_value_access_write_body(
               frame, &instruction->write_arguments.data[0],
               runtimec_bytes_view_make(
                   location.read_bytes + (size_t)index * type.element_size,
                   type.element_size),
               &error) == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : container_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
}

/*
 * `compactarray·set(a, i, v) -> a`：元素写，**仅同槽 patch**——目标就是读参那个
 * 槽本身，改的是它的 body 里的一段字节，head 与 attr 一个字节都不动。
 *
 * 值参的字节宽必须等于元素宽：改元素宽就是换 langtype，那是另一种算子的事。
 */
static inline runtimec_status_t container_set(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t location;
    vmbin_value_location_t value;
    runtimec_container_langtype_t type;
    int64_t index = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U ||
        instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = container_view(frame, &instruction->read_arguments.data[0],
                            &location, &type, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = container_index(frame, &instruction->read_arguments.data[1],
                                 &index, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[2], &value, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    if (index < 0 || (uint64_t)index >= location.size / type.element_size ||
        value.size != type.element_size || location.write_bytes == NULL) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(location.write_bytes + (size_t)index * type.element_size,
           value.read_bytes, type.element_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 注册用的薄封装：`binrwir_registry_entry_t` 要的是函数地址，而主体是
 * `static inline`（头文件内联点与注册地址各自独立）。薄封装只在**本文件**
 * 定义一次，注册表里因此只有一份地址。
 */
static runtimec_status_t container_numel_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_numel(frame, instruction, output_exit);
}

static runtimec_status_t container_at_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_at(frame, instruction, output_exit);
}

static runtimec_status_t container_set_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_set(frame, instruction, output_exit);
}

static runtimec_status_t container_new_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_new(frame, instruction, output_exit);
}

static runtimec_status_t container_fill_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_fill(frame, instruction, output_exit);
}

static runtimec_status_t container_newslice_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_newslice(frame, instruction, output_exit);
}

static runtimec_status_t container_newreshape_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_replace_langtype(frame, instruction, 0, output_exit);
}

static runtimec_status_t container_newreinterpret_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return container_replace_langtype(frame, instruction, 1, output_exit);
}

static const binrwir_registry_entry_t runtimec_container_entry_table[] = {
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW, BINRWIR_REGISTRY_FLAG_NONE,
     container_new_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NUMEL, BINRWIR_REGISTRY_FLAG_NONE,
     container_numel_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_AT, BINRWIR_REGISTRY_FLAG_NONE,
     container_at_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_SET, BINRWIR_REGISTRY_FLAG_NONE,
     container_set_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_FILL, BINRWIR_REGISTRY_FLAG_NONE,
     container_fill_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_SLICE,
     BINRWIR_REGISTRY_FLAG_NONE, container_newslice_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_RESHAPE,
     BINRWIR_REGISTRY_FLAG_NONE, container_newreshape_handler},
    {BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_REINTERPRET,
     BINRWIR_REGISTRY_FLAG_NONE, container_newreinterpret_handler},
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
