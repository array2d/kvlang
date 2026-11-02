/*
 * 组件：vmkv/rwir 的实现
 * 作用：在函数源上按 s0 顺序生成明文 RWIR 的只读指令视图：operation name、
 *       读/写两个参数分区、每个参数的类型名/名字/key expression。
 * 联系：vmkv/source 提供槽；encoder 消费本视图做 manifest 校验与地址解析。
 * 约束：不执行、不优化、不修复输入；畸形输入一律失败。相对指针保留为未解析
 *       的 key expression（addressability=DYNAMIC），不在此处求值成帧地址。
 */

#include "vmkv/rwir.h"

#include <string.h>

#include "vmkv/vmkv_internal.h"

/* 参数槽自身 key 的容纳上限：函数路径 + `[4294967295,-2147483648]`。 */
#define VMKV_SLOT_KEY_MARGIN 32U

struct vmkv_rwir_reader_t {
    vmkv_function_source_t *function;
    runtimec_allocator_t allocator;
    uint32_t next_line;
    vmkv_rwir_argument_view_t *arguments;
    size_t argument_capacity;
    char *key_scratch;
    size_t key_scratch_capacity;
};

static runtimec_status_t vmkv_rwir_fail(runtimec_error_t *error,
                                        runtimec_status_t status, int64_t code,
                                        const char *message,
                                        const char *logical_program_counter_text,
                                        size_t logical_program_counter_size) {
    runtimec_error_set(
        error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
        runtimec_string_view_from_c_string(message),
        runtimec_string_view_make(logical_program_counter_text, logical_program_counter_size));
    return status;
}

static bool vmkv_rwir_langtype_is(const vmkv_source_slot_t *slot,
                                  const char *kind) {
    const size_t length = strlen(kind);
    return slot->langtype.size == length &&
           memcmp(slot->langtype.data, kind, length) == 0;
}

/* operation name：opcode 槽的 `rwir`/`rwfunc` body（counts + 文本）。 */
static bool vmkv_rwir_read_opcode(const vmkv_source_slot_t *slot,
                                  runtimec_string_view_t *output_operation,
                                  uint32_t *output_read_count,
                                  uint32_t *output_write_count) {
    *output_operation = runtimec_string_view_make(NULL, 0U);
    *output_read_count = 0U;
    *output_write_count = 0U;
    if (slot == NULL || slot->body_size <= VMKV_COUNTS_HEADER_SIZE) {
        return false;
    }
    const uint8_t *body = slot->body;
    *output_read_count = (uint32_t)body[0] | ((uint32_t)body[1] << 8U);
    *output_write_count = (uint32_t)body[2] | ((uint32_t)body[3] << 8U);
    if (body[4] > 1U) {
        return false;
    }
    const size_t text_size = slot->body_size - VMKV_COUNTS_HEADER_SIZE;
    const char *text = (const char *)(const void *)(body + VMKV_COUNTS_HEADER_SIZE);
    if (text_size == 0U) {
        return false;
    }
    /* opcode 槽永不携带类型后缀（类型后缀只出现在读写槽）。 */
    if (memchr(text, '\0', text_size) != NULL) {
        return false;
    }
    *output_operation = runtimec_string_view_make(text, text_size);
    return true;
}

/* 参数槽分类：`rwir` 值 = 名字/相对指针（+ 可选 NUL 类型串）；其余为内联字面量。 */
static bool vmkv_rwir_classify_argument(const vmkv_source_slot_t *slot,
                                        vmkv_rwir_argument_view_t *output) {
    *output = (vmkv_rwir_argument_view_t){0};
    if (slot == NULL) {
        return false;
    }
    if (!vmkv_rwir_langtype_is(slot, "rwir") &&
        !vmkv_rwir_langtype_is(slot, "rwfunc")) {
        /* 内联字面量：没有计数头，没有相对指针；key 指向槽自身，
         * 同时给出该槽的完整 wire 字节（encoder 不再二次读取）。 */
        output->parameter_name = runtimec_string_view_make(NULL, 0U);
        output->parameter_type_name = slot->langtype;
        output->key = runtimec_key_view_make(NULL, 0U);
        output->key_addressability = VMKV_KEY_ADDRESSABILITY_STATIC;
        output->literal.bytes =
            runtimec_bytes_view_make(slot->wire, slot->wire_size);
        output->literal.langtype = slot->langtype;
        return true;
    }
    if (slot->body_size <= VMKV_COUNTS_HEADER_SIZE) {
        return false;
    }
    const uint8_t *body = slot->body;
    const uint32_t reads =
        (uint32_t)body[0] | ((uint32_t)body[1] << 8U);
    const uint32_t writes =
        (uint32_t)body[2] | ((uint32_t)body[3] << 8U);
    if (body[4] > 1U || reads != 0U || writes != 0U) {
        return false;  // 参数槽的 `rwir` 值恒为 0/0（相对指针或裸名）
    }
    const char *text =
        (const char *)(const void *)(body + VMKV_COUNTS_HEADER_SIZE);
    const size_t text_size = slot->body_size - VMKV_COUNTS_HEADER_SIZE;
    const char *nul = memchr(text, '\0', text_size);
    const size_t expression_size =
        nul == NULL ? text_size : (size_t)(nul - text);
    if (expression_size == 0U) {
        return false;
    }
    output->key = runtimec_key_view_make(text, expression_size);
    /* 绝对路径是可直接解析的静态 key；相对指针 `*[0,±k]` 与裸变量名
     * 都依赖帧，保留为未解析的 key expression。 */
    output->key_addressability = text[0] == '/' ? VMKV_KEY_ADDRESSABILITY_STATIC
                                                : VMKV_KEY_ADDRESSABILITY_DYNAMIC;
    if (nul != NULL && (size_t)(nul - text) + 1U < text_size) {
        output->parameter_type_name = runtimec_string_view_make(
            nul + 1, text_size - expression_size - 1U);
    } else {
        output->parameter_type_name = runtimec_string_view_make(NULL, 0U);
    }
    output->parameter_name = runtimec_string_view_make(NULL, 0U);
    return true;
}

/* 具体类型：非空且不含并集分隔符。 */
static bool vmkv_rwir_type_is_concrete(runtimec_string_view_t type_name) {
    return type_name.size != 0U &&
           memchr(type_name.data, '|', type_name.size) == NULL;
}

/* 变量引用 `*[0,±k]` 解析成参数定义，补上参数名与声明类型。 */
static void vmkv_rwir_attach_parameter(const vmkv_function_source_t *function,
                                       const runtimec_key_view_t *key,
                                       vmkv_rwir_argument_view_t *argument) {
    if (argument->parameter_name.size != 0U || key->size < 6U ||
        key->data[0] != '*' || memcmp(key->data, "*[0,", 4U) != 0) {
        return;
    }
    size_t cursor = 4U;
    bool negative = false;
    if (key->data[cursor] == '-' || key->data[cursor] == '+') {
        negative = key->data[cursor] == '-';
        cursor += 1U;
    }
    int64_t index = 0;
    const size_t digit_start = cursor;
    while (cursor < key->size && key->data[cursor] != ']') {
        if (key->data[cursor] < '0' || key->data[cursor] > '9') {
            return;
        }
        index = index * 10 + (key->data[cursor] - '0');
        if (index > UINT16_MAX) {
            return;
        }
        cursor += 1U;
    }
    if (cursor == digit_start || cursor + 1U != key->size || index == 0) {
        return;
    }
    const uint32_t position = (uint32_t)index - 1U;
    const vmkv_param_definition_t *definition = NULL;
    if (negative && position < function->read_parameter_count) {
        definition = &function->read_parameters[position];
    } else if (!negative && position < function->write_parameter_count) {
        definition = &function->write_parameters[position];
    }
    if (definition == NULL || !definition->present) {
        return;
    }
    argument->parameter_name =
        runtimec_string_view_make(definition->name, definition->name_size);
    /* 具体优先：槽上是实参的实际类型（layout 静态可知时写出），声明类型只是
     * 匹配类，可以含并集。槽上没有具体类型时才回落到声明类型。 */
    if (!vmkv_rwir_type_is_concrete(argument->parameter_type_name)) {
        argument->parameter_type_name = runtimec_string_view_make(
            definition->type_name, definition->type_name_size);
    }
}

/* 槽自身 key：`<function_path>/[line,column]`（内联字面量用）。 */
static bool vmkv_rwir_build_slot_key(vmkv_rwir_reader_t *reader,
                                     const vmkv_source_slot_t *slot,
                                     runtimec_key_view_t *output) {
    const vmkv_function_source_t *function = reader->function;
    size_t used = 0U;
    const size_t path_size = function->function_path_size;
    if (path_size + VMKV_SLOT_KEY_MARGIN > reader->key_scratch_capacity) {
        return false;
    }
    memcpy(reader->key_scratch, function->function_path, path_size);
    used = path_size;
    if (used != 0U && reader->key_scratch[used - 1U] != '/') {
        reader->key_scratch[used++] = '/';
    }
    /* `[line,column]`：十进制，列带符号。 */
    char digits[16];
    size_t count = 0U;
    uint32_t line = slot->line;
    do {
        digits[count++] = (char)('0' + (line % 10U));
        line /= 10U;
    } while (line != 0U);
    reader->key_scratch[used++] = '[';
    while (count != 0U) {
        reader->key_scratch[used++] = digits[--count];
    }
    reader->key_scratch[used++] = ',';
    int32_t column = slot->column;
    if (column < 0) {
        reader->key_scratch[used++] = '-';
        column = -column;
    }
    count = 0U;
    do {
        digits[count++] = (char)('0' + (column % 10));
        column /= 10;
    } while (column != 0);
    while (count != 0U) {
        reader->key_scratch[used++] = digits[--count];
    }
    reader->key_scratch[used++] = ']';
    *output = runtimec_key_view_make(reader->key_scratch, used);
    return true;
}

runtimec_status_t vmkv_rwir_reader_create(
    vmkv_function_source_t *function,
    vmkv_rwir_reader_t **output_reader,
    runtimec_error_t *error) {
    if (output_reader != NULL) {
        *output_reader = NULL;
    }
    runtimec_error_reset(error);
    if (function == NULL || output_reader == NULL || error == NULL) {
        return vmkv_rwir_fail(error, RUNTIMEC_STATUS_INVALID,
                              VMKV_DIAG_INVALID_REQUEST,
                              "vmkv rwir: invalid reader request", NULL, 0U);
    }
    const runtimec_allocator_t allocator = function->host_services.allocator;
    vmkv_rwir_reader_t *reader = vmkv_source_allocate(
        function, sizeof(*reader), _Alignof(vmkv_rwir_reader_t));
    if (reader == NULL) {
        return vmkv_rwir_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                              VMKV_DIAG_OUT_OF_MEMORY,
                              "vmkv rwir: allocation failed", NULL, 0U);
    }
    memset(reader, 0, sizeof(*reader));
    reader->function = function;
    reader->allocator = allocator;
    reader->next_line = 1U;
    *output_reader = reader;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 保证参数数组与 key scratch 容量；扩容后旧视图失效（调用方不跨指令持有）。 */
static runtimec_status_t vmkv_rwir_reserve(vmkv_rwir_reader_t *reader,
                                           size_t argument_count,
                                           runtimec_error_t *error) {
    vmkv_function_source_t *function = reader->function;
    if (argument_count > reader->argument_capacity) {
        const size_t capacity = argument_count;
        vmkv_rwir_argument_view_t *grown = vmkv_source_allocate(
            function, capacity * sizeof(*grown),
            _Alignof(vmkv_rwir_argument_view_t));
        if (grown == NULL) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                  VMKV_DIAG_OUT_OF_MEMORY,
                                  "vmkv rwir: argument allocation failed",
                                  NULL, 0U);
        }
        reader->arguments = grown;
        reader->argument_capacity = capacity;
    }
    if (reader->key_scratch == NULL) {
        const size_t capacity = function->function_path_size +
                                VMKV_SLOT_KEY_MARGIN;
        char *scratch = vmkv_source_allocate(function, capacity, 1U);
        if (scratch == NULL) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                  VMKV_DIAG_OUT_OF_MEMORY,
                                  "vmkv rwir: key scratch allocation failed",
                                  NULL, 0U);
        }
        reader->key_scratch = scratch;
        reader->key_scratch_capacity = capacity;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_rwir_read_argument(
    vmkv_rwir_reader_t *reader, uint32_t line, int32_t column,
    vmkv_rwir_argument_view_t *output, runtimec_error_t *error,
    const char *logical_program_counter_text, size_t logical_program_counter_size) {
    const vmkv_source_slot_t *slot =
        vmkv_source_find_slot(reader->function, line, column);
    if (slot == NULL) {
        return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                              VMKV_DIAG_ARGUMENT_COUNT_MISMATCH,
                              "vmkv rwir: declared argument slot is missing",
                              logical_program_counter_text, logical_program_counter_size);
    }
    if (!vmkv_rwir_classify_argument(slot, output)) {
        return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                              VMKV_DIAG_MALFORMED_VALUE,
                              "vmkv rwir: argument value is malformed",
                              logical_program_counter_text, logical_program_counter_size);
    }
    if (output->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC) {
        vmkv_rwir_attach_parameter(reader->function, &output->key, output);
    } else if (output->parameter_type_name.size != 0U &&
               output->parameter_name.size == 0U &&
               output->key.size == 0U) {
        /* 内联字面量：key 指向槽自身，供消费方取原始字节。 */
        if (!vmkv_rwir_build_slot_key(reader, slot, &output->key)) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                                  VMKV_DIAG_MALFORMED_KEY,
                                  "vmkv rwir: slot key exceeds capacity",
                                  logical_program_counter_text, logical_program_counter_size);
        }
    } else if (output->key.size == 0U) {
        if (!vmkv_rwir_build_slot_key(reader, slot, &output->key)) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                                  VMKV_DIAG_MALFORMED_KEY,
                                  "vmkv rwir: slot key exceeds capacity",
                                  logical_program_counter_text, logical_program_counter_size);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_rwir_reader_next(
    vmkv_rwir_reader_t *reader,
    vmkv_rwir_read_result_t *output_result,
    vmkv_rwir_instruction_view_t *output_instruction,
    runtimec_error_t *error) {
    if (output_result != NULL) {
        *output_result = VMKV_RWIR_READ_RESULT_INVALID;
    }
    if (output_instruction != NULL) {
        *output_instruction = (vmkv_rwir_instruction_view_t){0};
    }
    runtimec_error_reset(error);
    if (reader == NULL || output_result == NULL || output_instruction == NULL ||
        error == NULL) {
        return vmkv_rwir_fail(error, RUNTIMEC_STATUS_INVALID,
                              VMKV_DIAG_INVALID_REQUEST,
                              "vmkv rwir: invalid read request", NULL, 0U);
    }
    const vmkv_function_source_t *function = reader->function;
    while (reader->next_line <= function->instruction_line_count) {
        const uint32_t line = reader->next_line;
        const vmkv_source_slot_t *opcode_slot =
            vmkv_source_find_slot(function, line, 0);
        if (opcode_slot == NULL) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                                  VMKV_DIAG_MISSING_OPCODE,
                                  "vmkv rwir: instruction opcode slot is missing",
                                  NULL, 0U);
        }
        runtimec_string_view_t operation;
        uint32_t read_count = 0U;
        uint32_t write_count = 0U;
        if (!vmkv_rwir_read_opcode(opcode_slot, &operation, &read_count,
                                   &write_count) ||
            operation.size == 0U) {
            return vmkv_rwir_fail(error, RUNTIMEC_STATUS_ERROR,
                                  VMKV_DIAG_EMPTY_OPCODE,
                                  "vmkv rwir: instruction opcode is malformed",
                                  NULL, 0U);
        }
        /* 逻辑程序计数器 = `[line,0]`（与 layout 的坐标口径一致）。 */
        char logical_program_counter_text[24];
        size_t logical_program_counter_size = 0U;
        logical_program_counter_text[logical_program_counter_size++] = '[';
        {
            char digits[16];
            size_t count = 0U;
            uint32_t value = line;
            do {
                digits[count++] = (char)('0' + (value % 10U));
                value /= 10U;
            } while (value != 0U);
            while (count != 0U) {
                logical_program_counter_text[logical_program_counter_size++] = digits[--count];
            }
        }
        logical_program_counter_text[logical_program_counter_size++] = ',';
        logical_program_counter_text[logical_program_counter_size++] = '0';
        logical_program_counter_text[logical_program_counter_size++] = ']';

        const size_t total = (size_t)read_count + (size_t)write_count;
        runtimec_status_t status =
            vmkv_rwir_reserve(reader, total == 0U ? 1U : total, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        for (uint32_t i = 0U; i < read_count; i++) {
            status = vmkv_rwir_read_argument(
                reader, line, -(int32_t)(i + 1U), &reader->arguments[i], error,
                logical_program_counter_text, logical_program_counter_size);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
        }
        for (uint32_t i = 0U; i < write_count; i++) {
            status = vmkv_rwir_read_argument(
                reader, line, (int32_t)(i + 1U),
                &reader->arguments[read_count + i], error, logical_program_counter_text,
                logical_program_counter_size);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
        }
        output_instruction->operation_name = operation;
        output_instruction->logical_program_counter = line;
        output_instruction->read_arguments = reader->arguments;
        output_instruction->read_argument_count = read_count;
        output_instruction->write_arguments =
            read_count == 0U ? reader->arguments
                             : reader->arguments + read_count;
        output_instruction->write_argument_count = write_count;
        reader->next_line = line + 1U;
        *output_result = VMKV_RWIR_READ_RESULT_INSTRUCTION;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    *output_result = VMKV_RWIR_READ_RESULT_END;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_rwir_reader_destroy(vmkv_rwir_reader_t *reader) {
    if (reader == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* reader 与其数组都是函数源分配区的成员：随 source 一起释放。 */
    reader->function = NULL;
    reader->arguments = NULL;
    reader->argument_capacity = 0U;
    reader->key_scratch = NULL;
    reader->key_scratch_capacity = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}
