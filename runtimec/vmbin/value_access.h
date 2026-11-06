#ifndef VMBIN_VALUE_ACCESS_H
#define VMBIN_VALUE_ACCESS_H

/*
 * 组件：vmbin/value_access
 * 作用：阶段 1 的唯一值访问面——把参数项解析成「值体地址 + 当前长度」或
 *       「Program 池内字面量」，并在同一条路径上读写。
 * 联系：vmbin/interpreter 的 core 规则与 builtin handler 都只经这里取写值。
 * 约束：以 shm 后端为准——值体就在进程内，按地址直接读写，不做 key 往返。
 *       头/体几何全部经合同：基址 → 值体用 runtimec_xvalue_body_address，值体 →
 *       当前长度用 runtimec_value_body_length，本组件不写偏移算术。长度 0 是合法
 *       的空值体（None），地址的有效性由来源证明而不是由长度判断。写只允许与
 *       当前值体等长：截断、静默扩容、回退按 key 都被禁止，扩容只有存储层能做。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "contract/diagnostic_band.h"
#include "binrwir/execution_frame.h"
#include "contract/error.h"
#include "contract/kvspace_access.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 值访问失败的分类，经 `error.code` 报出。
 *
 * 放头文件而不是 .c：测试必须能引用具体码值做断言，并证明它**没有**被抄进
 * `exit.detail_code`——那里只放解释器自己的分类。只断言「非零」时，两个命名
 * 空间同值的缺陷照样绿，等于没测。
 *
 * 编号只增不改：已报出的码值会出现在下游日志里。
 */
typedef enum vmbin_value_access_diagnostic {
    VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST = UINT32_C(0x4400) + 1U,
    VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE = UINT32_C(0x4400) + 2U,
    VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT = UINT32_C(0x4400) + 3U,
    VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL = UINT32_C(0x4400) + 4U,
    VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY = UINT32_C(0x4400) + 5U,
    VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH = UINT32_C(0x4400) + 6U,
    VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE = UINT32_C(0x4400) + 7U,
    VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE = UINT32_C(0x4400) + 8U,
    /* 参数带 dereference=1，但它指向的值 head 不是 Ptr（没有可跟的一跳）。 */
    VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER =
        UINT32_C(0x4400) + 9U
} vmbin_value_access_diagnostic_t;

/*
 * 段断言：本模块的码必须落在自己的段内。
 *
 * 迁移前五个模块各自从 1 开始编号，同一个 `3` 同时是「解释器的指令非法」与
 * 「println 的值格式非法」。那造成过一整晚的误判——**不是细心能解决的，是
 * 命名空间缺失**。这两条断言把「漏加偏移」变成编译期错误。
 */
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER must stay inside its diagnostic band");
/* 本枚举没有 `NONE = 0`：值访问不产生「没有诊断」的出口。 */


/*
 * 一个参数项解析出的位置：可读写值体，或只读的池内字面量。
 *
 * `is_whole_wire` 由本函数回答，**调用方不得推断**：`write_bytes == NULL` 表示
 * 「不可写」，那是另一件事。今天恰好看不出差别（置 NULL 的两种引用正好都给整条
 * wire），但一旦出现「只读且 body 粒度」的引用种类，用不可写性去推就会把 body
 * 当成 wire 再剥一次头，读到越界内存——而且多数用例仍不崩，只是值错了。
 */
typedef struct vmbin_value_location {
    const uint8_t *read_bytes;
    uint8_t *write_bytes;
    size_t size;
    bool is_whole_wire;
} vmbin_value_location_t;

/*
 * 解析一个参数项。字面量与内联绑定给出池内只读视图（`is_whole_wire = true`，
 * 指向完整 wire）；其余给出可直接读写的值体与它当前的长度（`is_whole_wire =
 * false`）。写位置不可用（字面量/内联绑定）时 write_bytes 为 NULL。
 */
runtimec_status_t vmbin_value_access_locate(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

/*
 * 读一个参数项的值字节到 output->data。容量不足时返回 INSUFFICIENT_CAPACITY，
 * 并把 output->size 置成所需字节数，调用方扩容后重试。
 */
runtimec_status_t vmbin_value_access_read(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);

/*
 * 写一个参数项的值字节。长度必须与当前值体一致：放不下、需要改长度或字面量目标
 * 一律 INVALID。只有全部字节写完之后才返回 SUCCESS。
 */
runtimec_status_t vmbin_value_access_write(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error);

/*
 * 一个参数项的**声明** langtype（具体类型，不含并集）。四个来源按引用种类取：
 *   LITERAL       → 字面量池的 langtype
 *   LOCAL/STATIC  → 符号池的 langtype
 *   PARAMETER_*   → 签名的形参类型表
 *   FUNCTION      → 不是值，没有类型
 *
 * 与「值上带的 langtype」不是一回事：槽可以是 None（空 body、wire 里没有
 * langtype 记录），那时只有声明类型答得出这个参数该按什么宽度解释。需要元素
 * 宽度（数组下标算术）的调用方要的是本函数，不是 wire 里的那份。
 *
 * 取不到返回 UNSUPPORTED，并把 output_langtype 清成空视图——**不猜**。调用方
 * 据此失败封闭，而不是当成「不约束」继续走。
 */
runtimec_status_t vmbin_value_access_langtype(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, runtimec_error_t *error);

/*
 * **值的形态**：普通值 / 指针 / 扩展。
 *
 * 只有这三种；它们由 **langtype 首字符**（`*` / `@` / 其它）决定——head 里已经
 * 没有 ptr 字节了（见 kvspace `xvalue_head.h` 的新布局）。所以这里不读任何 head
 * 字段，也不引用 kvspace 的 `KVSPACE_REF_*` 常量（它们随 head 一起退役）。
 */
typedef enum vmbin_value_access_form {
    VMBIN_VALUE_ACCESS_FORM_VALUE = 0,
    VMBIN_VALUE_ACCESS_FORM_POINTER = 1,
    VMBIN_VALUE_ACCESS_FORM_EXTENSION = 2
} vmbin_value_access_form_t;

/*
 * 一个参数项值的**形态**（`VMBIN_VALUE_ACCESS_FORM_*`）。
 *
 * 为什么必须从 head 读、不能从 langtype 串里找：layout 把 `*`/`@` 前缀**剥进
 * head 的 ref 字节**，wire 的 langtype 串里根本没有那个前缀（spec
 * `03-类型系统/07-文法与合法性.kv`）。所以「这个参数是不是指针」只有一条
 * 事实源——head 的这一位。靠「body 长得像不像 key」去猜是另一种判据，而它
 * 在合法数据上就会答错。
 *
 * 两条路径都覆盖：整条 wire（字面量、内联绑定）直接从借出的字节读；body 粒度
 * 的绑定先经合同的逆几何入口 `runtimec_value_body_xvalue_address` 退回值头基址，
 * 本函数不写偏移算术。
 *
 * 读不到（坏 wire、非法的 storetype、地址不可逆）返回 UNSUPPORTED 并把输出清零
 * ——**不猜**。读到就返回 SUCCESS，output_reference 是那三个已知取值之一。
 */
runtimec_status_t vmbin_value_access_form(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error);

/*
 * ── body 粒度：上面三条的并列版本 ────────────────────────────────────
 *
 * 为什么要有第二组：上面三条对**字面量**给的是整条 wire（16 字节 head + body +
 * attr），对地址绑定给的是 body——同一个函数按引用种类承诺了两种东西。这不是
 * 「实现瑕疵」，是两种真实需求并存：
 *
 *   wire 语义  值的完整存储区间。`kvspaceSetValue` 收的就是完整 XValue，watch
 *              按整条比较，它们要 wire。
 *   body 语义  值体本身。**所有算子语义都只认 body**：元素宽、下标算术、标量
 *              解码、copy 的值——它们要 body。
 *
 * 于是分成两组名字，各自回答一个问题，调用方不必在拿到之后自己判断「这次是
 * 哪一种」。`_body` 三条对任何引用种类都返回 body：字面量经合同的
 * `runtimec_xvalue_body_address` + `runtimec_value_body_length` 剥出，其余引用
 * 本来就是 body。
 *
 * 写：必须落在真实存储上。字面量没有可写位置，写它一律 INVALID——`_body` 不放宽
 * 这一条，它只改变「读出来的是哪一段」。
 */
runtimec_status_t vmbin_value_access_locate_body(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

/*
 * **写方向的定位**：与 `_locate_body` 的差别只有一条——目标是 **STATIC 绝对 key
 * 且当前不存在**时，先按写参声明的 langtype 建零值（复用 `ensure_zero_value`），
 * 再定位。
 *
 * 为什么需要：`VMBIN_HANDLER_LOCATE_WRITE` 先要一个可写位置、算完再写。目标不存在
 * 时它无从定位 ⇒ `UNBOUND_SLOT`（最小复现 `1 + 2 -> /sum`）。#632 定过「写指令
 * 创造 key、读指令不创造」，但创建此前只落在整值写那两条路径上。
 *
 * **只对 STATIC 生效**：LOCAL 的定宽槽由帧准备物化、变长槽由 copy /
 * `write_new_value` 创建。**读方向仍用 `_locate_body`，绝不创建**。
 */
runtimec_status_t vmbin_value_access_locate_write(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);
/*
 * **指令不创造 key，创建源在源码里。**
 *
 * 创建动作按值的种类分两种，但都写在源码中：
 *   - 容器：`map·new` / `struct·new` / `compactarray·new` / `scatterarray·new`；
 *   - 标量：类型声明，`v:int32 = add(a, 4)` 里的 `v:int32` —— **声明建 key，
 *     赋值只写**。
 *
 * 帧准备阶段的 `ensure_zero_value` 只是**执行这条声明的手段**，不是创建源：
 * 没有声明就没有创建，实现手段不能反过来当规则用（按实现写规则，下一个人就会
 * 以为「没声明也能建」，然后把写路径创建再发明一遍）。
 *
 * 所以这一层没有、也不该有「写路径顺手建一个」的入口：写目标在执行期仍不存在
 * （payload 仍是 NONE）时，与读一样按未解析失败封闭。
 *
 * **绝对 key 的存在性是运行时前提，不是静态保证。** layout 对它什么都不做——
 * 既不要求声明，也不去读存储核对；碰不到就由 runtime 失败封闭（本条实现）。
 * 别在这里加「先检查一下再执行」：检查与执行之间还会有变化，静态保证是假保证。
 */
runtimec_status_t vmbin_value_access_read_body(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);
runtimec_status_t vmbin_value_access_write_body(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error);

/*
 * **创建型算子的第二步：绑定刚建出来的值。**
 *
 * 容器/结构体局部在帧准备阶段不建也不绑（它们的创建源是源码里的 `·new`），所以
 * 「建值」与「绑定」必须在同一处完成：值写进存储之后立刻解析地址并写进绑定表。
 * 只建不绑的结果是存储里有、表里没有——执行期读它就是 `UNBOUND_SLOT`，而那条
 * 硬错本来是留给「`·new` 之前就访问」的，不能被实现缺陷借用。
 *
 * 只有 LOCAL 槽需要绑：STATIC 目标每次按 key 解析，绑定表里没有它的槽（返回
 * SUCCESS 表示「该种类没有这一步」，不是失败被吞）。
 *
 * `target_key` 是本次写入用的绝对 key；绑定表**自己复制**它，调用方可以立即
 * 释放自己的缓冲。
 */
runtimec_status_t vmbin_value_access_bind_write_target(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_key_view_t target_key,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
