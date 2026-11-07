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
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    const binrwir_argument_t *argument) {
    const runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LITERAL: {
        runtimec_bytes_view_t wire;

        if (binrwir_binfunc_get_literal(program, argument->payload, &wire,
                                        &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    }
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC:
        if (binrwir_binfunc_get_symbol(program, argument->slot, &name,
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
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
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
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
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

/*
 * 该 base opid 是否**可特化**：`binrwir_specializations[]` 里出现过它 ⇒ 可特化。
 *
 * 依据（生成器侧已验证，2026-11-05）：
 *   `build_specializations` 是 `if len(specialization_combinations(row)) <= 1:
 *   continue` —— 展开不出多种类型组合的行**根本进不了这张表**。所以
 *   「可特化」⇔「表里有它的行」，两者是同一个集合的两种说法。
 *
 * **为什么这个判据必须存在**：`map·new` / `struct·new` / `string·*` / `time·*`
 * / `kvspace·*` 这些算子的操作数类型由**安装签名**给出，不是由实参静态类型选行。
 * 它们永远查不到特化行，但那是**正常形态**而非错误——它们的 base opid 本来就该
 * 分派到自己的 handler。
 *
 * 表按 `(base opid, 元组串)` 升序（生成器排序），故二分找首个 base >= 目标。
 * 不用线性扫：1836 行、指令级调用，线性扫是白送的开销。
 */
static int binrwir_specialize_applies(binrwir_operation_identifier_t base) {
    size_t low = 0U;
    size_t high = BINRWIR_SPECIALIZATION_TABLE_COUNT;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (binrwir_specializations[middle].base_operation_identifier < base) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low < BINRWIR_SPECIALIZATION_TABLE_COUNT &&
           binrwir_specializations[low].base_operation_identifier == base;
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
    const binrwir_binfunc_t *program, binrwir_binfunc_builder_t *builder,
    uint16_t *remap, size_t symbol_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;
        uint16_t index = 0U;

        if (binrwir_binfunc_get_symbol(program, (uint16_t)ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_builder_add_symbol(builder, name, langtype,
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
    const binrwir_binfunc_t *program, binrwir_binfunc_builder_t *builder,
    uint32_t *remap, size_t literal_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;
        uint32_t index = BINRWIR_LITERAL_INDEX_NONE;

        if (binrwir_binfunc_get_literal(program, (uint32_t)ordinal, &wire,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_builder_add_literal(builder, wire, langtype,
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
    const binrwir_binfunc_view_t *view, binrwir_binfunc_builder_t *builder) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->signature.read_parameter_count;
         ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_binfunc_builder_add_parameter(
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

        if (binrwir_binfunc_builder_add_parameter(
                builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
                view->signature.write_parameter_argument_indices[ordinal],
                view->signature.write_parameter_types[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < view->signature.alias_rule_count; ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_binfunc_builder_add_alias_rule(
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

/*
 * 特化失败时的三种成因，**诊断码互不相同**。
 *
 * 为什么必须分开：三者的修法指向完全不同的层。合成一句「特化失败」，看到的人
 * 只能知道「这里错了」，不知道该去改 layout、改 vmkv、还是补 specialization 表。
 *
 *   1 操作数没有 langtype  —— 上游（layout / vmkv）没给类型
 *   2 元组串超容量        —— 本模块内部缺陷
 *   3 查表未命中          —— specialization_table 缺这一行（生成器或 catalog）
 *
 * 编号只增不改：已发出的码会出现在 error 里，插中间或复用会让下游判据错位。
 */
typedef enum binrwir_specialize_diagnostic {
    /* 走不到：元组取不全已独立成第三类（见 OPERAND_TYPE_UNKNOWN）。
     * 编号**不复用**——1 曾经表示这件事，复用会让下游旧判据读错。 */
    BINRWIR_SPECIALIZE_DIAG_MISSING_OPERAND_TYPE = 1,
    BINRWIR_SPECIALIZE_DIAG_NO_SPECIALIZATION_ROW = 3,
    /*
     * 第三类：**上游没给类型**，与特化表无关。
     *
     * 单独成类而不是并进 1 或 3，因为它们指向完全不同的修法：
     *   3 → 补 `specialization_table` 的一行（生成器/catalog）
     *   4 → 修 layout/vmkv 的类型推导
     * 一个诊断码如果同时承载两件事，就不能用来派活。
     */
    BINRWIR_SPECIALIZE_DIAG_OPERAND_TYPE_UNKNOWN = 4,
    BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_UNREADABLE = 5,
    BINRWIR_SPECIALIZE_DIAG_ARGUMENT_COUNT_EXCEEDS_BUFFER = 6,
    BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_REJECTED = 7,
    BINRWIR_SPECIALIZE_DIAG_COPY_ALLOCATION_FAILED = 8
} binrwir_specialize_diagnostic_t;

/*
 * 释放一条指令的操作数副本。
 *
 * 个数**显式传入**而不是从 span 反推：反推要在释放点重新算「读 + 写」，而
 * 那个和正是分配时用的那个——两处各算一次就是「同一个事实两个来源」。
 *
 * 集中在一处释放，早退路径与正常路径走同一个函数；**每条 return 前都要调**，
 * 漏一处就是泄漏。
 */
static void binrwir_specialize_release_copies(
    const runtimec_allocator_t *allocator, binrwir_argument_t *copies,
    size_t operand_count) {
    if (copies == NULL) {
        return;
    }
    (void)runtimec_allocator_deallocate(
        allocator, copies, sizeof(*copies) * operand_count,
        _Alignof(binrwir_argument_t));
}

static int binrwir_specialize_carry_instructions(
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    binrwir_binfunc_builder_t *builder, const uint16_t *symbol_remap,
    size_t symbol_count, const runtimec_allocator_t *allocator,
    runtimec_error_t *error) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_instruction_build_request_t request;
        char tuple[BINRWIR_SPECIALIZE_TUPLE_CAPACITY];
        binrwir_operation_identifier_t specialized = 0U;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
        /*
         * 操作数副本按**实际个数**分配：
         *
         * builder 的参数存储是动态的（`reserve_arguments` 从 4 起按需翻倍，
         * 上限 `UINT32_MAX`），所以这里**不能**设一个「与 builder 一致」的常量
         * ——那个一致性不存在。曾写成 `binrwir_argument_t read_copies[8]` 并注释
         * 「与 builder 的容量一致」：注释凭空声称了一个对齐关系，于是没人会去
         * 检查它；而 9 个读参的指令（`container_water`）一律被判超限。
         *
         * **一个假的「已对齐」声明，比一个明显的限制更危险**——限制会被人踩到，
         * 假声明会让人不去踩。
         */
        binrwir_argument_t *copies = NULL;
        /*
         * 本条指令的操作数总数。声明在**循环体顶层**：分配、拷贝、释放三处
         * 都要用它，各自重算就是「同一个事实三个来源」。
         */
        size_t operand_count = 0U;
        binrwir_argument_span_t read_arguments;
        binrwir_argument_span_t write_arguments;

        if (binrwir_binfunc_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            /*
             * 取不到指令 = Program 与视图不一致（内部不一致）。**必须填 error**：
             * 只 return 0 会让调用方拿到 `code == 0` 的空错误，「失败了但没说
             * 为什么」与今天在灭的 `detail=6` 一类病症同族。
             */
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_UNREADABLE,
                runtimec_string_view_from_c_string(
                    "instruction is unreadable while carrying it into the "
                    "specialized program; the program view and the program "
                    "disagree (internal inconsistency)"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
        }
        /*
         * **顺序不能反**：先判「可特化」，不可特化的根本不去算元组。
         *
         * 不可特化的算子（`map·new` / `struct·new` / `string·*` / `time·*` /
         * `kvspace·*` / 数组族…）的操作数类型由**安装签名**给出，不是由实参静态
         * 类型选行。它们**不需要类型来选特化行**，所以也不该因为「没有类型」报错
         * ——那是它们的正常形态，base opid 直接分派到自己的 handler。
         *
         * 反过来的话（先算元组再判），这些算子会因为 tuple_size == 0 落到 code=4，
         * 报「生产者没给类型」。**那个错误指错了层**：它们的类型本来就该由安装
         * 签名供，不该由 layout 按实参推。
         */
        if (!binrwir_specialize_applies(
                instruction.instruction->operation_identifier)) {
            specialized = instruction.instruction->operation_identifier;
        } else {
            const size_t tuple_size = binrwir_specialize_tuple(
                program, view, &instruction, tuple, sizeof(tuple));
        /*
         * **必须特化，不命中即失败**（p10：先删掉基础 opid 的回落实现，再补
         * 替代路径）。此前这里是「保留原 opid」，于是「特化表没覆盖」和「特化
         * 生效」在行为上无法区分——回落让缺失永远看不出来。
         */
        if (tuple_size == 0U) {
            /*
             * 第三类：**上游没给类型**，与特化表无关。
             *
             * 元组拼不出来只有两种成因，都不是「表缺一行」：
             *   1. 某个操作数没有 langtype（`base_name` 对空 langtype 返回 0）
             *      —— layout/vmkv 没推出来，去修生产者；
             *   2. 元组串超容量 —— 本模块缓冲不够。
             * 两者共用这一条，因为 `binrwir_specialize_tuple` 只返回长度、不返回
             * 原因（见 `specialize.h` 的「已知限制」）。
             *
             * **本分支现在只会被可特化的算子走到**——不可特化的在更前面的
             * `binrwir_specialize_applies` 判断里就保留 base opid 了，根本不会
             * 算元组。所以这里报「生产者没给类型」时，指的一定是一个**本该**
             * 按实参类型选特化行、却拿不到类型的算子，归因是准确的。
             */
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_OPERAND_TYPE_UNKNOWN,
                runtimec_string_view_from_c_string(
                    "operand type is unknown: an operand has no langtype or the "
                    "tuple exceeds its buffer; this is a producer (layout/vmkv) "
                    "problem, unrelated to the specialization table"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
        }
        if (!binrwir_specialize_lookup(
                instruction.instruction->operation_identifier, tuple,
                &specialized)) {
            /*
             * 元组完整、但表里没有这个组合 —— `specialization_table` 缺一行。
             * 这是唯一指向「补生成器/catalog」的成因。
             *
             * 消息里**必须带 opid 与元组串**：只报「没有特化行」，看到的人还得
             * 自己去翻 Program 才知道缺的是哪一条——那就不是「清单」，是线索。
             *
             * 缓冲是**线程局部静态**：`runtimec_error_set` 只借用视图，栈上的
             * 缓冲在函数返回后就悬垂（与 `vmbin/value_access.c` 同一做法）。
             */
            static _Thread_local char message[512];
            static const char prefix[] =
                "no specialization row for operand types [";
            static const char middle[] = "] under base opid 0x";
            const size_t prefix_size = sizeof(prefix) - 1U;
            size_t used = 0U;
            size_t tuple_copied = tuple_size;

            memcpy(message, prefix, prefix_size);
            used = prefix_size;
            if (tuple_copied > sizeof(message) - used - 64U) {
                tuple_copied = sizeof(message) - used - 64U;
            }
            memcpy(message + used, tuple, tuple_copied);
            used += tuple_copied;
            memcpy(message + used, middle, sizeof(middle) - 1U);
            used += sizeof(middle) - 1U;
            {
                static const char digits[] = "0123456789abcdef";
                const unsigned base =
                    (unsigned)instruction.instruction->operation_identifier;
                int shift = 12;
                int wrote = 0;

                for (; shift >= 0; shift -= 4) {
                    message[used] = digits[(base >> shift) & 0xFU];
                    used += 1U;
                    wrote += 1;
                }
                (void)wrote;
            }
            {
                static const char tail[] =
                    "; add the row in specialization_table (generator/"
                    "catalog), not in the handler";
                size_t tail_size = sizeof(tail) - 1U;

                if (tail_size > sizeof(message) - used - 1U) {
                    tail_size = sizeof(message) - used - 1U;
                }
                memcpy(message + used, tail, tail_size);
                used += tail_size;
            }
            message[used] = '\0';
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_NO_SPECIALIZATION_ROW,
                runtimec_string_view_make(message, used),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
            }
        }
        /*
         * 参数项要拷一份再改 slot：`instruction` 的 span 是指向 Program 的
         * 只读视图，直接写它会改到旧 Program。
         *
         * 不可特化的指令也在这一路走：它保留 base opid，操作数照旧搬运（参数项
         * 里的 slot 需要按 `symbol_remap` 改写，这一步与是否特化无关）。
         */
        {
            size_t copy_ordinal;

            operand_count = instruction.read_arguments.count +
                            instruction.write_arguments.count;

            /*
             * 零操作数的指令（`return`）不分配：`runtimec_allocator_allocate`
             * 对 0 字节的行为未定义，且没有意义。
             */
            if (operand_count != 0U) {
                copies = (binrwir_argument_t *)runtimec_allocator_allocate(
                    allocator, sizeof(*copies) * operand_count,
                    _Alignof(binrwir_argument_t));
                if (copies == NULL) {
                    runtimec_error_set(
                        error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                        RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                        (int64_t)BINRWIR_SPECIALIZE_DIAG_COPY_ALLOCATION_FAILED,
                        runtimec_string_view_from_c_string(
                            "cannot allocate the operand copies for one "
                            "instruction"),
                        RUNTIMEC_PROGRAM_COUNTER_NONE);
                    return 0;
                }
            }
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
        if (binrwir_binfunc_builder_add_instruction(builder, &request,
                                                    &identifier) !=
            RUNTIMEC_STATUS_SUCCESS) {
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_REJECTED,
                runtimec_string_view_from_c_string(
                    "the specialized program rejected an instruction while "
                    "carrying it over; the instruction is malformed or its "
                    "arguments are inconsistent"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            (void)binrwir_specialize_release_copies(allocator, copies,
                                                    operand_count);
            return 0;
        }
        /* 本条已交给 builder（它拷贝参数项），副本立刻还回去。 */
        (void)binrwir_specialize_release_copies(allocator, copies,
                                                operand_count);
    }
    return 1;
}

runtimec_status_t binrwir_binfunc_specialize(
    const binrwir_binfunc_t *program, const runtimec_allocator_t *allocator,
    binrwir_binfunc_t **output_program, runtimec_error_t *error) {
    binrwir_binfunc_view_t view;
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_binfunc_t *specialized = NULL;
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
    if (binrwir_binfunc_validate(program) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_identity(program, &identity) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_symbol_count(program, &symbol_count) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_literal_count(program, &literal_count) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_validate(program) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_identity(program, &identity) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_literal_count(program, &literal_count) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_binfunc_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (symbol_count != 0U) {
        symbol_remap = (uint16_t *)runtimec_allocator_allocate(
            allocator, sizeof(*symbol_remap) * symbol_count,
            _Alignof(uint16_t));
        if (symbol_remap == NULL) {
            (void)binrwir_binfunc_builder_destroy(builder);
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
            (void)binrwir_binfunc_builder_destroy(builder);
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
    }
    if (!binrwir_specialize_carry_parameters(&view, builder) ||
        !binrwir_specialize_carry_symbols(program, builder, symbol_remap,
                                          symbol_count) ||
        !binrwir_specialize_carry_literals(program, builder, literal_remap,
                                           literal_count) ||
        !binrwir_specialize_carry_instructions(program, &view, builder,
                                              symbol_remap, symbol_count,
                                              allocator, error)) {
        status = RUNTIMEC_STATUS_INVALID;
    } else {
        status = binrwir_binfunc_builder_finalize(builder, &specialized);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
        }
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
    (void)binrwir_binfunc_builder_destroy(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    *output_program = specialized;
    return RUNTIMEC_STATUS_SUCCESS;
}
