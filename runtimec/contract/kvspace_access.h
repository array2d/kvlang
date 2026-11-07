#ifndef RUNTIMEC_CONTRACT_KVSPACE_ACCESS_H
#define RUNTIMEC_CONTRACT_KVSPACE_ACCESS_H

/*
 * 组件：contract/kvspace_access
 * 作用：runtimec 访问 kvspace 的**唯一入口**。它持有的就是一个 `void *` 句柄
 *       （kvspace 仓 `include/kvspace/kvspace.h` 的句柄类型）与分配器；能力本身
 *       直接调 `kvspaceGet` / `kvspaceSetValue` 等仓库头里的函数，不再经过任何
 *       函数指针表。
 * 联系：engine 的 session 拥有一个实例并按指针下传；vmkv / vmbin / engine 都只
 *       认这一个类型。
 * 约束：
 *   - **只借用句柄**：不 connect、不 close、不拥有。生命周期由组合根保证。
 *   - **不封装 kvspace ABI**：本模块提供的是「runtimec 需要的组合动作」
 *     （例如「读回一条值并校验容量」），不是一个可以替换实现的抽象层。
 *     能直接调 kvspace 的地方就直调，不在这里再包一层。
 *   - body 偏移算术只有一处：`runtimec_xvalue_body_address`（地址形态）与
 *     `runtimec_xvalue_body_from_wire`（字节形态），都在本模块。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kvspace/xvalue_head.h>

#include "contract/allocator.h"
#include "contract/checkpoint.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 进程内地址。两者都是「不透明整数」：`xvalue` 指向一条 XValue 的 head 起点，
 * `body` 指向它的 body 起点。它们之间只差一个固定偏移——那个偏移的算术**只在
 * 本模块出现一次**（见文件末尾的 `runtimec_xvalue_body_address`）。
 */
typedef uint64_t runtimec_xvalue_address_t;
typedef uint64_t runtimec_value_body_address_t;

#define RUNTIMEC_XVALUE_ADDRESS_NONE UINT64_C(0)
#define RUNTIMEC_VALUE_BODY_ADDRESS_NONE UINT64_C(0)
/* body 起点相对 XValue 基址的固定偏移。**只有一个定义点**，两种输入形态共用。 */
#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES KVSPACE_XVALUE_HEAD_LEN

/*
 * key 文本缓冲的容量 = **kvspace 能接受的 key 上界**（它拒绝长度 >= 本值的 key）。
 *
 * **只有一个定义点**：建 key 的缓冲（本模块二十余处）与地址账本记录的 key 槽
 * 必须是同一个数。两个数出现分歧时，症状是「能建出来的 key 存不进账本」——
 * 一个编码一个含义。
 */
#define RUNTIMEC_KVSPACE_KEY_CAPACITY 1024U

#ifdef __cplusplus
static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U,
              "V.body must start 16 bytes after the XValue base");
#else
_Static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U,
               "V.body must start 16 bytes after the XValue base");
#endif

/* 一条 XValue 的头部（16 字节，见 kvspace 的 xvalue_head.h）。 */
typedef struct runtimec_kvspace_value_header {
    uint64_t value_size;
    uint64_t value_revision;
    uint32_t flags;
    uint32_t reserved;
} runtimec_kvspace_value_header_t;

typedef struct runtimec_handoff_request {
    runtimec_string_view_t operation;
    runtimec_checkpoint_t checkpoint;
} runtimec_handoff_request_t;

typedef runtimec_status_t (*runtimec_kvspace_list_visitor_function_t)(
    void *context, runtimec_key_view_t key, runtimec_error_t *error);

/*
 * 地址账本的一个槽：**key 是主键**（一 key 一条，重复解析就地更新，见 .c 的
 * `runtimec_kvspace_access_ledger_record`）。
 *
 * `key_size == 0` 是**唯一**的空槽判据——不另设 `occupied` 位：两个字段描述
 * 同一件事就是两份真相，而它们一旦不同步，症状是「槽里有地址却查不到」。
 */
typedef struct runtimec_kvspace_resolved_address {
    uint64_t address;
    uint64_t observed_size;
    uint64_t key_hash; /* 命中判据的一半；另一半是 key 逐字节比 */
    uint16_t key_size;
    char key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
} runtimec_kvspace_resolved_address_t;

typedef struct runtimec_kvspace_access {
    runtimec_allocator_t allocator;
    void *handle;
    /*
     * 该 store 实例的稳定身份，必须非零。由组合根提供：本模块不知道自己在跟谁
     * 说话，不推导、不哈希路径、不猜。它随每次 source revision 原样外送，是
     * 派生状态（compilation、地址）随 store 实例失效的判据。
     */
    uint64_t instance_identifier;
    /* 以下为实现内部状态：解析过的地址与当前 generation。调用方不得读写。 */
    struct runtimec_kvspace_resolved_address *resolved_addresses;
    size_t resolved_address_count;    /* 已占用槽数 */
    size_t resolved_address_capacity; /* 槽数（2 的幂） */
    uint64_t generation;
    bool generation_started;
} runtimec_kvspace_access_t;

typedef struct runtimec_kvspace_access_request {
    runtimec_allocator_t allocator;
    void *borrowed_kvspace_handle;
    uint64_t instance_identifier;
} runtimec_kvspace_access_request_t;

/*
 * 创建/销毁。创建只分配所有者结构并登记句柄，**不连接**；销毁**不关闭**句柄。
 */
runtimec_status_t runtimec_kvspace_access_create(
    const runtimec_kvspace_access_request_t *request,
    runtimec_kvspace_access_t **output, runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_destroy(
    runtimec_kvspace_access_t *access, runtimec_error_t *error);

runtimec_status_t runtimec_kvspace_access_read(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_read_header(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_kvspace_value_header_t *output,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_write(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_write_partial(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset, runtimec_bytes_view_t value,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_remove(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_list(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_kvspace_list_visitor_function_t visitor, void *visitor_context,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_watch(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t expected,
    uint64_t timeout_nanoseconds, runtimec_bytes_buffer_t *observed,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_signal(
    runtimec_kvspace_access_t *access, runtimec_key_view_t key,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_source_revision(
    runtimec_kvspace_access_t *access, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_resolve_static_xvalue_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_resolve_frame_xvalue_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_ensure_frame(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_ensure_zero_value(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_string_view_t langtype,
    runtimec_error_t *error);
/*
 * 该 langtype 是否有**定宽零值**——即 `ensure_zero_value` 能不能建它。
 *
 * 存在的理由只有一个：**帧准备必须能在调用 `ensure_zero_value` 之前问出这件事**，
 * 否则它只能靠「调用失败再分类」来区分「这个类型本来就不该由我建」与「建失败了」，
 * 而那正是把错误当控制流。判据本身仍然只有一份：`ensure_zero_value` 用的宽度表。
 *
 * **定长数组是定宽**：`[N]K` 与 `[d0,d1,…]K`（各维非空十进制）的零值宽度是
 * 各维乘积 × 基种类宽度，帧准备按它物化。`vthread·pc()` 的 `result1` 是 `[2]int32`，
 * 槽不物化则第一次写就 `UNBOUND_SLOT`。
 *
 * **变长数组不是定宽**：`[]K`、`[?]K`、`[*]K`、`[+]K` 的长度由内容决定，零值长度
 * 是 0，物化出零长槽反而让第一次真实写必失败——它们仍**由源码里的 `·new` 创建**，
 * 帧准备不建也不绑。散列容器、结构体、元组 map、字符串同理。
 */
bool runtimec_kvspace_access_langtype_has_fixed_zero_width(
    runtimec_string_view_t langtype);

/*
 * 定宽 langtype 的零值字节数：`has_fixed_zero_width` 的那一事实的**数值形态**。
 *
 * 存在的理由：调用方有时不只需要「是不是定宽」，还需要「多宽」——测试要断言
 * `[2]int32` 恰好 8 字节、`[2,3]int32` 恰好 24 字节；将来若帧准备要一次算总量，
 * 也靠它而不是再写一份解析。
 *
 * 与 `has_fixed_zero_width` **同源**：两者都调同一个 `runtimec_kvspace_zero_width`，
 * 不是两份判据。不是定宽时返回 false 且 `*output_width` 置 0。
 */
bool runtimec_kvspace_access_langtype_zero_width(
    runtimec_string_view_t langtype, uint64_t *output_width);
/*
 * 一帧的存储 generation。
 *
 * 语义：**属于帧本身，不属于「恰好第一个被绑的槽」**。窗口层建帧、绑参数、绑局部
 * 都要用同一个 generation，并且执行期由 handler 懒绑定的槽也要用它——若它取决于
 * 「有没有槽先被绑上」，那么一个只有容器局部的帧就永远拿不到 generation，而容器
 * 恰恰是唯一延迟绑定的那一类。
 *
 * 取值来源是帧目录里那枚由 `ensure_frame` 保证存在的系统键（`‥lib`），调用方不必
 * 知道它是哪个键——键的拼法只在本模块内有一份。
 */
runtimec_status_t runtimec_kvspace_access_frame_generation(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, uint64_t *output_generation,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_checkpoint(
    runtimec_kvspace_access_t *access, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error);
runtimec_status_t runtimec_kvspace_access_handoff(
    runtimec_kvspace_access_t *access,
    const runtimec_handoff_request_t *request, runtimec_error_t *error);

/* 该 key 是否属于可以解析成稳定基址的静态 key。 */
bool runtimec_kvspace_access_key_is_static(runtimec_key_view_t key);

/* XValue body 偏移算术的**唯一定义点**，两种输入形态。 */
runtimec_value_body_address_t runtimec_xvalue_body_address(
    runtimec_xvalue_address_t xvalue_address);
runtimec_xvalue_address_t runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address);
/*
 * 值体的**有效长度**（storetype 4 取 attr tag 2 的 `reallen`，其它取 `bodylen`）。
 *
 * **无错误通道**：不可信 wire（零地址/下溢/storetype 不认识/超 uint32/缺 tag 2/
 * tag 2 畸形或重复/`reallen > bodylen`）与「长度本来就是 0」**都返回 0**。
 *
 * ⇒ **任何「读一个 storetype 4 值并需要知道它是不是空」的新消费方，必须走
 * 带状态码的路径，不能用本函数判断空值。** 第一个这样的消费方是 `‥status`（J-5）：
 * 实现 J-5 时必须先补一条能区分「空」与「坏」的入口，否则 status 损坏会被当成
 * 「未设置」。
 *
 * 现在没有 storetype 4 的生产者，所以这条歧义没有活的危险；一旦 J-5 成为第一个
 * 消费者就会立刻变成真问题——在消费者出现之前把约束写死，比事后补便宜。
 */
uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address);
/*
 * 字节形态：从**已读回内存的完整 wire**（16 字节 head + body + attr）取出 **body**
 * 视图——**不含 attr**。长度取自头的 `bodylen`，不从 `wire.size` 推：
 * `wire.size - 16` 是「body + attr」，拿去当 body 会把属性字节当成值的内容。
 *
 * 与 `runtimec_xvalue_body_address` 是同一处偏移算术的两个入口——一个从地址推，
 * 一个从字节切。分成两处写就是第二份算术。
 *
 * 头不自洽（未知 storetype / body 或 attr 越出 wire）返回空视图，不猜。
 */
runtimec_string_view_t runtimec_xvalue_body_from_wire(
    runtimec_bytes_view_t wire);

void runtimec_kvspace_value_header_reset(runtimec_kvspace_value_header_t *header);
void runtimec_handoff_request_reset(runtimec_handoff_request_t *request);
bool runtimec_handoff_request_is_valid(const runtimec_handoff_request_t *request);

#ifdef __cplusplus
}
#endif

#endif
