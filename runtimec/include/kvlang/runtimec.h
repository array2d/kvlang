#ifndef KVLANG_RUNTIMEC_H
#define KVLANG_RUNTIMEC_H

/*
 * runtimec 的唯一公共 C 接口。
 *
 * 公共层只负责创建 runtimec、提交一次执行请求和销毁 runtimec。RWIR、
 * BinRWIR、vmkv、vmbin、BinRWIR Compiler 与 kvspace 的内部结构都不在此暴露。
 * 所有可失败调用返回 runtimec_status_t，输出对象在失败前先进入无效状态，
 * 错误写入调用方提供的缓冲区。当前骨架只会明确返回未实现，不伪造成功。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(KVLANG_RUNTIMEC_BUILDING_LIBRARY)
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllexport)
#  else
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define KVLANG_RUNTIMEC_EXPORT __attribute__((visibility("default")))
#else
#  define KVLANG_RUNTIMEC_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_status {
    RUNTIMEC_STATUS_SUCCESS = 0,
    RUNTIMEC_STATUS_YIELD = 1,
    RUNTIMEC_STATUS_WAIT = 2,
    RUNTIMEC_STATUS_UNSUPPORTED = 3,
    RUNTIMEC_STATUS_NOT_IMPLEMENTED = 4,
    RUNTIMEC_STATUS_INVALID = 5,
    RUNTIMEC_STATUS_OUT_OF_MEMORY = 6,
    RUNTIMEC_STATUS_ERROR = 7,
    RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY = 8
} runtimec_status_t;

typedef struct kvlang_runtime kvlang_runtime_t;

typedef struct kvlang_runtime_text_buffer {
    char *data;              /* 调用方拥有；capacity 为 0 时可以为 NULL。 */
    size_t capacity;         /* 包含字符串终止空字符的可写字节数。 */
    size_t length;           /* 实际写入字节数，不含字符串终止空字符。 */
    size_t required_length;  /* 完整内容字节数，不含字符串终止空字符。 */
} kvlang_runtime_text_buffer_t;

/*
 * message 容量不足时不写入半条诊断：函数返回
 * RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY，status 与返回值一致，
 * operation_status 保存原始失败，required_length 给出完整消息长度。
 */
typedef struct kvlang_runtime_error {
    runtimec_status_t status;            /* 与函数返回状态完全一致。 */
    runtimec_status_t operation_status;  /* 容量失败时保存原始操作状态。 */
    int64_t code;
    kvlang_runtime_text_buffer_t message;
} kvlang_runtime_error_t;

/*
 * 扩展算子处理函数。
 *
 * 参数是不透明句柄：frame / instruction / output_exit 的内部结构**不复制到本头**
 * ——那是第二份结构真相。扩展方（byteseek）拿到什么就原样转交给 runtimec 的值访问
 * API（`kvlang_runtime_*` 后面按需补的入口）。
 *
 * 代价：编译期没有类型检查。补偿是同一条目里的 `structure_size`——`create` 会
 * 校验它与 runtimec 的期望一致，**结构漂移由 size 拦截，不靠约定**。
 */
typedef runtimec_status_t (*kvlang_runtime_extension_handler_t)(
    void *frame, const void *instruction, void *output_exit);

/*
 * 一条扩展算子。
 *
 * `structure_size` 必须是 `sizeof(kvlang_runtime_extension_entry_t)`。Rust 侧按
 * `#[repr(C)]` 对齐并显式填 `size_of::<Entry>()`；两边字段顺序/数量一旦不同，
 * `create` 直接拒绝，而不是在 handler 里读错偏移。
 */
typedef struct kvlang_runtime_extension_entry {
    uint32_t structure_size;
    uint32_t operation_identifier;
    uint32_t flags; /* EFFECT=1<<0, MAY_YIELD=1<<1（与内部 registry 同值） */
    kvlang_runtime_extension_handler_t handler;
} kvlang_runtime_extension_entry_t;

/* 一个扩展模块：一组条目共用一个诊断名（条目重复时靠它点名）。 */
typedef struct kvlang_runtime_extension_module {
    const char *module_name;
    const kvlang_runtime_extension_entry_t *entries;
    size_t entry_count;
} kvlang_runtime_extension_module_t;

typedef struct kvlang_runtime_create_request {
    /*
     * `sizeof(kvlang_runtime_create_request_t)`。**第一个字段**，因为它的作用就是
     * 「让接收方在按任何偏移读之前知道对方按哪个布局填的」。放在别的字段后面，
     * 读它本身就要先假设布局——那正是要防的错。
     *
     * 与 `kvlang_runtime_create_request_size()` 不等即拒绝，不留兼容分支。
     */
    size_t structure_size;
    const char *data_source_name;
    /*
     * 该 store 实例的稳定身份，必须非零。由组合根（调用方）提供：compilation
     * 缓存与已解析地址都以它为失效判据，换实例就必须换号。
     */
    uint64_t instance_identifier;
    /*
     * 扩展算子表。NULL + 0 = 无扩展（绝大多数调用方）。
     *
     * **`structure_size` 不是可选字段**：本结构体加了这两个字段之后，老调用方
     * 按旧尺寸传入会被拒绝（见 `create` 的校验），不向后兼容——按 p5。
     */
    const kvlang_runtime_extension_module_t *extensions;
    size_t extension_count;
} kvlang_runtime_create_request_t;

/* 本版本两个结构的尺寸。调用方填 `structure_size` 时用它们，**不新增导出符号**
 * （`static inline` 与 `sizeof` 等价，Rust 侧用 `size_of::<T>()`）。
 * 两边对不上即拒绝——结构漂移由 size 拦截，不靠约定。 */
static inline size_t kvlang_runtime_create_request_size(void) {
    return sizeof(kvlang_runtime_create_request_t);
}
static inline size_t kvlang_runtime_extension_entry_size(void) {
    return sizeof(kvlang_runtime_extension_entry_t);
}

typedef enum kvlang_runtime_entry_kind {
    KVLANG_RUNTIME_ENTRY_FUNCTION = 0,
    KVLANG_RUNTIME_ENTRY_PROGRAM_COUNTER = 1,
    KVLANG_RUNTIME_ENTRY_VTHREAD = 2,
    KVLANG_RUNTIME_ENTRY_BOOTSTRAP = 3
} kvlang_runtime_entry_kind_t;

typedef struct kvlang_runtime_execute_request {
    kvlang_runtime_entry_kind_t entry_kind;
    /*
     * 进入哪个函数，**裸函数名**（`"test"`），不是 key。
     *
     * 本入口自己补 `/lib/` 前缀。**一个名字只许一种含义**：以 `/` 开头的值
     * （例如 `"/lib/test"`）一律拒绝并返回 INVALID——它不是「换个写法」，
     * 拼出来会变成 `/lib//lib/test`。而 kvspace 对缺失 key 返回成功 + 空值，
     * 于是双前缀的错误只会表现为下游一句误导性的「没有 source revision key」。
     */
    const char *entry_name;
    const char *const *arguments;
    size_t argument_count;
} kvlang_runtime_execute_request_t;

typedef struct kvlang_runtime_execute_result {
    kvlang_runtime_text_buffer_t value;
    kvlang_runtime_text_buffer_t program_counter;
} kvlang_runtime_execute_result_t;

/* ── TASK-047：扩展 handler 的值访问 ────────────────────────────────
 *
 * 每条 API 的**共同约定**（不再逐条重复）：
 *   1. **一次调用一件事**——没有「取整个操作数数组」的接口，那会把 frame 布局
 *      泄回 handler；
 *   2. **操作数按 catalog kindexpr 解释**——字符串/整数/wire 由本层按该算子的
 *      签名与实参 langtype 决定，handler 不需要知道；
 *   3. **失败语义**——越界/方向不符/类型不符 → `INVALID`；写参宽度不符 →
 *      `INVALID`，**不是部分写**（p1：不许以奇怪的方式成功）。
 *
 * `frame` / `instruction` / `output_exit` 就是 `kvlang_runtime_extension_handler_t`
 * 的入参，原样转发即可。
 */

/* 第 index 个读参的文本（char 族）。容量不足返回 INSUFFICIENT_CAPACITY 并写
 * `required_length`。非 char 族返回 INVALID——不隐式转。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_text(
    const void *frame, const void *instruction, size_t index,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error);

/* 第 index 个读参的整数。按实参声明宽度读、符号扩展到 int64。
 * 宽度不是 1/2/4/8 → INVALID。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_integer(
    const void *frame, const void *instruction, size_t index, int64_t *output,
    kvlang_runtime_error_t *error);

/* 第 index 个读参的整条 wire 字节。**借用**：与 frame 同生命周期。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_wire(
    const void *frame, const void *instruction, size_t index,
    const uint8_t **output_data, size_t *output_size,
    kvlang_runtime_error_t *error);

/* 第 index 个读参的**声明** langtype（不是值上带的那份——槽可以是 None）。
 * 取不到返回 UNSUPPORTED，不猜。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_langtype(
    const void *frame, const void *instruction, size_t index,
    const char **output_data, size_t *output_size,
    kvlang_runtime_error_t *error);

/* 把整条 wire 写进第 index 个写参（head 必须自洽，长度可与现值不同）。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_wire(
    void *frame, const void *instruction, size_t index, const uint8_t *data,
    size_t size, kvlang_runtime_error_t *error);

/* 把文本写进第 index 个写参的 body（编码由写槽声明的 char 族类型决定）。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_text(
    void *frame, const void *instruction, size_t index, const char *data,
    size_t size, kvlang_runtime_error_t *error);

/* 把整数写进第 index 个写参，宽度取写槽的声明类型。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_integer(
    void *frame, const void *instruction, size_t index, int64_t value,
    kvlang_runtime_error_t *error);

/* 输出槽状态：handler 用它声明「我失败了」或「我要让出」。
 * `KVLANG_RUNTIME_EXIT_NONE` 把输出槽清零（正常返回时不需要调）。 */
typedef enum kvlang_runtime_exit_kind {
    KVLANG_RUNTIME_EXIT_NONE = 0,
    KVLANG_RUNTIME_EXIT_ERROR = 1,
    KVLANG_RUNTIME_EXIT_YIELD = 2
} kvlang_runtime_exit_kind_t;

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_set_exit(
    void *output_exit, kvlang_runtime_exit_kind_t kind, int64_t detail_code,
    const char *message, kvlang_runtime_error_t *error);

/* ── TASK-047：Ptr 构造/解引用（底层能力，给扩展 handler 用）──────────
 *
 * **与语言层算子 `kvlang·resolvekey` 不是同一层**：那是 catalog 里的一条指令
 * （由 base 与运行期键值算目标位置、不读目标值）；这两个 API 操作的是 wire 层面
 * 的 `*T` 值，让 handler 不必自己拼字节。
 */

/* 造一个 Ptr 值：`*` 进 langtype，body 是目标 key 文本。
 * **只造值**——不解析、不读目标。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_make_pointer(
    const char *target_key, size_t target_size, const char *target_langtype,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error);

/* 解引用一个 Ptr：给出它指向的 key 文本与目标 langtype。**只跟一跳**，不递归
 * （与 wire 的 Ptr 语义一致）。不是 Ptr → INVALID。 */
KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_dereference_pointer(
    const uint8_t *wire, size_t size, kvlang_runtime_text_buffer_t *output_key,
    kvlang_runtime_text_buffer_t *output_langtype,
    kvlang_runtime_error_t *error);

/* ── 不提供的能力：写 `/lib/<op>` 签名槽 ───────────────────────────────
 *
 * 旧 `kvlangDefRwir` 的对应物**不在 runtimec**（裁决 2026-11-10 (c)）：
 *
 *   - defrwir wire 格式的唯一真相在 **layout**（`kvkind.rs` 的 `new_defrwir`），
 *     抄一份进 runtimec 就是第二份真相；
 *   - layout 将提供纯声明 C ABI `kvlangLayoutDefineRwir(...)`，**由扩展方直接调用**；
 *   - runtimec 不新增对 layout 的依赖：扩展方本来已链接 layout，它负责把
 *     「layout 声明 + runtimec 扩展表」串起来（TASK-042 的分工）。
 *
 * 之所以写这一段而不是默默不提供：调用方在头里找不到会以为漏了，
 * 然后自己造 .kv 文本或抄 wire 格式——两条都是错的路。
 */

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_create(
    const kvlang_runtime_create_request_t *request,
    kvlang_runtime_t **output, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_destroy(
    kvlang_runtime_t *runtime, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_execute(
    kvlang_runtime_t *runtime,
    const kvlang_runtime_execute_request_t *request,
    kvlang_runtime_execute_result_t *output,
    kvlang_runtime_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
