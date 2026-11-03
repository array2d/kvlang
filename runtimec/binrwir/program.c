/*
 * 组件：binrwir/program 的实现
 * 作用：增量构造不可变 BinRWIR Program，并提供只读查询。
 * 联系：vmkv 经 builder 登记签名、三个私有池与 instruction；BinRWIR Compiler 与
 *       vmbin 只读。
 * 约束：Program 自持全部存储，不指向 builder 或调用方内存；参数池只存引用与常量，
 *       不存帧实参地址，也不缓存 V.body。空数组的指针恒为 NULL、count 恒为零，
 *       没有第二种表示。符号池在 finalize 时排成名字字典序并同步改写参数槽，
 *       同一函数体因此总产出同一个 Program。
 */

#include "binrwir/program.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "binrwir/execution_exit.h"

#define BINRWIR_PROGRAM_INITIAL_CAPACITY 4U

/*
 * instruction 的私有 framing。二进制载荷只有 heads[ordinal] 的 opid 与参数池里的
 * 参数项；source logical program counter 与两个方向的边界留在这里，不进 instruction。
 * 读区起点是 read_argument_offset，写区紧接读区，因此只存一个偏移。
 */
typedef struct binrwir_program_instruction_record {
    uint32_t source_logical_program_counter;
    uint32_t read_argument_offset;
    uint32_t read_argument_count;
    uint32_t write_argument_count;
} binrwir_program_instruction_record_t;

/* 参数表：argidx 与具体 langtype 是两个平行数组，共用同一个 count 与 capacity。 */
typedef struct binrwir_program_parameter_arrays {
    uint32_t *argument_indices;
    runtimec_string_view_t *types;
    size_t count;
    size_t capacity;
} binrwir_program_parameter_arrays_t;

/* 指令表：二进制头与私有 framing 平行，共用同一个 count 与 capacity。 */
typedef struct binrwir_program_instruction_arrays {
    binrwir_instruction_t *heads;
    binrwir_program_instruction_record_t *records;
    size_t count;
    size_t capacity;
} binrwir_program_instruction_arrays_t;

/* 字面量池项：已编码的值字节与它的具体 langtype，两者都指向 Program 自持存储。 */
typedef struct binrwir_program_literal_record {
    runtimec_bytes_view_t wire_bytes;
    runtimec_string_view_t langtype;
} binrwir_program_literal_record_t;

struct binrwir_program {
    runtimec_allocator_t allocator;
    binrwir_program_identity_t identity;
    binrwir_program_instruction_arrays_t instructions;
    binrwir_argument_t *arguments;
    size_t argument_count;
    binrwir_program_parameter_arrays_t read_parameters;
    binrwir_program_parameter_arrays_t write_parameters;
    runtimec_string_view_t *symbols;
    size_t symbol_count;
    binrwir_program_literal_record_t *literals;
    size_t literal_count;
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    char *pool_text;
    size_t pool_text_size;
    uint8_t *pool_bytes;
    size_t pool_bytes_size;
};

struct binrwir_program_builder {
    runtimec_allocator_t allocator;
    binrwir_program_identity_t identity;
    bool finalized;
    binrwir_program_instruction_arrays_t instructions;
    binrwir_argument_t *arguments;
    size_t argument_count;
    size_t argument_capacity;
    binrwir_program_parameter_arrays_t read_parameters;
    binrwir_program_parameter_arrays_t write_parameters;
    runtimec_string_view_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    binrwir_program_literal_record_t *literals;
    size_t literal_count;
    size_t literal_capacity;
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    size_t alias_rule_capacity;
};

static void binrwir_program_identity_invalidate(
    binrwir_program_identity_t *identity) {
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
        identity->schema_version = UINT32_MAX;
        identity->rwir_manifest_version = UINT32_MAX;
        identity->binrwir_binary_interface_generation = UINT32_MAX;
        identity->canonical_encoding_revision = UINT32_MAX;
    }
}

/* identity 是可消费的当且仅当版本字段都是显式值，且二进制接口代次就是当前代次。 */
static int binrwir_program_identity_is_usable(
    const binrwir_program_identity_t *identity) {
    return identity->schema_version != UINT32_MAX &&
           identity->rwir_manifest_version != UINT32_MAX &&
           identity->binrwir_binary_interface_generation ==
               BINRWIR_BINARY_INTERFACE_GENERATION &&
           identity->canonical_encoding_revision != UINT32_MAX;
}

/* 规范形状：count 为零时指针必须是 NULL，count 非零时指针必须有值。 */
static int binrwir_program_array_shape_is_invalid(const void *array,
                                                  size_t count) {
    if (count == 0U) {
        return array != NULL;
    }
    return array == NULL;
}

static size_t binrwir_program_grown_capacity(size_t capacity,
                                             size_t required_count) {
    size_t grown_capacity =
        capacity == 0U ? BINRWIR_PROGRAM_INITIAL_CAPACITY : capacity;

    while (grown_capacity < required_count) {
        if (grown_capacity > SIZE_MAX / 2U) {
            return 0U;
        }
        grown_capacity *= 2U;
    }
    return grown_capacity;
}

/* 换一块更大数组并保留前 capacity 个元素；失败返回 NULL 且不动旧数组。 */
static void *binrwir_program_regrow(const runtimec_allocator_t *allocator,
                                    const void *array, size_t capacity,
                                    size_t grown_capacity, size_t element_size,
                                    size_t element_alignment) {
    void *grown;

    if (grown_capacity > SIZE_MAX / element_size) {
        return NULL;
    }
    grown = runtimec_allocator_allocate(
        allocator, element_size * grown_capacity, element_alignment);
    if (grown == NULL) {
        return NULL;
    }
    if (array != NULL) {
        memcpy(grown, array, element_size * capacity);
        (void)runtimec_allocator_deallocate(allocator, (void *)array,
                                            element_size * capacity,
                                            element_alignment);
    }
    return grown;
}

/* 精确尺寸分配；count 为零时返回 NULL（调用方把它当「该池为空」）。 */
static void *binrwir_program_allocate_exact(
    const runtimec_allocator_t *allocator, size_t count, size_t element_size,
    size_t element_alignment) {
    if (count == 0U || count > SIZE_MAX / element_size) {
        return NULL;
    }
    return runtimec_allocator_allocate(allocator, element_size * count,
                                       element_alignment);
}

static void binrwir_program_release_array(const runtimec_allocator_t *allocator,
                                          void *array, size_t count,
                                          size_t element_size,
                                          size_t element_alignment) {
    if (array == NULL) {
        return;
    }
    (void)runtimec_allocator_deallocate(allocator, array,
                                        element_size * count,
                                        element_alignment);
}

/* 名字的字节字典序：先比公共前缀，再短者在前。 */
static int binrwir_program_symbol_compare(runtimec_string_view_t left,
                                          runtimec_string_view_t right) {
    size_t common = left.size < right.size ? left.size : right.size;
    int order = memcmp(left.data, right.data, common);

    if (order != 0) {
        return order;
    }
    if (left.size < right.size) {
        return -1;
    }
    if (left.size > right.size) {
        return 1;
    }
    return 0;
}

/* 把符号池排成规范顺序：n 很小，插入排序让「稳定 + 总序」一眼可读。 */
static void binrwir_program_symbol_order(
    const runtimec_string_view_t *symbols, size_t count, size_t *output_order) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < count; ++ordinal) {
        size_t candidate = ordinal;

        output_order[ordinal] = ordinal;
        while (candidate > 0U &&
               binrwir_program_symbol_compare(
                   symbols[output_order[candidate - 1U]],
                   symbols[output_order[candidate]]) > 0) {
            size_t moved = output_order[candidate - 1U];

            output_order[candidate - 1U] = output_order[candidate];
            output_order[candidate] = moved;
            --candidate;
        }
    }
}

static int binrwir_program_size_add(size_t *total, size_t size) {
    if (*total > SIZE_MAX - size) {
        return 0;
    }
    *total += size;
    return 1;
}

/* 文本池总长：参数类型 + 符号名 + 字面量类型文本，四组共用一个文本块。 */
static int binrwir_program_text_size(
    const binrwir_program_parameter_arrays_t *read_parameters,
    const binrwir_program_parameter_arrays_t *write_parameters,
    const runtimec_string_view_t *symbols, size_t symbol_count,
    const binrwir_program_literal_record_t *literals, size_t literal_count,
    size_t *output_size) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < read_parameters->count; ++ordinal) {
        if (!binrwir_program_size_add(
                output_size, read_parameters->types[ordinal].size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < write_parameters->count; ++ordinal) {
        if (!binrwir_program_size_add(
                output_size, write_parameters->types[ordinal].size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        if (!binrwir_program_size_add(output_size, symbols[ordinal].size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        if (!binrwir_program_size_add(output_size,
                                      literals[ordinal].langtype.size)) {
            return 0;
        }
    }
    return 1;
}

static int binrwir_program_bytes_size(
    const binrwir_program_literal_record_t *literals, size_t literal_count,
    size_t *output_size) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        if (!binrwir_program_size_add(output_size,
                                      literals[ordinal].wire_bytes.size)) {
            return 0;
        }
    }
    return 1;
}

/* 深拷贝一段文本进 Program 文本池，返回指向副本的视图。 */
static runtimec_string_view_t binrwir_program_copy_text(
    binrwir_program_t *program, runtimec_string_view_t text,
    size_t *text_offset) {
    char *copy = program->pool_text + *text_offset;

    if (text.size != 0U) {
        memcpy(copy, text.data, text.size);
    }
    *text_offset += text.size;
    return runtimec_string_view_make(copy, text.size);
}

/* 深拷贝一段字节进 Program 字节池，返回指向副本的视图。 */
static runtimec_bytes_view_t binrwir_program_copy_bytes(
    binrwir_program_t *program, runtimec_bytes_view_t bytes,
    size_t *bytes_offset) {
    uint8_t *copy = program->pool_bytes + *bytes_offset;

    if (bytes.size != 0U) {
        memcpy(copy, bytes.data, bytes.size);
    }
    *bytes_offset += bytes.size;
    return runtimec_bytes_view_make(copy, bytes.size);
}

/*
 * LOCAL / FUNCTION 的 slot 与 LITERAL 的 payload 都是 Program 私有池下标，
 * PARAMETER_READ / PARAMETER_WRITE 的 slot 必须指回签名里同一个 argidx。任何一条
 * 对不上，同一个引用就有了第二处真相。
 */
static int binrwir_program_argument_is_unbound(
    const binrwir_program_t *program, const binrwir_argument_t *argument) {
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
        return argument->slot >= program->read_parameters.count ||
               program->read_parameters.argument_indices[argument->slot] !=
                   argument->argument_index;
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        return argument->slot >= program->write_parameters.count ||
               program->write_parameters.argument_indices[argument->slot] !=
                   argument->argument_index;
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_FUNCTION:
    case BINRWIR_REFERENCE_STATIC:
        /* 三个 kind 的 slot 都是符号池下标：越界会被当成合法并越界取文本。 */
        return (size_t)argument->slot >= program->symbol_count;
    case BINRWIR_REFERENCE_LITERAL:
        return argument->payload >= (uint64_t)program->literal_count;
    default:
        return 0;
    }
}

static int binrwir_program_span_is_invalid(
    const binrwir_program_t *program, const binrwir_argument_t *arguments,
    size_t count) {
    size_t argument_ordinal;

    for (argument_ordinal = 0U; argument_ordinal < count; ++argument_ordinal) {
        const binrwir_argument_t *argument = &arguments[argument_ordinal];

        if (binrwir_argument_validate(argument) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_program_argument_is_unbound(program, argument)) {
            return 1;
        }
    }
    return 0;
}

static int binrwir_program_instructions_are_invalid(
    const binrwir_program_t *program) {
    size_t instruction_ordinal;

    for (instruction_ordinal = 0U;
         instruction_ordinal < program->instructions.count;
         ++instruction_ordinal) {
        const binrwir_program_instruction_record_t *record =
            &program->instructions.records[instruction_ordinal];
        size_t read_offset = (size_t)record->read_argument_offset;
        size_t read_count = (size_t)record->read_argument_count;
        size_t write_count = (size_t)record->write_argument_count;
        size_t span_count = read_count + write_count;

        if (program->instructions.heads[instruction_ordinal]
                    .operation_identifier ==
                BINRWIR_OPERATION_IDENTIFIER_NONE ||
            record->source_logical_program_counter ==
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE ||
            span_count > program->argument_count ||
            read_offset > program->argument_count - span_count) {
            return 1;
        }
        if (binrwir_program_span_is_invalid(
                program, program->arguments + read_offset, read_count) ||
            binrwir_program_span_is_invalid(
                program, program->arguments + read_offset + read_count,
                write_count)) {
            return 1;
        }
    }
    return 0;
}

/* 三个私有池的形状：空池即空指针；符号名非空且严格字典升序（规范顺序）；
 * 字面量必须带字节且类型是具体 langtype。 */
static int binrwir_program_pools_are_invalid(
    const binrwir_program_t *program) {
    size_t ordinal;

    if (program->symbol_count > (size_t)UINT16_MAX ||
        binrwir_program_array_shape_is_invalid(program->symbols,
                                               program->symbol_count) ||
        binrwir_program_array_shape_is_invalid(program->literals,
                                               program->literal_count) ||
        binrwir_program_array_shape_is_invalid(program->pool_text,
                                               program->pool_text_size) ||
        binrwir_program_array_shape_is_invalid(program->pool_bytes,
                                               program->pool_bytes_size) ||
        program->literal_count > UINT32_MAX) {
        return 1;
    }
    for (ordinal = 0U; ordinal < program->symbol_count; ++ordinal) {
        if (program->symbols[ordinal].size == 0U) {
            return 1;
        }
        if (ordinal != 0U &&
            binrwir_program_symbol_compare(program->symbols[ordinal - 1U],
                                           program->symbols[ordinal]) >= 0) {
            return 1;
        }
    }
    for (ordinal = 0U; ordinal < program->literal_count; ++ordinal) {
        const binrwir_program_literal_record_t *literal =
            &program->literals[ordinal];

        if (literal->wire_bytes.size == 0U ||
            binrwir_signature_validate_parameter_type(literal->langtype) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 1;
        }
    }
    return 0;
}

static binrwir_signature_view_t binrwir_program_signature_view(
    const binrwir_program_t *program) {
    binrwir_signature_view_t signature = {
        .read_parameter_argument_indices =
            program->read_parameters.argument_indices,
        .read_parameter_count = program->read_parameters.count,
        .write_parameter_argument_indices =
            program->write_parameters.argument_indices,
        .write_parameter_count = program->write_parameters.count,
        .read_parameter_types = program->read_parameters.types,
        .write_parameter_types = program->write_parameters.types,
        .alias_rules = program->alias_rules,
        .alias_rule_count = program->alias_rule_count,
    };

    return signature;
}

runtimec_status_t binrwir_program_get_identity(
    const binrwir_program_t *program,
    binrwir_program_identity_t *output_identity) {
    binrwir_program_identity_invalidate(output_identity);
    if (program == NULL || output_identity == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_identity = program->identity;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_validate(const binrwir_program_t *program) {
    binrwir_signature_view_t signature;
    size_t text_size = 0U;
    size_t bytes_size = 0U;

    if (program == NULL || !runtimec_allocator_is_valid(&program->allocator) ||
        !binrwir_program_identity_is_usable(&program->identity) ||
        program->instructions.count == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    signature = binrwir_program_signature_view(program);
    if (signature.read_parameter_count > UINT32_MAX ||
        signature.write_parameter_count > UINT32_MAX ||
        program->instructions.count > UINT32_MAX ||
        program->argument_count > UINT32_MAX ||
        signature.alias_rule_count > UINT32_MAX ||
        binrwir_program_array_shape_is_invalid(
            program->instructions.heads, program->instructions.count) ||
        binrwir_program_array_shape_is_invalid(
            program->instructions.records, program->instructions.count) ||
        binrwir_program_array_shape_is_invalid(program->arguments,
                                               program->argument_count) ||
        binrwir_program_array_shape_is_invalid(signature.alias_rules,
                                               signature.alias_rule_count) ||
        binrwir_program_pools_are_invalid(program) ||
        binrwir_signature_validate(&signature) != RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 文本池恰好装下全部类型串与符号名、字节池恰好装下全部字面量：多一个字节或
     * 少一个字节都是被改坏的 Program。 */
    if (!binrwir_program_text_size(&program->read_parameters,
                                   &program->write_parameters,
                                   program->symbols, program->symbol_count,
                                   program->literals, program->literal_count,
                                   &text_size) ||
        !binrwir_program_bytes_size(program->literals, program->literal_count,
                                    &bytes_size) ||
        text_size != program->pool_text_size ||
        bytes_size != program->pool_bytes_size) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_program_instructions_are_invalid(program)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_get_view(
    const binrwir_program_t *program, binrwir_program_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        binrwir_program_identity_invalidate(&output_view->identity);
    }
    if (program == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_view->identity = program->identity;
    output_view->signature = binrwir_program_signature_view(program);
    output_view->instruction_count = program->instructions.count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_get_instruction(
    const binrwir_program_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction) {
    const binrwir_program_instruction_record_t *record;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;

    if (output_instruction != NULL) {
        memset(output_instruction, 0, sizeof(*output_instruction));
    }
    if (program == NULL || output_instruction == NULL ||
        instruction_identifier == BINRWIR_IDENTIFIER_NONE ||
        (size_t)instruction_identifier >= program->instructions.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    record = &program->instructions.records[instruction_identifier];
    read_arguments.data = program->arguments + record->read_argument_offset;
    read_arguments.count = (size_t)record->read_argument_count;
    write_arguments.data = read_arguments.data + record->read_argument_count;
    write_arguments.count = (size_t)record->write_argument_count;
    return binrwir_instruction_get_view(
        &program->instructions.heads[instruction_identifier],
        record->source_logical_program_counter, read_arguments,
        write_arguments, output_instruction);
}

runtimec_status_t binrwir_program_find_instruction(
    const binrwir_program_t *program, uint32_t source_logical_program_counter,
    uint32_t *output_instruction_identifier) {
    size_t instruction_ordinal;

    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (program == NULL || output_instruction_identifier == NULL ||
        source_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (instruction_ordinal = 0U;
         instruction_ordinal < program->instructions.count;
         ++instruction_ordinal) {
        if (program->instructions.records[instruction_ordinal]
                .source_logical_program_counter ==
            source_logical_program_counter) {
            *output_instruction_identifier = (uint32_t)instruction_ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_INVALID;
}

runtimec_status_t binrwir_program_get_symbol(
    const binrwir_program_t *program, uint16_t symbol_index,
    runtimec_string_view_t *output_name) {
    if (output_name != NULL) {
        *output_name = runtimec_string_view_make(NULL, 0U);
    }
    if (program == NULL || output_name == NULL ||
        (size_t)symbol_index >= program->symbol_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_name = program->symbols[symbol_index];
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_get_literal(
    const binrwir_program_t *program, uint32_t literal_index,
    runtimec_bytes_view_t *output_wire_bytes,
    runtimec_string_view_t *output_langtype) {
    if (output_wire_bytes != NULL) {
        *output_wire_bytes = runtimec_bytes_view_make(NULL, 0U);
    }
    if (output_langtype != NULL) {
        *output_langtype = runtimec_string_view_make(NULL, 0U);
    }
    if (program == NULL || output_wire_bytes == NULL ||
        output_langtype == NULL ||
        (size_t)literal_index >= program->literal_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_wire_bytes = program->literals[literal_index].wire_bytes;
    *output_langtype = program->literals[literal_index].langtype;
    return RUNTIMEC_STATUS_SUCCESS;
}

static void binrwir_program_release(binrwir_program_t *program) {
    const runtimec_allocator_t *allocator = &program->allocator;

    binrwir_program_release_array(allocator, program->instructions.heads,
                                  program->instructions.capacity,
                                  sizeof(binrwir_instruction_t),
                                  _Alignof(binrwir_instruction_t));
    binrwir_program_release_array(
        allocator, program->instructions.records,
        program->instructions.capacity,
        sizeof(binrwir_program_instruction_record_t),
        _Alignof(binrwir_program_instruction_record_t));
    binrwir_program_release_array(allocator, program->arguments,
                                  program->argument_count,
                                  sizeof(binrwir_argument_t),
                                  _Alignof(binrwir_argument_t));
    binrwir_program_release_array(allocator,
                                  program->read_parameters.argument_indices,
                                  program->read_parameters.capacity,
                                  sizeof(uint32_t), _Alignof(uint32_t));
    binrwir_program_release_array(allocator, program->read_parameters.types,
                                  program->read_parameters.capacity,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(allocator,
                                  program->write_parameters.argument_indices,
                                  program->write_parameters.capacity,
                                  sizeof(uint32_t), _Alignof(uint32_t));
    binrwir_program_release_array(allocator, program->write_parameters.types,
                                  program->write_parameters.capacity,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(allocator, program->symbols,
                                  program->symbol_count,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(allocator, program->literals,
                                  program->literal_count,
                                  sizeof(binrwir_program_literal_record_t),
                                  _Alignof(binrwir_program_literal_record_t));
    binrwir_program_release_array(allocator, program->alias_rules,
                                  program->alias_rule_count,
                                  sizeof(binrwir_alias_rule_t),
                                  _Alignof(binrwir_alias_rule_t));
    binrwir_program_release_array(allocator, program->pool_text,
                                  program->pool_text_size, sizeof(char),
                                  _Alignof(char));
    binrwir_program_release_array(allocator, program->pool_bytes,
                                  program->pool_bytes_size, sizeof(uint8_t),
                                  _Alignof(uint8_t));
    memset(program, 0, sizeof(*program));
}

runtimec_status_t binrwir_program_destroy(binrwir_program_t *program) {
    runtimec_allocator_t allocator;

    if (program == NULL ||
        !runtimec_allocator_is_valid(&program->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = program->allocator;
    binrwir_program_release(program);
    (void)runtimec_allocator_deallocate(&allocator, program, sizeof(*program),
                                        _Alignof(binrwir_program_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 按精确尺寸一次分配 Program 需要的全部数组；空池留 NULL。 */
static runtimec_status_t binrwir_program_allocate_storage(
    binrwir_program_t *program, const binrwir_program_builder_t *builder) {
    const runtimec_allocator_t *allocator = &program->allocator;
    size_t text_size = 0U;
    size_t bytes_size = 0U;

    if (!binrwir_program_text_size(&builder->read_parameters,
                                   &builder->write_parameters,
                                   builder->symbols, builder->symbol_count,
                                   builder->literals, builder->literal_count,
                                   &text_size) ||
        !binrwir_program_bytes_size(builder->literals, builder->literal_count,
                                    &bytes_size)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->instructions.count = builder->instructions.count;
    program->instructions.capacity = builder->instructions.count;
    program->argument_count = builder->argument_count;
    program->read_parameters.count = builder->read_parameters.count;
    program->read_parameters.capacity = builder->read_parameters.count;
    program->write_parameters.count = builder->write_parameters.count;
    program->write_parameters.capacity = builder->write_parameters.count;
    program->symbol_count = builder->symbol_count;
    program->literal_count = builder->literal_count;
    program->alias_rule_count = builder->alias_rule_count;
    program->pool_text_size = text_size;
    program->pool_bytes_size = bytes_size;
    program->instructions.heads = (binrwir_instruction_t *)
        binrwir_program_allocate_exact(allocator, program->instructions.count,
                                       sizeof(binrwir_instruction_t),
                                       _Alignof(binrwir_instruction_t));
    if (program->instructions.count != 0U &&
        program->instructions.heads == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->instructions.records = (binrwir_program_instruction_record_t *)
        binrwir_program_allocate_exact(
            allocator, program->instructions.count,
            sizeof(binrwir_program_instruction_record_t),
            _Alignof(binrwir_program_instruction_record_t));
    if (program->instructions.count != 0U &&
        program->instructions.records == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->arguments = (binrwir_argument_t *)binrwir_program_allocate_exact(
        allocator, program->argument_count, sizeof(binrwir_argument_t),
        _Alignof(binrwir_argument_t));
    if (program->argument_count != 0U && program->arguments == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->read_parameters.argument_indices = (uint32_t *)
        binrwir_program_allocate_exact(allocator,
                                       program->read_parameters.count,
                                       sizeof(uint32_t), _Alignof(uint32_t));
    program->write_parameters.argument_indices = (uint32_t *)
        binrwir_program_allocate_exact(allocator,
                                       program->write_parameters.count,
                                       sizeof(uint32_t), _Alignof(uint32_t));
    program->read_parameters.types = (runtimec_string_view_t *)
        binrwir_program_allocate_exact(allocator,
                                       program->read_parameters.count,
                                       sizeof(runtimec_string_view_t),
                                       _Alignof(runtimec_string_view_t));
    program->write_parameters.types = (runtimec_string_view_t *)
        binrwir_program_allocate_exact(allocator,
                                       program->write_parameters.count,
                                       sizeof(runtimec_string_view_t),
                                       _Alignof(runtimec_string_view_t));
    if ((program->read_parameters.count != 0U &&
         (program->read_parameters.argument_indices == NULL ||
          program->read_parameters.types == NULL)) ||
        (program->write_parameters.count != 0U &&
         (program->write_parameters.argument_indices == NULL ||
          program->write_parameters.types == NULL))) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->symbols = (runtimec_string_view_t *)binrwir_program_allocate_exact(
        allocator, program->symbol_count, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    if (program->symbol_count != 0U && program->symbols == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->literals = (binrwir_program_literal_record_t *)
        binrwir_program_allocate_exact(allocator, program->literal_count,
                                       sizeof(binrwir_program_literal_record_t),
                                       _Alignof(
                                           binrwir_program_literal_record_t));
    if (program->literal_count != 0U && program->literals == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->alias_rules = (binrwir_alias_rule_t *)
        binrwir_program_allocate_exact(allocator, program->alias_rule_count,
                                       sizeof(binrwir_alias_rule_t),
                                       _Alignof(binrwir_alias_rule_t));
    if (program->alias_rule_count != 0U && program->alias_rules == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->pool_text = (char *)binrwir_program_allocate_exact(
        allocator, program->pool_text_size, sizeof(char), _Alignof(char));
    program->pool_bytes = (uint8_t *)binrwir_program_allocate_exact(
        allocator, program->pool_bytes_size, sizeof(uint8_t),
        _Alignof(uint8_t));
    if ((program->pool_text_size != 0U && program->pool_text == NULL) ||
        (program->pool_bytes_size != 0U && program->pool_bytes == NULL)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 三个池的深拷贝与规范化。符号池在这里排成字典序，同时算出「builder 序号 →
 * Program 序号」的重映射，并把参数池里 LOCAL / FUNCTION 的 slot 改写成新下标。
 */
static void binrwir_program_copy_pools(binrwir_program_t *program,
                                       const binrwir_program_builder_t *builder,
                                       size_t *symbol_order,
                                       size_t *symbol_remap) {
    size_t text_offset = 0U;
    size_t bytes_offset = 0U;
    size_t ordinal;

    for (ordinal = 0U; ordinal < builder->read_parameters.count; ++ordinal) {
        program->read_parameters.types[ordinal] = binrwir_program_copy_text(
            program, builder->read_parameters.types[ordinal], &text_offset);
    }
    for (ordinal = 0U; ordinal < builder->write_parameters.count; ++ordinal) {
        program->write_parameters.types[ordinal] = binrwir_program_copy_text(
            program, builder->write_parameters.types[ordinal], &text_offset);
    }
    binrwir_program_symbol_order(builder->symbols, builder->symbol_count,
                                 symbol_order);
    for (ordinal = 0U; ordinal < builder->symbol_count; ++ordinal) {
        size_t source_index = symbol_order[ordinal];

        program->symbols[ordinal] = binrwir_program_copy_text(
            program, builder->symbols[source_index], &text_offset);
        symbol_remap[source_index] = ordinal;
    }
    for (ordinal = 0U; ordinal < builder->literal_count; ++ordinal) {
        program->literals[ordinal].langtype = binrwir_program_copy_text(
            program, builder->literals[ordinal].langtype, &text_offset);
    }
    for (ordinal = 0U; ordinal < builder->literal_count; ++ordinal) {
        program->literals[ordinal].wire_bytes = binrwir_program_copy_bytes(
            program, builder->literals[ordinal].wire_bytes, &bytes_offset);
    }
    for (ordinal = 0U; ordinal < program->argument_count; ++ordinal) {
        binrwir_argument_t *argument = &program->arguments[ordinal];

        /* STATIC 的 slot 也是符号池下标（key 文本），和 LOCAL / FUNCTION 一起
         * 重映射；越界的 slot 不猜、也不查表，留给 validate 判 INVALID。 */
        if ((argument->reference_kind == BINRWIR_REFERENCE_LOCAL ||
             argument->reference_kind == BINRWIR_REFERENCE_FUNCTION ||
             argument->reference_kind == BINRWIR_REFERENCE_STATIC) &&
            (size_t)argument->slot < program->symbol_count) {
            argument->slot = (uint16_t)symbol_remap[argument->slot];
        }
    }
}

static int binrwir_program_builder_is_writable(
    const binrwir_program_builder_t *builder) {
    return builder != NULL && !builder->finalized &&
           runtimec_allocator_is_valid(&builder->allocator);
}

/* 参数表扩容：argidx 与类型两个平行数组一起长，容量只有一个。 */
static int binrwir_program_builder_reserve_parameters(
    binrwir_program_builder_t *builder,
    binrwir_program_parameter_arrays_t *arrays, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (arrays->capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(arrays->capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(&builder->allocator,
                                   arrays->argument_indices, arrays->capacity,
                                   grown_capacity, sizeof(uint32_t),
                                   _Alignof(uint32_t));
    if (grown == NULL) {
        return 0;
    }
    arrays->argument_indices = (uint32_t *)grown;
    grown = binrwir_program_regrow(&builder->allocator, arrays->types,
                                   arrays->capacity, grown_capacity,
                                   sizeof(runtimec_string_view_t),
                                   _Alignof(runtimec_string_view_t));
    if (grown == NULL) {
        return 0;
    }
    arrays->types = (runtimec_string_view_t *)grown;
    arrays->capacity = grown_capacity;
    return 1;
}

static int binrwir_program_builder_reserve_instructions(
    binrwir_program_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->instructions.capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(
        builder->instructions.capacity, required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(
        &builder->allocator, builder->instructions.heads,
        builder->instructions.capacity, grown_capacity,
        sizeof(binrwir_instruction_t), _Alignof(binrwir_instruction_t));
    if (grown == NULL) {
        return 0;
    }
    builder->instructions.heads = (binrwir_instruction_t *)grown;
    grown = binrwir_program_regrow(
        &builder->allocator, builder->instructions.records,
        builder->instructions.capacity, grown_capacity,
        sizeof(binrwir_program_instruction_record_t),
        _Alignof(binrwir_program_instruction_record_t));
    if (grown == NULL) {
        return 0;
    }
    builder->instructions.records =
        (binrwir_program_instruction_record_t *)grown;
    builder->instructions.capacity = grown_capacity;
    return 1;
}

static int binrwir_program_builder_reserve_arguments(
    binrwir_program_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->argument_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(builder->argument_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(
        &builder->allocator, builder->arguments, builder->argument_capacity,
        grown_capacity, sizeof(binrwir_argument_t),
        _Alignof(binrwir_argument_t));
    if (grown == NULL) {
        return 0;
    }
    builder->arguments = (binrwir_argument_t *)grown;
    builder->argument_capacity = grown_capacity;
    return 1;
}

static int binrwir_program_builder_reserve_alias_rules(
    binrwir_program_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->alias_rule_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(
        builder->alias_rule_capacity, required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(
        &builder->allocator, builder->alias_rules,
        builder->alias_rule_capacity, grown_capacity,
        sizeof(binrwir_alias_rule_t), _Alignof(binrwir_alias_rule_t));
    if (grown == NULL) {
        return 0;
    }
    builder->alias_rules = (binrwir_alias_rule_t *)grown;
    builder->alias_rule_capacity = grown_capacity;
    return 1;
}

static int binrwir_program_builder_reserve_symbols(
    binrwir_program_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->symbol_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(builder->symbol_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(
        &builder->allocator, builder->symbols, builder->symbol_capacity,
        grown_capacity, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    if (grown == NULL) {
        return 0;
    }
    builder->symbols = (runtimec_string_view_t *)grown;
    builder->symbol_capacity = grown_capacity;
    return 1;
}

static int binrwir_program_builder_reserve_literals(
    binrwir_program_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->literal_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_program_grown_capacity(builder->literal_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_program_regrow(
        &builder->allocator, builder->literals, builder->literal_capacity,
        grown_capacity, sizeof(binrwir_program_literal_record_t),
        _Alignof(binrwir_program_literal_record_t));
    if (grown == NULL) {
        return 0;
    }
    builder->literals = (binrwir_program_literal_record_t *)grown;
    builder->literal_capacity = grown_capacity;
    return 1;
}

runtimec_status_t binrwir_program_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_program_identity_t *identity,
    binrwir_program_builder_t **output_builder) {
    binrwir_program_builder_t *builder;

    if (output_builder != NULL) {
        *output_builder = NULL;
    }
    if (output_builder == NULL || identity == NULL ||
        !runtimec_allocator_is_valid(allocator) ||
        !binrwir_program_identity_is_usable(identity)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    builder = (binrwir_program_builder_t *)runtimec_allocator_allocate(
        allocator, sizeof(*builder), _Alignof(binrwir_program_builder_t));
    if (builder == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(builder, 0, sizeof(*builder));
    builder->allocator = *allocator;
    builder->identity = *identity;
    *output_builder = builder;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 回到空的可写状态并保留已分配容量；也是定稿之后唯一的复用入口。 */
runtimec_status_t binrwir_program_builder_reset(
    binrwir_program_builder_t *builder) {
    if (builder == NULL ||
        !runtimec_allocator_is_valid(&builder->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    builder->finalized = false;
    builder->instructions.count = 0U;
    builder->argument_count = 0U;
    builder->read_parameters.count = 0U;
    builder->write_parameters.count = 0U;
    builder->symbol_count = 0U;
    builder->literal_count = 0U;
    builder->alias_rule_count = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_destroy(
    binrwir_program_builder_t *builder) {
    runtimec_allocator_t allocator;

    if (builder == NULL ||
        !runtimec_allocator_is_valid(&builder->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = builder->allocator;
    binrwir_program_release_array(
        &allocator, builder->instructions.heads,
        builder->instructions.capacity, sizeof(binrwir_instruction_t),
        _Alignof(binrwir_instruction_t));
    binrwir_program_release_array(
        &allocator, builder->instructions.records,
        builder->instructions.capacity,
        sizeof(binrwir_program_instruction_record_t),
        _Alignof(binrwir_program_instruction_record_t));
    binrwir_program_release_array(&allocator, builder->arguments,
                                  builder->argument_capacity,
                                  sizeof(binrwir_argument_t),
                                  _Alignof(binrwir_argument_t));
    binrwir_program_release_array(
        &allocator, builder->read_parameters.argument_indices,
        builder->read_parameters.capacity, sizeof(uint32_t),
        _Alignof(uint32_t));
    binrwir_program_release_array(
        &allocator, builder->read_parameters.types,
        builder->read_parameters.capacity, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(
        &allocator, builder->write_parameters.argument_indices,
        builder->write_parameters.capacity, sizeof(uint32_t),
        _Alignof(uint32_t));
    binrwir_program_release_array(
        &allocator, builder->write_parameters.types,
        builder->write_parameters.capacity, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(&allocator, builder->symbols,
                                  builder->symbol_capacity,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_program_release_array(&allocator, builder->literals,
                                  builder->literal_capacity,
                                  sizeof(binrwir_program_literal_record_t),
                                  _Alignof(binrwir_program_literal_record_t));
    binrwir_program_release_array(&allocator, builder->alias_rules,
                                  builder->alias_rule_capacity,
                                  sizeof(binrwir_alias_rule_t),
                                  _Alignof(binrwir_alias_rule_t));
    (void)runtimec_allocator_deallocate(&allocator, builder,
                                        sizeof(*builder),
                                        _Alignof(binrwir_program_builder_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_add_parameter(
    binrwir_program_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t argument_index, runtimec_string_view_t parameter_type,
    uint32_t *output_parameter_index) {
    binrwir_program_parameter_arrays_t *arrays = NULL;

    if (output_parameter_index != NULL) {
        *output_parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
    }
    if (!binrwir_program_builder_is_writable(builder) ||
        argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        output_parameter_index == NULL ||
        binrwir_signature_validate_parameter_type(parameter_type) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (direction == BINRWIR_PARAMETER_DIRECTION_READ) {
        arrays = &builder->read_parameters;
    } else if (direction == BINRWIR_PARAMETER_DIRECTION_WRITE) {
        arrays = &builder->write_parameters;
    }
    if (arrays == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (arrays->count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_program_builder_reserve_parameters(builder, arrays,
                                                    arrays->count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    arrays->argument_indices[arrays->count] = argument_index;
    arrays->types[arrays->count] = parameter_type;
    *output_parameter_index = (uint32_t)arrays->count;
    arrays->count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_add_alias_rule(
    binrwir_program_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier) {
    if (output_alias_rule_identifier != NULL) {
        *output_alias_rule_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (!binrwir_program_builder_is_writable(builder) || rule == NULL ||
        output_alias_rule_identifier == NULL ||
        binrwir_signature_validate_alias_rule(rule) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (builder->alias_rule_count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_program_builder_reserve_alias_rules(
            builder, builder->alias_rule_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->alias_rules[builder->alias_rule_count] = *rule;
    *output_alias_rule_identifier = (uint32_t)builder->alias_rule_count;
    builder->alias_rule_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_add_symbol(
    binrwir_program_builder_t *builder, runtimec_string_view_t name,
    uint16_t *output_symbol_index) {
    size_t ordinal;

    if (output_symbol_index != NULL) {
        *output_symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    }
    if (!binrwir_program_builder_is_writable(builder) ||
        output_symbol_index == NULL || name.size == 0U || name.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 同名重复登记是幂等的：一个名字对应一个符号，这也挡住池内重名。 */
    for (ordinal = 0U; ordinal < builder->symbol_count; ++ordinal) {
        if (builder->symbols[ordinal].size == name.size &&
            memcmp(builder->symbols[ordinal].data, name.data, name.size) == 0) {
            *output_symbol_index = (uint16_t)ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    if (builder->symbol_count >= (size_t)UINT16_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_program_builder_reserve_symbols(builder,
                                                 builder->symbol_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->symbols[builder->symbol_count] = name;
    *output_symbol_index = (uint16_t)builder->symbol_count;
    builder->symbol_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_add_literal(
    binrwir_program_builder_t *builder, runtimec_bytes_view_t wire_bytes,
    runtimec_string_view_t langtype, uint32_t *output_literal_index) {
    if (output_literal_index != NULL) {
        *output_literal_index = BINRWIR_LITERAL_INDEX_NONE;
    }
    if (!binrwir_program_builder_is_writable(builder) ||
        output_literal_index == NULL || wire_bytes.size == 0U ||
        wire_bytes.data == NULL ||
        binrwir_signature_validate_parameter_type(langtype) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (builder->literal_count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_program_builder_reserve_literals(
            builder, builder->literal_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->literals[builder->literal_count].wire_bytes = wire_bytes;
    builder->literals[builder->literal_count].langtype = langtype;
    *output_literal_index = (uint32_t)builder->literal_count;
    builder->literal_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_add_instruction(
    binrwir_program_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier) {
    size_t read_count;
    size_t write_count;
    size_t span_count;
    binrwir_program_instruction_record_t *record;

    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (!binrwir_program_builder_is_writable(builder) || request == NULL ||
        output_instruction_identifier == NULL ||
        binrwir_instruction_validate_build_request(request) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    read_count = request->read_arguments.count;
    write_count = request->write_arguments.count;
    span_count = read_count + write_count;
    if (builder->instructions.count >= UINT32_MAX ||
        span_count > UINT32_MAX ||
        builder->argument_count > UINT32_MAX - span_count) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_program_builder_reserve_instructions(
            builder, builder->instructions.count + 1U) ||
        !binrwir_program_builder_reserve_arguments(
            builder, builder->argument_count + span_count)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (read_count != 0U) {
        memcpy(builder->arguments + builder->argument_count,
               request->read_arguments.data,
               sizeof(binrwir_argument_t) * read_count);
    }
    if (write_count != 0U) {
        memcpy(builder->arguments + builder->argument_count + read_count,
               request->write_arguments.data,
               sizeof(binrwir_argument_t) * write_count);
    }
    record = &builder->instructions.records[builder->instructions.count];
    record->source_logical_program_counter =
        request->source_logical_program_counter;
    record->read_argument_offset = (uint32_t)builder->argument_count;
    record->read_argument_count = (uint32_t)read_count;
    record->write_argument_count = (uint32_t)write_count;
    builder->instructions.heads[builder->instructions.count]
        .operation_identifier = request->operation_identifier;
    *output_instruction_identifier = (uint32_t)builder->instructions.count;
    builder->instructions.count += 1U;
    builder->argument_count += span_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_program_builder_finalize(
    binrwir_program_builder_t *builder, binrwir_program_t **output_program) {
    binrwir_program_t *program;
    size_t *symbol_order = NULL;
    size_t *symbol_remap = NULL;
    runtimec_status_t status;

    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (!binrwir_program_builder_is_writable(builder) ||
        output_program == NULL || builder->instructions.count == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    program = (binrwir_program_t *)runtimec_allocator_allocate(
        &builder->allocator, sizeof(*program), _Alignof(binrwir_program_t));
    if (program == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(program, 0, sizeof(*program));
    program->allocator = builder->allocator;
    program->identity = builder->identity;
    status = binrwir_program_allocate_storage(program, builder);
    if (status == RUNTIMEC_STATUS_SUCCESS && builder->symbol_count != 0U) {
        symbol_order = (size_t *)binrwir_program_allocate_exact(
            &builder->allocator, builder->symbol_count * 2U, sizeof(size_t),
            _Alignof(size_t));
        if (symbol_order == NULL) {
            status = RUNTIMEC_STATUS_OUT_OF_MEMORY;
        } else {
            symbol_remap = symbol_order + builder->symbol_count;
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memcpy(program->instructions.heads, builder->instructions.heads,
               sizeof(binrwir_instruction_t) * builder->instructions.count);
        memcpy(program->instructions.records, builder->instructions.records,
               sizeof(binrwir_program_instruction_record_t) *
                   builder->instructions.count);
        if (program->argument_count != 0U) {
            memcpy(program->arguments, builder->arguments,
                   sizeof(binrwir_argument_t) * program->argument_count);
        }
        if (program->read_parameters.count != 0U) {
            memcpy(program->read_parameters.argument_indices,
                   builder->read_parameters.argument_indices,
                   sizeof(uint32_t) * program->read_parameters.count);
        }
        if (program->write_parameters.count != 0U) {
            memcpy(program->write_parameters.argument_indices,
                   builder->write_parameters.argument_indices,
                   sizeof(uint32_t) * program->write_parameters.count);
        }
        if (program->alias_rule_count != 0U) {
            memcpy(program->alias_rules, builder->alias_rules,
                   sizeof(binrwir_alias_rule_t) * program->alias_rule_count);
        }
        binrwir_program_copy_pools(program, builder, symbol_order,
                                   symbol_remap);
        status = binrwir_program_validate(program);
    }
    if (symbol_order != NULL) {
        (void)runtimec_allocator_deallocate(
            &builder->allocator, symbol_order,
            sizeof(size_t) * builder->symbol_count * 2U, _Alignof(size_t));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        binrwir_program_release(program);
        (void)runtimec_allocator_deallocate(
            &builder->allocator, program, sizeof(*program),
            _Alignof(binrwir_program_t));
        return status;
    }
    builder->finalized = true;
    *output_program = program;
    return RUNTIMEC_STATUS_SUCCESS;
}
