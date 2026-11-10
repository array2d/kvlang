#ifndef RUNTIMEC_VMKV_RWIR_MANIFEST_H
#define RUNTIMEC_VMKV_RWIR_MANIFEST_H

/*
 * 组件：vmkv/rwir_manifest
 * 作用：把 specification/rwir_catalog.def（能力行）与 rwir_alias_table.def
 *       （surface 名 → 能力行）以 X-macro 展开成两张进程内只读表，并提供
 *       surface 名解析、按操作数类型集改选特化、TypeSet 单值判定。
 * 联系：vmkv/encoder 先经本表把 operation name 收敛成唯一能力行，再按能力行
 *       的读写掩码与结果规则收敛参数类型。
 * 约束：本表不复制第二张签名表，也不保存第三个事实源；名字解析只走别名表，
 *       不拼前缀、不做名称归一化、没有回退。掩码位 19 以上保留且必须为零。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/instruction.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TypeSet 单个位；位号与 rwir_catalog.def 头注释完全一致。 */
#define VMKV_RWIR_TYPE_SET_NONE UINT32_C(0)
#define VMKV_RWIR_TYPE_SET_INT8 (UINT32_C(1) << 0)
#define VMKV_RWIR_TYPE_SET_INT16 (UINT32_C(1) << 1)
#define VMKV_RWIR_TYPE_SET_INT32 (UINT32_C(1) << 2)
#define VMKV_RWIR_TYPE_SET_INT64 (UINT32_C(1) << 3)
#define VMKV_RWIR_TYPE_SET_UINT8 (UINT32_C(1) << 4)
#define VMKV_RWIR_TYPE_SET_UINT16 (UINT32_C(1) << 5)
#define VMKV_RWIR_TYPE_SET_UINT32 (UINT32_C(1) << 6)
#define VMKV_RWIR_TYPE_SET_UINT64 (UINT32_C(1) << 7)
#define VMKV_RWIR_TYPE_SET_FLOAT32 (UINT32_C(1) << 8)
#define VMKV_RWIR_TYPE_SET_FLOAT64 (UINT32_C(1) << 9)
#define VMKV_RWIR_TYPE_SET_BOOLEAN (UINT32_C(1) << 10)
#define VMKV_RWIR_TYPE_SET_CHARACTER_UTF8 (UINT32_C(1) << 11)
#define VMKV_RWIR_TYPE_SET_CHARACTER_UTF32 (UINT32_C(1) << 12)
#define VMKV_RWIR_TYPE_SET_CHARACTER_ASCII (UINT32_C(1) << 13)
#define VMKV_RWIR_TYPE_SET_ARRAY (UINT32_C(1) << 14)
#define VMKV_RWIR_TYPE_SET_STRING_KEY_MAP (UINT32_C(1) << 15)
#define VMKV_RWIR_TYPE_SET_STRUCT_REFERENCE (UINT32_C(1) << 16)
#define VMKV_RWIR_TYPE_SET_EXTENSION (UINT32_C(1) << 17)
#define VMKV_RWIR_TYPE_SET_POINTER (UINT32_C(1) << 18)
/* 位 19：**任意单一类型**标记，必须独占（与任何具体位并集即非法）。
   只有 `nonnull` / `nonnull·not` 用；它的意义是「这个操作数可以是任意单一种类」，
   所以它不能被枚举成具体位——那会退回成一份会漂的类型白名单。 */
#define VMKV_RWIR_TYPE_SET_ANY_SINGLE (UINT32_C(1) << 19)

#define VMKV_RWIR_TYPE_SET_WIDTH 20U
#define VMKV_RWIR_TYPE_SET_LIMIT (UINT32_C(1) << VMKV_RWIR_TYPE_SET_WIDTH)
#define VMKV_RWIR_TYPE_SET_RESERVED_MASK (~(VMKV_RWIR_TYPE_SET_LIMIT - UINT32_C(1)))

/*
 * manifest 版本：能力行的算子语义、opid 或掩码发生不兼容变化时必须 +1。
 * 它进 Program identity，因此旧 Program 不会与新 manifest 混用。
 */
#define VMKV_RWIR_MANIFEST_VERSION 1U

/* 能力行的三组枚举词汇；取值只供本组件使用。 */
typedef enum runtimec_rwir_constraint {
    RUNTIMEC_RWIR_CONSTRAINT_INVALID = 0,
    RUNTIMEC_RWIR_CONSTRAINT_NONE = 1,
    RUNTIMEC_RWIR_CONSTRAINT_SAME_CLASS = 2,
    RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPATIBLE = 3,
    RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPARE = 4
} runtimec_rwir_constraint_t;

typedef enum runtimec_rwir_result_rule {
    RUNTIMEC_RWIR_RESULT_INVALID = 0,
    RUNTIMEC_RWIR_RESULT_NONE = 1,
    RUNTIMEC_RWIR_RESULT_SAME_FIRST = 2,
    RUNTIMEC_RWIR_RESULT_SAME_SECOND = 3,
    RUNTIMEC_RWIR_RESULT_WIDEN = 4,
    RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER = 5,
    RUNTIMEC_RWIR_RESULT_BOOLEAN = 6,
    RUNTIMEC_RWIR_RESULT_FLOAT64 = 7,
    RUNTIMEC_RWIR_RESULT_TARGET_TYPE = 8,
    RUNTIMEC_RWIR_RESULT_IDENTITY = 9,
    RUNTIMEC_RWIR_RESULT_CHARACTER_UTF8 = 10
} runtimec_rwir_result_rule_t;

typedef enum runtimec_rwir_alias_policy {
    RUNTIMEC_RWIR_ALIAS_INVALID = 0,
    RUNTIMEC_RWIR_ALIAS_FORBIDDEN = 1,
    RUNTIMEC_RWIR_ALIAS_DECLARED = 2
} runtimec_rwir_alias_policy_t;

/* 能力行 identifier 的枚举：值直接来自 .def 的书写顺序，只在本组件内使用。 */
typedef enum vmkv_rwir_capability_identifier {
/* 枚举只需要第一个形参，其余显式列出并丢弃。
 *
 * 为什么不用 `#define X(a, ...) a,`：变参不引用时 GCC 仍把实参里的逗号留在
 * 展开结果中（`A, 0, 0, 0U, (..), ..`），形成逗号表达式并触发
 * `-Wunused-value`。显式列出每个形参、宏体只引用 `identifier`，实参就整体消失。 */
#define RUNTIMEC_RWIR_OPERATION(identifier, operation_name, numeric_identifier, \
                                read_mask, write_mask,                         \
                                read_roles, write_roles,                       \
                                constraint_value, result_value, alias_value,    \
                                decisions)                                      \
    identifier,
#include "specification/rwir_catalog.def"
#undef RUNTIMEC_RWIR_OPERATION
    VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT
} vmkv_rwir_capability_identifier_t;

/* 一条能力行。 */
typedef struct vmkv_rwir_capability {
    runtimec_string_view_t operator_name;
    runtimec_string_view_t behavior_decisions;
    binrwir_operation_identifier_t operation_identifier;
    uint32_t read_parameter_type_set;
    uint32_t write_parameter_type_set;
    runtimec_rwir_constraint_t constraint;
    runtimec_rwir_result_rule_t result_rule;
    runtimec_rwir_alias_policy_t alias_policy;
    /* 按位置的角色，逗号分隔的文本（`"key"`、`"key,"`、空）。空 = 全部 ANY。
     * 用文本而不是位集：预处理器传不了带逗号的小列表，而字符串里逗号无特殊含义。 */
    runtimec_string_view_t read_parameter_roles;
    runtimec_string_view_t write_parameter_roles;
} vmkv_rwir_capability_t;

/* 一条别名记录：surface 名 → 能力行，不携带类型。 */
typedef struct vmkv_rwir_alias {
    runtimec_string_view_t surface_name;
    vmkv_rwir_capability_identifier_t capability_identifier;
} vmkv_rwir_alias_t;

/* 一条编译器改选规则：操作数静态类型集落进两个掩码时改选能力行。 */
typedef struct vmkv_rwir_specialization {
    runtimec_string_view_t surface_name;
    uint32_t first_parameter_type_set;
    uint32_t second_parameter_type_set;
    bool result_inverted;
    vmkv_rwir_capability_identifier_t capability_identifier;
} vmkv_rwir_specialization_t;

typedef enum vmkv_rwir_specialization_result {
    VMKV_RWIR_SPECIALIZATION_RESULT_INVALID = 0,
    /* 没有匹配的特化：调用方保留别名表解析出的能力行。 */
    VMKV_RWIR_SPECIALIZATION_RESULT_NONE = 1,
    /* 选中特化行：output_capability 指向该行。 */
    VMKV_RWIR_SPECIALIZATION_RESULT_SELECTED = 2,
    /* 选中特化行，但结果取反；当前 BinRWIR 指令头只有 opid，无取反位。 */
    VMKV_RWIR_SPECIALIZATION_RESULT_INVERTED = 3
} vmkv_rwir_specialization_result_t;

/* surface 名 → 能力行；未注册返回 NULL，不做任何名称归一化。 */
const vmkv_rwir_capability_t *vmkv_rwir_manifest_find_operation(
    runtimec_string_view_t surface_name);

/* 按 identifier 取能力行；同一 identifier 恒返回同一地址，可用于行比较。 */
const vmkv_rwir_capability_t *vmkv_rwir_manifest_capability(
    vmkv_rwir_capability_identifier_t identifier);

/*
 * opid → 算子名。未登记返回 false，不做任何猜测与归一化。
 *
 * 存在的理由：BinRWIR 要能打印成文本以检查编译结果，打印时按 opid 显示算子名。
 * 名字解析放在这一层，是因为 `binrwir` 不得依赖 `vmkv`（否则编码层与指令层成环），
 * 由调用方把本函数作为回调传给 `binrwir_binfunc_format`。
 *
 * 数据只来自 `rwir_catalog.def` 展开的那张能力表——**不建第二张 opid 表**。
 * `FOREIGN`（0xFFFE）与保留区不是算子，不在表里，返回 false，由调用方自行
 * 打印 `foreign` 或数值。
 *
 * 所有权：输出的字符串指向能力表里的静态文本，**生存期与进程同长**，调用方不拥有、
 * 不得释放；需要持有或修改时必须自己拷贝。
 */
bool vmkv_rwir_manifest_find_operation_by_identifier(
    binrwir_operation_identifier_t operation_identifier, runtimec_string_view_t *output_name);

/*
 * 该 surface 名是否解析到 `call` 这条能力行。
 *
 * 注意两件事：
 *  1. 这里只回答「这个名字是不是 call」。**判断一条 RWIR 指令是不是函数调用，
 *     要看 opcode 槽的 langtype**（`vmkv_rwir_manifest_is_rwfunc_call`），不能靠
 *     名字形状——拼错的裸名也可能以 `/lib/` 开头。
 *  2. 调用指令的读参分区**不是**「第一个是函数名」。目标函数路径在 opcode 槽的
 *     body 里，编码器会**额外追加**一个 FUNCTION 读参放在分区最前，其后才是真实
 *     实参。按「读参 0 = 函数名」实现会静默错位：写参区随读参区增长而右移，
 *     任何漏加该偏移的指针都会落到类型串上，拷出一个 kind=INVALID 的参数项——
 *     不报错，只是数据错了。
 */
bool vmkv_rwir_manifest_is_call(runtimec_string_view_t operation_name);

/*
 * opcode 槽的 langtype 是否表示「一次 rwfunc 调用」。
 *
 * layout 在生成时就按目标选定了槽类型：调用槽是 `rwfunc`，算子槽是 `rwir`。
 * 这是「这条指令是不是调用」的唯一权威判据——不用名字是否以 `/lib/` 开头之类的
 * 字符串特判，那会把拼错的裸名也当成合法调用。
 */
bool vmkv_rwir_manifest_is_rwfunc_call(
    runtimec_string_view_t operation_langtype);

/* 按 .def 声明顺序求第一条匹配的改选规则。 */
vmkv_rwir_specialization_result_t vmkv_rwir_manifest_find_specialization(
    runtimec_string_view_t surface_name,
    const uint32_t *parameter_type_sets, size_t parameter_count,
    const vmkv_rwir_capability_t **output_capability);

/* 恰好一位、且不含保留位。空集（None）不是单值。 */
bool vmkv_rwir_type_set_is_single(uint32_t type_set);

#ifdef __cplusplus
}
#endif

#endif
