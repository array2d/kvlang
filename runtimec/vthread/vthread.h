#ifndef RUNTIMEC_VTHREAD_VTHREAD_H
#define RUNTIMEC_VTHREAD_VTHREAD_H

/*
 * 组件：vthread
 * 作用：一个 vthread 的执行期句柄——`‥pc` 与 `‥status` 的**进程内地址**。
 *
 * 为什么是结构体而不是每次现解析：这两个系统键在 vthread 生命周期内**原地写、
 * 地址不搬移**。解析一次、长期持有，执行循环每一步只做 `memcpy`；每次失败或
 * 每步推进都重走一遍 resolvekey + resolveoffset，是把常量当变量用。
 *
 * 键的拼装只在本组件：`/vthread/<vid>/‥pc`、`/vthread/<vid>/‥status`。
 * 其它组件拿到的是**地址**，不再拼键。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 状态值 `bufferarray=8`：body 恒 8 字节。 */
#define RUNTIMEC_VTHREAD_STATUS_LENGTH 8U

/*
 * 状态词汇：**一个数组 + 下标**。表项是 8 字节定长字符数组，字符串初始化器由
 * 编译器补零，`memcpy(..., 8)` 读满不越界，也不需要运行期的长度。
 */
typedef enum runtimec_vthread_status_identifier {
    RUNTIMEC_VTHREAD_STATUS_INIT = 0,
    RUNTIMEC_VTHREAD_STATUS_RUNNING,
    RUNTIMEC_VTHREAD_STATUS_WAIT,
    RUNTIMEC_VTHREAD_STATUS_PAUSED,
    RUNTIMEC_VTHREAD_STATUS_ERROR,
    RUNTIMEC_VTHREAD_STATUS_DONE,
    RUNTIMEC_VTHREAD_STATUS_COUNT
} runtimec_vthread_status_identifier_t;

extern const char
    RUNTIMEC_VTHREAD_STATUS_TEXT[RUNTIMEC_VTHREAD_STATUS_COUNT]
                                [RUNTIMEC_VTHREAD_STATUS_LENGTH];

/* 表项长度检查：含 NUL 必须 ≤ 8 字节，超长编译期失败。 */
#define RUNTIMEC_STATUS_TEXT_LENGTH_OK(text)                                  \
    _Static_assert(sizeof(text) <= RUNTIMEC_VTHREAD_STATUS_LENGTH,             \
                   "vthread status text exceeds the 8-byte slot: " text)

/* `‥pc` 是 `[2]int32`：{stackid, s0}，body 8 字节。 */
#define RUNTIMEC_VTHREAD_PROGRAM_COUNTER_LENGTH 8U

/* 错误消息槽：固定 64 字节，零补齐。 */
#define RUNTIMEC_VTHREAD_MESSAGE_LENGTH 64U

/* 标识符缓冲：vid 文本，键的拼装来源。 */
#define RUNTIMEC_VTHREAD_IDENTIFIER_CAPACITY 64U

typedef struct runtimec_vthread {
    char identifier[RUNTIMEC_VTHREAD_IDENTIFIER_CAPACITY];
    /* `/vthread/<vid>/‥status` 的 body（8 字节，状态文本 + 零补齐）。 */
    uint8_t *status_body;
    /* `/vthread/<vid>/‥pc` 的 body（8 字节，`[2]int32` 小端）。 */
    uint8_t *pc_body;
    /* `/vthread/<vid>/‥msg` 的 body（64 字节，错误消息 + 零补齐）。 */
    uint8_t *message_body;
} runtimec_vthread_t;

/*
 * 解析一次。`identifier` 是 vid 文本；键在这里拼，偏移与进程地址在这里算。
 *
 * 键不存在、形状不符、坐标越界——一律 `abort()`：vthread 没有状态槽或 pc 槽，
 * 说明它没有被正确建立，此时继续执行任何一个算子都是「以看起来合理的方式
 * 跑错东西」。不返回错误码，也就没有第二个降级点。
 */
void runtimec_vthread_open(runtimec_vthread_t *vthread, void *kvspace_handle,
                           const char *identifier);

/* 状态：按词汇表下标写固定 8 字节，不改 head。 */
void runtimec_vthread_write_status(
    runtimec_vthread_t *vthread,
    runtimec_vthread_status_identifier_t status_identifier);

/* 错误消息：固定 64 字节写，先清零再拷正文（旧消息不能留在尾部）。 */
void runtimec_vthread_write_message(runtimec_vthread_t *vthread,
                                    const char *message);

/*
 * 退出宏：写状态，然后 **从调用者函数** 返回。
 *
 * 必须是宏——`return` 要落在 handler/解释器自己的函数体里。写成函数只回到
 * 调用它的那一层，调用者会带着半成品继续执行。
 */
#define RUNTIMEC_VTHREAD_STATUS_RETURN(vthread, status_identifier, message)   \
    do {                                                                      \
        _Static_assert(sizeof(message) - 1U <=                                \
                           RUNTIMEC_VTHREAD_MESSAGE_LENGTH,                   \
                       "vthread message exceeds the 64-byte slot: " message); \
        runtimec_vthread_write_status((vthread), (status_identifier));        \
        runtimec_vthread_write_message((vthread), (message));                 \
        return;                                                               \
    } while (0)

/* 位置：`[2]int32 {stack_identifier, source_instruction_ordinal}` 小端写。 */
void runtimec_vthread_write_pc(runtimec_vthread_t *vthread,
                                            int32_t stack_identifier,
                                            int32_t source_instruction_ordinal);

#ifdef __cplusplus
}
#endif

#endif
