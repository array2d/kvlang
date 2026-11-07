/*
 * 组件：vmbin/value_access 的实现
 * 作用：按参数项的引用种类（reference_kind）找值体地址（或池内字面量），
 *       并直接读写值体字节；值的**形态**（普通/指针/扩展）另由 langtype 前缀派生。
 * 约束：值头/值体的几何只经合同——基址 → 值体用 runtimec_xvalue_body_address，
 *       值体 → 当前长度用 runtimec_value_body_length；本文件不写任何偏移算术。
 *       实参是字面量时绑定表给的是内联字节，直接借出，不推导任何地址。
 *       写只接受与当前值体等长的值：改长度要连带改值头与 attr 位置，那是存储层
 *       的事，vmbin 不做，也不回退成按 key 写。
 */

#include "contract/langtype.h"
#include "value_access.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/frame_bindings.h"
#include "frame_slot.h"
#include "frame_target_key.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"

static runtimec_status_t vmbin_value_access_fail(
    runtimec_error_t *error, runtimec_status_t status,
    vmbin_value_access_diagnostic_t code, const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                       (int64_t)code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/*
 * 成员键的地址解析：**每次按 key 现解析**，不查也不写绑定表
 * （`decision-member-slot-binding.md` §三）。
 *
 * 三步：拼 `<frame_path>/<符号名>` → 向 host 现解析 XValue 基址 → 取 body 地址。
 * key 不存在是**真实失败**（不是「空值」）：成员没被写过就读，与「局部读而未写」
 * 同一类，报 `UNBOUND_SLOT` 并把 key 文本写进消息——只报「槽没绑」的话，
 * 下一个人不知道该是哪个 key。
 */
static runtimec_status_t vmbin_value_access_locate_member(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t member_name,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    char key_text[512];
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    runtimec_status_t status;

    (void)argument;
    if (frame->kvspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
            "frame carries no kvspace access for a member key");
    }
    /*
     * **基座的 key 走共用派生**（LOCAL → 帧槽、STATIC → 绝对路径本身）。
     *
     * 这里曾经直接调只接 LOCAL 的 `vmbin_frame_target_key`——于是基座是绝对路径
     * （`062_unique_paths` 的 `/tmp/up` 这类）时成员键建不出来，报
     * 「member key cannot be built」。**这是「参数 → key 文本」这条规则的第三个
     * 出口**：前两个（`container_target_key`、`vmbin_handler_write_new_value`）
     * 被当成两个独立 bug 修掉之后，这一处才现形。
     *
     * 成员键 = 该 key + `·<成员名>`，拼接规则在下游，本处不改。
     */
    status = vmbin_value_access_argument_key(frame, argument, key_text,
                                             sizeof(key_text), error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key cannot be built");
    }
    /*
     * ── base 的值本身是路径时，成员键沿该路径展开 ──────────────────────
     *
     * spec `15-成员访问.kv`：「base 必须是容器……或**路径串**」。这一条在实现里
     * 很长一段时间是**空条款**——没有任何代码
     * 读过「base 的值」当路径用。
     *
     * 触发它的第一个真实形态是 structref 引用值：`*cur -> _5` 之后 `_5` 的 body
     * 就写着目标容器的 key（`/vthread/1/[1]/n1`），而 `_5·val` 若按 `_5` 这个
     * **符号名**拼键，就是 `/vthread/1/[2.7]/_5·val` —— 那个 key 从来不存在，
     * 链表/树这类「解引用存进局部再迭代」的写法整类失败。
     *
     * **代价**：这里比原来多一次 `kvspaceGet`（先取 base 的值、再解析成员）。
     * 借用视图，不分配：读回的是存储内指针，不是拷贝。
     *
     * 判据两条同时成立才沿路径走，缺一不猜：
     *   1. base 的**声明类型**是 structref 或路径串（`*T` 指针不在内——它的 body
     *      也是 key，但语义是「跟一跳」，混用会让指针拷贝被当成别名）；
     *   2. base 的**值 body 以 `/` 开头**（真的是一条绝对路径）。
     */
    {
        /*
         * **判据是「base 的值是不是一条绝对路径」，不是「base 的类型是什么」。**
         *
         * 先试过按类型判（`vmbin_value_access_langtype` 查 base 的声明类型），
         * 实测走不通：`argument` 是 `_5·val` 这个**成员**符号，它的类型是成员值
         * （`int64`）；而按名字查 base（`_5`）还需要一条「按名字找符号下标」的
         * 接口——符号池只有「按下标取值」，没有反向索引。
         *
         * 值的形态本身就是充分证据：**绝对路径文本以 `/` 开头**，而 `int64`、
         * `bool`、数组 body 都不可能以 `/` 开头（标量的首字节是数值，数组是元素
         * 编码）。所以这里读一次 base 的 body、看首字节，比补一条反查接口更短，
         * 且判据更贴近事实。
         *
         * **代价**：比原来多一次 `kvspaceGet`（先取 base 的值、再解析成员）。
         * 借用视图，不分配——读回的是存储内指针，不是拷贝。
         *
         * 长度上限 512 是 key 缓冲的容量；真正的合法性由随后那次 key 解析把关。
         */
        runtimec_string_view_t base_name = runtimec_string_view_make(
            member_name.data, member_name.size);
        vmbin_value_location_t base_value;
        const char *separator = NULL;
        (void)base_value;

        for (size_t offset = 0U; offset + 2U <= member_name.size; ++offset) {
            if ((uint8_t)member_name.data[offset] == 0xC2U &&
                (uint8_t)member_name.data[offset + 1U] == 0xB7U) {
                separator = member_name.data + offset;
                break;
            }
        }
        if (separator != NULL) {
            base_name = runtimec_string_view_make(
                member_name.data, (size_t)(separator - member_name.data));
        }
        /*
         * **base 的值要按 base 自己的 key 去读**，不能复用 `argument`：
         * `argument` 指向成员符号（`_5·val`），对它能定位到的是成员、不是 base。
         * 这里先拼出 `<帧根>/<base 名>`，解析它的 XValue，再读 body——就是
         * 「多一次 kvspaceGet」那一笔。
         */
        if (separator != NULL) {
            char base_key[512];
            const int written = snprintf(
                base_key, sizeof(base_key), "%.*s/%.*s",
                (int)frame->frame_path.size, frame->frame_path.data,
                (int)base_name.size, base_name.data);

            if (written > 0 && (size_t)written < sizeof(base_key)) {
                runtimec_xvalue_address_t base_address =
                    RUNTIMEC_XVALUE_ADDRESS_NONE;
                uint64_t base_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;

                if (runtimec_kvspace_access_resolve_frame_xvalue_address(
                        frame->kvspace_access, &frame->snapshot,
                        runtimec_key_view_from_c_string(base_key),
                        &base_address, &base_generation, NULL) ==
                        RUNTIMEC_STATUS_SUCCESS &&
                    base_address != RUNTIMEC_XVALUE_ADDRESS_NONE) {
                    const runtimec_value_body_address_t base_body =
                        runtimec_xvalue_body_address(base_address);
                    const size_t base_size =
                        (size_t)runtimec_value_body_length(base_body);

                    if (base_body != RUNTIMEC_VALUE_BODY_ADDRESS_NONE &&
                        base_size != 0U &&
                        ((const uint8_t *)(uintptr_t)base_body)[0] == '/' &&
                        base_size + member_name.size + 1U <= sizeof(key_text)) {
                        const size_t tail_size =
                            member_name.size -
                            (size_t)(separator - member_name.data);

                        memcpy(key_text, (const void *)(uintptr_t)base_body,
                               base_size);
                        memcpy(key_text + base_size, separator, tail_size);
                        key_text[base_size + tail_size] = '\0';
                    }
                }
            }
        }
        (void)base_value;
    }
    status = runtimec_kvspace_access_resolve_frame_xvalue_address(
        frame->kvspace_access, &frame->snapshot,
        runtimec_key_view_from_c_string(key_text), &xvalue_address,
        &generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        static _Thread_local char message[512];
        static const char prefix[] =
            "member key is not present at execution time: ";
        const size_t prefix_size = sizeof(prefix) - 1U;
        const size_t key_size = strlen(key_text);
        const size_t bounded =
            key_size < sizeof(message) - prefix_size - 1U
                ? key_size
                : sizeof(message) - prefix_size - 1U;

        memcpy(message, prefix, prefix_size);
        memcpy(message + prefix_size, key_text, bounded);
        message[prefix_size + bounded] = '\0';
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT, (const char *)message);
    }
    const runtimec_value_body_address_t body_address =
        runtimec_xvalue_body_address(xvalue_address);
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
            "member address has no value body");
    }
    output_location->read_bytes = (const uint8_t *)(uintptr_t)body_address;
    /* **写视图也给**：容器已存在时写成员是合法的（§五反向对照）。容器不存在
     * 时上面的解析已经失败，走不到这里。 */
    output_location->write_bytes = (uint8_t *)(uintptr_t)body_address;
    output_location->size = (size_t)runtimec_value_body_length(body_address);
    output_location->is_whole_wire = false;
    (void)member_name;
    return RUNTIMEC_STATUS_SUCCESS;
}


/* 前向声明：解引用要先借用「不带 dereference 的同一参数」的普通解析。 */
/*
 * `is_none` 的**唯一判据**。三态：任一事实未知 ⇒ UNKNOWN。
 *
 * 与编码期 `binrwir_literal_is_none_value` 用同一组 wire 事实
 * （`bodylen == 0` 且 `attrslen == 0`），不各写一套边界。
 */
vmbin_value_fact_t vmbin_value_location_is_none(
    const vmbin_value_location_t *location) {
    if (location == NULL || location->body_is_empty == VMBIN_VALUE_FACT_UNKNOWN ||
        location->has_langtype_record == VMBIN_VALUE_FACT_UNKNOWN ||
        location->is_directory_key == VMBIN_VALUE_FACT_UNKNOWN) {
        return VMBIN_VALUE_FACT_UNKNOWN;
    }
    if (location->is_directory_key == VMBIN_VALUE_FACT_YES) {
        return VMBIN_VALUE_FACT_NO;
    }
    return (location->body_is_empty == VMBIN_VALUE_FACT_YES &&
            location->has_langtype_record == VMBIN_VALUE_FACT_NO)
               ? VMBIN_VALUE_FACT_YES
               : VMBIN_VALUE_FACT_NO;
}

/*
 * 从已定位的位置填 `is_none` 的三个事实。
 *
 * 拿得到 wire 头就填真值；拿不到（派生路径上只有 body 视图且反推不出基址）
 * 就留 `UNKNOWN`——**不许默认成「不是 None」**（架构师裁决：拿不到事实时
 * 调用点必须具名失败，默认 false 正是 `0x4406` 通用消息压过具名诊断的来源）。
 */
static void vmbin_value_access_fill_none_facts(
    vmbin_value_location_t *location) {
    const void *wire = NULL;

    if (location == NULL) {
        return;
    }
    if (location->is_whole_wire) {
        wire = location->read_bytes;
    } else if (location->read_bytes != NULL) {
        const runtimec_xvalue_address_t base =
            runtimec_value_body_xvalue_address(
                (runtimec_value_body_address_t)(uintptr_t)location->read_bytes);
        if (base != RUNTIMEC_XVALUE_ADDRESS_NONE) {
            wire = (const void *)(uintptr_t)base;
        }
    }
    if (wire == NULL) {
        return; /* 三个字段保持 UNKNOWN */
    }
    {
        const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)wire;
        const uint64_t total = kvspaceXvalueHeadTotalLen(head);

        if (total == 0U) {
            return; /* head 不自洽：事实未知，不猜 */
        }
        location->body_is_empty = kvspaceXvalueHeadLen(head) == 0U
                                      ? VMBIN_VALUE_FACT_YES
                                      : VMBIN_VALUE_FACT_NO;
        location->has_langtype_record =
            kvspaceXvalueHeadAttrsLen(head) == 0U ? VMBIN_VALUE_FACT_NO
                                                  : VMBIN_VALUE_FACT_YES;
    }
    /* 目录键：位置本身是 index（body 为空且 key 以 `/` 结尾）——本层拿不到
     * key 文本，保持调用点传入的既有值（默认 NO）。 */
}

static runtimec_status_t vmbin_value_access_locate_plain(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

/*
 * 显式解引用：参数项的 `dereference=1` 表示「这个值是一个指针，跟一跳再访问」。
 *
 * 四件事都只在这一层做（handler 里不许重复）：
 *   1. 先按**不带 dereference 的同一参数**解析出指针值本身；
 *   2. 形态必须是 `VMBIN_VALUE_ACCESS_FORM_POINTER`（由 langtype 的 `*` 前缀
 *      派生，不读 head）——不是指针就具名失败，不猜；
 *   3. 指针值的 body 就是目标 key 文本，拿它**再解析一次**；
 *   4. 只返回目标位置：绑定表仍记参数项那个槽，**不把 target 写进绑定表**
 *      （绑定表是「槽 → 值」的一次性事实，每跳都写就把表变成了缓存）。
 */
static runtimec_status_t vmbin_value_access_dereference_ex(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, bool missing_target_is_none,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;
    vmbin_value_location_t pointer_location;
    runtimec_xvalue_address_t target_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_value_body_address_t target_body = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    uint8_t form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    runtimec_status_t status;

    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "a literal has no address to follow");
    }
    plain.dereference = 0U;
    status = vmbin_value_access_locate_plain(frame, &plain, &pointer_location,
                                             error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_form(frame, &plain, &form, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (form != VMBIN_VALUE_ACCESS_FORM_POINTER) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER,
            "dereferenced operand does not hold a pointer");
    }
    if (pointer_location.read_bytes == NULL || pointer_location.size == 0U ||
        frame->kvspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "pointer value does not carry a resolvable target key");
    }
    {
        const runtimec_key_view_t key = runtimec_key_view_make(
            (const char *)pointer_location.read_bytes, pointer_location.size);

        if (pointer_location.size >= sizeof("/vthread/") - 1U &&
            memcmp(pointer_location.read_bytes, "/vthread/",
                   sizeof("/vthread/") - 1U) == 0) {
            uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
            status = runtimec_kvspace_access_resolve_frame_xvalue_address(
                frame->kvspace_access, &frame->snapshot, key, &target_address,
                &generation, error);
            if (status == RUNTIMEC_STATUS_SUCCESS &&
                generation != frame->snapshot_generation) {
                return vmbin_value_access_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                    "dereference target is not in the frame generation");
            }
        } else {
            status = runtimec_kvspace_access_resolve_static_xvalue_address(
                frame->kvspace_access, &frame->snapshot, key, &target_address,
                error);
        }
    }
    /*
     * ── 解引用**读**到不存在的目标 ⇒ `None` ──────────────────────────────
     *
     * spec `05-runtime语义/12-动态键.kv` §四：「`resolveptr` 定位到的目标不存在时，
     * **读得 `None`**（成员访问语义）；普通绝对 key 的读仍失败封闭。」
     *
     * 这是「解引用**写**创建目标」的读侧对偶，两条走不同入口：
     *   - 写（`write_body`）拿到的仍是失败，随后去创建；
     *   - 读（本支，`missing_target_is_none`）交出一条**空 body、无 langtype**
     *     的 None 位置，让 `cur == None` 成立。
     *
     * **只有解引用这一层给 None**：普通绝对 key 的读不经这里，仍具名失败。
     * 这正是规范那句「两者走不同路径，不冲突」的落地。
     */
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        target_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        runtimec_xvalue_body_address(target_address) ==
            RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
        if (missing_target_is_none) {
            output_location->read_bytes = NULL;
            output_location->write_bytes = NULL;
            output_location->size = 0U;
            output_location->is_whole_wire = false;
            /*
             * 解引用到的目标**不存在** ⇒ 按 spec `12-动态键` §四 它就是 `None`，
             * 而这正是 `is_none` 三条全部成立的情形（没有 wire，也就没有 body、
             * 没有 langtype 记录）。**这不是「拿不到事实」，是事实本身**——
             * 三者标 YES/NO/NO，让谓词直接答 YES，不要在调用点重判一次。
             */
            output_location->body_is_empty = VMBIN_VALUE_FACT_YES;
            output_location->has_langtype_record = VMBIN_VALUE_FACT_NO;
            output_location->is_directory_key = VMBIN_VALUE_FACT_NO;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return status != RUNTIMEC_STATUS_SUCCESS
                   ? status
                   : vmbin_value_access_fail(
                         error, RUNTIMEC_STATUS_INVALID,
                         VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                         "dereference target has no derivable body");
    }
    target_body = runtimec_xvalue_body_address(target_address);
    output_location->read_bytes = (const uint8_t *)(uintptr_t)target_body;
    output_location->write_bytes = (uint8_t *)(uintptr_t)target_body;
    output_location->size = runtimec_value_body_length(target_body);
    output_location->is_whole_wire = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || argument == NULL || output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    runtimec_status_t status;
    if (argument->dereference != 0U) {
        status = vmbin_value_access_dereference_ex(frame, argument, false,
                                                   output_location, error);
    } else {
        status = vmbin_value_access_locate_plain(frame, argument,
                                                 output_location, error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        vmbin_value_access_fill_none_facts(output_location);
    }
    return status;
}

static runtimec_status_t vmbin_value_access_locate_plain(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    runtimec_bytes_view_t literal;
    runtimec_string_view_t langtype;
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_value_body_address_t body_address = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    runtimec_status_t status;

    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || frame->program == NULL || argument == NULL ||
        output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        status = binrwir_binfunc_get_literal(frame->program,
                                             (uint32_t)argument->payload,
                                             &literal, &langtype);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL,
                "literal index is not in the program pool");
        }
        output_location->read_bytes = literal.data;
        output_location->write_bytes = NULL;
        output_location->size = literal.size;
        output_location->is_whole_wire = true;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_STATIC:
        /* 静态参数的 payload 已经是值体地址；值头在它前面。 */
        if (argument->payload == BINRWIR_BODY_ADDRESS_NONE) {
            /*
             * 静态参数在编码期可能还没解析（payload 记 NONE）。这里只做**解析**，
             * 不创建：创建源是源码里的声明（标量 `v:int32 = …`、容器 `·new`），
             * 帧准备只是执行声明的手段；执行指令只许触碰已经存在的 key。
             *
             * **绝对 key 的存在性是运行时前提，不是静态保证**：layout 对它什么都不做
             * （不要求声明、不读存储核对），碰不到就在这里失败封闭。解析不到是真实
             * 错误——读一个不存在的静态 key 不该被当成「空值」，写一个不存在的 key
             * 也不该顺手建出来。
             */
            /* 初始化成空视图：下面 host 能力缺失时会短路，`key_text` 还没被赋值，
             * 读它的 size 就是读未初始化栈——拼消息时就会成段越界。 */
            runtimec_string_view_t key_text = runtimec_string_view_make(NULL, 0U);
            runtimec_string_view_t key_langtype;
            runtimec_xvalue_address_t resolved = RUNTIMEC_XVALUE_ADDRESS_NONE;
            if (frame->kvspace_access == NULL ||
                binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                           &key_text, &key_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                runtimec_kvspace_access_resolve_static_xvalue_address(
                    frame->kvspace_access, &frame->snapshot,
                    runtimec_key_view_make(key_text.data, key_text.size),
                    &resolved, error) != RUNTIMEC_STATUS_SUCCESS ||
                resolved == RUNTIMEC_XVALUE_ADDRESS_NONE) {
                {
                    /*
                     * 说清是哪个 key：只报「解析失败」等于让人自己去翻符号池。
                     *
                     * 缓冲必须是**线程局部静态**，不能是栈上的：`runtimec_error_set`
                     * 只借用视图，函数一返回栈帧就没了，调用方拿到的是悬垂指针——
                     * 实测紧跟着的 printf 还能读到正确文本（栈尚未被覆盖），
                     * 隔几行再读就是垃圾。执行窗口那边用的是同一个做法。
                     */
                    static const char prefix[] =
                        "static key is unresolved at execution time: ";
                    static _Thread_local char message[512];
                    const size_t prefix_size = sizeof(prefix) - 1U;
                    const size_t key_size =
                        key_text.size < sizeof(message) - prefix_size - 1U
                            ? key_text.size
                            : sizeof(message) - prefix_size - 1U;
                    /* 走不到 get_symbol 的分支（host 能力缺失）连 key 都没有：
                     * 此时只报「未解析」，不去解引用空视图。 */
                    if (key_size == 0U) {
                        static const char bare[] =
                            "static key is unresolved at execution time";
                        memcpy(message, bare, sizeof(bare));
                    } else {
                        memcpy(message, prefix, prefix_size);
                        memcpy(message + prefix_size, key_text.data, key_size);
                        message[prefix_size + key_size] = '\0';
                    }
                    return vmbin_value_access_fail(
                        error, RUNTIMEC_STATUS_INVALID,
                        VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, message);
                }
            }
            body_address = runtimec_xvalue_body_address(resolved);
        } else {
            body_address = (runtimec_value_body_address_t)argument->payload;
        }
        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
                "static argument has no derivable value body");
        }
        break;
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
    case BINRWIR_REFERENCE_LOCAL: {
        const binrwir_frame_binding_t *binding = NULL;
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

        /*
         * **成员键每次按 key 现解析，不走绑定表**
         * （`decision-member-slot-binding.md` §四.3）。
         *
         * 成员是容器值内部的存储，容器会被整容器写**整体重建**，地址永不稳定；
         * 绑死地址就会读到已不存在的 box。所以这里：
         *   拼 `<frame_path>/<符号名>` → 现解析 XValue 基址 → 取 body 地址。
         * key 不存在 ⇒ `UNBOUND_SLOT`，**消息里带 key 文本**（否则下一个人只看到
         * 「槽没绑」，不知道该是哪个 key）。
         */
        {
            runtimec_string_view_t member_name;
            runtimec_string_view_t member_langtype;
            if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                           &member_name, &member_langtype) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                binrwir_symbol_name_is_member(member_name)) {
                return vmbin_value_access_locate_member(frame, argument,
                                                        member_name,
                                                        output_location, error);
            }
        }
        if (frame->bindings == NULL ||
            vmbin_frame_slot_of_argument(frame->program, argument,
                                         &binding_slot) !=
                RUNTIMEC_STATUS_SUCCESS ||
            binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                       frame->snapshot_generation,
                                       &binding) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "argument slot is not bound");
        }
        if (binding->kind == (uint8_t)BINRWIR_BINDING_INLINE) {
            /* 内联实参：字节就在 Program 字面量池里，只有读视图。 */
            output_location->read_bytes = binding->bytes;
            output_location->write_bytes = NULL;
            output_location->size = binding->length;
            output_location->is_whole_wire = true;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (binding->kind != (uint8_t)BINRWIR_BINDING_ADDRESS ||
            binding->xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "binding carries no XValue address");
        }
        xvalue_address = binding->xvalue_address;
    }
        body_address = runtimec_xvalue_body_address(xvalue_address);
        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "binding address has no value body");
        }
        break;
    default:
        /* FUNCTION 是调用目标，不是值；未知 kind 同样没有可写位置。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "argument has no value body");
    }
    /* 地址的有效性由来源证明（STATIC 的 payload 经参数校验非零，绑定只在解析
     * 成功后写入）；长度 0 是合法的空值体（None），不是坏地址的哨兵。 */
    output_location->read_bytes = (const uint8_t *)(uintptr_t)body_address;
    output_location->write_bytes = (uint8_t *)(uintptr_t)body_address;
    output_location->size = (size_t)runtimec_value_body_length(body_address);
    output_location->is_whole_wire = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_buffer_is_valid(output)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid read buffer");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->size = location.size;
    if (output->data == NULL || output->capacity < location.size) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    if (location.size != 0U) {
        memcpy(output->data, location.read_bytes, location.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * body 定位：先按 wire 语义定位，再把字面量那一段剥成 body。
 *
 * 只对「wire 语义给的是整条 wire」的两种引用剥离：LITERAL 与内联绑定。其余
 * 引用 `locate` 给的已经是 body，原样返回——再加一次剥离就会把 body 当 wire
 * 解，读出一个不存在的位置。
 */
/*
 * **读语义**的定位：解引用到不存在的目标时给 None（空 body），不是失败。
 *
 * 与 `locate_body` 的唯一差别就在解引用那一支；其余引用种类完全共用。
 * 调用方**只该是读路径**（`read_body`、打印、比较等）：写路径走 `locate_body`，
 * 那里缺失仍失败、随后由 `write_body` 创建（#632 的写侧）。
 */
runtimec_status_t vmbin_value_access_locate_body_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (output_location != NULL) {
        *output_location = (vmbin_value_location_t){
            .read_bytes = NULL,
            .write_bytes = NULL,
            .size = 0U,
            .is_whole_wire = false,
            .body_is_empty = VMBIN_VALUE_FACT_UNKNOWN,
            .has_langtype_record = VMBIN_VALUE_FACT_UNKNOWN,
            .is_directory_key = VMBIN_VALUE_FACT_NO};
    }
    if (frame == NULL || argument == NULL || output_location == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid locate request");
    }
    if (argument->dereference != 0U) {
        status = vmbin_value_access_dereference_ex(frame, argument, true,
                                                   output_location, error);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            vmbin_value_access_fill_none_facts(output_location);
        }
        return status;
    }
    status = vmbin_value_access_locate_plain(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 事实要在**剥成 body 之前**取：剥完就没有 wire 头可读了。 */
    vmbin_value_access_fill_none_facts(&location);
    if (!location.is_whole_wire) {
        *output_location = location;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    {
        const runtimec_value_body_address_t body_address =
            runtimec_xvalue_body_address(
                (runtimec_xvalue_address_t)(uintptr_t)location.read_bytes);

        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "wire has no derivable value body");
        }
        *output_location = location;
        output_location->read_bytes =
            (const uint8_t *)(uintptr_t)body_address;
        output_location->write_bytes = NULL;
        output_location->size =
            (size_t)runtimec_value_body_length(body_address);
        output_location->is_whole_wire = false;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_locate_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status =
        vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 判据只认 locate 自己给的标志：不可写 != 是整条 wire。 */
    if (!location.is_whole_wire) {
        *output_location = location;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * 整条 wire → body。长度经合同的唯一几何入口取，本文件不写偏移算术；
     * 零基址与溢出都由合同返回 NONE，这里折成 UNSUPPORTED 而不是 0 长度：
     * 「剥不出来」和「值是空的」是两件事。
     */
    {
        const runtimec_value_body_address_t body_address =
            runtimec_xvalue_body_address(
                (runtimec_xvalue_address_t)(uintptr_t)location.read_bytes);

        if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "wire has no derivable value body");
        }
        output_location->read_bytes =
            (const uint8_t *)(uintptr_t)body_address;
        /* 字面量与内联绑定都不可写：写它没有真实存储位置。 */
        output_location->write_bytes = NULL;
        output_location->size = (size_t)runtimec_value_body_length(
            body_address);
        output_location->is_whole_wire = false;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_read_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_buffer_is_valid(output)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid read buffer");
    }
    /* 读路径：解引用到缺失目标给 None（空 body），不是失败。 */
    status = vmbin_value_access_locate_body_read(frame, argument, &location,
                                                 error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->size = location.size;
    if (output->data == NULL || output->capacity < location.size) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    if (location.size != 0U) {
        memcpy(output->data, location.read_bytes, location.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 指令不创造 key。
 *
 * 曾有 `vmbin_value_access_prepare_static_target`：「写目标还不存在就建一个」。
 * 那是把创建放错了阶段——创建源是**源码里的声明**（标量类型声明、容器 `·new`），
 * 帧准备只是执行声明的手段；执行指令只许触碰已经存在的 key。删掉之后，STATIC 写目标若在执行期仍不存在，
 * 由 `locate_body` 的解析分支失败封闭（消息里带 key 名），不在这里补建。
 */


/*
 * **写一个尚未绑定的局部 ⇒ 建值 + 绑定**（写路径唯一的一处创建规则）。
 *
 * 为什么这条规则只在写入原语里：今天撞见的是 `copy`，明天会是 `map·get`、
 * 容器返回值、`compactarray·at`……给每条指令各加一个「目标未绑定就创建」的
 * 分支，同一条规则就散落在 N 处，漏掉一处就退化成 `UNBOUND_SLOT`，而那种缺陷
 * 只有特定数据流才能触发。
 *
 * **读写不对称是刻意的**：读一个没建过的局部 = 使用未初始化（错，仍是
 * `UNBOUND_SLOT`）；写它 = 初始化它（对，就地建出来）。两者本来就不同。
 *
 * 只对 LOCAL 生效：STATIC 绝对 key 的存在性是运行时前提，写一个不存在的绝对 key
 * 必须继续失败封闭（「指令不创造 key」对绝对 key 仍然成立）。
 *
 * 已绑定 ⇒ 什么都不做，维持「与当前值体等长」的直写规则，不重复创建。
 */
/*
 * 成员键的创建：**按 key 写一次，不绑定**（`decision-member-slot-binding.md` §四.3）。
 *
 * 与局部槽的差别只有一点，但那一点是根本的：
 *   局部槽 —— 建 key + **绑定地址**（地址在帧生命周期内稳定）；
 *   成员键 —— 只建 key，**永不绑定**（容器可被整体重建，地址不稳定）。
 *
 * 容器不存在时不在这里兜底创建：成员写要求 memhead 存在，kvspace 会硬失败——
 * 那是对的（「往一个不存在的容器里写成员」是真错误，不是缺能力）。
 */
static runtimec_status_t vmbin_value_access_create_member(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    runtimec_string_view_t declared;
    char langtype_text[256];
    char key_text[512];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype does not fit the member slot");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    status = vmbin_frame_target_key(frame, argument, key_text,
                                    sizeof(key_text));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key cannot be built");
    }
    /*
     * **按写入值建 wire**，不是空 body。这一点与非成员局部那条完全一致
     * （`create_unbound_local` 也是 `NewSlack(value.data, value.size, …)`）：
     * 建 key 只是「这个位置存在」，body 必须当场就是值本身——否则紧接着的
     * `write_body` 会拿一个 0 长 body 去 memcpy 一个 int64，得到畸形。
     *
     * langtype 用**声明类型**（`declared`），不用值自带的：槽类型是成员类型，
     * 值的 langtype 是它自己的。
     */
    if (kvspaceXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  (uint64_t)value.size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "member value cannot be encoded as an XValue");
    }
    message[0] = '\0';
    const int set_status = kvspaceSetValue(frame->kvspace_handle, key_text, wire,
                                           (uint32_t)wire_size, message,
                                           sizeof(message));
    free(wire);
    if (set_status != 0) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "member key could not be created in storage");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 写路径上「建值 / 核绑定」这一步只属于**拥有绑定槽**的引用种类：
 *
 *   LOCAL           建值并绑定（槽就是本帧的符号池项）；
 *   PARAMETER_WRITE 核调用方那一侧的绑定在不在。
 *
 * `STATIC` / `LITERAL` / `FUNCTION` 没有绑定槽——它们的地址在 `locate` 里现取。
 * 在这里插一步不会「多一层保险」，只会把一整类合法路径按「未处理的引用种类」
 * 报成失败：`vmbin.execute` 的 STATIC 写目标（编译期已解析出 body 地址）
 * 原本就是这样整组变红的。
 *
 * 解引用目标同理跳过：它由指针指向，本该已存在；未绑定时失败比替它建一个值
 * 更诚实（建出来的那个值没有任何人指得回去）。
 */
static bool vmbin_value_access_writes_through_binding_slot(
    const binrwir_argument_t *argument) {
    if (argument->dereference != 0U) {
        return false;
    }
    return argument->reference_kind == BINRWIR_REFERENCE_LOCAL ||
           argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE;
}

static runtimec_status_t vmbin_value_access_create_unbound_local(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    const binrwir_frame_binding_t *binding = NULL;
    runtimec_string_view_t declared;
    runtimec_key_view_t target_key;
    char langtype_text[256];
    char key_text[512];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    runtimec_status_t status;

    /*
     * **未处理的引用种类不许静默成功**（p1：不容许「以奇怪的方式成功」）。
     *
     * 本函数只负责「LOCAL 局部槽的建值 + 绑定」。`PARAMETER_WRITE` 是唯一另一种
     * 合法入口：目标是对端（调用方）的 key，由**绑定**给出——本帧 `frame_path`
     * 拼出来的一定是错 key（`/vthread/1/[2.4]/[0,+1]` 这种）。所以这里**只核
     * 绑定在不在**：在 ⇒ 合法且无事可做；不在 ⇒ 具名失败，说明调用方那一侧
     * 从没绑过它。
     *
     * 曾经这一支对一切非 LOCAL 都返回 `SUCCESS`——写形参因此既不建值、也不绑定、
     * 也不报错，随后 `LOCATE_WRITE` 读它报 `UNBOUND_SLOT`，而失败点指向的是
     * 「读」而不是「从未创建」。`strings.kv` 的 `replace_at` 就是这条。
     *
     * 其余种类不该走到这里（调用点的 `writes_through_binding_slot` 已挡），
     * 所以这一条是**直接调用**的兜底：漏挡即失败，不猜。
     */
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
        if (argument->reference_kind != BINRWIR_REFERENCE_PARAMETER_WRITE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
                "this reference kind has no binding slot to create or verify");
        }
        if (frame->bindings == NULL ||
            vmbin_frame_slot_of_argument(frame->program, argument,
                                         &binding_slot) !=
                RUNTIMEC_STATUS_SUCCESS ||
            binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                       frame->snapshot_generation,
                                       &binding) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT,
                "write parameter has no binding; the caller side never bound "
                "it");
        }
        /* 有绑定即合法：写形参的值由调用方那一侧的 key 承载，本函数无事可做。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (frame->bindings == NULL || frame->kvspace_access == NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * **成员键：建 key，但不绑定**（`decision-member-slot-binding.md` §四.3）。
     *
     * 与 `locate` 的成员分支同一条判据（`binrwir_symbol_name_is_member`）：
     * 成员不拥有稳定地址，所以「创建」这件事就是一次 `SetValue`，**没有第二步**。
     * 走到下面的绑定逻辑会给它一个注定失效的地址——那正是本裁决要消灭的形态。
     */
    {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;
        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            return vmbin_value_access_create_member(frame, argument,
                                                     value, error);
        }
    }
    if (vmbin_frame_slot_of_argument(frame->program, argument, &binding_slot) !=
        RUNTIMEC_STATUS_SUCCESS) {
        /* 换算不出绑定槽号 ⇒ 这是**已知会失败的写**，不能当「无事可做」放过去：
         * 放过去就是「写成功但值没落」，比失败更难查。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "local slot index is not in the program pool");
    }
    if (binrwir_frame_bindings_get(frame->bindings, binding_slot,
                                   frame->snapshot_generation,
                                   &binding) == RUNTIMEC_STATUS_SUCCESS) {
        /* **已有绑定 ⇒ 无事可做**：这是本函数唯一合法的「不创建」出口——
         * 槽已经存在且可写，`write_body` 随后会照常定位并写入。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /*
     * ── 声明类型缺席：从**写入 wire 的头**取类型 ──────────────────────────
     * （裁决 `decision-type-from-target-local.md` 第三段）
     *
     * 形态是 `绝对 key -> 局部`：layout 不知道绝对 key 的类型（运行态事实），
     * 编码器据此放行了缺席类型，于是这里必须自己定。
     *
     * **头里没有 langtype ⇒ 失败封闭**：
     *   - 不建成无类型值（读侧无从判宽，必然退化成「按字节猜」）；
     *   - **不回退到旧路径**（那是静默回退，A′ 那次的前置原则）；
     *   - 不猜一个「看起来合理」的类型。
     */
    if (declared.size == 0U) {
        if (!runtimec_bytes_view_is_valid(value) || value.data == NULL ||
            value.size < KVSPACE_XVALUE_HEAD_LEN) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "local slot has no declared langtype and its written value "
                "carries no readable head");
        }
        const uint8_t *head_langtype_bytes = NULL;
        const uint16_t head_langtype_size = kvspaceXvalueHeadLangtypeBounded(
            value.data, (uint64_t)value.size, &head_langtype_bytes);
        const runtimec_string_view_t head_langtype =
            head_langtype_size != 0U && head_langtype_bytes != NULL
                ? runtimec_string_view_make(
                      (const char *)(const void *)head_langtype_bytes,
                      (size_t)head_langtype_size)
                : runtimec_string_view_make(NULL, 0U);
        if (!runtimec_string_view_is_valid(head_langtype) ||
            head_langtype.size == 0U) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "local slot has no declared langtype and the written wire's "
                "head carries none either");
        }
        declared = head_langtype;
    }
    if (declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID, VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype does not fit the frame slot");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    status = vmbin_frame_target_key(frame, argument, key_text, sizeof(key_text));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID, VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "local slot key cannot be built");
    }
    if (kvspaceXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  (uint64_t)value.size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR, VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "written value cannot be encoded as an XValue");
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, key_text, wire,
                        (uint32_t)wire_size, message, sizeof(message)) != 0) {
        free(wire);
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR, VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "local slot could not be created in storage");
    }
    free(wire);
    target_key = runtimec_key_view_make(key_text, strlen(key_text));
    return vmbin_value_access_bind_write_target(frame, argument, target_key,
                                                error);
}

/*
 * STATIC 写目标的**创建**（裁决 #632：写指令创造 key，读指令不创造）。
 *
 * 三条边界：
 *   1. 只对**写**目标调用；读目标仍然只解析、不创建（`locate` 那条不变）；
 *   2. 类型取**写参声明的 langtype**；声明缺席 ⇒ 具名失败（不猜、不建无类型值）；
 *   3. **整值写**，不是「先建零值再写」——后者会对变长目标必然 SIZE_MISMATCH
 *      （今晚踩过），而且本来就要多写一次。
 *
 * 已存在的 key 走 `SetValue` 覆盖；**类型冲突由存储层拒**（#632：头只作冲突
 * 检测，不作类型来源——本函数从不从旧头取类型）。
 */
static runtimec_status_t vmbin_value_access_create_static_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    runtimec_string_view_t key_text;
    runtimec_string_view_t key_langtype;
    char langtype_text[256];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    /*
     * **句柄必须先判**：`kvspaceSetValue` 收到 NULL 句柄是段错误，不是错误码
     * （实测 `runtimec.engine.absolute_write` 那条用例构造的帧没有句柄，本函数
     * 直接把它打死）。与 `create_unbound_local` 开头 `kvspace_access == NULL`
     * 即返回的写法同一纪律：**不拿未证明的前置条件去调 ABI**。
     */
    if (frame->kvspace_handle == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target has no kvspace handle; the key cannot be "
            "created or written");
    }
    status = binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                        &key_text, &key_langtype);
    if (status != RUNTIMEC_STATUS_SUCCESS || key_text.size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target has no key in the symbol pool");
    }
    /*
     * 类型取**写参声明的 langtype**（符号池里那份）。缺席 ⇒ 具名失败：
     * 不许建成无类型值——读侧无从判宽，必然退化成「按字节猜」。
     */
    if (!runtimec_string_view_is_valid(key_langtype) ||
        key_langtype.size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "static write target has no declared langtype; a key cannot be "
            "created without one");
    }
    if (key_langtype.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "static write target's langtype does not fit the buffer");
    }
    memcpy(langtype_text, key_langtype.data, key_langtype.size);
    langtype_text[key_langtype.size] = '\0';
    if (value.size != 0U && value.data == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write value has no bytes");
    }
    if (kvspaceXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  (uint64_t)value.size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "static write value cannot be encoded as an XValue");
    }
    char key_c_string[512];
    if (key_text.size + 1U > sizeof(key_c_string)) {
        free(wire);
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "static write target key does not fit the buffer");
    }
    memcpy(key_c_string, key_text.data, key_text.size);
    key_c_string[key_text.size] = '\0';
    message[0] = '\0';
    const int set_status = kvspaceSetValue(frame->kvspace_handle, key_c_string,
                                           wire, (uint32_t)wire_size, message,
                                           sizeof(message));
    free(wire);
    if (set_status != 0) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            message[0] != '\0' ? message
                                : "static write target could not be written");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * **写方向的定位：STATIC 绝对 key 尚不存在时先创建，再定位。**
 *
 * 为什么不直接调 `locate_body`：它只认**已存在**的值。而 spec 与 #632 定过
 * 「**写指令创造 key、读指令不创造**」——但创建此前只落在
 * `write_new_value` / `create_static_target`（都是**整值写**）上。
 * 算术/比较这类 handler 走的是 `VMBIN_HANDLER_LOCATE_WRITE`：它先要一个**可写
 * 位置**，算完再写进去。目标不存在时它无从定位，于是 `UNBOUND_SLOT`。
 *
 * 最小复现（3 行）：
 *
 *     rwfunc test() -> () {
 *         1 + 2 -> /sum        // 写目标是 STATIC 绝对 key，且不存在
 *         println(/sum)
 *     }
 *
 * **创建复用 `ensure_zero_value`**，不写第二份创建实现：它按宽度表建定宽零值，
 * 类型取写参声明；表外类型（数组/容器/无声明）返回失败——那时**具名失败，
 * 不猜类型**（与 `create_static_target` 同一条纪律）。
 *
 * **只对 STATIC 生效**：LOCAL 的定宽槽由帧准备物化、变长槽由 copy /
 * `write_new_value` 创建，这条路径不碰它们（否则就是第三份创建实现）。
 * **读方向绝不创建**（#632 的反向对照）。
 */
runtimec_status_t vmbin_value_access_locate_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, vmbin_value_location_t *output_location,
    runtimec_error_t *error) {
    runtimec_status_t status =
        vmbin_value_access_locate_body(frame, argument, output_location, error);

    if (status == RUNTIMEC_STATUS_SUCCESS) {
        if (output_location->write_bytes != NULL) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        /* 字面量不可写：与 `write_body` 的判据同一句，不在这里另立一套。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    /*
     * ── 写即创建：**成员写也创建**（#632）────────────────────────────────
     *
     * 「写即创建」是**语言规则**，不是 `copy` 这个算子的特性。曾经只有
     * `write_body`（copy / 整值写）会创建成员槽，而 handler 的写定位
     * （`VMBIN_HANDLER_LOCATE_WRITE`）不会——于是同一个写，成不成立取决于
     * **谁来写**：`copy(7 -> a·[1,2])` 会经 `create_unbound_local` 分派到
     * `create_member`，而 `cast(7 -> a·[1,2])` 直接报
     * `member key is not present at execution time`（`0x4A02`）。
     *
     * 那是「同一件事两条路径」，正是要消灭的分叉。这里补的**不是新机制**：
     * 复用 `create_member`（与 `create_unbound_local` 里同一处调用），
     * 用**零值**建槽——本函数只定位、不知道要写的值，真正的值随后由 handler
     * 写进返回的位置。
     *
     * **读路径一字不动**：`locate_body` 读缺失成员仍必须失败，绝不许顺手创建。
     * 创建只发生在 `_locate_write` 这一条上，且只对 LOCAL 成员与 STATIC。
     */
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL &&
        frame->program != NULL && frame->kvspace_access != NULL) {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            char key_text[512];

            /* 零值建 key：**与 STATIC 那条同一个入口**（`ensure_zero_value`，
             * 宽度表在那里唯一）。不用 `create_member`：那个按**写入值**建 wire，
             * 而本函数只定位、手上没有值，传空值会建出 0 长 body——正是
             * `0x4A03 LENGTH_MISMATCH` 的来源。 */
            status = vmbin_value_access_argument_key(
                frame, argument, key_text, sizeof(key_text), error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            if (!runtimec_kvspace_access_langtype_has_fixed_zero_width(
                    member_langtype)) {
                /* 非定宽（容器/字符串等）：由源码的 `·new` 创建，这里不抢。
                 * 原样上报 locate 的失败，不猜。 */
                return status;
            }
            status = runtimec_kvspace_access_ensure_zero_value(
                frame->kvspace_access, &frame->snapshot,
                runtimec_key_view_from_c_string(key_text), member_langtype,
                error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            return vmbin_value_access_locate_body(frame, argument,
                                                  output_location, error);
        }
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC ||
        frame->program == NULL || frame->kvspace_access == NULL) {
        return status;
    }
    {
        runtimec_string_view_t key_text;
        runtimec_string_view_t key_langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &key_text, &key_langtype) !=
                RUNTIMEC_STATUS_SUCCESS ||
            key_text.size == 0U) {
            return status;
        }
        if (!runtimec_string_view_is_valid(key_langtype) ||
            key_langtype.size == 0U) {
            /* **不猜类型**：没有声明就建不出零值，具名失败。 */
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "static write target has no declared langtype; the key cannot "
                "be created without one");
        }
        status = runtimec_kvspace_access_ensure_zero_value(
            frame->kvspace_access, &frame->snapshot,
            runtimec_key_view_make(key_text.data, key_text.size), key_langtype,
            error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    /* 建完之后再定位一次：这一次它一定存在。 */
    status = vmbin_value_access_locate_body(frame, argument, output_location,
                                            error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (output_location->write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 解引用写目标的**创建**：目标 key 由指针值给出，类型由写槽声明给出。
 *
 * 只被 `write_body` 在「`dereference` 且定位失败」时调用——**读路径永不创建**。
 */
static runtimec_status_t vmbin_value_access_create_dereference_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;
    vmbin_value_location_t pointer_value;
    runtimec_string_view_t declared = runtimec_string_view_make(NULL, 0U);
    char langtype_text[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char message[256];
    runtimec_status_t status;

    if (frame->kvspace_access == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "frame carries no kvspace access to create a dereference target");
    }
    /* 指针值 = 目标 key。用不带 dereference 的同一参数取它。 */
    plain.dereference = 0U;
    status = vmbin_value_access_locate_body(frame, &plain, &pointer_value,
                                            error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (pointer_value.read_bytes == NULL || pointer_value.size == 0U ||
        pointer_value.read_bytes[0] != '/') {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "dereference target key is not an absolute key");
    }
    /* 类型取**写槽声明**：`*T` 的 `*` 是传递方式，剥一层才是值类型。 */
    status = vmbin_value_access_langtype(frame, argument, &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (declared.size != 0U && declared.data[0] == '*') {
        declared = runtimec_string_view_make(declared.data + 1U,
                                             declared.size - 1U);
    }
    if (declared.size == 0U || declared.size + 1U > sizeof(langtype_text)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "dereference write target has no usable declared langtype");
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    if (kvspaceXvalueHeadNewSlack(value.data, (uint64_t)value.size,
                                  (uint64_t)value.size, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "dereference write target cannot be encoded as an XValue");
    }
    message[0] = '\0';
    {
        char key_text[512];
        const size_t key_size = pointer_value.size < sizeof(key_text) - 1U
                                    ? pointer_value.size
                                    : sizeof(key_text) - 1U;

        memcpy(key_text, pointer_value.read_bytes, key_size);
        key_text[key_size] = '\0';
        if (kvspaceSetValue(frame->kvspace_handle, key_text, wire,
                            (uint32_t)wire_size, message,
                            sizeof(message)) != 0) {
            free(wire);
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
                "dereference write target could not be created in storage");
        }
    }
    free(wire);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_view_is_valid(value)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write value");
    }
    /*
     * **STATIC 写目标：写即创建**（裁决 #632）。
     *
     * 这里不改 `locate`：读目标仍然只解析、不创建——读一个不存在的绝对 key
     * 是真实错误。创建只发生在**写**路径，且是整值写。
     */
    /*
     * **只有 payload 未解析（NONE）的 STATIC 写目标才创建。**
     *
     * payload 非 NONE 时它是**已解析的值体地址**，那种情况走下面的
     * 「定位 + 比长度 + memcpy」原路径——照抄 `locate` 里 STATIC 分支的同一分叉，
     * 不在这里另立一套判据。
     *
     * 判据是「有没有地址」，不是「是不是 STATIC」：只看后者会把已解析的静态槽
     * 也当成待创建，于是放着现成地址不用去拼 key——那会让所有走内联地址的
     * 单测整组失败（实测 vmbin.execute / vmbin.call 两组红）。
     */
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC &&
        argument->payload == BINRWIR_BODY_ADDRESS_NONE) {
        return vmbin_value_access_create_static_target(frame, argument, value,
                                                       error);
    }
    status = vmbin_value_access_writes_through_binding_slot(argument)
                 ? vmbin_value_access_create_unbound_local(frame, argument,
                                                           value, error)
                 : RUNTIMEC_STATUS_SUCCESS;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_locate_body(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /*
         * 先确认**目标真的不存在**：定位失败也可能来自「key 在、但不是值」之类
         * 的其它原因。只有「解引用后的目标 key 解析不出 XValue」才创建；
         * 别的失败原样上报——把失败一律转成创建会掩盖真实错误。
         *
         * ── 解引用写目标尚不存在 ⇒ **创建**（`05-runtime语义/12-动态键.kv` §一）──
         *
         * 规范写明「`resolveptr` 不判目标是否存在……若提前判存在，`v -> base·*k`
         * 的首次写永远不成立」。所以「写到不存在的目标」不是错误，是**写即创建**
         * （#632）在解引用形态上的落地：`h·*k` 里的 `k` 运行期才定，目标的 key
         * 只有指针值自己知道。
         *
         * 三条边界，一条都不许放宽：
         *   1. **只对写参带 `dereference` 时**生效；读路径仍失败（读不存在的目标
         *      是真实错误）；
         *   2. key 取自**指针值本身**（它的 body 就是目标 key，与 `dereference`
         *      跟跳用的是同一份字节）；
         *   3. 类型取**写槽的声明类型**（`*T` 剥一层），不是写入值的头——写新值
         *      以写入方携带的类型为准是 #632 的另一半。
         *
         * 创建之后**重新定位**再走下面的定宽路径：宽度检查仍在，只是这次目标存在。
         */
        if (argument->dereference != 0U) {
            status = vmbin_value_access_create_dereference_target(
                frame, argument, value, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            status = vmbin_value_access_locate_body(frame, argument, &location,
                                                    error);
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    /* **定宽路径一字不动**：size 比对与 memcpy 都保留——它是快路径，也是
     * 「写入宽度必须与声明一致」这道检查的唯一所在。
     *
     * ── 但消息要分得清是哪一种「宽度不符」────────────────────────────────
     *
     * 判据是**「写进来的这条值是不是 `None`」**（空 body），不是「读参带不带
     * dereference 位」：`h·*c -> cur` 的 copy 在编码期已经把解引用读成了一条
     * **普通 0 字节值**，到这一层 `dereference` 早已是 0。按位判会让这条消息
     * 永远不出现，退回到那句「宽度不符」——实测就是这样，改了判据却一条都没
     * 命中。
     *
     * 而目标槽是**已声明的标量**（`cur:int64` / `ex:bool`）——严格类型语言里
     * 它不存在「无值」这种状态，所以这是类型错误，不是「凑巧宽度不等」。
     *
     * 两种成因给同一条 `0x4406`，但**消息必须指出路**：只说「宽度不符」，看日志的
     * 人要去猜「我写错了宽度」还是「这个位置本来就没有值」。k_nucleotide 在这条
     * 消息上绕了三轮，前两轮都归错因（先怀疑缺 deref 位，再怀疑推断变量）。
     */
    /*
     * `None` 落进有值的位置：由**调用方**判（那里拿得到源操作数的 langtype）。
     * 本函数只有 body 视图，判不出 `is_none` 的第二条（无 langtype 记录）——
     * 拿目标槽宽度倒推是代理指标，会漏掉「None 写进变长声明槽」那一格。
     */

    if (value.size != location.size) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
            "value does not match the current body size");
    }
    if (value.size != 0U) {
        memcpy(location.write_bytes, value.data, value.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status;

    if (!runtimec_bytes_view_is_valid(value)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write value");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.write_bytes == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY,
            "a literal is not a writable location");
    }
    if (value.size != location.size) {
        /* 改长度要连带改值头与 attr 位置：那是存储层的事。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
            "value does not match the current body size");
    }
    if (value.size != 0U) {
        memcpy(location.write_bytes, value.data, value.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_write_whole_value(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t body,
    runtimec_error_t *error) {
    if (!runtimec_bytes_view_is_valid(body)) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, "invalid write body");
    }
    /*
     * 复用既有创建入口，不新造第三条写路径：它按**写槽声明类型**建 wire、
     * `kvspaceSetValue` 覆盖、再把 LOCAL 槽重新绑到新地址。这三件事正是
     * 「换整个值」需要的全部。
     *
     * STATIC 写目标同样走这里：`create_static_target` 与它做的是同一件事
     * （按声明类型整值写），只是 key 的派生不同——`create_unbound_local` 里
     * 对非 LOCAL 的引用种类会具名失败，所以 STATIC 先分派过去。
     */
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC) {
        return vmbin_value_access_create_static_target(frame, argument, body,
                                                       error);
    }
    return vmbin_value_access_create_unbound_local(frame, argument, body, error);
}

bool vmbin_value_access_langtype_is_structref(runtimec_string_view_t langtype) {
    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    /* 串首 `*` 是指针（值是 key，跟一跳才是容器），不是引用值。 */
    if (langtype.data[0] == '*') {
        return false;
    }
    if (langtype.size >= 10U &&
        memcmp(langtype.data, "def struct", 10U) == 0) {
        return true;
    }
    return langtype.size > sizeof("/lib/") - 1U &&
           memcmp(langtype.data, "/lib/", sizeof("/lib/") - 1U) == 0;
}

bool vmbin_value_access_langtype_is_container(runtimec_string_view_t langtype) {
    runtimec_langtype_t parsed;

    if (vmbin_value_access_langtype_is_structref(langtype)) {
        return true;
    }
    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    if (runtimec_langtype_parse(langtype, &parsed) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    return parsed.form == RUNTIMEC_LANGTYPE_FORM_MAPPING;
}

bool vmbin_value_access_langtype_is_path_string(runtimec_string_view_t langtype) {
    runtimec_string_view_t rest = langtype;

    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return false;
    }
    if (langtype.data[0] == '*') {
        return false;
    }
    /* `[]char/utf8` / `[]char/utf32` / `[N]char/<enc>`：剥维度前缀再看基类型。 */
    while (rest.size != 0U && rest.data[0] == '[') {
        const char *closing = memchr(rest.data, ']', rest.size);

        if (closing == NULL) {
            return false;
        }
        rest = runtimec_string_view_make(closing + 1,
                                         rest.size - (size_t)(closing - rest.data) - 1U);
    }
    return rest.size >= 5U && memcmp(rest.data, "char/", 5U) == 0;
}

runtimec_status_t vmbin_value_access_langtype(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, runtimec_error_t *error) {
    const runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);

    if (output_langtype != NULL) {
        *output_langtype = empty;
    }
    if (frame == NULL || frame->program == NULL || argument == NULL ||
        output_langtype == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid langtype request");
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LITERAL: {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;

        if (binrwir_binfunc_get_literal(frame->program,
                                        (uint32_t)argument->payload, &wire,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "literal index is not in the program pool");
        }
        *output_langtype = langtype;
        break;
    }
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC: {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;

        if (binrwir_binfunc_get_symbol(frame->program, argument->slot, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "symbol index is not in the program pool");
        }
        *output_langtype = langtype;
        break;
    }
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE: {
        binrwir_binfunc_view_t view;
        const runtimec_string_view_t *types = NULL;
        size_t count = 0U;

        if (binrwir_binfunc_get_view(frame->program, &view) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "program view is unavailable");
        }
        if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_READ) {
            types = view.signature.read_parameter_types;
            count = view.signature.read_parameter_count;
        } else {
            types = view.signature.write_parameter_types;
            count = view.signature.write_parameter_count;
        }
        if (types == NULL || (size_t)argument->slot >= count) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
                "parameter index is outside the signature type table");
        }
        *output_langtype = types[argument->slot];
        break;
    }
    default:
        /* FUNCTION 是调用目标，不是值；未知 kind 同样没有声明类型。 */
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "argument has no declared langtype");
    }
    if (output_langtype->size == 0U) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE,
            "declared langtype is empty");
    }
    /*
     * **这里不按 `dereference` 剥 `*`。**
     *
     * 直觉上该剥：`argument->dereference` 表示「访问前跟一跳」，调用方要的理应
     * 是跟过之后那个值的类型。实测不成立——`println(*p)` 走到这里时
     * `argument->dereference` 是 **0**，而声明的 langtype 仍是 `*int64`。
     * 也就是说这一位在 encoder → program 之间没有传下来（`vmkv_encoder_map_argument`
     * 里 `has_dereference` 已置 1、`output->dereference` 也写了 1，运行期读到的
     * 却是 0）。在这里补一条依赖它的判据不会生效，只会让下一个人以为链路是通的。
     *
     * 谁需要「跟过之后的类型」，就得先把这一位传对；在那之前，本函数如实返回
     * **声明的类型**（含 `*`），不猜调用方想问哪一个。
     */
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * **取形态但不跟 dereference**：`ptreq` 这类「比较指针值本身」的算子要的是
 * 指针/None/标量三态，**不是指针指向的那个值**。
 *
 * 跟跳会：① 跳到不存在的目标就失败（`h·*k != None` 判存在时的常见情形）；
 * ② 即使跳成功，拿到的也是目标的形态而非指针的形态——两件事。
 *
 * 与 `vmbin_value_access_form` 的唯一差别是这一步：其余（wire 定位、
 * langtype 首字符定形态）完全共用，不复制实现。
 */
runtimec_status_t vmbin_value_access_form_unfollowed(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error) {
    binrwir_argument_t plain = *argument;

    plain.dereference = 0U;
    return vmbin_value_access_form(frame, &plain, output_form, error);
}

runtimec_status_t vmbin_value_access_form(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    const uint8_t *wire = NULL;
    runtimec_status_t status;

    if (output_form != NULL) {
        *output_form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    }
    if (output_form == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid value form request");
    }
    status = vmbin_value_access_locate(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.is_whole_wire) {
        /* 字面量/内联绑定借出的就是整条 wire，值头基址即首字节。 */
        wire = location.read_bytes;
    } else {
        /*
         * body 粒度：经合同的逆几何入口退回值头基址，这里不写偏移算术。
         * 指针值自身没有可逆的 body 地址证明，因此失败封闭而不是拿 body 当前值。
         */
        const runtimec_xvalue_address_t base =
            runtimec_value_body_xvalue_address(
                (runtimec_value_body_address_t)(uintptr_t)location.read_bytes);

        if (base == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "value body has no derivable XValue head");
        }
        wire = (const uint8_t *)(uintptr_t)base;
    }
    if (wire == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
            "argument has no readable XValue head");
    }
    /*
     * **形态来自 langtype，不来自 head**：head 现在只有 bodylen/attrslen/layout。
     * 读 langtype 记录（tag 1）的首字符即可判：`*` 指针、`@` 扩展、其它普通值。
     * 读不到 langtype ⇒ 普通值（未打类型的 None 也走这支）。
     */
    {
        const kvspaceXvalueHead *head =
            (const kvspaceXvalueHead *)(const void *)wire;
        const uint64_t total = kvspaceXvalueHeadTotalLen(head);

        if (total == 0) {
            return vmbin_value_access_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                "value head is not a well-formed XValue");
        }
        const uint8_t *langtype = NULL;
        const uint16_t langtype_size = kvspaceXvalueHeadLangtypeBounded(
            wire, total, &langtype);
        if (langtype != NULL && langtype_size != 0U) {
            if (langtype[0] == '*') {
                *output_form = VMBIN_VALUE_ACCESS_FORM_POINTER;
            } else if (langtype[0] == '@') {
                *output_form = VMBIN_VALUE_ACCESS_FORM_EXTENSION;
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_value_access_bind_write_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_key_view_t target_key,
    runtimec_error_t *error) {
    uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    runtimec_status_t status;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        frame->bindings == NULL || frame->kvspace_access == NULL ||
        !runtimec_key_view_is_valid(target_key) || target_key.size == 0U ||
        error == NULL) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "invalid bind-write-target request");
    }
    /*
     * STATIC 写目标不绑：值访问对静态 key 每次按 key 解析地址，绑定表里没有它的
     * 槽。这里返回成功不是「跳过失败」，是「该种类本来就没有这一步」。
     */
    if (argument->reference_kind == BINRWIR_REFERENCE_STATIC) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * **成员键不绑**（`decision-member-slot-binding.md` §四.3）：与 STATIC 同理，
     * 该类本来就没有绑定这一步——成员地址每次按 key 现解析，绑死就会读到容器
     * 整体重建后已不存在的旧 box。
     *
     * 与 `locate` / `create_unbound_local` 用**同一个判据**，任何一处自己再扫
     * 字节就是第二套定义。
     */
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
        runtimec_string_view_t member_name;
        runtimec_string_view_t member_langtype;
        if (binrwir_binfunc_get_symbol(frame->program, argument->slot,
                                       &member_name, &member_langtype) ==
                RUNTIMEC_STATUS_SUCCESS &&
            binrwir_symbol_name_is_member(member_name)) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    /*
     * `PARAMETER_WRITE` 同样需要重绑：整值写会把 wire 换到新地址，调用方那一侧
     * 的绑定表条目指向旧 box，不刷新就会「写完接着读还是旧值」。
     *
     * 判据用 `vmbin_frame_slot_of_argument`——它对 LOCAL 与两种 PARAMETER 都给出
     * 绑定槽号（`vmbin/frame_slot.c` 的分区），其余种类（LITERAL / STATIC /
     * FUNCTION）本来就没有绑定这一步。
     */
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL &&
        argument->reference_kind != BINRWIR_REFERENCE_PARAMETER_WRITE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "only local and parameter-write slots are bound after creation");
    }
    if (frame->snapshot_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "frame has no storage generation");
    }
    if (vmbin_frame_slot_of_argument(frame->program, argument, &binding_slot) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE,
            "local slot index is not in the program pool");
    }
    status = runtimec_kvspace_access_resolve_frame_xvalue_address(
        frame->kvspace_access, &frame->snapshot, target_key, &xvalue_address,
        &generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /*
     * 刚创建出来的值必须与帧属于同一份存储快照。不一致说明建值与绑帧之间存储
     * 被换过——那正是绑定表 generation 存在的理由，不是可以继续的情形。
     */
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        generation != frame->snapshot_generation) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "created value is not in the frame storage generation");
    }
    if (binrwir_frame_bindings_bind(frame->bindings, binding_slot, target_key,
                                    target_key, xvalue_address,
                                    generation) != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_value_access_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST,
            "created value could not be bound to its slot");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
