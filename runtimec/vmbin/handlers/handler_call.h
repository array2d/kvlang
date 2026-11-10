#ifndef VMBIN_HANDLERS_HANDLER_CALL_H
#define VMBIN_HANDLERS_HANDLER_CALL_H

/*
 * 组件：vmbin/handlers/handler_call
 * 作用：把「注册表里的 handler」与「直指类型化指针的逻辑函数」之间那一层样板宏化。
 * 联系：`container` / `string` / `scatter` / `scalar` 各模块用这里的宏一行产出一个
 *       handler；条目表仍由各模块手写（指定初始化器）。
 * 约束：
 *   - **宏只做搬运**：取读参 → 核对长度 → 转型 → 调逻辑函数。不做语义判断、
 *     不按 langtype 分叉。
 *   - 逻辑函数**成功时无 return**（返回 void），失败由宏置 `output_exit`。
 *   - 一个宏只服务一种参数形状。**不写万能变参宏**：那会让「哪个 handler 有几个
 *     参数」从代码里消失。
 *   - 本文件**不认识任何业务类型**（`string_text_t` 之类）；吃文本的变体在
 *     `handler_call_text.h` 里。
 *
 * **本文件同时是两类原语的落点**，这是有意的：
 *   1. **定位/长度类**（`LOCATE_READ` / `REQUIRE` / 各 `_TYPED` 与 `_LOCATION` 族）——
 *      把参数项换成可读写的值体位置；
 *   2. **创建类**（`vmbin_handler_write_new_value`）——造 wire 并整条写进目标 key。
 * 创建型算子**只能走这里**，不许在各模块各写一份「造 wire + 写 key」。下一个人看到
 * 通用层里有 `kvspaceSetValue` 不必怀疑层被穿透：那是所有创建型算子的共同语义，
 * 与 string/container 无关，放模块里才是重复。
 *
 * 为什么需要这一层：registry 的 handler 签名是 `(frame, instruction, exit)`，而
 * 逻辑函数要的是已定位、已核长的类型化指针。把这段转换写一次，三百个条目各自一行
 * 宏调用；否则每个 handler 都要重复十行样板，那正是行数超限的来源。
 *
 * ── 选宏前先数读参 ────────────────────────────────────────────────────
 *
 * **每个宏只接受一种操作数形状**，形状不符会在装载期报 `0x4A01 WRONG_ARITY`。
 * 写 handler 时先数「这条指令有几个读参、几个写参」，再照下表选宏——
 * 不要凭名字猜（`_SCATTER_CROSS` 与 `_SCATTER_EXPAND` 就因此被混用过两次）。
 *
 *   | 宏                                  | 读 | 写 | 备注                          |
 *   |-------------------------------------|----|----|-------------------------------|
 *   | `_UNARY_TYPED` / `_UNARY_TYPED_EXIT`   | 1 | 1 | 类型化指针                  |
 *   | `_BINARY_TYPED` / `_BINARY_TYPED_EXIT` | 2 | 1 | **两读参各有类型**          |
 *   | `_TERNARY_TYPED` / `_TERNARY_TYPED_EXIT` | 3 | 1 | 三读参各有类型            |
 *   | `_UNARY_LOCATION`                   | 1 | 1 | 位置级（长度运行期决定）      |
 *   | `_BINARY_LOCATION`                  | 2 | 1 | 位置级                        |
 *   | `_TERNARY_LOCATION`                 | 3 | 1 | 位置级                        |
 *   | `_UNARY_ARRAY`                      | 1 | 1 | 读参是 compact 数组           |
 *   | `_ARRAY_INDEX`                      | 2 | 1 | 数组 + 下标                   |
 *   | `_ARRAY_INDEX_WRITE`                | 3 | 1 | 数组 + 下标 + 新值（同槽）    |
 *   | `_CREATE_NONE`                      | 0 | 1 | 纯创建                        |
 *   | `_CREATE_ONE`                       | 1 | 1 | 一读参创建                    |
 *   | `_CREATE_TWO`                       | 2 | 1 | 两读参创建                    |
 *   | `_CREATE_TERNARY`                   | 3 | 1 | 三读参创建                    |
 *   | `_CREATE_VARIADIC`                  | 不定 | 1 | 读参个数运行期才知道        |
 *   | `_SCATTER_READ_SCALAR`              | 1 | 1 | 容器 → 标量                   |
 *   | `_SCATTER_CREATE`                   | 0 | 1 | 空容器                        |
 *   | `_SCATTER_READ_WRITE`               | 2 | 1 | 容器 + 下标 → 元素            |
 *   | `_SCATTER_WRITE_READS`              | ≥2 | 1 | 容器 + 若干读参（`set`/`append`） |
 *   | `_SCATTER_CROSS`                    | ≥2 | 1 | **跨容器搬运**（源 + 目标）  |
 *   | `_SCATTER_EXPAND`                   | 1 | 1 | **单容器转换**（散列 ↔ compact） |
 *
 * `handler_call_text.h` 的文本族同理，见该文件顶部的表。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"

/* **下标个数上限**：一次下标访问最多带这么多个下标，也即数组最多这么多维。
 *
 * 它是同一个数的两个名字所指——`indices[]` 能放几个下标，`dimensions[]` 就能
 * 描述几维。所以两处用一个常量，**不留两个同义常量**。
 *
 * 多于它返回 UNSUPPORTED，**不静默截断**：截断会让 `[1,2,3,4,5]int64` 被当成
 * 前四维算偏移，写到一个看起来合法、实际错误的位置上。
 *
 * 定义在这里而不是 `container.h`：使用它的两个 handler 宏在本文件，而
 * `container.c` 反向包含本文件。放反了就得让本文件包含 `container.h`，
 * 那是循环包含。 */
#define VMBIN_HANDLER_MAX_INDEX_COUNT 8

/* 长度未定的一维（`[]T`，等价 `[.]T`）。解析时记成它，**不是** 0 或 1：
 * 需要确定长度的调用方（多维下标线性化）看到它必须失败，不能猜一个值。 */
#define RUNTIMEC_CONTAINER_DIMENSION_UNBOUNDED INT64_C(-1)
#include <kvspace/kvspace_xvalue.h>

#include "contract/diagnostic_band.h"
#include "contract/kvspace_interface.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>
#include "vmbin/frame_target_key.h"
#include "vmbin/value_access.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 本模块的失败细节码。**编号只增不改**：已发出的码会出现在 `exit.detail_code` 里。
 *
 * 结构化退出没有文本通道（`execution_exit.h` 的 24 字节断言），所以「为什么失败」
 * 只能靠码。这几个码属于**搬运层**，与具体算子无关。
 */
/* 创建型算子的缓冲尺寸：langtype 文本与目标 key。 */
#define VMBIN_HANDLER_LANGTYPE_CAPACITY 256U
#define VMBIN_HANDLER_KEY_CAPACITY 512U

typedef enum vmbin_handler_call_diagnostic {
    VMBIN_HANDLER_CALL_DIAG_NONE = 0,
    /* 读参/写参个数与算子形状不符。 */
    VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY = 0x4A01,
    /* 参数项定位失败（未绑定、字面量不可写、地址不可逆）。 */
    VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED = 0x4A02,
    /*
     * 值体字节数与逻辑函数要求的类型宽度不符。
     *
     * 这条必须是**可诊断的失败**而不是直接转型：Program 说 8 字节、body 实际
     * 4 字节时，转成 `int64_t *` 再解引用就是越界读——那是段错误，不是错误码。
     */
    VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH = 0x4A03,
    /* 写参落到只读值（字面量）：没有可写位置，下一步就是空指针写。 */
    VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY = 0x4A04,
    /* 造 wire 或写存储失败。 */
    VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED = 0x4A05,
    /*
     * `member_key_text` 的四个诊断在这里的对应码。
     *
     * **为什么不透传 `contract/member_key_text` 的 1..5**：那是另一个域。
     * `detail_code` 是全进程一张段表（`contract/diagnostic_band.h`），把 1..5
     * 塞进来就是「一个字段两种命名空间」——看到 3 分不出是 contract 的
     * `UNSUPPORTED_LANGTYPE` 还是执行窗口的 3。转码是这一步的职责。
     *
     * **为什么不复用 `LENGTH_MISMATCH`（0x4A03）**：那条的意思是「值体宽度与
     * 逻辑函数要求的宽度不符」，是**已经拿到值之后**的判定；容量不足是**还没
     * 写完**。两者修法不同（改数据 vs 给大缓冲），合成一条会把人引错方向。
     */
    VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY = 0x4A06,
    /*
     * 该值的 langtype 不能作成员键：标量/字符/字符串之外的一切。
     *
     * 「取不到声明 langtype」也报这条——它同样回答「这个操作数能不能作键」，
     * 报 `LOCATE_FAILED`（0x4A02）是错的：值定位到了，只是没有类型。
     */
    VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE = 0x4A07,
    /* 值体宽度与该 langtype 应有的宽度不符（标量定宽、utf32 非 4 的倍数）。 */
    VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY = 0x4A08,
    /* 浮点键是 NaN。±Inf 合法（键文本 `inf`/`-inf`），不报此码。 */
    VMBIN_HANDLER_CALL_DIAG_NON_FINITE = 0x4A09,
    /*
     * 生成的键文本 + 父路径长度超出存储能表达的 key 上界（1023 字节）。
     *
     * **不复用 `INSUFFICIENT_CAPACITY`（0x4A06）**：两者含义相反的重试性。
     * 0x4A06 是「我的缓冲小、给我更大的就能成」；本码是「这个键存储根本装不下」，
     * 换多大的缓冲都不成——它是**不可重试**的失败。合成一条会让调用方无限重试。
     *
     * 上界的两处来源（都实读，见 `handler_call.h` 上方注释）：
     *   `runtimec/contract/kvspace_access.c` 的 `RUNTIMEC_KVSPACE_KEY_CAPACITY`
     *   （1024，栈缓冲尺寸）与 `kvspace-c/src/kvspace.c` 的 `>= 1024` 后端拒收。
     */
    VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG = 0x4A0A,
    /*
     * **超出运行时定长缓冲**（当前使用者：字符串算子，`STRING_TEXT_CODEPOINT_CAPACITY`）。
     *
     * ── 判据是「缓冲」，不是「字符串太长」──────────────────────────────
     *
     * 名字里的 `STRING` 说的是**第一个**撞上它的算子，不是判据的范围。判据是
     * 「这件事的容量由运行时的定长缓冲决定，而不是由数据本身决定」。将来别的
     * 定长缓冲撞上限（JSON、正则、任何 `T x[N]` 的栈容器）**复用本码**，
     * 不要再开 `0x4A0C`——那个数会被下一个人当成「又一种字符串问题」。
     *
     * **不复用 `INSUFFICIENT_CAPACITY`（0x4A06）**：理由与上一条 `KEY_TOO_LONG`
     * 逐字相同——0x4A06 是「我的缓冲小、给我更大的就能成」（可重试）；本码是
     * 「这个定长缓冲根本装不下，程序换多久都跑不了」（不可重试）。
     *
     * **也不复用 `LENGTH_MISMATCH`（0x4A03）**：那是「值体宽度与逻辑函数要求的
     * 宽度不符」，已经拿到值之后的判定。容量不足是还没写完。把它报成 0x4A03，
     * 用户会去改自己的数据宽度，而真正的原因是拼接结果太长。
     *
     * 上界来源：`vmbin/handlers/string_text.h` 的 `STRING_TEXT_CODEPOINT_CAPACITY`
     * （1024 码点）与 `vmbin/handlers/handler_call_text.h` 的
     * `VMBIN_HANDLER_CREATE_BUFFER_CAPACITY`（4096 字节 = 1024 × 4）。
     * **两个数必须对得上**：只改一边会得到一个说谎的上限——实测过，把码点上限
     * 提到 1024 而字节缓冲还是 512，130 个码点照样失败。
     */
    VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG = 0x4A0B
} vmbin_handler_call_diagnostic_t;

/* 段断言：本模块的码必须落在自己的段内（见 contract/diagnostic_band.h）。 */
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_NON_FINITE, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_NON_FINITE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG must stay inside its diagnostic band");

/*
 * 存储能表达的键文本长度上界（**不含结尾 NUL**）：**1023 字节**。
 *
 * ── 这不是「定长缓冲」，是「最大可能长度」的栈空间 ────────────────────
 *
 * `0x4501` 治的是**任意猜的数**（曾经的 64 字节 scratch），不是「有据可依的
 * 上界」。本常量不是猜的，有两个互相印证的外部来源：
 *
 *   1. `kvspace-c/src/kvspace.c:2796,3329`——后端的 Get 与 Set 两条路径都对
 *      `strlen(key) >= 1024` 做**语义拒收**。这是硬上限。
 *   2. `runtimec/contract/kvspace_access.c:23` 的 `RUNTIMEC_KVSPACE_KEY_CAPACITY`
 *      = 1024，是拼接完整 key 的**栈缓冲尺寸**，必须容得下任何后端接受的 key。
 *
 * 两处独立给出同一个数，所以 1023 是**系统真实限制推出来的**，不是选出来的。
 * 分配 `1023 + 1` 字节栈空间因此相当于「按最大可能长度开缓冲」，与
 * 「拍一个 64 字节试试看」是两件事——后者在超限时静默出错，前者不可能超限。
 *
 * ── 超限怎么办 ────────────────────────────────────────────────────────
 *
 * `member_key_text` 若回报所需长度 > 本上界（理论上不该发生：完整 key 的父路径
 * 只会更长），报 `KEY_TOO_LONG`，**不截断、不降级**——截断会写到一个看起来像、
 * 实际不是的键上。
 */
#define VMBIN_HANDLER_KEY_TEXT_MAXIMUM UINT32_C(1023)


/*
 * thunk 的失败出口：只做一件事——把结构化 exit 置成 ERROR。
 * 要让人一眼区分成因，用 `detail_code`（见上面的枚举）。
 */
static inline void vmbin_handler_fail(binrwir_execution_exit_t *output_exit,
                                      runtimec_status_t status,
                                      uint32_t detail_code,
                                      uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = detail_code;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
}

/*
 * 取一个读参的位置；失败即置 exit 并 `return`。
 *
 * `do { ... } while (0)` 是 C 的既定写法：让它在 `if` 后面也当成一条语句。
 */
#define VMBIN_HANDLER_LOCATE_READ(frame, instruction, ordinal, location,           \
                                  output_exit)                                     \
    do {                                                                           \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_locate_body(                    \
            (frame), &(instruction)->read_arguments.data[(ordinal)], (location),   \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

/* 同上，写参。 */
#define VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, ordinal, location,          \
                                   output_exit)                                    \
    do {                                                                           \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        /* **写方向用 `_locate_write`**：STATIC 绝对 key 尚不存在时先创建
         * （`1 + 2 -> /sum` 那种形态）；读方向仍用 `_locate_body`，绝不创建
         * （#632 的反向对照）。LOCAL 槽不由此路径创建——定宽由帧准备物化、
         * 变长由 copy / `write_new_value` 建。 */                        \
        vmbin_handler_status_ =                                                   \
            vmbin_value_access_locate_write(                                       \
                (frame), &(instruction)->write_arguments.data[(ordinal)],          \
                (location), &vmbin_handler_error_);                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)


/*
 * 写参必须真的可写。
 *
 * `locate_body` 对**字面量**给的 `write_bytes` 是 NULL（值在池里、只读），而类型化
 * 宏下一步就是 `*out = …`——不查就是**空指针写**，崩溃而不是可诊断失败。
 *
 * 这条检查在三处落地，理由同一个：`_TYPED` 族（写参）、`_UNARY_ARRAY` /
 * `_ARRAY_INDEX`（写参）、`_ARRAY_INDEX_WRITE`（同槽 patch 的目标）。
 * **同一个事实查三次不是冗余**——不查的那一处就是漏洞。
 */
#define VMBIN_HANDLER_REQUIRE_WRITABLE(position, output_exit, instruction)         \
    do {                                                                           \
        if ((position).write_bytes == NULL) {                                      \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY,       \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

/*
 * 创建型算子的**唯一写入口**：造 wire（含 langtype）→ 取目标 key → 整条写。
 *
 * 为什么所有创建型算子都必须走这里、不能走「定位写参 + memcpy」：
 * 它们的写槽按 A-3 **尚未物化**——`LOCATE_WRITE` 会直接失败，`write_bytes` 为空。
 * 那不是「写不进去」，是「这个值还不存在」。创建型算子的语义正是**写入即创建**，
 * 它要的是 key，不是地址。
 *
 * langtype **从写参的声明类型取**（Program 的参数类型表里就有），不拼串、不解析
 * ——类型本来就存在，只是没被传下来。
 *
 * 分配归存储层：`kvspaceSetValue` 收完整 wire，容量它说了算；本函数只用
 * `kvlangXvalueHeadNewSlack` 临时产一条，传完即还。
 *
 * **判据**：创建型 handler 里出现 `LOCATE_WRITE` + `memcpy(write_bytes, …)` 就是
 * 走错了路。用一条用例钉住——对一个从未写过的局部变量调 `new` 必须成功。
 */
static inline runtimec_status_t vmbin_handler_write_new_value(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *bytes,
    size_t length, binrwir_execution_exit_t *output_exit) {
    char langtype_buffer[VMBIN_HANDLER_LANGTYPE_CAPACITY];
    char key_buffer[VMBIN_HANDLER_KEY_CAPACITY];
    runtimec_string_view_t declared;
    runtimec_error_t error;
    runtimec_status_t status;
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char message[256];
    runtimec_key_view_t target_key;

    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->write_arguments.data[0], &declared, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_buffer)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    memcpy(langtype_buffer, declared.data, declared.size);
    langtype_buffer[declared.size] = '\0';
    if (kvlangXvalueHeadNewSlack(bytes, (uint64_t)length,
                                  langtype_buffer, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    /*
     * **写目标的 key 派生只在这里调用一次**，并且走共用的 helper——
     * LOCAL 走 `<frame_path>/<符号名>`、STATIC（绝对路径 `/tmp/adk`）就是符号名本身。
     *
     * 曾经这里直接调只接 LOCAL 的 `vmbin_frame_target_key`：`map·new` 到绝对路径时
     * 建 key 失败、报 `CREATE_FAILED`（0x4A05）。而**上层 `container_open_marker`
     * 已经把正确的 key 拼出来了**（gdb 实测 `buffer="/tmp/adk"`），本函数却没用它、
     * 又自己派生了一次——「同一事实两个来源」的教科书形态：
     * `container_target_key` 与这里各有一份，`0x4308` 与 `0x4A05` 是同一个 bug 的
     * 两个出口。
     *
     * LOCAL 行为**逐字不变**：helper 的 LOCAL 分支直接转调 `vmbin_frame_target_key`。
     */
    status = vmbin_value_access_argument_key(
        frame, &instruction->write_arguments.data[0], key_buffer,
        sizeof(key_buffer), &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        message[0] = '\0';
        if (!runtimec_kvspace_write_wire(frame->kvspace_handle, key_buffer, wire,
                                         (uint32_t)wire_size, message,
                                         sizeof(message))) {
            status = RUNTIMEC_STATUS_ERROR;
        }
    }
    free(wire);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    /*
     * **写进存储只是「存储里有了」，必须紧接着绑定**——与
     * `vmbin_value_access_create_unbound_local`（`value_access.c`）末尾同一步。
     *
     * 少了这一步：容器的写槽按 A-3 **不由帧准备物化**（没有定宽零值），
     * 于是它唯一的绑定来源就是「第一次写」。写成功却不绑定 ⇒ 绑定表里没有这个槽
     * ⇒ 后续任何读它的 handler 报 `0x4A02 LOCATE_FAILED`（`UNBOUND_SLOT`）。
     *
     * **`array_append.kv` 就是这条**：`compactarray·new` 建出 `a` 却没绑，
     * `scatterarray·newscatter(a)` 读 `a` 直接失败。
     *
     * 「写新建值」这件事曾有两处实现：`create_unbound_local`（带绑定）与本函数
     * （漏了）。不是难写，是两处各自演化——今晚反复治的「同一事实两个来源」。
     */
    target_key = runtimec_key_view_make(
        key_buffer, strlen(key_buffer));
    status = vmbin_value_access_bind_write_target(
        frame, &instruction->write_arguments.data[0], target_key, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 宽度核对 + 转型：把位置里的字节当 `type` 用。
 *
 * `position` 是本次要核的一方（读或写）。宽度不符一律失败封闭——**绝不截断转型**。
 */
#define VMBIN_HANDLER_AS(position, type, output_exit, instruction)                 \
    (((position).size == sizeof(type))                                             \
         ? (type *)(void *)(position).write_bytes                                  \
         : (vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter),     \
            (type *)NULL))

#define VMBIN_HANDLER_AS_READ(position, type, output_exit, instruction)            \
    (((position).size == sizeof(type))                                             \
         ? (const type *)(const void *)(position).read_bytes                       \
         : (vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter),     \
            (const type *)NULL))

/* 一次核两个/三个参数：任一不符即失败。`VMBIN_HANDLER_REQUIRE` 是单条判据。 */
#define VMBIN_HANDLER_REQUIRE(position, type, output_exit, instruction)            \
    do {                                                                           \
        if ((position).size != sizeof(type)) {                                     \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

/* 参数个数核对；不符即失败。 */
#define VMBIN_HANDLER_REQUIRE_ARITY(instruction, reads, writes, output_exit)       \
    do {                                                                           \
        if ((instruction)->read_arguments.count != (reads) ||                      \
            (instruction)->write_arguments.count != (writes)) {                    \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

/* 变参族的 arity 失败出口：判据由调用方自己写（读参个数是**范围**而不是定值），
 * 这里只产生与定值版**完全相同**的诊断与返回码——两条出口产生同一个码，
 * 读者才不会以为 `0x4A01` 有两个来源。 */
#define VMBIN_HANDLER_REQUIRE_ARITY_FAIL_(output_exit, instruction)                \
    do {                                                                           \
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,                   \
                           VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                    \
                           (instruction)->source_logical_program_counter);         \
        return RUNTIMEC_STATUS_ERROR;                                              \
    } while (0)

/*
 * ── 类型化宏：逻辑函数直接拿 `T *`，不需要自己转型 ─────────────────────
 *
 * 这是**用户要的形态**：`*out = *a < *b ? *a : *b;` 一行落地，不加转型样板。
 * 宽度核对在调用前完成，逻辑函数里不会出现长度判断。
 *
 * 逻辑函数签名：
 *     一元    void f(const read_type *, write_type *)
 *     二元    void f(const read_type *, const read_type *, write_type *)
 *     三元    void f(const read_type *, const read_type *, const read_type *,
 *                    write_type *)
 */
#define VMBIN_HANDLER_UNARY_TYPED(handler_name, function, read_type, write_type)   \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(read_position_, read_type, output_exit, instruction);\
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type *)(const void *)read_position_.read_bytes,       \
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

/*
 * 二元类型化：**两个读参各有类型**。混合精度算子的唯一写法——
 * `min_int32_int64(const int32_t *, const int64_t *, int64_t *)` 靠这个宏表达。
 * 两个读参同类型时把两个类型参数写成同一个即可。
 */
#define VMBIN_HANDLER_BINARY_TYPED(handler_name, function, read_type_first,        \
                                   read_type_second, write_type)                   \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_TERNARY_TYPED(handler_name, function, read_type_first,       \
                                   read_type_second, read_type_third, write_type)  \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(third_position_, read_type_third, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (const read_type_third *)(const void *)third_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

/*
 * ── 位置级宏：逻辑函数拿 `vmbin_value_location_t` ─────────────────────
 *
 * 给「长度本身是语义」的算子用（变长文本、容器、跨形态搬运）——它们的逻辑函数
 * 必须知道字节数，不能转成定长类型。类型化宏覆盖不到这类，它们是并列关系而不是
 * 上下层：一个算子要么用类型化（长度固定），要么用位置级（长度运行期决定）。
 */
#define VMBIN_HANDLER_UNARY_LOCATION(handler_name, function)                       \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &read_position_, &write_position_,            \
                 output_exit);                                                     \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_BINARY_LOCATION(handler_name, function)                      \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &first_position_, &second_position_,          \
                 &write_position_, output_exit);                                   \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TERNARY_LOCATION(handler_name, function)                     \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &first_position_, &second_position_,          \
                 &third_position_, &write_position_, output_exit);                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 一元 · 数组视图：读参是 compact 数组，逻辑函数拿「body + 长度 + 声明 langtype」。
 *
 * 为什么单列一族：容器算子的**元素宽取决于声明类型**，而声明类型只能从 Program
 * 的参数类型表取——那是宏该做的事，不是逻辑函数该做的。逻辑函数因此拿到完整
 * 三件：字节、长度、类型；它只负责算。
 */
#define VMBIN_HANDLER_UNARY_ARRAY(handler_name, function, write_type)              \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t read_langtype_;                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(read_position_.read_bytes, read_position_.size, read_langtype_,   \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 「数组 + N 个下标」读参的**准备**：定位数组、逐个取出下标的值、取数组声明类型。
 *
 * 单独成函数而不是写成宏体：这套准备逻辑在**读**与**写**两个族里逐字相同，
 * 各写一遍就是两份同一个规则；而写进宏体会让两个 handler 都超出门禁的 5 行上限
 * （实测 18 行 / 17 行）——那不是「handler 在分岔」，是准备步骤被塞进了 handler。
 *
 * 下标个数由 layout 按源码里的个数发射，运行期与声明维数的核对在
 * `container_element_offset` 里（那是唯一判据点），此处只做**形状合法性**：
 * 至少一个下标、不超过 `VMBIN_HANDLER_MAX_INDEX_COUNT`。
 */
static inline runtimec_status_t vmbin_handler_prepare_indices(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t index_count,
    int64_t *indices, vmbin_value_location_t *output_array,
    runtimec_string_view_t *output_array_langtype,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t index_position;
    runtimec_error_t error;
    runtimec_status_t status;

    if (index_count == 0U ||
        index_count > VMBIN_HANDLER_MAX_INDEX_COUNT) {
        VMBIN_HANDLER_REQUIRE_ARITY_FAIL_(output_exit, instruction);
    }
    VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, output_array, output_exit);
    for (size_t ordinal = 0U; ordinal < index_count; ++ordinal) {
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, ordinal + 1U,
                                  &index_position, output_exit);
        VMBIN_HANDLER_REQUIRE(index_position, int64_t, output_exit, instruction);
        indices[ordinal] =
            *(const int64_t *)(const void *)index_position.read_bytes;
    }
    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], output_array_langtype,
        &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 数组下标读：读参是 compact 数组 + **N 个整数下标**（N ≥ 1），写参是元素。
 *
 * 单下标是 N == 1 的特例，走同一条路径——不为「一维」留第二种写法，否则
 * 「单下标行为不变」这条回归判据就没有唯一事实源可以对照。
 */
#define VMBIN_HANDLER_ARRAY_INDEX(handler_name, function, index_type)              \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t array_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t array_langtype_;                                    \
        int64_t indices_[VMBIN_HANDLER_MAX_INDEX_COUNT];                       \
        const size_t index_count_ = instruction->read_arguments.count - 1U;        \
                                                                                   \
        if (vmbin_handler_prepare_indices(frame, instruction, index_count_,        \
                                          indices_, &array_position_,              \
                                          &array_langtype_, output_exit) !=        \
            RUNTIMEC_STATUS_SUCCESS) {                                             \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(indices_, index_count_, write_position_.write_bytes,              \
                 write_position_.size, array_position_.read_bytes,                 \
                 array_position_.size, array_langtype_, output_exit);              \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 数组下标写：读参是「数组 + **N 个整数下标** + 新值」（N ≥ 1），
 * 写参是数组本身（同槽 patch）。
 *
 * 与 `_ARRAY_INDEX` 的差别：写参就是读参那个槽，改的是它的 body 里一段字节，
 * head 与 attr 不动。逻辑函数拿「下标 int64 值数组 + 个数 + 新值 + 目标指针 +
 * 目标长度 + 声明类型」。
 */
#define VMBIN_HANDLER_ARRAY_INDEX_WRITE(handler_name, function, index_type)        \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t array_position_;                                    \
        vmbin_value_location_t value_position_;                                    \
        runtimec_string_view_t array_langtype_;                                    \
        int64_t indices_[VMBIN_HANDLER_MAX_INDEX_COUNT];                       \
        const size_t index_count_ = instruction->read_arguments.count - 2U;        \
                                                                                   \
        if (vmbin_handler_prepare_indices(frame, instruction, index_count_,        \
                                          indices_, &array_position_,              \
                                          &array_langtype_, output_exit) !=        \
            RUNTIMEC_STATUS_SUCCESS) {                                             \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, index_count_ + 1U,           \
                                  &value_position_, output_exit);                  \
        VMBIN_HANDLER_REQUIRE_WRITABLE(array_position_, output_exit, instruction); \
        function(indices_, index_count_, value_position_.read_bytes,               \
                 value_position_.size, array_position_.write_bytes,                \
                 array_position_.size, array_langtype_, output_exit);              \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * ── 创建型族：写参**只用来取 key**，绝不 LOCATE_WRITE ──────────────────
 *
 * 这是创建型与普通算子最根本的区别，写在这里免得后来人「顺手补一句 LOCATE_WRITE」：
 *
 *   - 普通算子的写参**已存在**，定位它、改它的字节；
 *   - 创建型算子的写槽按 A-3 **尚未物化**，`LOCATE_WRITE` 会失败——而那是**正常
 *     状态**，不是错误。创建语义就是「写入即创建」。
 *
 * 所以下面几个宏对写参只做一件事：`vmbin_frame_target_key` 取目标 key。
 * 读参仍然照常定位——**读参定位失败是真错误**（输入有问题，`0x4A02`）。
 */

/* 零读参创建：`new` 无参形态、`scatterarray·new` 等。 */
#define VMBIN_HANDLER_CREATE_NONE(handler_name, function)                          \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 0U, 1U, output_exit);             \
        function(frame, instruction, NULL, 0U, output_exit);                       \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

/*
 * 一读参创建：`newslice` / `fill` 这类「读一个源、造一个新值」。
 *
 * 逻辑函数签名：
 *     void f(frame, instruction, const vmbin_value_location_t *read,
 *            const runtimec_string_view_t *read_langtype,
 *            binrwir_execution_exit_t *output_exit)
 *
 * 它自己要调 `vmbin_handler_write_new_value` 写结果——那是创建入口，不是 LOCATE。
 */
#define VMBIN_HANDLER_CREATE_ONE(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t read_position_;                                     \
        runtimec_string_view_t read_langtype_;                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(frame, instruction, &read_position_, &read_langtype_,             \
                 output_exit);                                                     \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

/*
 * 两读参创建：`newreshape(a, dims…)` 这类「源 + 若干整数参数 → 新值」。
 * 两个读参都定位；写参仍不定位。
 */
#define VMBIN_HANDLER_CREATE_TWO(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        runtimec_string_view_t first_langtype_;                                    \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &first_langtype_,         \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(frame, instruction, &first_position_, &first_langtype_,           \
                 &second_position_, output_exit);                                  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * N 读参创建：`compactarray·new(e0, e1, …)` 这类「元素个数运行期才知道」的形态。
 *
 * 它不能用 `_CREATE_ONE`（那是固定 1 个读参）；也不能用 `_LOCATION` 族（那会
 * `LOCATE_WRITE`，而创建型的写槽不存在）。所以单独一族：只核写参数 1、读参数
 * 交给逻辑函数自己遍历（个数不定）。
 *
 * 逻辑函数签名：
 *     void f(frame, instruction, binrwir_execution_exit_t *output_exit)
 * 它自己遍历 `instruction->read_arguments`，自己调 `vmbin_handler_write_new_value`。
 */
#define VMBIN_HANDLER_CREATE_VARIADIC(handler_name, function)                      \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        runtimec_string_view_t result_langtype_;                                   \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        if (instruction->write_arguments.count != 1U) {                            \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        /* 结果 langtype 是参数准备：从写参的声明类型取，不进逻辑函数。 */          \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->write_arguments.data[0], &result_langtype_,       \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(frame, instruction, result_langtype_, output_exit);               \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 三读参创建：`newslice(a, lo, hi)` 这类「源 + 两个整数参数 → 新值」。
 *
 * 三个读参都定位；**写参仍不定位**（创建型的写槽尚未物化，只用来取 key）。
 * 逻辑函数自己调 `vmbin_handler_write_new_value` 写结果。
 */
#define VMBIN_HANDLER_CREATE_TERNARY(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * ── `_EXIT` 族：逻辑函数自己报错的那一批 ────────────────────────────────
 *
 * 与 `_TYPED` 族的唯一差别：**逻辑函数多收一个 `binrwir_execution_exit_t *`**，
 * 它自己决定成功还是失败（除零、类型不兼容、越界……）。宏不再替它假定「必然成功」。
 *
 * 逻辑函数签名（以二元为例）：
 *     void f(const t1 *a, const t2 *b, w *out,
 *            binrwir_execution_exit_t *output_exit)
 *
 * 三条要求（写在这里是为了让调用方一眼看到，不用翻实现）：
 *   1. **收口只在宏里写一次**：回值按 `output_exit->kind` 判，pc 补写也在宏里；
 *      逻辑函数里一个字都不写这两件事；
 *   2. **pc 在调逻辑函数之前写进 exit**：逻辑函数自己调 `vmbin_handler_fail`
 *      时，`output_exit->logical_program_counter` 已经是本次指令的位置——
 *      它不需要再传 pc，也不会写成 0；
 *   3. **`_TYPED`（无 exit 族）签名不动**：那批逻辑函数不会失败，不该被迫多收参数。
 *
 * 为什么 pc 要先写：`vmbin_handler_fail` 的最后一个实参是 source pc，而逻辑函数
 * 手里只有 exit。不预先写进去，它就得把 pc 一路传下来——那是把宏的职责漏进业务。
 */
#define VMBIN_HANDLER_UNARY_TYPED_EXIT(handler_name, function, read_type,          \
                                       write_type)                                 \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(read_position_, read_type, output_exit, instruction);\
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        /* pc 先写：逻辑函数失败时不必再把它传下来。 */                           \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type *)(const void *)read_position_.read_bytes,       \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_BINARY_TYPED_EXIT(handler_name, function, read_type_first,   \
                                        read_type_second, write_type)              \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TERNARY_TYPED_EXIT(handler_name, function, read_type_first,  \
                                         read_type_second, read_type_third,        \
                                         write_type)                               \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(third_position_, read_type_third, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (const read_type_third *)(const void *)third_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * ── scatter 族：成员是**独立子 key**，不是 body 里的偏移 ──────────────────
 *
 * 与 `_TYPED` / `_ARRAY_*` 的根本区别：容器本体只是一个空 body 的 marker，
 * 元素在 `base·[i]` 这些独立的键下面。所以「读写元素」= 拼 key + 走 kvspace ABI，
 * 不经过 `value_access` 的 body 路径。
 *
 * 判据：**跨形态搬运几乎必然是新建**（形态都变了），不要套用 container 的
 * 「同槽 patch」假设。唯一的例外是散列容器内就地换一个成员——那仍然是写一个
 * 独立键，不是改容器的 body。
 */

/*
 * 1 读参（容器）+ 标量结果：`scatterarray·len`。
 *
 * 逻辑函数签名：
 *     void f(const binrwir_binvthread_frame_t *frame,
 *            const binrwir_instruction_view_t *instruction, write_type *out,
 *            binrwir_execution_exit_t *output_exit)
 * 它自己从 read arg 拼容器 key——那是 scatter 的「取参数」，与 body 形态无关。
 */
#define VMBIN_HANDLER_SCATTER_READ_SCALAR(handler_name, function, write_type)      \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction,                                               \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/* 0 读参 + 写参（新建）：`scatterarray·new` 这类「只建空容器」。 */
#define VMBIN_HANDLER_SCATTER_CREATE(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->write_arguments.count != 1U ||                            \
            frame->kvspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/* 2 读参（容器 + 下标）+ 写参：`scatterarray·at`。 */
#define VMBIN_HANDLER_SCATTER_READ_WRITE(handler_name, function)                   \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, write_position_.write_bytes,                  \
                 write_position_.size, output_exit);                               \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/* 写参（容器）+ 若干读参：`scatterarray·set` 这类「往容器里写一个成员」。 */
#define VMBIN_HANDLER_SCATTER_WRITE_READS(handler_name, function)                  \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count < 2U ||                              \
            instruction->write_arguments.count != 1U ||                            \
            frame->kvspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/* 多个读参 + 写参（跨容器搬运）：`scatterarray·slice` / `newscatter` / `newcompact`。 */
#define VMBIN_HANDLER_SCATTER_CROSS(handler_name, function)                        \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count < 2U ||                              \
            instruction->write_arguments.count != 1U ||                            \
            frame->kvspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/* 1 读参（compact）+ 1 写参（新建散列容器）：`scatterarray·newscatter`。 */
#define VMBIN_HANDLER_SCATTER_EXPAND(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count != 1U ||                             \
            instruction->write_arguments.count != 1U ||                            \
            frame->kvspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

/*
 * handler 层写一个 key：`value` 必须是**完整 wire**（head+body+attr）。
 *
 * 为什么单独一层：handler 拿不到 `runtimec_kvspace_access_t`（那是 engine 持有的），
 * 只有裸句柄。本函数把「拆段 + Realloc + 填两段」这一串收在一处，避免每个 handler
 * 各写一遍——那种重复迟早在某一处漏掉 attr，症状是「值能读出来但没有类型」。
 *
 * 失败只返回 false 并写 `*message`，由调用方决定诊断码（各 handler 的 diag 不同）。
 */
/* 读写 wire 的两个入口在 `contract/kvspace_interface.h`（engine 也要用，而 engine
 * 不依赖 vmbin）。本头已经 include 它，handler 直接用那两个名字。 */

#ifdef __cplusplus
}
#endif

#endif
