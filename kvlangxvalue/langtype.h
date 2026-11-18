#ifndef KVLANG_XVALUE_LANGTYPE_H
#define KVLANG_XVALUE_LANGTYPE_H

/*
 * 组件：kvlangxvalue/langtype
 * 作用：langtype 文本 → **形态描述符**的唯一解析点。
 * 联系：`kvlangxvalue/member_key_text` 只调这里决定「这个 langtype 能不能作成员键」；
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
 *   | `vmbin/temp_output.c` | 打印取类 |
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
 * 调用约定：对 `kvlangXvalueLangtypeForm` 用**不带 `default` 的 `switch`**。
 * `-Wswitch` 因此成为一张检查网——将来新增形态时，所有调用点在编译期报警。
 * 带 `default` 会把它变成运行期静默，正是要避免的。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 解析用的只读文本视图：借用调用方内存，不拥有、不释放。
   size 为 0 时 data 可以为 NULL。 */
typedef struct kvlangXvalueStringView {
    const char *data;
    size_t size;
} kvlangXvalueStringView;

static inline kvlangXvalueStringView kvlangXvalueStringViewMake(const char *data,
                                                               size_t size) {
    kvlangXvalueStringView view;
    view.data = data;
    view.size = size;
    return view;
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一个 langtype 的形态。**六选一，没有「无」**（见头注释）。
 *
 * 判据见 `kvlangXvalueLangtype.c` 的 `classify`，逐条可查。这里只列语义：
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
typedef enum kvlangXvalueLangtypeForm {
    KVLANG_XVALUE_FORM_SCALAR = 0,
    KVLANG_XVALUE_FORM_CHARACTER,
    KVLANG_XVALUE_FORM_STRING,
    KVLANG_XVALUE_FORM_ARRAY,
    KVLANG_XVALUE_FORM_TUPLE,
    /*
     * map 类型：`'<formatter>'<value>`（issue #631）。**键侧是 formatter**，
     * 不是「键的类型」——它规定「什么值格式化成什么路径段文本」，因为 member
     * key 永远活在路径系统里、从不进入任何 XValue 的 body。
     *
     * 改名前是 `K·V`，那时 map 落在 `UNRECOGNIZED`（靠「不是已知 form」识别）；
     * 现在它是一等 form，所有 `switch (form)` 都必须为它写明行为。
     */
    KVLANG_XVALUE_FORM_MAPPING,
    KVLANG_XVALUE_FORM_UNRECOGNIZED
} kvlangXvalueLangtypeForm;

/* 裸标量的分类；只有 `form == SCALAR` 时有效。 */
typedef enum kvlangXvalueLangtypeScalar {
    KVLANG_XVALUE_SCALAR_NONE = 0, /* 非标量 */
    KVLANG_XVALUE_SCALAR_SIGNED_INTEGER,
    KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER,
    KVLANG_XVALUE_SCALAR_FLOAT,
    KVLANG_XVALUE_SCALAR_BOOLEAN
} kvlangXvalueLangtypeScalar;

/* 字符编码；只有 `CHARACTER` / `STRING` 时有效。 */
typedef enum kvlangXvalueCharEncoding {
    KVLANG_XVALUE_CHAR_ENCODING_NONE = 0,
    KVLANG_XVALUE_CHAR_ENCODING_UTF8,
    KVLANG_XVALUE_CHAR_ENCODING_UTF32,
    KVLANG_XVALUE_CHAR_ENCODING_ASCII
} kvlangXvalueCharEncoding;

/*
 * 解析结果。字段的有效性由 `form` 决定，不在别的字段上重复编码。
 *
 * **判「值是什么种类」用 `base`，不要用 `form` 或文本首字节。**
 * `[1]char/utf32`（字符串字面量的声明类型）是 ARRAY 形态，按 `form` 判会把它当成
 * 「不是字符」；按首字节（`[`）判同样错。种类在 `base`（剥掉形状段后的 `char/utf32`），
 * 形状在 `dimension_count` 与原文前缀。这一条已被三个人各踩一次。
 */
typedef struct kvlangXvalueLangtype {
    /* 基种类文本（`int64`、`char/utf8`…），指向入参内部，不拷贝。 */
    kvlangXvalueStringView base;
    kvlangXvalueLangtypeForm form;
    kvlangXvalueLangtypeScalar scalar;               /* form == SCALAR */
    uint32_t scalar_width;                           /* form == SCALAR；其余 0 */
    /*
     * 只在 CHARACTER / STRING 填。**`[N]char/X` 是 ARRAY 形态，这里不自填**——
     * 拿它判「这是不是字符序列」会得到 0。
     */
    kvlangXvalueCharEncoding char_encoding; /* CHARACTER / STRING */
    /* 形状维度段个数；无前缀 0。`[]char/utf8` 与 `[.]char/utf8` 都是 1。 */
    int32_t dimension_count;
} kvlangXvalueLangtype;

/*
 * 解析 langtype 文本。
 *
 * 输入是**值槽的完整 langtype 文本**，含维度前缀（`[]char/utf32`、`[4]int64`）；
 * 调用方不剥前缀——剥法是本函数的一部分，由调用方各写一遍就是第二条规则。
 *
 * 输出先清零；失败也清零。空文本是**畸形**（`INVALID`），不是「不约束」：
 * 不约束的语义只属于签名层，本函数回答的是「这个值是什么形态」。
 */
void kvlangXvalueLangtypeParse(kvlangXvalueStringView text,
                             kvlangXvalueLangtype *output);

#ifdef __cplusplus
}
#endif

#endif
