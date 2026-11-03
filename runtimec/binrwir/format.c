/*
 * 组件：binrwir/format 的实现
 * 作用：把 Program 的签名、指令序列与两个池打印成分区文本。
 * 约束：只用 sprintf 级别的整数格式化，不打印地址与 generation，因此同一 Program
 *       在任意进程里都打印出相同字节。每个输出片段都走 sink 回调，不做隐式缓冲。
 */

#include "binrwir/format.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 一行的临时缓冲；一行最长的是 [pc] op(所有实参) -> (所有结果)，远超也就增长
 * 不了——超了就走 sink 失败路径，不静默截断。 */
#define BINRWIR_FORMAT_CHUNK_CAPACITY 4096U

typedef struct binrwir_format_writer {
    binrwir_format_sink_function sink;
    void *context;
    char chunk[BINRWIR_FORMAT_CHUNK_CAPACITY];
    size_t length;
    runtimec_status_t status;
} binrwir_format_writer_t;

static void binrwir_format_writer_initialize(
    binrwir_format_writer_t *writer, binrwir_format_sink_function sink,
    void *context) {
    memset(writer, 0, sizeof(*writer));
    writer->sink = sink;
    writer->context = context;
    writer->status = RUNTIMEC_STATUS_SUCCESS;
}

/* 交付已缓冲的片段。返回 0 表示 sink 接不下，后续写入全部作废。 */
static int binrwir_format_writer_flush(binrwir_format_writer_t *writer) {
    runtimec_string_view_t chunk;
    runtimec_string_view_t accepted;

    if (writer->status != RUNTIMEC_STATUS_SUCCESS || writer->length == 0U) {
        writer->length = 0U;
        return writer->status == RUNTIMEC_STATUS_SUCCESS;
    }
    chunk = runtimec_string_view_make(writer->chunk, writer->length);
    accepted = writer->sink(writer->context, chunk);
    writer->length = 0U;
    if (accepted.size != chunk.size) {
        writer->status = RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        return 0;
    }
    return 1;
}

/* 追加一段原样文本。 */
static void binrwir_format_write(binrwir_format_writer_t *writer,
                                 const char *text) {
    size_t size = strlen(text);

    if (writer->length + size > sizeof(writer->chunk)) {
        if (!binrwir_format_writer_flush(writer)) {
            return;
        }
    }
    if (size > sizeof(writer->chunk)) {
        /* 单段超过一行缓冲：直接交付，不复制。 */
        runtimec_string_view_t chunk = runtimec_string_view_make(text, size);

        if (writer->sink(writer->context, chunk).size != size) {
            writer->status = RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        return;
    }
    memcpy(writer->chunk + writer->length, text, size);
    writer->length += size;
}

static void binrwir_format_write_view(binrwir_format_writer_t *writer,
                                      runtimec_string_view_t view) {
    size_t offset = 0U;

    while (offset < view.size) {
        size_t room;
        size_t take;

        if (writer->length == sizeof(writer->chunk) &&
            !binrwir_format_writer_flush(writer)) {
            return;
        }
        room = sizeof(writer->chunk) - writer->length;
        take = view.size - offset < room ? view.size - offset : room;
        memcpy(writer->chunk + writer->length, view.data + offset, take);
        writer->length += take;
        offset += take;
    }
}

static void binrwir_format_write_unsigned(binrwir_format_writer_t *writer,
                                          uint64_t value) {
    char text[24];

    snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    binrwir_format_write(writer, text);
}

static void binrwir_format_write_hex(binrwir_format_writer_t *writer,
                                     uint32_t value) {
    char text[16];

    snprintf(text, sizeof(text), "%08x", value);
    binrwir_format_write(writer, text);
}

/* 两个十六进制字符一组，空格分隔：`01 00 00 00`。 */
static void binrwir_format_write_bytes(binrwir_format_writer_t *writer,
                                       runtimec_bytes_view_t bytes) {
    static const char digits[] = "0123456789abcdef";
    char pair[4];
    size_t offset;

    for (offset = 0U; offset < bytes.size; ++offset) {
        pair[0] = digits[(bytes.data[offset] >> 4) & 0x0FU];
        pair[1] = digits[bytes.data[offset] & 0x0FU];
        pair[2] = offset + 1U < bytes.size ? ' ' : '\0';
        pair[3] = '\0';
        binrwir_format_write(writer, pair);
    }
}

/* 一个操作数：`<argidx>:<reference>`。argidx 必须打印——它决定实参位置。 */
static void binrwir_format_write_argument(
    binrwir_format_writer_t *writer, const binrwir_argument_t *argument) {
    binrwir_format_write_unsigned(writer, argument->argument_index);
    binrwir_format_write(writer, ":");
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
        binrwir_format_write(writer, "argument");
        binrwir_format_write_unsigned(writer, argument->slot);
        return;
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        binrwir_format_write(writer, "result");
        binrwir_format_write_unsigned(writer, argument->slot);
        return;
    case BINRWIR_REFERENCE_LOCAL:
        binrwir_format_write(writer, "local[");
        binrwir_format_write_unsigned(writer, argument->slot);
        binrwir_format_write(writer, "]");
        return;
    case BINRWIR_REFERENCE_STATIC:
        /* 只打印符号下标：payload 是进程内 body 地址，打印它就不确定了。 */
        binrwir_format_write(writer, "static[");
        binrwir_format_write_unsigned(writer, argument->slot);
        binrwir_format_write(writer, "]");
        return;
    case BINRWIR_REFERENCE_FUNCTION:
        binrwir_format_write(writer, "function[");
        binrwir_format_write_unsigned(writer, argument->slot);
        binrwir_format_write(writer, "]");
        return;
    case BINRWIR_REFERENCE_LITERAL:
        binrwir_format_write(writer, "literal[");
        binrwir_format_write_unsigned(writer, argument->payload);
        binrwir_format_write(writer, "]");
        return;
    default:
        /* 非法 kind 不出现在合法 Program 里；打印数值，不猜含义。 */
        binrwir_format_write(writer, "invalid_kind=");
        binrwir_format_write_unsigned(writer, argument->reference_kind);
        return;
    }
}

static void binrwir_format_write_arguments(binrwir_format_writer_t *writer,
                                           binrwir_argument_span_t span) {
    size_t ordinal;

    binrwir_format_write(writer, "(");
    for (ordinal = 0U; ordinal < span.count; ++ordinal) {
        if (ordinal != 0U) {
            binrwir_format_write(writer, ", ");
        }
        binrwir_format_write_argument(writer, &span.data[ordinal]);
    }
    binrwir_format_write(writer, ")");
}

/* 算子名：回调给不出就打印数值，绝不猜。 */
static void binrwir_format_write_operation(
    binrwir_format_writer_t *writer, uint32_t operation_identifier,
    binrwir_operation_name_function resolve, void *resolve_context) {
    runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);
    int resolved = 0;

    if (operation_identifier == BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
        binrwir_format_write(writer, "foreign");
        return;
    }
    if (resolve != NULL) {
        resolved =
            resolve(operation_identifier, &name, resolve_context) ? 1 : 0;
    }
    if (resolved && runtimec_string_view_is_valid(name) && name.size != 0U) {
        binrwir_format_write_view(writer, name);
        return;
    }
    binrwir_format_write(writer, "opid=0x");
    binrwir_format_write_hex(writer, operation_identifier);
}

/* 定义区：函数自己的签名。被调用算子的签名不在这里重复。 */
static void binrwir_format_write_definition(
    binrwir_format_writer_t *writer, runtimec_string_view_t function_name,
    const binrwir_signature_view_t *signature) {
    size_t ordinal;

    binrwir_format_write(writer, "rwir ");
    if (runtimec_string_view_is_valid(function_name) &&
        function_name.size != 0U) {
        binrwir_format_write_view(writer, function_name);
    }
    binrwir_format_write(writer, "(");
    for (ordinal = 0U; ordinal < signature->read_parameter_count; ++ordinal) {
        if (ordinal != 0U) {
            binrwir_format_write(writer, ", ");
        }
        binrwir_format_write(writer, "argument");
        binrwir_format_write_unsigned(
            writer, signature->read_parameter_argument_indices[ordinal]);
        binrwir_format_write(writer, ": ");
        binrwir_format_write_view(writer,
                                  signature->read_parameter_types[ordinal]);
    }
    binrwir_format_write(writer, ") -> (");
    for (ordinal = 0U; ordinal < signature->write_parameter_count; ++ordinal) {
        if (ordinal != 0U) {
            binrwir_format_write(writer, ", ");
        }
        binrwir_format_write(writer, "result");
        binrwir_format_write_unsigned(
            writer, signature->write_parameter_argument_indices[ordinal]);
        binrwir_format_write(writer, ": ");
        binrwir_format_write_view(writer,
                                  signature->write_parameter_types[ordinal]);
    }
    binrwir_format_write(writer, ")\n\n");
}

static runtimec_status_t binrwir_format_write_instructions(
    binrwir_format_writer_t *writer, const binrwir_program_t *program,
    size_t instruction_count,
    binrwir_operation_name_function resolve_operation_name,
    void *resolve_context) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;

        if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return RUNTIMEC_STATUS_INVALID;
        }
        binrwir_format_write(writer, "[pc ");
        binrwir_format_write_unsigned(
            writer, instruction.source_logical_program_counter);
        binrwir_format_write(writer, "] ");
        binrwir_format_write_operation(
            writer, instruction.instruction->operation_identifier,
            resolve_operation_name, resolve_context);
        binrwir_format_write_arguments(writer, instruction.read_arguments);
        if (instruction.write_arguments.count != 0U) {
            binrwir_format_write(writer, " -> ");
            binrwir_format_write_arguments(writer, instruction.write_arguments);
        }
        binrwir_format_write(writer, "\n");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t binrwir_format_write_symbols(
    binrwir_format_writer_t *writer, const binrwir_program_t *program) {
    size_t symbol_count = 0U;
    size_t ordinal;

    if (binrwir_program_get_symbol_count(program, &symbol_count) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (symbol_count == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    binrwir_format_write(writer, "\n");
    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);

        if (binrwir_program_get_symbol(program, (uint16_t)ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return RUNTIMEC_STATUS_INVALID;
        }
        binrwir_format_write(writer, "symbol[");
        binrwir_format_write_unsigned(writer, ordinal);
        binrwir_format_write(writer, "] ");
        binrwir_format_write_view(writer, name);
        binrwir_format_write(writer, " : ");
        binrwir_format_write_view(writer, langtype);
        binrwir_format_write(writer, "\n");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t binrwir_format_write_literals(
    binrwir_format_writer_t *writer, const binrwir_program_t *program) {
    size_t literal_count = 0U;
    size_t ordinal;

    if (binrwir_program_get_literal_count(program, &literal_count) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        runtimec_bytes_view_t bytes = runtimec_bytes_view_make(NULL, 0U);
        runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);

        if (binrwir_program_get_literal(program, (uint32_t)ordinal, &bytes,
                                        &langtype) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return RUNTIMEC_STATUS_INVALID;
        }
        binrwir_format_write(writer, "literal[");
        binrwir_format_write_unsigned(writer, ordinal);
        binrwir_format_write(writer, "] ");
        binrwir_format_write_view(writer, langtype);
        binrwir_format_write(writer, " : ");
        binrwir_format_write_bytes(writer, bytes);
        binrwir_format_write(writer, "\n");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_format(
    const binrwir_program_t *program, runtimec_string_view_t function_name,
    binrwir_operation_name_function resolve_operation_name,
    void *resolve_context, binrwir_format_sink_function sink,
    void *sink_context) {
    binrwir_format_writer_t writer;
    binrwir_program_view_t view;
    runtimec_status_t status;

    if (program == NULL || sink == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (!runtimec_string_view_is_valid(function_name)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_program_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    binrwir_format_writer_initialize(&writer, sink, sink_context);
    binrwir_format_write_definition(&writer, function_name, &view.signature);
    status = binrwir_format_write_instructions(
        &writer, program, view.instruction_count, resolve_operation_name,
        resolve_context);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_format_write_symbols(&writer, program);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_format_write_literals(&writer, program);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!binrwir_format_writer_flush(&writer)) {
        return writer.status;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_string_view_t binrwir_format_buffer_sink(
    void *context, runtimec_string_view_t chunk) {
    binrwir_format_buffer_sink_context_t *sink_context =
        (binrwir_format_buffer_sink_context_t *)context;
    runtimec_bytes_buffer_t *buffer;
    uint8_t *grown;
    size_t grown_capacity;
    size_t required;

    if (sink_context == NULL || sink_context->buffer == NULL) {
        return runtimec_string_view_make(NULL, 0U);
    }
    buffer = sink_context->buffer;
    required = buffer->size + chunk.size;
    if (required > buffer->capacity || buffer->data == NULL) {
        grown_capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
        while (grown_capacity < required) {
            if (grown_capacity > SIZE_MAX / 2U) {
                return runtimec_string_view_make(NULL, 0U);
            }
            grown_capacity *= 2U;
        }
        grown = (uint8_t *)runtimec_allocator_allocate(
            &sink_context->allocator, grown_capacity, _Alignof(uint8_t));
        if (grown == NULL) {
            return runtimec_string_view_make(NULL, 0U);
        }
        if (buffer->data != NULL && buffer->size != 0U) {
            memcpy(grown, buffer->data, buffer->size);
        }
        buffer->data = grown;
        buffer->capacity = grown_capacity;
    }
    memcpy(buffer->data + buffer->size, chunk.data, chunk.size);
    buffer->size += chunk.size;
    return chunk;
}
