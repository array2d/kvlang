/*
 * 组件：vmbin/handlers/time 的实现
 * 作用：`time·`（now / sub / add / before / after）与 `time/duration·`
 *       （单位构造、单位换算、加减、比较）两族内建，共 17 条 handler。
 * 联系：条目由 `engine/session` 装进 `binrwir_registry_t`；值经
 *       `vmbin/value_access` 按地址读写。
 * 约束：见 `time.h` —— 单一 int64 纳秒、墙上时间、溢出回绕、除法向零截断。
 *
 * **为什么单独一个文件**：`time·` 与 `time/duration·` 是同一族的两个前缀
 * （spec `05-runtime语义/05-runtimec内建.kv` 的「`·` 限定内建族」把它们并列
 * 列出），17 条 handler 必须放在同一处——分散到 host/scalar 会让「时间语义」
 * 这个事实变成两份，而两份迟早漂。
 */

/* `clock_gettime` / `CLOCK_REALTIME` 是 POSIX：本工程 C11 关闭扩展，
 * 必须显式要 POSIX，且必须在**任何** include 之前定义——它影响系统头里
 * 暴露哪些声明，晚一步就等于没写。 */
#define _POSIX_C_SOURCE 200809L

#include "vmbin/handlers/time.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "vmbin/handlers/handler_call.h"

/*
 * 墙上时间，纳秒自 Unix epoch。
 *
 * **不用 `CLOCK_MONOTONIC`**：矩阵 `time-realtime-nanoseconds` 是 consensus 行，
 * 五家都读墙上时间（见 time.h 的注释）。单调钟会让 `time·now` 与
 * `time·sub` 的结果对不上日历语义。
 */
static inline void time_clock_nanoseconds(int64_t *out) {
    struct timespec now;

    (void)clock_gettime(CLOCK_REALTIME, &now);
    *out = (int64_t)now.tv_sec * INT64_C(1000000000) + (int64_t)now.tv_nsec;
}

/* 「把一个 int64 纳秒写进一个尚未物化的写槽」——创建型只有这一处写法。 */
static inline runtimec_status_t write_nanoseconds(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int64_t nanoseconds,
    binrwir_execution_exit_t *output_exit) {
    return vmbin_handler_write_new_value(
        frame, instruction, (const uint8_t *)&nanoseconds, sizeof(nanoseconds),
        output_exit);
}

/* `time·now()`：无读参；写参是**尚未物化**的槽，走创建型写法。 */
static inline void time_now_logic(binrwir_binvthread_frame_t *frame,
                                  const binrwir_instruction_view_t *instruction,
                                  const vmbin_value_location_t *read,
                                  size_t read_count,
                                  binrwir_execution_exit_t *output_exit) {
    int64_t nanoseconds = 0;

    (void)read;
    (void)read_count;
    time_clock_nanoseconds(&nanoseconds);
    (void)write_nanoseconds(frame, instruction, nanoseconds, output_exit);
}

/* `time·sub(t1, t0)` → duration（t1 - t0）。溢出按矩阵回绕，不检查。 */
static inline void time_sub_logic(const int64_t *later, const int64_t *earlier,
                                  int64_t *out) {
    *out = *later - *earlier;
}

/* `time·add(t, d)` → time。 */
static inline void time_add_logic(const int64_t *moment,
                                  const int64_t *duration, int64_t *out) {
    *out = *moment + *duration;
}

/* `time·before(a, b)` / `after(a, b)` → bool：纳秒精度比较。 */
static inline void time_before_logic(const int64_t *first,
                                     const int64_t *second, bool *out) {
    *out = *first < *second;
}

static inline void time_after_logic(const int64_t *first, const int64_t *second,
                                    bool *out) {
    *out = *first > *second;
}

/*
 * duration 的单位倍数。`nanos`/`as_nanos` 常在 1，其余按纳秒换算。
 * 乘法溢出按矩阵回绕（int64 自然行为），不检查。
 */
#define TIME_DURATION_MICROS INT64_C(1000)
#define TIME_DURATION_MILLIS INT64_C(1000000)
#define TIME_DURATION_SECONDS INT64_C(1000000000)
#define TIME_DURATION_MINUTES INT64_C(60000000000)
#define TIME_DURATION_HOURS INT64_C(3600000000000)

/* `time/duration·<unit>(n)`：int64 → duration，乘该单位在纳秒下的倍数。 */
static inline void time_duration_nanos_logic(const int64_t *count,
                                             int64_t *out) {
    *out = *count;
}

static inline void time_duration_millis_logic(const int64_t *count,
                                              int64_t *out) {
    *out = *count * TIME_DURATION_MILLIS;
}

static inline void time_duration_seconds_logic(const int64_t *count,
                                               int64_t *out) {
    *out = *count * TIME_DURATION_SECONDS;
}

static inline void time_duration_minutes_logic(const int64_t *count,
                                               int64_t *out) {
    *out = *count * TIME_DURATION_MINUTES;
}

static inline void time_duration_hours_logic(const int64_t *count,
                                             int64_t *out) {
    *out = *count * TIME_DURATION_HOURS;
}

/* `time/duration·as_<unit>(d)`：duration → int64，整除**向零截断**（C 语义）。 */
static inline void time_duration_as_nanos_logic(const int64_t *duration,
                                                int64_t *out) {
    *out = *duration;
}

static inline void time_duration_as_millis_logic(const int64_t *duration,
                                                 int64_t *out) {
    *out = *duration / TIME_DURATION_MILLIS;
}

static inline void time_duration_as_seconds_logic(const int64_t *duration,
                                                  int64_t *out) {
    *out = *duration / TIME_DURATION_SECONDS;
}

static inline void time_duration_as_minutes_logic(const int64_t *duration,
                                                  int64_t *out) {
    *out = *duration / TIME_DURATION_MINUTES;
}

static inline void time_duration_as_hours_logic(const int64_t *duration,
                                                int64_t *out) {
    *out = *duration / TIME_DURATION_HOURS;
}

/* `time/duration·add` / `sub`：duration ± duration → duration。 */
static inline void time_duration_add_logic(const int64_t *first,
                                           const int64_t *second,
                                           int64_t *out) {
    *out = *first + *second;
}

static inline void time_duration_sub_logic(const int64_t *first,
                                           const int64_t *second,
                                           int64_t *out) {
    *out = *first - *second;
}

/* `time/duration·before` / `after`：比较。 */
static inline void time_duration_before_logic(const int64_t *first,
                                              const int64_t *second,
                                              bool *out) {
    *out = *first < *second;
}

static inline void time_duration_after_logic(const int64_t *first,
                                             const int64_t *second, bool *out) {
    *out = *first > *second;
}

VMBIN_HANDLER_CREATE_NONE(time_now_handler, time_now_logic)
VMBIN_HANDLER_BINARY_TYPED(time_sub_handler, time_sub_logic, int64_t, int64_t,
                           int64_t)
VMBIN_HANDLER_BINARY_TYPED(time_add_handler, time_add_logic, int64_t, int64_t,
                           int64_t)
VMBIN_HANDLER_BINARY_TYPED(time_before_handler, time_before_logic, int64_t,
                           int64_t, bool)
VMBIN_HANDLER_BINARY_TYPED(time_after_handler, time_after_logic, int64_t,
                           int64_t, bool)

VMBIN_HANDLER_UNARY_TYPED(time_duration_nanos_handler,
                          time_duration_nanos_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_millis_handler,
                          time_duration_millis_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_seconds_handler,
                          time_duration_seconds_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_minutes_handler,
                          time_duration_minutes_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_hours_handler,
                          time_duration_hours_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_as_nanos_handler,
                          time_duration_as_nanos_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_as_millis_handler,
                          time_duration_as_millis_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_as_seconds_handler,
                          time_duration_as_seconds_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_as_minutes_handler,
                          time_duration_as_minutes_logic, int64_t, int64_t)
VMBIN_HANDLER_UNARY_TYPED(time_duration_as_hours_handler,
                          time_duration_as_hours_logic, int64_t, int64_t)
VMBIN_HANDLER_BINARY_TYPED(time_duration_add_handler, time_duration_add_logic,
                           int64_t, int64_t, int64_t)
VMBIN_HANDLER_BINARY_TYPED(time_duration_sub_handler, time_duration_sub_logic,
                           int64_t, int64_t, int64_t)
VMBIN_HANDLER_BINARY_TYPED(time_duration_before_handler,
                           time_duration_before_logic, int64_t, int64_t, bool)
VMBIN_HANDLER_BINARY_TYPED(time_duration_after_handler,
                           time_duration_after_logic, int64_t, int64_t, bool)

/* 静态存储，进程级生命周期；调用方不得释放。 */
static const binrwir_registry_entry_t time_entries[] = {
    /* rwir_catalog.def:667 起，opid 见各条注释。 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_TIME_NOW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_now_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_TIME_SUB,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_sub_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_TIME_ADD,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_add_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_TIME_BEFORE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_before_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_TIME_AFTER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_after_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_NANOSECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_nanos_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_MILLISECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_millis_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_SECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_seconds_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_MINUTES,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_minutes_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_HOURS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_hours_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AS_NANOSECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_as_nanos_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AS_MILLISECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_as_millis_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AS_SECONDS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_as_seconds_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AS_MINUTES,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_as_minutes_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AS_HOURS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_as_hours_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_ADD,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_add_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_SUB,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_sub_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_BEFORE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_before_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DURATION_AFTER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = time_duration_after_handler},
};

const binrwir_registry_entry_t *runtimec_time_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(time_entries) / sizeof(time_entries[0]);
    return time_entries;
}
