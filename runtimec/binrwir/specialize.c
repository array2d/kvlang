/*
 * 组件：binrwir/specialize 的实现
 * 作用：按操作数静态类型组合把通用算子改写为特化 opid，产出新 Program。
 * 约束：纯函数；按原序搬运四类池，不重排。
 */

#include "binrwir/specialize.h"

#include <string.h>

#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"

/* 元组串缓冲：读参 + 写参，每项最多 32 字节（最长类型名 `stringkeymap`）。 */
#define BINRWIR_SPECIALIZE_TUPLE_CAPACITY 256U
#define BINRWIR_SPECIALIZE_NAME_CAPACITY 32U

/*
 * 把一个实参项的静态 langtype 归约成**基名**。
 *
 * 基名 = 去掉前导 `*` 与 `[dims]`：`*int64` 记 `int64`（指针不参与算术），
 * `[3]int64` 记 `int64`（形状归容器算子，不进算术元组）。**这条规则写死在
 * 这里**，不允许各 handler 各解一遍——两处解析必然漂移。
 *
 * 取不到类型返回空视图，调用方据此不特化该指令：**不猜**。
 */
static runtimec_string_view_t binrwir_specialize_argument_type(
    const binrwir_program_t *program, const binrwir_program_view_t *view,
    const binrwir_argument_t *argument) {
    const runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LITERAL: {
        runtimec_bytes_view_t wire;

        if (binrwir_program_get_literal(program, argument->payload, &wire,
                                        &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    }
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC:
        if (binrwir_program_get_symbol(program, argument->slot, &name,
                                       &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    case BINRWIR_REFERENCE_PARAMETER_READ:
        if (view->signature.read_parameter_types != NULL &&
            (size_t)argument->slot < view->signature.read_parameter_count) {
            return view->signature.read_parameter_types[argument->slot];
        }
        return empty;
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        if (view->signature.write_parameter_types != NULL &&
            (size_t)argument->slot < view->signature.write_parameter_count) {
            return view->signature.write_parameter_types[argument->slot];
        }
        return empty;
    default:
        return empty;
    }
}

/* 归约并写入 output；返回长度，0 表示「取不到基名」。 */
static size_t binrwir_specialize_base_name(runtimec_string_view_t langtype,
                                           char *output, size_t capacity) {
    size_t begin = 0U;
    size_t end;

    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return 0U;
    }
    end = langtype.size;
    while (begin < end && langtype.data[begin] == '*') {
        begin += 1U;
    }
    if (begin < end && langtype.data[begin] == '[') {
        while (begin < end && langtype.data[begin] != ']') {
            begin += 1U;
        }
        if (begin < end) {
            begin += 1U;
        }
    }
    if (begin >= end || end - begin >= capacity) {
        return 0U;
    }
    memcpy(output, langtype.data + begin, end - begin);
    return end - begin;
}

/* 追加一项到元组串；成功返回 1。任何一项取不到类型则整体放弃。 */
static int binrwir_specialize_append(
    const binrwir_program_t *program, const binrwir_program_view_t *view,
    const binrwir_argument_t *argument, char *output, size_t capacity,
    size_t *used) {
    char base_name[BINRWIR_SPECIALIZE_NAME_CAPACITY];
    const runtimec_string_view_t type =
        binrwir_specialize_argument_type(program, view, argument);
    const size_t length = binrwir_specialize_base_name(
        type, base_name, sizeof(base_name));

    if (length == 0U) {
        return 0;
    }
    if (*used + (length + (*used ? 1U : 0U)) >= capacity) {
        return 0;
    }
    if (*used != 0U) {
        output[*used] = ',';
        *used += 1U;
    }
    memcpy(output + *used, base_name, length);
    *used += length;
    output[*used] = '\0';
    return 1;
}

/*
 * 拼一条指令的元组串：**读参按声明序在前、写参在后，同型也写全**。
 * 写全是唯一性前提——省略重复项会让 `add_int64` 与 `add_int64_int64_int64`
 * 指同一条，而查找表用 `(base opid, 元组串)` 做键。
 */
static size_t binrwir_specialize_tuple(
    const binrwir_program_t *program, const binrwir_program_view_t *view,
    const binrwir_instruction_view_t *instruction, char *output,
    size_t capacity) {
    size_t used = 0U;
    size_t ordinal;

    output[0] = '\0';
    for (ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        if (!binrwir_specialize_append(program, view,
                                       &instruction->read_arguments.data[ordinal],
                                       output, capacity, &used)) {
            return 0U;
        }
    }
    for (ordinal = 0U; ordinal < instruction->write_arguments.count;
         ++ordinal) {
        if (!binrwir_specialize_append(program, view,
                                       &instruction->write_arguments.data[ordinal],
                                       output, capacity, &used)) {
            return 0U;
        }
    }
    return used;
}

/* 查表：`(base opid, 元组串)` → 特化 opid。 */
static int binrwir_specialize_lookup(binrwir_operation_identifier_t base,
                                     const char *tuple,
                                     binrwir_operation_identifier_t *output) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < BINRWIR_SPECIALIZATION_TABLE_COUNT;
         ++ordinal) {
        const binrwir_specialization_t *entry = &binrwir_specializations[ordinal];

        /* 表按 (base opid, 元组串) 排序：base 大了后面不可能再有。 */
        if (entry->base_operation_identifier < base) {
            continue;
        }
        if (entry->base_operation_identifier > base) {
            return 0;
        }
        if (strcmp(entry->operand_types, tuple) != 0) {
            continue;
        }
        *output = entry->specialized_operation_identifier;
        return 1;
    }
    return 0;
}

/* 按原序搬运符号池。返回 0 表示失败。 */
static int binrwir_specialize_carry_symbols(
    const binrwir_program_t *program, binrwir_program_builder_t *builder,
    uint16_t *remap, size_t symbol_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;
        uint16_t index = 0U;

        if (binrwir_program_get_symbol(program, (uint16_t)ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_program_builder_add_symbol(builder, name, langtype,
                                               &index) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
        remap[ordinal] = index;
    }
    return 1;
}

/* 按原序搬运字面量池。 */
static int binrwir_specialize_carry_literals(
    const binrwir_program_t *program, binrwir_program_builder_t *builder,
    uint32_t *remap, size_t literal_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;
        uint32_t index = BINRWIR_LITERAL_INDEX_NONE;

        if (binrwir_program_get_literal(program, (uint32_t)ordinal, &wire,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_program_builder_add_literal(builder, wire, langtype,
                                                &index) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
        remap[ordinal] = index;
    }
    return 1;
}

/* 按原序搬运签名参数：两个方向各自逐条搬。 */
static int binrwir_specialize_carry_parameters(
    const binrwir_program_view_t *view, binrwir_program_builder_t *builder) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->signature.read_parameter_count;
         ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_program_builder_add_parameter(
                builder, BINRWIR_PARAMETER_DIRECTION_READ,
                view->signature.read_parameter_argument_indices[ordinal],
                view->signature.read_parameter_types[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < view->signature.write_parameter_count;
         ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_program_builder_add_parameter(
                builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
                view->signature.write_parameter_argument_indices[ordinal],
                view->signature.write_parameter_types[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < view->signature.alias_rule_count; ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_program_builder_add_alias_rule(
                builder, &view->signature.alias_rules[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    return 1;
}

/* 搬运全部指令：操作数按原样复制，只改写 opid。 */
/*
 * 按 remap 改写参数项里的符号下标。
 *
 * **这一步不能省**：`add_symbol` 返回的是 builder 内序号，而 `finalize` 会按
 * 名字字典序重排符号池并同步改写 slot。逐条按原序 add 时新旧序号通常相同，
 * 但「通常」不是「一定」——一旦不同，所有 LOCAL / STATIC / FUNCTION 引用就
 * 指向别的符号，而错误只会在运行期以「值不对」或「绑定失败」的形式出现。
 */
static void binrwir_specialize_remap_argument(
    binrwir_argument_t *argument, const uint16_t *symbol_remap,
    size_t symbol_count) {
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC:
    case BINRWIR_REFERENCE_FUNCTION:
        if ((size_t)argument->slot < symbol_count) {
            argument->slot = symbol_remap[argument->slot];
        }
        break;
    default:
        break;
    }
}

static int binrwir_specialize_carry_instructions(
    const binrwir_program_t *program, const binrwir_program_view_t *view,
    binrwir_program_builder_t *builder, const uint16_t *symbol_remap,
    size_t symbol_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_instruction_build_request_t request;
        char tuple[BINRWIR_SPECIALIZE_TUPLE_CAPACITY];
        binrwir_operation_identifier_t specialized = 0U;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
        /* 一条指令的参数项上限：读 4 + 写 2，与 builder 的容量一致。 */
        binrwir_argument_t read_copies[8];
        binrwir_argument_span_t read_arguments;
        binrwir_argument_span_t write_arguments;

        if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
        if (instruction.read_arguments.count +
                instruction.write_arguments.count >
            sizeof(read_copies) / sizeof(read_copies[0])) {
            return 0;
        }
        const size_t tuple_size = binrwir_specialize_tuple(
            program, view, &instruction, tuple, sizeof(tuple));
        /* 元组取不全或查不到 → **保留原 opid**，不是失败。 */
        if (tuple_size == 0U ||
            !binrwir_specialize_lookup(
                instruction.instruction->operation_identifier, tuple,
                &specialized)) {
            specialized = instruction.instruction->operation_identifier;
        }
        /*
         * 参数项要拷一份再改 slot：`instruction` 的 span 是指向 Program 的
         * 只读视图，直接写它会改到旧 Program。
         */
        {
            size_t copy_ordinal;
            binrwir_argument_t *copies = read_copies;

            for (copy_ordinal = 0U;
                 copy_ordinal < instruction.read_arguments.count +
                                    instruction.write_arguments.count;
                 ++copy_ordinal) {
                const binrwir_argument_t *source =
                    copy_ordinal < instruction.read_arguments.count
                        ? &instruction.read_arguments.data[copy_ordinal]
                        : &instruction.write_arguments.data[
                              copy_ordinal - instruction.read_arguments.count];

                copies[copy_ordinal] = *source;
                binrwir_specialize_remap_argument(&copies[copy_ordinal],
                                                  symbol_remap, symbol_count);
            }
            read_arguments.data = copies;
            read_arguments.count = instruction.read_arguments.count;
            write_arguments.data =
                copies + instruction.read_arguments.count;
            write_arguments.count = instruction.write_arguments.count;
        }
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter =
            instruction.source_logical_program_counter;
        request.operation_identifier = specialized;
        request.read_arguments = read_arguments;
        request.write_arguments = write_arguments;
        if (binrwir_program_builder_add_instruction(builder, &request,
                                                    &identifier) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    return 1;
}

runtimec_status_t binrwir_program_specialize(
    const binrwir_program_t *program, const runtimec_allocator_t *allocator,
    binrwir_program_t **output_program, runtimec_error_t *error) {
    binrwir_program_view_t view;
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *specialized = NULL;
    size_t symbol_count = 0U;
    size_t literal_count = 0U;
    uint16_t *symbol_remap = NULL;
    uint32_t *literal_remap = NULL;
    runtimec_status_t status;

    runtimec_error_reset(error);
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (program == NULL || allocator == NULL || output_program == NULL ||
        !runtimec_allocator_is_valid(allocator) || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_program_validate(program) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_identity(program, &identity) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_literal_count(program, &literal_count) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (symbol_count != 0U) {
        symbol_remap = (uint16_t *)runtimec_allocator_allocate(
            allocator, sizeof(*symbol_remap) * symbol_count,
            _Alignof(uint16_t));
        if (symbol_remap == NULL) {
            (void)binrwir_program_builder_destroy(builder);
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
    }
    if (literal_count != 0U) {
        literal_remap = (uint32_t *)runtimec_allocator_allocate(
            allocator, sizeof(*literal_remap) * literal_count,
            _Alignof(uint32_t));
        if (literal_remap == NULL) {
            (void)runtimec_allocator_deallocate(
                allocator, symbol_remap, sizeof(*symbol_remap) * symbol_count,
                _Alignof(uint16_t));
            (void)binrwir_program_builder_destroy(builder);
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
    }
    if (!binrwir_specialize_carry_parameters(&view, builder) ||
        !binrwir_specialize_carry_symbols(program, builder, symbol_remap,
                                          symbol_count) ||
        !binrwir_specialize_carry_literals(program, builder, literal_remap,
                                           literal_count) ||
        !binrwir_specialize_carry_instructions(program, &view, builder,
                                              symbol_remap, symbol_count)) {
        status = RUNTIMEC_STATUS_INVALID;
    } else {
        status = binrwir_program_builder_finalize(builder, &specialized);
    }
    if (symbol_remap != NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, symbol_remap, sizeof(*symbol_remap) * symbol_count,
            _Alignof(uint16_t));
    }
    if (literal_remap != NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, literal_remap, sizeof(*literal_remap) * literal_count,
            _Alignof(uint32_t));
    }
    (void)binrwir_program_builder_destroy(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    *output_program = specialized;
    return RUNTIMEC_STATUS_SUCCESS;
}
