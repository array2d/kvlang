/*
 * 组件：vmkv/source 的实现
 * 作用：经主机能力表读取一个函数目录的明文 RWIR：签名行计数头、参数定义键
 *       `.[0,±j]`、全部坐标槽 `[s0,s1]`，按快照约定建立只读视图。
 * 联系：vmkv/rwir 只在本结构上顺序读取；encoder 只消费 rwir 的视图。
 * 约束：不执行、不生成机器码、不修改执行事实；任何畸形输入都必须失败，
 *       失败时不留下函数源（已分配的全部释放）。
 */

#include "vmkv/source.h"

#include <stdio.h>
#include <string.h>

#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>
#include "vmkv/vmkv_internal.h"

/* `[s0,s1]` / `.[0,±j]` 两种后缀的最大解析长度：足够容纳 10 位 s0 与 11 位
 * 有符号 s1（含符号），远超 layout 的实际输出。 */
#define VMKV_COORDINATE_TEXT_CAPACITY 32U

typedef struct vmkv_source_build {
    vmkv_function_source_t *function;
    const char *prefix;        /* 枚举前缀：函数键（无尾斜杠） */
    size_t prefix_size;
    size_t slot_capacity;
} vmkv_source_build_t;

static runtimec_status_t vmkv_source_fail(runtimec_error_t *error,
                                          runtimec_status_t status,
                                          int64_t code, const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

void *vmkv_source_allocate(vmkv_function_source_t *function, size_t size,
                           size_t alignment) {
    if (function == NULL || size == 0U) {
        return NULL;
    }
    if (function->allocation_count == function->allocation_capacity) {
        const size_t capacity = function->allocation_capacity == 0U
                                    ? 8U
                                    : function->allocation_capacity * 2U;
        vmkv_allocation_t *grown = runtimec_allocator_allocate(
            &function->kvspace_access->allocator,
            capacity * sizeof(*grown), _Alignof(vmkv_allocation_t));
        if (grown == NULL) {
            return NULL;
        }
        if (function->allocation_count != 0U) {
            memcpy(grown, function->allocations,
                   function->allocation_count * sizeof(*grown));
        }
        if (function->allocations != NULL) {
            (void)runtimec_allocator_deallocate(
                &function->kvspace_access->allocator, function->allocations,
                function->allocation_capacity * sizeof(*grown),
                _Alignof(vmkv_allocation_t));
        }
        function->allocations = grown;
        function->allocation_capacity = capacity;
    }
    void *pointer = runtimec_allocator_allocate(
        &function->kvspace_access->allocator, size, alignment);
    if (pointer == NULL) {
        return NULL;
    }
    vmkv_allocation_t *entry =
        &function->allocations[function->allocation_count++];
    entry->pointer = pointer;
    entry->size = size;
    entry->alignment = alignment;
    return pointer;
}

bool vmkv_source_split_wire(const uint8_t *wire, size_t wire_size,
                            const uint8_t **output_body,
                            size_t *output_body_size,
                            runtimec_string_view_t *output_langtype) {
    *output_body = NULL;
    *output_body_size = 0U;
    *output_langtype = runtimec_string_view_make(NULL, 0U);
    if (wire == NULL) {
        return false;
    }
    /*
     * 读 head 本身要求 wire 至少有 head 那么长；这是「能不能解引用」，不是
     * body/attr 的边界算术。除此之外的边界判定全部交给 head helper。
     */
    if (wire_size < sizeof(kvspaceXvalueHead)) {
        return false;
    }
    const kvspaceXvalueHead *head =
        (const kvspaceXvalueHead *)(const void *)wire;
    /* 总长 = 16 + bodylen + attrslen；helper 同时校验 padding 为零与乘法溢出。 */
    const uint64_t total_size = kvspaceXvalueHeadTotalLen(head);
    if (total_size == 0U || total_size > (uint64_t)wire_size) {
        return false;
    }
    /*
     * langtype 记录是可选的：`‥ctime` 这类值没有它，仍然是合法值。所以「无
     * langtype」与「值非法」不能都靠 Bounded 的返回 0 来判——上面已经用总长
     * 判过合法性，这里只取 langtype。
     */
    const uint8_t *langtype = NULL;
    const uint16_t langtype_size =
        kvlangXvalueHeadLangtypeBounded(wire, (uint64_t)wire_size, &langtype);
    *output_body = wire + KVSPACE_XVALUE_HEAD_LEN;
    *output_body_size = (size_t)kvspaceXvalueHeadLen(head);
    if (langtype_size != 0U && langtype != NULL) {
        *output_langtype = runtimec_string_view_make(
            (const char *)(const void *)langtype, langtype_size);
    }
    return true;
}

/* 读一个完整值到调用方缓冲；缓冲不足时由 host 报告容量需求，本层按
 * INSUFFICIENT_CAPACITY 再分配重试一次（值大小在读取前不可知）。 */
static runtimec_status_t vmkv_source_read_value(
    vmkv_source_build_t *build, runtimec_key_view_t key,
    const uint8_t **output_wire, size_t *output_size, runtimec_error_t *error) {
    runtimec_kvspace_access_t *host = build->function->kvspace_access;
    const runtimec_snapshot_t *snapshot = &build->function->snapshot;

    runtimec_kvspace_value_header_t header;
    runtimec_kvspace_value_header_reset(&header);
    runtimec_status_t status = runtimec_kvspace_access_read_header(
        host, snapshot, key, &header, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (header.value_size == 0U || header.value_size > (uint64_t)SIZE_MAX) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: value size is invalid");
    }
    const size_t size = (size_t)header.value_size;
    uint8_t *buffer = vmkv_source_allocate(build->function, size, 1U);
    if (buffer == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                VMKV_DIAG_OUT_OF_MEMORY,
                                "vmkv source: value buffer allocation failed");
    }
    runtimec_bytes_buffer_t output;
    output.data = buffer;
    output.size = 0U;
    output.capacity = size;
    status = runtimec_kvspace_access_read(host, snapshot, key, &output, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (output.size != size) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: value size changed while reading");
    }
    *output_wire = buffer;
    *output_size = size;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 解析坐标后缀：`[s0,s1]` 或 `.[0,±j]`。返回 false = 不是坐标键。 */
static bool vmkv_source_parse_coordinate(const char *text, size_t text_size,
                                         bool *output_is_parameter,
                                         uint32_t *output_line,
                                         int32_t *output_column) {
    *output_is_parameter = false;
    size_t cursor = 0U;
    if (text_size != 0U && text[0] == '.') {
        *output_is_parameter = true;
        cursor = 1U;
    }
    if (text_size - cursor < 4U || text[cursor] != '[') {
        return false;
    }
    cursor += 1U;
    const size_t body_start = cursor;
    while (cursor < text_size && text[cursor] != ',') {
        if (text[cursor] < '0' || text[cursor] > '9') {
            return false;
        }
        cursor += 1U;
    }
    if (cursor == body_start || cursor >= text_size) {
        return false;
    }
    uint64_t line = 0U;
    for (size_t i = body_start; i < cursor; i++) {
        line = line * 10U + (uint64_t)(text[i] - '0');
        if (line > UINT32_MAX) {
            return false;
        }
    }
    cursor += 1U;
    bool negative = false;
    if (cursor < text_size && (text[cursor] == '-' || text[cursor] == '+')) {
        negative = text[cursor] == '-';
        cursor += 1U;
    }
    const size_t column_start = cursor;
    int64_t column = 0;
    while (cursor < text_size && text[cursor] != ']') {
        if (text[cursor] < '0' || text[cursor] > '9') {
            return false;
        }
        column = column * 10 + (int64_t)(text[cursor] - '0');
        if (column > INT32_MAX) {
            return false;
        }
        cursor += 1U;
    }
    if (cursor == column_start || cursor + 1U != text_size) {
        return false;
    }
    if (*output_is_parameter && line != 0U) {
        return false;  // 参数定义键恒为 `.[0,±j]`
    }
    *output_line = (uint32_t)line;
    *output_column = negative ? (int32_t)(-column) : (int32_t)column;
    return true;
}

static vmkv_source_slot_t *vmkv_source_take_slot(
    vmkv_source_build_t *build) {
    vmkv_function_source_t *function = build->function;
    if (function->slot_count == build->slot_capacity) {
        const size_t capacity =
            build->slot_capacity == 0U ? 32U : build->slot_capacity * 2U;
        vmkv_source_slot_t *grown = vmkv_source_allocate(
            function, capacity * sizeof(*grown), _Alignof(vmkv_source_slot_t));
        if (grown == NULL) {
            return NULL;
        }
        if (function->slot_count != 0U) {
            memcpy(grown, function->slots,
                   function->slot_count * sizeof(*grown));
        }
        function->slots = grown;
        build->slot_capacity = capacity;
    }
    return &function->slots[function->slot_count++];
}

/* True when the type text ends with " inplace" after a non-empty type. */
static bool vmkv_source_has_inplace_suffix(const char *type_name,
                                           size_t type_size) {
    static const char inplace_suffix[] = " inplace";
    const size_t inplace_suffix_size = sizeof(inplace_suffix) - 1U;
    return type_size > inplace_suffix_size &&
           memcmp(type_name + (type_size - inplace_suffix_size),
                  inplace_suffix, inplace_suffix_size) == 0;
}

/* Param key body: `name NUL langtype[ inplace]`; the suffix only on write params. */
static bool vmkv_source_parse_parameter_definition(
    const uint8_t *body, size_t body_size, bool read_side,
    vmkv_param_definition_t *output) {
    const size_t inplace_suffix_size = sizeof(" inplace") - 1U;
    *output = (vmkv_param_definition_t){0};
    size_t name_size = 0U;
    while (name_size < body_size && body[name_size] != 0U) {
        name_size += 1U;
    }
    if (name_size == 0U || name_size == body_size) {
        return false;
    }
    size_t type_size = body_size - name_size - 1U;
    if (type_size == 0U) {
        return false;
    }
    const char *type_name =
        (const char *)(const void *)(body + name_size + 1U);
    bool inplace = false;
    if (vmkv_source_has_inplace_suffix(type_name, type_size)) {
        if (read_side) {
            return false;
        }
        type_size -= inplace_suffix_size;
        inplace = true;
        /* A doubled suffix is malformed. */
        if (vmkv_source_has_inplace_suffix(type_name, type_size)) {
            return false;
        }
    }
    if (type_size == 0U) {
        return false;
    }
    output->name = (const char *)(const void *)body;
    output->name_size = name_size;
    output->type_name = type_name;
    output->type_name_size = type_size;
    output->inplace = inplace;
    output->present = true;
    return true;
}

static runtimec_status_t vmkv_source_accept_listed_key(
    vmkv_source_build_t *build, const char *text, size_t text_size,
    runtimec_error_t *error);

static runtimec_status_t vmkv_source_list_visit(void *context,
                                                runtimec_key_view_t key,
                                                runtimec_error_t *error) {
    vmkv_source_build_t *build = context;
    if (key.data == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_KEY,
                                "vmkv source: listed key is empty");
    }
    return vmkv_source_accept_listed_key(build, key.data, key.size, error);
}

/*
 * 读一个参数定义键并写进参数表。key 是完整键（`<func>.[0,±j]`）。
 * 与枚举路径共用同一套 wire 解析与重复检查。
 */
static runtimec_status_t vmkv_source_read_parameter_key(
    vmkv_source_build_t *build, runtimec_key_view_t key, bool read_side,
    runtimec_error_t *error) {
    vmkv_function_source_t *function = build->function;
    const char *text = key.data;
    const size_t text_size = key.size;
    if (text_size < 3U || text[text_size - 1U] != ']') {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /* 从键尾解出 `.[0,±j]` 的 j 与方向：`]` 前后是十进制数。 */
    size_t cursor = text_size - 1U;
    uint32_t index = 0U;
    while (cursor > 0U && text[cursor] != ',' && text[cursor] != '[') {
        cursor -= 1U;
    }
    if (cursor == 0U || text[cursor] != ',') {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    size_t digit = cursor + 1U;
    /* 负坐标 `.[0,-j]` 的符号在逗号之后：先跳过 `-` 再读数字。 */
    if (digit < text_size && text[digit] == '-') {
        digit += 1U;
    }
    while (digit < text_size && text[digit] >= '0' && text[digit] <= '9') {
        index = index * 10U + (uint32_t)(text[digit] - '0');
        digit += 1U;
    }
    if (index == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    const uint8_t *wire = NULL;
    size_t wire_size = 0U;
    runtimec_status_t status =
        vmkv_source_read_value(build, key, &wire, &wire_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        if (status == RUNTIMEC_STATUS_UNSUPPORTED) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return status;
    }
    const uint8_t *body = NULL;
    size_t body_size = 0U;
    runtimec_string_view_t langtype;
    if (!vmkv_source_split_wire(wire, wire_size, &body, &body_size, &langtype)) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: parameter definition is malformed");
    }
    vmkv_param_definition_t *table =
        read_side ? function->read_parameters : function->write_parameters;
    if (index > (read_side ? function->read_parameter_count
                           : function->write_parameter_count)) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (table[index - 1U].present) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (!vmkv_source_parse_parameter_definition(body, body_size, read_side,
                                                &table[index - 1U])) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: parameter definition is malformed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 接受一个「完整键」：判断它是不是本函数的坐标/参数槽，是则读值并登记。
 * 枚举路径与「按坐标补读」路径共用这一份——槽解析只有一处。
 */
static runtimec_status_t vmkv_source_accept_listed_key(
    vmkv_source_build_t *build, const char *text, size_t text_size,
    runtimec_error_t *error) {
    vmkv_function_source_t *function = build->function;
    runtimec_key_view_t key = runtimec_key_view_make(text, text_size);
    if (text == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_KEY,
                                "vmkv source: listed key is empty");
    }
    /* 三类键（前缀为函数键，不带尾斜杠）：
     *   `<func>`        函数目录本身        → 忽略
     *   `<func>/…`      目录内坐标槽        → suffix = `/…` 之后
     *   `<func>.[0,±j]` 参数定义键          → suffix = `.` 起
     * 其余 `<func>X…`（同前缀的兄弟函数）一律忽略。 */
    if (text_size < build->prefix_size ||
        memcmp(text, build->prefix, build->prefix_size) != 0) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    const char *suffix = text + build->prefix_size;
    size_t suffix_size = text_size - build->prefix_size;
    if (suffix_size == 0U || suffix_size == 1U) {
        return RUNTIMEC_STATUS_SUCCESS;  // 目录本身（含尾斜杠形态）
    }
    if (suffix[0] == '/') {
        suffix += 1;
        suffix_size -= 1U;
    } else if (suffix[0] != '.') {
        return RUNTIMEC_STATUS_SUCCESS;  // 同前缀的兄弟键
    }
    bool is_parameter = false;
    uint32_t line = 0U;
    int32_t column = 0;
    if (!vmkv_source_parse_coordinate(suffix, suffix_size, &is_parameter,
                                      &line, &column)) {
        /* 目录本身、`‥ctime` 等非坐标键忽略；只在它们位于子槽形态时失败。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    const uint8_t *wire = NULL;
    size_t wire_size = 0U;
    runtimec_status_t status =
        vmkv_source_read_value(build, key, &wire, &wire_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    const uint8_t *body = NULL;
    size_t body_size = 0U;
    runtimec_string_view_t langtype;
    if (!vmkv_source_split_wire(wire, wire_size, &body, &body_size,
                                &langtype)) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: coordinate value is malformed");
    }
    if (is_parameter) {
        const uint32_t index =
            column < 0 ? (uint32_t)(-column) : (uint32_t)column;
        if (index == 0U) {
            return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                    VMKV_DIAG_MALFORMED_KEY,
                                    "vmkv source: parameter index is zero");
        }
        vmkv_param_definition_t *table = column < 0 ? function->read_parameters
                                                    : function->write_parameters;
        if (table[index - 1U].present) {
            return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                    VMKV_DIAG_MALFORMED_KEY,
                                    "vmkv source: duplicate parameter key");
        }
        if (!vmkv_source_parse_parameter_definition(body, body_size, column < 0,
                                                    &table[index - 1U])) {
            return vmkv_source_fail(
                error, RUNTIMEC_STATUS_ERROR, VMKV_DIAG_MALFORMED_VALUE,
                "vmkv source: parameter definition is malformed");
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (line == 0U) {
        /* 签名行 [0,0] 在枚举前单独读取；其余 [0,±j] 槽不属于本读者。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    vmkv_source_slot_t *slot = vmkv_source_take_slot(build);
    if (slot == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                VMKV_DIAG_OUT_OF_MEMORY,
                                "vmkv source: slot table allocation failed");
    }
    *slot = (vmkv_source_slot_t){
        .line = line,
        .column = column,
        .wire = wire,
        .wire_size = wire_size,
        .body = body,
        .body_size = body_size,
        .langtype = langtype,
        .present = true,
    };
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 读签名行锚点 `[0,0]`：五字节计数头。 */
static runtimec_status_t vmkv_source_read_signature(
    vmkv_source_build_t *build, size_t *output_anchor_size,
    runtimec_error_t *error) {
    vmkv_function_source_t *function = build->function;
    char key_text[VMKV_COORDINATE_TEXT_CAPACITY + 1U];
    const size_t prefix_size = build->prefix_size;
    if (prefix_size + 6U > sizeof(key_text)) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_KEY,
                                "vmkv source: function path is too long");
    }
    memcpy(key_text, build->prefix, prefix_size);
    memcpy(key_text + prefix_size, "/[0,0]", 6U);
    const runtimec_key_view_t key =
        runtimec_key_view_make(key_text, prefix_size + 6U);
    const uint8_t *wire = NULL;
    size_t wire_size = 0U;
    runtimec_status_t status =
        vmkv_source_read_value(build, key, &wire, &wire_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        if (status == RUNTIMEC_STATUS_UNSUPPORTED) {
            return vmkv_source_fail(
                error, RUNTIMEC_STATUS_ERROR, VMKV_DIAG_MISSING_SIGNATURE_ANCHOR,
                "vmkv source: function signature anchor is missing");
        }
        return status;
    }
    const uint8_t *body = NULL;
    size_t body_size = 0U;
    runtimec_string_view_t langtype;
    if (!vmkv_source_split_wire(wire, wire_size, &body, &body_size,
                                &langtype) ||
        body_size != VMKV_COUNTS_HEADER_SIZE) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: signature anchor is malformed");
    }
    const uint32_t reads =
        (uint32_t)body[0] | ((uint32_t)body[1] << 8U);
    const uint32_t writes =
        (uint32_t)body[2] | ((uint32_t)body[3] << 8U);
    const uint8_t dynamic = body[4];
    if (dynamic > 1U) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_ERROR,
                                VMKV_DIAG_MALFORMED_VALUE,
                                "vmkv source: signature dynamic flag is invalid");
    }
    function->read_parameter_count = reads;
    function->write_parameter_count = writes;
    function->dynamic_parameters = dynamic != 0U;
    *output_anchor_size = wire_size;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_source_release(
    vmkv_function_source_t *function, const runtimec_allocator_t *allocator) {
    runtimec_status_t result = RUNTIMEC_STATUS_SUCCESS;
    if (function->allocations != NULL) {
        for (size_t i = function->allocation_count; i > 0U; i--) {
            const vmkv_allocation_t *entry = &function->allocations[i - 1U];
            if (runtimec_allocator_deallocate(allocator, entry->pointer,
                                              entry->size,
                                              entry->alignment) !=
                RUNTIMEC_STATUS_SUCCESS) {
                result = RUNTIMEC_STATUS_ERROR;
            }
        }
    }
    /* allocation 表自身在最后释放：它记录了自己的大小与对齐。 */
    if (function->allocations != NULL) {
        if (runtimec_allocator_deallocate(
                allocator, function->allocations,
                function->allocation_capacity * sizeof(vmkv_allocation_t),
                _Alignof(vmkv_allocation_t)) != RUNTIMEC_STATUS_SUCCESS) {
            result = RUNTIMEC_STATUS_ERROR;
        }
    }
    if (runtimec_allocator_deallocate(allocator, function, sizeof(*function),
                                      _Alignof(vmkv_function_source_t)) !=
        RUNTIMEC_STATUS_SUCCESS) {
        result = RUNTIMEC_STATUS_ERROR;
    }
    return result;
}

runtimec_status_t vmkv_source_open_function(
    runtimec_kvspace_access_t *kvspace_access,
    runtimec_string_view_t function_path,
    vmkv_function_source_t **output_function,
    runtimec_snapshot_t *output_snapshot,
    runtimec_error_t *error) {
    if (output_function != NULL) {
        *output_function = NULL;
    }
    if (output_snapshot != NULL) {
        runtimec_snapshot_reset(output_snapshot);
    }
    runtimec_error_reset(error);
    if (kvspace_access == NULL ||
        !runtimec_string_view_is_valid(function_path) ||
        function_path.size == 0U || output_function == NULL ||
        output_snapshot == NULL || error == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_INVALID,
                                VMKV_DIAG_INVALID_REQUEST,
                                "vmkv source: invalid open request");
    }
    const runtimec_allocator_t allocator = kvspace_access->allocator;
    vmkv_function_source_t *function = runtimec_allocator_allocate(
        &allocator, sizeof(*function), _Alignof(vmkv_function_source_t));
    if (function == NULL) {
        return vmkv_source_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                VMKV_DIAG_OUT_OF_MEMORY,
                                "vmkv source: allocation failed");
    }
    memset(function, 0, sizeof(*function));
    function->kvspace_access = kvspace_access;

    vmkv_source_build_t build = {
        .function = function,
        .prefix = function_path.data,
        .prefix_size = function_path.size,
        .slot_capacity = 0U,
    };

    /* 快照：函数键 + source revision + 契约版本（逻辑程序计数器由读取器给出）。 */
    runtimec_snapshot_reset(&function->snapshot);
    function->snapshot.function_key =
        runtimec_key_view_make(function_path.data, function_path.size);
    runtimec_status_t status = runtimec_kvspace_access_source_revision(
        kvspace_access, function->snapshot.function_key,
        &function->snapshot.source_revision, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)vmkv_source_release(function, &allocator);
        return status;
    }

    function->function_path = function_path.data;
    function->function_path_size = function_path.size;

    /* 枚举前缀 = 函数键去掉尾斜杠：坐标槽是 `<func>/…` 子键，参数定义键是
     * `<func>.[0,±j]` 兄弟键，二者都要被枚举到，故前缀停在函数名处。 */
    size_t prefix_size = function_path.size;
    while (prefix_size != 0U && function_path.data[prefix_size - 1U] == '/') {
        prefix_size -= 1U;
    }
    if (prefix_size == 0U) {
        (void)vmkv_source_release(function, &allocator);
        return vmkv_source_fail(error, RUNTIMEC_STATUS_INVALID,
                                VMKV_DIAG_MALFORMED_KEY,
                                "vmkv source: function path is empty");
    }
    char *prefix = vmkv_source_allocate(function, prefix_size + 1U, 1U);
    if (prefix == NULL) {
        (void)vmkv_source_release(function, &allocator);
        return vmkv_source_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                VMKV_DIAG_OUT_OF_MEMORY,
                                "vmkv source: prefix allocation failed");
    }
    memcpy(prefix, function_path.data, prefix_size);
    prefix[prefix_size] = '/';
    build.prefix = prefix;
    build.prefix_size = prefix_size;

    size_t anchor_size = 0U;
    status = vmkv_source_read_signature(&build, &anchor_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)vmkv_source_release(function, &allocator);
        return status;
    }

    /* 参数定义键按签名声明的数量一次分配；缺失由枚举结束后统一判定。 */
    const size_t read_capacity =
        function->read_parameter_count == 0U ? 1U
                                             : function->read_parameter_count;
    const size_t write_capacity =
        function->write_parameter_count == 0U ? 1U
                                              : function->write_parameter_count;
    function->read_parameters = vmkv_source_allocate(
        function, read_capacity * sizeof(*function->read_parameters),
        _Alignof(vmkv_param_definition_t));
    function->write_parameters = vmkv_source_allocate(
        function, write_capacity * sizeof(*function->write_parameters),
        _Alignof(vmkv_param_definition_t));
    if (function->read_parameters == NULL ||
        function->write_parameters == NULL) {
        (void)vmkv_source_release(function, &allocator);
        return vmkv_source_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                VMKV_DIAG_OUT_OF_MEMORY,
                                "vmkv source: parameter table allocation failed");
    }
    memset(function->read_parameters, 0,
           read_capacity * sizeof(*function->read_parameters));
    memset(function->write_parameters, 0,
           write_capacity * sizeof(*function->write_parameters));

    status = runtimec_kvspace_access_list(
        kvspace_access, &function->snapshot,
        runtimec_key_view_make(prefix, prefix_size), vmkv_source_list_visit,
        &build, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)vmkv_source_release(function, &allocator);
        return status;
    }

    /*
     * 枚举可能整片看不到：嵌套 lib 的函数键含 `·`（`/lib/outer/inner·add1`），
     * 目录枚举只返回 `inner`，函数子树里的坐标槽**一条都拿不到**。此时按
     * **确定坐标**补读：`<func>/[n,0]` 是第 n 条指令的算子槽，其 body 的计数头
     * 给出该行的读写参数个数，依此读 `[n,∓j]`；算子槽不存在即指令序列结束。
     *
     * 只在枚举一无所获时启用：普通函数走枚举，不付这份代价。
     */
    if (function->slot_count == 0U) {
        char key_text[512];
        for (uint32_t line = 1U; line <= 4096U; ++line) {
            const int key_size = snprintf(key_text, sizeof(key_text),
                                          "%.*s/[%u,0]", (int)build.prefix_size,
                                          build.prefix, (unsigned)line);
            if (key_size < 0 || (size_t)key_size >= sizeof(key_text)) {
                break;
            }
            const uint8_t *wire = NULL;
            size_t wire_size = 0U;
            if (vmkv_source_read_value(
                    &build, runtimec_key_view_make(key_text, (size_t)key_size),
                    &wire, &wire_size, error) != RUNTIMEC_STATUS_SUCCESS) {
                break; /* 不存在即结束 */
            }
            status = vmkv_source_accept_listed_key(&build, key_text,
                                                   (size_t)key_size, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                (void)vmkv_source_release(function, &allocator);
                return status;
            }
            const uint8_t *opcode_body = NULL;
            size_t opcode_body_size = 0U;
            runtimec_string_view_t opcode_langtype;
            if (!vmkv_source_split_wire(wire, wire_size, &opcode_body,
                                        &opcode_body_size, &opcode_langtype) ||
                opcode_body_size < VMKV_COUNTS_HEADER_SIZE) {
                break;
            }
            const uint32_t read_count =
                (uint32_t)opcode_body[0] | ((uint32_t)opcode_body[1] << 8U);
            const uint32_t write_count =
                (uint32_t)opcode_body[2] | ((uint32_t)opcode_body[3] << 8U);
            for (uint32_t index = 1U; index <= read_count; ++index) {
                const int argument_key_size = snprintf(
                    key_text, sizeof(key_text), "%.*s/[%u,-%u]",
                    (int)build.prefix_size, build.prefix, (unsigned)line,
                    (unsigned)index);
                if (argument_key_size < 0 ||
                    (size_t)argument_key_size >= sizeof(key_text)) {
                    break;
                }
                status = vmkv_source_accept_listed_key(
                    &build, key_text, (size_t)argument_key_size, error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    (void)vmkv_source_release(function, &allocator);
                    return status;
                }
            }
            for (uint32_t index = 1U; index <= write_count; ++index) {
                const int argument_key_size = snprintf(
                    key_text, sizeof(key_text), "%.*s/[%u,%u]",
                    (int)build.prefix_size, build.prefix, (unsigned)line,
                    (unsigned)index);
                if (argument_key_size < 0 ||
                    (size_t)argument_key_size >= sizeof(key_text)) {
                    break;
                }
                status = vmkv_source_accept_listed_key(
                    &build, key_text, (size_t)argument_key_size, error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    (void)vmkv_source_release(function, &allocator);
                    return status;
                }
            }
        }
    }

    /*
     * 补齐枚举可能漏掉的参数定义键：**直接按坐标读** `<func>.[0,±j]`。
     *
     * 为什么需要：枚举前缀是函数键的父目录，而嵌套 lib 的函数键形如
     * `/lib/outer/inner·add1`——目录枚举把含 `·` 的条目当扩展项，默认不返回，
     * 于是参数定义键「存在但没被发现」，编码期报「read parameter definition is
     * missing」。坐标是**确定**的（签名给了 nr/nw），直接构造键比依赖枚举可靠。
     *
     * 只补枚举没给到的槽：已 present 的不再读，避免与重复检查冲突。
     */
    {
        char key_text[512];
        for (uint32_t i = 0U; i < function->read_parameter_count; ++i) {
            if (function->read_parameters[i].present) {
                continue;
            }
            const int key_size = snprintf(key_text, sizeof(key_text),
                                          "%.*s.[0,-%u]", (int)build.prefix_size,
                                          build.prefix, (unsigned)(i + 1U));
            if (key_size < 0 || (size_t)key_size >= sizeof(key_text)) {
                continue; /* 键名放不下不是「缺失」的成因，留给下面统一报错 */
            }
            (void)vmkv_source_read_parameter_key(
                &build, runtimec_key_view_make(key_text, (size_t)key_size),
                true, error);
        }
        for (uint32_t i = 0U; i < function->write_parameter_count; ++i) {
            if (function->write_parameters[i].present) {
                continue;
            }
            const int key_size = snprintf(key_text, sizeof(key_text),
                                          "%.*s.[0,%u]", (int)build.prefix_size,
                                          build.prefix, (unsigned)(i + 1U));
            if (key_size < 0 || (size_t)key_size >= sizeof(key_text)) {
                continue;
            }
            (void)vmkv_source_read_parameter_key(
                &build, runtimec_key_view_make(key_text, (size_t)key_size),
                false, error);
        }
    }

    /* 签名声明的每个参数必须有定义键；多余的定义键在上面的重复检查中已拒绝。 */
    for (uint32_t i = 0U; i < function->read_parameter_count; i++) {
        if (!function->read_parameters[i].present) {
            (void)vmkv_source_release(function, &allocator);
            return vmkv_source_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_DIAG_MISSING_PARAMETER_DEFINITION,
                "vmkv source: read parameter definition is missing");
        }
    }
    for (uint32_t i = 0U; i < function->write_parameter_count; i++) {
        if (!function->write_parameters[i].present) {
            (void)vmkv_source_release(function, &allocator);
            return vmkv_source_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_DIAG_MISSING_PARAMETER_DEFINITION,
                "vmkv source: write parameter definition is missing");
        }
    }

    /* 指令行 = 出现过的最大行号；opcode 槽存在性在读取器逐行校验。 */
    uint32_t maximum_line = 0U;
    for (size_t i = 0U; i < function->slot_count; i++) {
        if (function->slots[i].line > maximum_line) {
            maximum_line = function->slots[i].line;
        }
    }
    function->instruction_line_count = maximum_line;

    *output_snapshot = function->snapshot;
    (void)anchor_size;
    *output_function = function;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_source_close_function(vmkv_function_source_t *function) {
    if (function == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const runtimec_allocator_t allocator = function->kvspace_access->allocator;
    return vmkv_source_release(function, &allocator);
}

const vmkv_source_slot_t *vmkv_source_find_slot(
    const vmkv_function_source_t *function, uint32_t line, int32_t column) {
    if (function == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < function->slot_count; i++) {
        const vmkv_source_slot_t *slot = &function->slots[i];
        if (slot->line == line && slot->column == column) {
            return slot;
        }
    }
    return NULL;
}
