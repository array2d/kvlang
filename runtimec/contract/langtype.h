#ifndef RUNTIMEC_CONTRACT_LANGTYPE_H
#define RUNTIMEC_CONTRACT_LANGTYPE_H

/*
 * 组件：contract/langtype
 * 作用：langtype 文本 → **形态描述符**的唯一解析点。
 * 联系：`contract/member_key_text` 只调这里决定「这个 langtype 能不能作成员键」；
 *       本组件不依赖任何上层组件，也不访问存储。
 * 约束：纯函数、不分配；输出参数先清零，失败也清零，不留陈旧值。
 *
 * ── 为什么要有这个组件 ────────────────────────────────────────────────
 *
 * 「langtype 文本 → 基种类 / 宽度 / 维度」这条规则在今天有**四份实现**：
 *
 *   | 位置 | 用途 |
 *   |---|---|
 *   | `vmbin/handlers/scalar.c` | 数值算子取类型 |
 *   | `vmbin/handlers/container.c` | 容器元素宽 |
 *   | `vmbin/temp_println.c` | 打印取类 |
 *   | `contract/kvspace_access.c` | 零值宽度 |
 *
 * 本文件是**唯一真相的候选 home**。随后的统一任务把上面四份改成调它、然后删掉
 * 那四张表；本步只新增，不动它们（会碰别人在飞的文件）。
 *
 * ── 失败只能用状态码表达 ──────────────────────────────────────────────
 *
 * **没有 `FORM_NONE`。** 语法畸形返回 `INVALID`，不产出 form；其余一律成功，
 * `form` 必是下面六个之一。因此 `form` 字段**只有成功时才可信**。
 *
 * 调用约定：对 `runtimec_langtype_form_t` 用**不带 `default` 的 `switch`**。
 * `-Wswitch` 因此成为一张检查网——将来新增形态时，所有调用点在编译期报警。
 * 带 `default` 会把它变成运行期静默，正是要避免的。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一个 langtype 的形态。**六选一，没有「无」**（见头注释）。
 *
 * 判据见 `runtimec_langtype.c` 的 `classify`，逐条可查。这里只列语义：
 *
 *   SCALAR       裸标量：`bool` + 十个定宽数字（int8..64 / uint8..64 / float32/64）
 *   CHARACTER    裸 `char/<enc>`：单字符
 *   STRING       一个变长维度段 + `char/<enc>`：字符串
 *   ARRAY        有维度前缀但不是 STRING：定长数组、`[]int32`、多维
 *   TUPLE        `[scalar,…]` 且其后无 base：标量元组键
 *   UNRECOGNIZED 其余（time、/lib/…、mapexpr、stringkeymap…）
 *
 * **`UNRECOGNIZED` 不是「可以当键」的意思**——它只说「这不是畸形，但不在本表」。
 * `member_key_text` 必须拒绝它。读到「解析成功」不要理解成「可用」。
 */
typedef enum runtimec_langtype_form {
    RUNTIMEC_LANGTYPE_FORM_SCALAR = 0,
    RUNTIMEC_LANGTYPE_FORM_CHARACTER,
    RUNTIMEC_LANGTYPE_FORM_STRING,
    RUNTIMEC_LANGTYPE_FORM_ARRAY,
    RUNTIMEC_LANGTYPE_FORM_TUPLE,
    RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED
} runtimec_langtype_form_t;

/* 裸标量的分类；只有 `form == SCALAR` 时有效。 */
typedef enum runtimec_langtype_scalar {
    RUNTIMEC_LANGTYPE_SCALAR_NONE = 0, /* 非标量 */
    RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER,
    RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER,
    RUNTIMEC_LANGTYPE_SCALAR_FLOAT,
    RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN
} runtimec_langtype_scalar_t;

/* 字符编码；只有 `CHARACTER` / `STRING` 时有效。 */
typedef enum runtimec_langtype_char_encoding {
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_NONE = 0,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII
} runtimec_langtype_char_encoding_t;

/* 解析结果。字段的有效性由 `form` 决定，不在别的字段上重复编码。 */
typedef struct runtimec_langtype {
    /* 基种类文本（`int64`、`char/utf8`…），指向入参内部，不拷贝。 */
    runtimec_string_view_t base;
    runtimec_langtype_form_t form;
    runtimec_langtype_scalar_t scalar;               /* form == SCALAR */
    uint32_t scalar_width;                           /* form == SCALAR；其余 0 */
    runtimec_langtype_char_encoding_t char_encoding; /* CHARACTER / STRING */
    /* 形状维度段个数；无前缀 0。`[]char/utf8` 与 `[.]char/utf8` 都是 1。 */
    int32_t dimension_count;
} runtimec_langtype_t;

/*
 * 解析 langtype 文本。
 *
 * 输入是**值槽的完整 langtype 文本**，含维度前缀（`[]char/utf32`、`[4]int64`）；
 * 调用方不剥前缀——剥法是本函数的一部分，由调用方各写一遍就是第二条规则。
 *
 * 输出先清零；失败也清零。空文本是**畸形**（`INVALID`），不是「不约束」：
 * 不约束的语义只属于签名层，本函数回答的是「这个值是什么形态」。
 */
runtimec_status_t runtimec_langtype_parse(runtimec_string_view_t text,
                                          runtimec_langtype_t *output);

#ifdef __cplusplus
}
#endif

#endif
