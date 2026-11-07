#ifndef BINRWIR_PROGRAM_H
#define BINRWIR_PROGRAM_H

/*
 * 组件：binrwir/program
 * 作用：定义与 RWIR 语义等价、不可变、地址化的 BinRWIR Program，以及增量构造它的
 *       builder。
 * 联系：vmkv 把 capability 行的 union 掩码收敛成具体类型后经 builder 构造 Program；
 *       BinRWIR Compiler 与 vmbin 只读消费。
 * 约束：公开二进制载荷只有 opid、argidx 与引用种类/槽号/payload。可变参数分区、
 *       source mapping、framing 与三个私有池（具体类型文本、符号名、字面量字节）
 *       都不进入 instruction，也不缓存 V.body 或建任何中间值存储。
 * 所有权：三类池数据（类型文本、符号名、字面量）由 builder 借用**直到 finalize**；
 *       finalize 把它们全部深拷贝进 Program 自己的存储。Program 既不指向 builder、
 *       也不指向调用方内存，销毁后全部 view 指针失效。
 * 指令标识符：就是加入顺序的序号 0,1,2,…。execution plan 用
 *       「first_instruction_identifier + instruction_count」表达连续区间，所以标识符
 *       必须稠密连续，不能是哈希值或指针值。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "binrwir/instruction.h"
#include "binrwir/signature.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_IDENTIFIER_NONE UINT32_MAX
/* 符号下标上限是 UINT16_MAX - 1，所以 UINT16_MAX 可以当「无效下标」哨兵。 */
#define BINRWIR_SYMBOL_INDEX_NONE UINT16_MAX
#define BINRWIR_LITERAL_INDEX_NONE UINT32_MAX

typedef struct binrwir_program_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t schema_version;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t canonical_encoding_revision;
} binrwir_program_identity_t;

typedef struct binrwir_program binrwir_program_t;
typedef struct binrwir_program_builder binrwir_program_builder_t;

typedef struct binrwir_program_view {
    binrwir_program_identity_t identity;
    binrwir_signature_view_t signature;
    /* 逐条内容只能经 get_instruction 取得，私有 framing 不对外暴露。 */
    size_t instruction_count;
} binrwir_program_view_t;

/*
 * 只读查询。Program 由 finalize 产出后不再变化，因此调用方对同一个 Program 只需
 * 调用一次 validate；get_view 与 get_instruction 只做 O(1) 范围检查后借出指针，
 * 不在每次查询里重复全量校验。
 */
runtimec_status_t binrwir_program_get_identity(
    const binrwir_program_t *program,
    binrwir_program_identity_t *output_identity);
/* 全量校验：identity、参数类型、别名规则、每条 instruction 的参数绑定。 */
runtimec_status_t binrwir_program_validate(const binrwir_program_t *program);
runtimec_status_t binrwir_program_get_view(const binrwir_program_t *program,
                                           binrwir_program_view_t *output_view);
runtimec_status_t binrwir_program_get_instruction(
    const binrwir_program_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction);
/*
 * 按 source logical program counter 找指令标识符。goto / br 的跳转目标是 source
 * pc（layout 的 irseq），恢复时持久化的也是 source pc，而执行游标是稠密标识符；
 * 两个空间之间的唯一映射就是这里。找不到返回 INVALID。
 */
runtimec_status_t binrwir_program_find_instruction(
    const binrwir_program_t *program, uint32_t source_logical_program_counter,
    uint32_t *output_instruction_identifier);
/* 符号池规模：调用入口要在建绑定表前知道有多少个局部符号要留槽。 */
runtimec_status_t binrwir_program_get_symbol_count(
    const binrwir_program_t *program, size_t *output_symbol_count);
/*
 * 符号池只读借出：LOCAL / FUNCTION / STATIC 参数项的 slot 就是这个下标。
 * 名字与语言类型一起给出——创建槽的调用入口两样都要。
 */
runtimec_status_t binrwir_program_get_symbol(
    const binrwir_program_t *program, uint16_t symbol_index,
    runtimec_string_view_t *output_name,
    runtimec_string_view_t *output_langtype);
/* 字面量池规模：需要整池搬运（例如编译期深拷贝 Program）时必须先知道它。 */
runtimec_status_t binrwir_program_get_literal_count(
    const binrwir_program_t *program, size_t *output_literal_count);
/* 字面量池只读借出：LITERAL 参数项的 payload 就是这个下标。 */
runtimec_status_t binrwir_program_get_literal(
    const binrwir_program_t *program, uint32_t literal_index,
    runtimec_bytes_view_t *output_wire_bytes,
    runtimec_string_view_t *output_langtype);
runtimec_status_t binrwir_program_destroy(binrwir_program_t *program);

/*
 * 符号名是否含**成员分隔符** `·`（U+00B7，UTF-8 完整两字节 `0xC2 0xB7`）。
 *
 * ── 这个判据回答什么 ────────────────────────────────────────────────
 * **帧拥有的槽，还是一个容器内部的存储位置？**
 *
 *   不含 `·` —— 帧拥有的槽：由声明创建，地址在帧生命周期内稳定 ⇒ 可以建、可以绑；
 *   含  `·` —— 成员键：它是**容器值内部的存储**，容器可被 `·new` 或任何整容器写
 *                **整体重建**，地址永不稳定 ⇒ 不建、不绑，每次按 key 现解析。
 *
 * **判据不是「谁声明的」，而是「它是不是一段路径」**（`vmkv/rwir.h` 早有此条，
 * 只是 `materialize_locals` 与绑定路径没照它执行）。此前帧准备给成员符号建零值
 * 并绑死地址，而 `struct·new` 的 `kvspaceCpTree` 会整体重建子树 ⇒ 绑定表里留着
 * 一具已不存在的 box，`println(p·x)` 读到 0 ——**写侧完全正确、读侧读旧地址**。
 *
 * ── 为什么按完整字节序列匹配 ────────────────────────────────────────
 * 只判首字节 `0xC2` 会把任何以它开头的其他字符也当成成员符；只判「含 `·`」又会
 * 把动态键 `base·*k` 与静态成员混在一起（后者另有一条判据，见 vmkv）。
 *
 * ── 唯一定义点 ──────────────────────────────────────────────────────
 * `vmkv_rwir_key_is_member` **转调本函数**（vmkv 依赖 binrwir，反向依赖不存在）。
 * engine 与 vmbin 也调它。任何地方再写第二份扫描，就是「同一事实两个来源」。
 */
bool binrwir_symbol_name_is_member(runtimec_string_view_t name);

/*
 * builder 生命周期：可写 —— finalize 成功 ——> 已定稿。定稿后所有 add_* 与再次
 * finalize 都返回 INVALID；reset 回到空的可写状态并保留已分配容量；destroy 只释放
 * builder 自身存储，不影响已经产出的 Program。
 */
runtimec_status_t binrwir_program_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_program_identity_t *identity,
    binrwir_program_builder_t **output_builder);
runtimec_status_t
binrwir_program_builder_reset(binrwir_program_builder_t *builder);
runtimec_status_t binrwir_program_builder_destroy(
    binrwir_program_builder_t *builder);

/*
 * 登记签名参数。output_parameter_index 是该参数在所属方向内的序号，也就是
 * PARAMETER_READ / PARAMETER_WRITE 引用要填的 slot。parameter_type 必须是收敛后的
 * 具体 langtype：非空、非 union，判据只有 binrwir_signature_validate_parameter_type
 * 一处。同一方向内 argidx 不得重复，该规则由 finalize 的签名校验统一裁定。
 */
runtimec_status_t binrwir_program_builder_add_parameter(
    binrwir_program_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t argument_index, runtimec_string_view_t parameter_type,
    uint32_t *output_parameter_index);

/* 登记一条别名规则；字段合法性同样复用 signature 的判据。 */
runtimec_status_t binrwir_program_builder_add_alias_rule(
    binrwir_program_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier);

/*
 * 登记符号：名字 + 语言类型。函数名与局部变量名都**不存路径**（`/lib/`、帧目录由
 * 调用入口拼）。
 *
 * 为什么要带类型：局部槽必须**先以正确长度存在**——解析接口只能解析已存在的 key，
 * 而执行器只接受与当前 body 等长的写入，零长槽写不进任何真实值。谁创建槽谁就得
 * 知道槽多大：vmbin 没有 host，route 有 host 却不该有第二份类型事实源，类型只能
 * 来自 Program。
 *
 * 冲突：同名重复登记且**类型相同**是幂等的，返回同一个下标；同名但类型不同是
 * **非法 Program**，当场返回 INVALID——不去猜哪个精度对。读取位置上出现的局部，
 * 其类型就是它被写的类型，由 encoder 判定后写入，Program 只存结果。
 *
 * 下标规范化：插入期返回 builder 内的序号，finalize 按名字的字节字典序把符号池
 * 重排成 Program 的规范顺序，并同步改写全部 LOCAL / FUNCTION / STATIC 参数项的 slot。
 * 因此同一个函数体无论 encoder 以什么顺序登记符号，产出的 Program 都相同（编译缓存
 * 才可复用）；而 add_symbol 返回的下标只在 builder 内有效——拿到直接填进参数项，
 * 不要在 finalize 之后还拿它跟 Program 里的 slot 比对。
 */
runtimec_status_t binrwir_program_builder_add_symbol(
    binrwir_program_builder_t *builder, runtimec_string_view_t name,
    runtimec_string_view_t langtype, uint16_t *output_symbol_index);

/*
 * 登记字面量：wire_bytes 是该槽已编码的值字节（XValue head + body + attr），
 * langtype 是同一个值的具体 langtype。LITERAL 参数项的 payload 就是这里返回的池
 * 下标。字节与类型文本都借用至 finalize，之后 Program 自持。
 */
/*
 * `wire_bytes` 是否恰好是一条 **None 值**：head 自洽 + 空 body + 空 attr。
 *
 * **这是「空 langtype 是否合法」的唯一判据**，三个调用点共用：
 *   `binrwir_program_builder_add_literal`（字面量登记）、
 *   `binrwir_program_validate`（Program 完整性）、
 *   `binrwir_signature_validate` 的空类型分支（经参数项交叉）。
 *
 * None 的定义见 spec `03-类型系统/03-种类-代码与特殊值.kv`「None 判据」：
 * 空 body + wire 无 langtype + key 不是目录。本谓词只回答 wire 那一半（空 body、
 * 空 attr、head 自洽）；「是不是引用」「是不是目录」由调用方按各自上下文补。
 *
 * 判据全部取自线格式访问器，本模块不写偏移算术。
 */
bool binrwir_literal_is_none_value(runtimec_bytes_view_t wire_bytes);

runtimec_status_t binrwir_program_builder_add_literal(
    binrwir_program_builder_t *builder, runtimec_bytes_view_t wire_bytes,
    runtimec_string_view_t langtype, uint32_t *output_literal_index);

/*
 * 登记一条 instruction。参数 span 按 binrwir_argument_validate 校验并被复制进
 * Program 参数池；request 的读区、写区在池内相邻存放，顺序不变。
 */
runtimec_status_t binrwir_program_builder_add_instruction(
    binrwir_program_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier);

/* 产出不可变 Program 并定稿 builder；Program 与 builder 各自独立销毁。 */
runtimec_status_t binrwir_program_builder_finalize(
    binrwir_program_builder_t *builder, binrwir_program_t **output_program);

_Static_assert(sizeof(binrwir_program_identity_t) == 32U,
               "binrwir program identity must remain 32 bytes");

#ifdef __cplusplus
}
#endif

#endif
