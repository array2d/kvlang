/*
 * 变参 `...` 只落在**一个**位置：签名行锚点的 dynamic 字节。
 *
 * 判据（`stdlib/kvlang/spec/03-类型系统/22-storetype-def族与签名行.kv` 的「铁律」
 * 段落原文）：
 *
 *   rwfunc 的 `...` 是**签名层 arity**，吸收 0..N 个实参，**不是 langtype 的一部分**；
 *   它由主槽 `/lib/f/[0,0]` 的计数头第三字节 `dynamic` 承载，参数定义键
 *   `/lib/f/.[0,-j]` 的 body（名字 NUL 类型串）里的类型串**纯净、不含 `...`**。
 *
 * 为什么这条值得单独一个用例：`...` 有两种可能的落点（类型串 / dynamic 字节），
 * 写实现时很容易**两边都写**——而两边都写跑起来完全正常（arity 对、类型也「看着
 * 对」），只有断言结构才发现。所以本用例断言三件事，缺一不可：
 *
 *   1. 锚点 `[0,0]` 的 dynamic 字节 **是** 1；
 *   2. 末读参的类型串 **不含** `...`；
 *   3. 末读参的类型串 **不为空**（dynamic 位不是「省掉类型串」的替代品）。
 *
 * 判据 3 是「另一个落点为空」的另一半：只断言「不含 ...」会放过「整个类型串被
 * 变参标记吃掉」——那种实现 arity 与 dynamic 都对，但签名丢了类型。
 *
 * 依赖：真 layout 库（`kvlangLayoutCode`）+ 真 kvspace（shm）。两者缺一时返回 77
 * 跳过——**跳过只给「没装依赖」**，layout 拒载本用例的 fixture 一律算失败。
 */

#include <dlfcn.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "contract/error.h"
#include "contract/status.h"
#include "host/kvspace_adapter.h"
#include "host/kvspace_binary_interface.h"
#include "kvspace/xvalue_head.h"

#define SKIP_RETURN_CODE 77

/* layout 的 C 入口：源码串 → 布局进 dsn 指向的 kvspace。 */
typedef int (*layout_code_function_t)(const char *source, const char *data_source,
                                      char *entry, uint32_t entry_capacity,
                                      char *error, uint32_t error_capacity);

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            ++failures;                                                       \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__,            \
                    #condition);                                              \
        }                                                                     \
    } while (0)

static runtimec_kvspace_symbol_address_t resolve_symbol(void *context,
                                                        const char *name) {
    void *symbol = dlsym(context, name);
    return (runtimec_kvspace_symbol_address_t)(uintptr_t)symbol;
}

/* wire → body 视图。head 16 字节，body 紧随其后。 */
static const uint8_t *wire_body(const uint8_t *wire, size_t wire_size,
                                size_t *output_body_size) {
    *output_body_size = 0U;
    if (wire == NULL || wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return NULL;
    }
    {
        const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)wire;
        const uint64_t body_length = kvspaceXvalueHeadLen(head);

        if ((uint64_t)wire_size <
            (uint64_t)KVSPACE_XVALUE_HEAD_LEN + body_length) {
            return NULL;
        }
        *output_body_size = (size_t)body_length;
    }
    return wire + KVSPACE_XVALUE_HEAD_LEN;
}

/*
 * 读一个 key 的整条 wire。
 *
 * **借用，不 free**：`kvspaceGet` 的注释写明 `*out` 指向后端存储（shm mmap /
 * durable 读缓存），在下一次覆盖/删除/关句柄之前有效。free 它会让 glibc 报
 * `free(): invalid size` 并中止——那不是被测代码的错，是用例自己越界释放。
 */
static bool read_wire(runtimec_kvspace_binary_interface_t *binary_interface,
                      void *handle, const char *key, uint8_t **output_wire,
                      uint32_t *output_size) {
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;

    *output_wire = NULL;
    *output_size = 0U;
    if (binary_interface->get(handle, key, 1, &wire, &wire_size) != 0) {
        return false;
    }
    *output_wire = wire;
    *output_size = wire_size;
    return true;
}

/*
 * 参数定义键 `/lib/f/.[0,-j]` 的 body 形态（spec 明写）：
 *
 *   名字字节 + NUL + 类型串
 *
 * 返回类型串视图；没有 NUL 或 body 为空时返回 NULL。
 */
static const char *parameter_type_text(const uint8_t *body, size_t body_size,
                                       size_t *output_type_size) {
    size_t nul_index = 0U;

    *output_type_size = 0U;
    if (body == NULL || body_size == 0U) {
        return NULL;
    }
    while (nul_index < body_size && body[nul_index] != 0U) {
        nul_index += 1U;
    }
    if (nul_index >= body_size) {
        return NULL;
    }
    *output_type_size = body_size - nul_index - 1U;
    return (const char *)(body + nul_index + 1U);
}

/* 类型串里是否出现变参标记 `...`。 */
static bool contains_variadic_marker(const char *text, size_t size) {
    size_t ordinal;

    if (text == NULL || size < 3U) {
        return false;
    }
    for (ordinal = 0U; ordinal + 3U <= size; ++ordinal) {
        if (text[ordinal] == '.' && text[ordinal + 1U] == '.' &&
            text[ordinal + 2U] == '.') {
            return true;
        }
    }
    return false;
}

/*
 * 任意 XValue 的 langtype：**在 attr 区**，不在 body。
 *
 *   wire = [bodylen:u64][attrslen:u32][storetype][ptr][pad:2][body][attrs]
 *   attr = [tag:u8][len:u16 LE][data]  × attrslen 个字节，tag 1 = langtype
 *
 * 之前按「body 里带长度前缀的 langtype」解析过，那是错的：`/lib/f/[1,-1]` 的
 * body 只有 8 字节的值本体，langtype 整段在 attr 里。head 是唯一事实源，
 * 不要另写一套 body 内嵌布局。
 *
 * 找不到 tag 1 或形态不符返回 NULL，不猜。
 */
static const char *xvalue_langtype(const uint8_t *wire, size_t wire_size,
                                   size_t *output_type_size) {
    const kvspaceXvalueHead *head = NULL;
    const uint8_t *attrs = NULL;
    size_t attrs_size = 0U;
    size_t cursor = 0U;

    *output_type_size = 0U;
    if (wire == NULL || wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return NULL;
    }
    head = (const kvspaceXvalueHead *)wire;
    attrs_size = (size_t)kvspaceXvalueHeadAttrsLen(head);
    if ((size_t)KVSPACE_XVALUE_HEAD_LEN + (size_t)kvspaceXvalueHeadLen(head) +
            attrs_size >
        wire_size) {
        return NULL;
    }
    attrs = wire + KVSPACE_XVALUE_HEAD_LEN + (size_t)kvspaceXvalueHeadLen(head);
    while (cursor + 3U <= attrs_size) {
        const uint8_t tag = attrs[cursor];
        const uint16_t length =
            (uint16_t)(attrs[cursor + 1U] | ((uint16_t)attrs[cursor + 2U] << 8));

        if (cursor + 3U + (size_t)length > attrs_size) {
            return NULL;
        }
        if (tag == 1U) {
            *output_type_size = (size_t)length;
            return (const char *)(attrs + cursor + 3U);
        }
        cursor += 3U + (size_t)length;
    }
    return NULL;
}

/* 判据 4 的独立函数：读指令行签名槽，断言类型串不含 `...`。 */
static void check_instruction_signature_slot(
    runtimec_kvspace_binary_interface_t *binary_interface, void *handle,
    const char *key) {
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;

    EXPECT(read_wire(binary_interface, handle, key, &wire, &wire_size));
    if (wire == NULL) {
        fprintf(stderr, "DIAG: %s is missing or unreadable\n", key);
        return;
    }
    {
        size_t type_size = 0U;
        const char *type_text = xvalue_langtype(wire, (size_t)wire_size,
                                                &type_size);

        EXPECT(type_text != NULL);
        if (type_text == NULL) {
            fprintf(stderr,
                    "DIAG: %s has no langtype in its attrs\n", key);
        }
        if (type_text != NULL) {
            EXPECT(type_size != 0U);
            /*
             * 同一事实的第二个可能落点：这里也不许出现 `...`。
             *
             * **当前实现红在这里**：指令槽的类型串是 `/lib/int64...`——不只是
             * 多带了 `...`，整个类型名已被展开成 structref 路径。与判据 3 同源。
             */
            EXPECT(!contains_variadic_marker(type_text, type_size));
            /* 更进一步：类型串必须仍是裸 kind，不是 structref 路径。 */
            EXPECT(type_size == 5U);
            if (type_size == 5U) {
                EXPECT(memcmp(type_text, "int64", 5U) == 0);
            }
        }
    }
}

int main(int argument_count, char **arguments) {
    /*
     * 末读参变参。**`...` 必须贴在类型后面**（`*int64... $x`），不是名字后面。
     *
     * 实测（2026-11-05，layout 库 HEAD）：`*int64 $x...` 与 `int64 $x...` 都会被
     * 解析成普通参数（锚点 dynamic=0，参数键类型串是 `int64` 没有标记）——不报错，
     * 只是变参标记被静默丢掉。写成 `*int64... $x` 才会让 dynamic=1。
     *
     * 这条本身就是一个「同一事实有两个可能落点」的例子：标记写在名字后面时，
     * 语法上一路放行，语义上哪儿都没落下。
     */
    /*
     * 函数体**不引用形参**。
     *
     * 原因：当前 layout 在形参被引用时会先撞上 `TypeError: cannot assign
     * /lib/int64... to int64`（见下面判据 2 的根因说明），整个 fixture 加载失败，
     * 连锚点都读不到。形参引用与否不影响签名行的落点，所以这里取能加载的那一支，
     * 让判据测的是**结构**而不是被另一个缺陷挡住。
     *
     * 形参被引用时的那条症状另行记录，不在本用例的判据里越位断言。
     */
    static const char layout_source[] =
        "rwfunc f(*int64... $x) -> () {\n"
        "\t1 -> /t/discard\n"
        "}\n";
    static const char anchor_path[] = "/lib/f/[0,0]";
    /*
     * 末读参（j = nr = 1）的**参数定义键**：点后缀，不是坐标斜杠键。
     *
     * 两者都在 /lib 下但含义不同（spec 03-类型系统/22「签名行」）：
     *   `/lib/f.[0,-1]`   def langtype，body = 名字 NUL 类型串  ← 本用例断言这个
     *     （**在 /lib 同级**，不是 `f/` 目录内——`f/` 目录里放的是指令行）
     *   `/lib/f/[1,-1]`   指令行签名槽 / 调用期实参地址槽       ← 另一件事
     *
     * 实测（2026-11-05）：两者都已存在，且**都必须**不含 `...`——本用例先把
     * 判据钉在参数定义键上，再对指令行槽做同样的断言（见判据 4）。
     */
    static const char parameter_path[] = "/lib/f.[0,-1]";
    static const char instruction_signature_path[] = "/lib/f/[1,-1]";
    const char *kvspace_library =
        argument_count > 1 ? arguments[1] : NULL;
    const char *layout_library = argument_count > 2 ? arguments[2] : NULL;
    char directory[256];
    char data_source_name[320];

    if (kvspace_library == NULL || layout_library == NULL) {
        fprintf(stderr, "SKIP: need kvspace and layout library paths\n");
        return SKIP_RETURN_CODE;
    }

    void *kvspace_handle_library =
        dlopen(kvspace_library, RTLD_NOW | RTLD_LOCAL);
    void *layout_handle_library = dlopen(layout_library, RTLD_NOW | RTLD_LOCAL);
    if (kvspace_handle_library == NULL || layout_handle_library == NULL) {
        fprintf(stderr, "SKIP: cannot load the libraries\n");
        return SKIP_RETURN_CODE;
    }

    layout_code_function_t layout_code = NULL;
    {
        void *symbol = dlsym(layout_handle_library, "kvlangLayoutCode");

        memcpy(&layout_code, &symbol, sizeof(layout_code));
    }
    if (layout_code == NULL) {
        fprintf(stderr, "SKIP: layout library lacks kvlangLayoutCode\n");
        return SKIP_RETURN_CODE;
    }

    runtimec_kvspace_symbol_resolver_t resolver = {
        .context = kvspace_handle_library,
        .resolve = resolve_symbol,
    };
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (runtimec_kvspace_binary_interface_bind(&resolver, &binary_interface,
                                               &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "SKIP: bind failed: %.*s\n", (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        return SKIP_RETURN_CODE;
    }

    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding2/"
             "variadic-%d",
             (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = binary_interface.connect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", data_source_name);
        return SKIP_RETURN_CODE;
    }

    {
        char entry[128];
        char layout_error[512];

        entry[0] = '\0';
        layout_error[0] = '\0';
        if (layout_code(layout_source, data_source_name, entry,
                        (uint32_t)sizeof(entry), layout_error,
                        (uint32_t)sizeof(layout_error)) != 0) {
            /*
             * layout 拒载是**失败**，不是跳过：缩进、语法、变参写法都由用例自己
             * 决定，跑不起来说明输入坏了，不是环境缺依赖。
             */
            fprintf(stderr, "FAIL: layout rejected the fixture: %s\n",
                    layout_error);
            return 1;
        }
    }

    /* ── 判据 1：主槽 [0,0] 的 dynamic 字节为 1 ─────────────────────── */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0U;

        EXPECT(read_wire(&binary_interface, handle, anchor_path, &wire,
                         &wire_size));
        if (wire != NULL) {
            size_t body_size = 0U;
            const uint8_t *body = wire_body(wire, (size_t)wire_size,
                                            &body_size);

            EXPECT(body != NULL);
            /* 计数头 = [nr:u16 LE][nw:u16 LE][dynamic:u8]，共 5 字节。 */
            EXPECT(body_size == 5U);
            if (body != NULL && body_size == 5U) {
                const uint16_t read_count =
                    (uint16_t)(body[0] | ((uint16_t)body[1] << 8));
                const uint16_t write_count =
                    (uint16_t)(body[2] | ((uint16_t)body[3] << 8));

                EXPECT(read_count == 1U);
                EXPECT(write_count == 0U);
                /* 正身：`...` 落在这里。 */
                EXPECT(body[4] == 1U);
            }
        }
    }

    /*
     * ── 判据 2、3：末读参的类型串不含 `...`，且不为空 ─────────────────
     *
     * 两条一起才构成「另一个落点为空」的完整断言：
     *   - 只断言「不含 ...」→ 放过「整个类型串被吃掉」；
     *   - 只断言「不为空」  → 放过「两边都写」（`int64...` 也是非空）。
     */
    {
        uint8_t *wire = NULL;
        uint32_t wire_size = 0U;

        EXPECT(read_wire(&binary_interface, handle, parameter_path, &wire,
                         &wire_size));
        if (wire != NULL) {
            size_t body_size = 0U;
            const uint8_t *body = wire_body(wire, (size_t)wire_size,
                                            &body_size);
            size_t type_size = 0U;
            const char *type_text =
                parameter_type_text(body, body_size, &type_size);

            EXPECT(body != NULL);
            EXPECT(type_text != NULL);
            if (type_text != NULL) {
                /* 3：类型串必须还在（不被变参标记吃掉）。 */
                EXPECT(type_size != 0U);
                /* 2：类型串里不许有 `...`。 */
                EXPECT(!contains_variadic_marker(type_text, type_size));
                /*
                 * 正身：类型串必须恰好是 `int64`。
                 *
                 * **当前实现红在这里**（2026-11-05 实测）：参数键的 body 是
                 * `x\0/lib/int64` —— `int64` 被 `expand_atom` 当成 structref
                 * 展开成了 `/lib/int64`。根因是 `langtype::expand_atom` 先判
                 * `known_kind(s)`，而带 `...` 尾缀的 `int64...` 不在 known_kind
                 * 里，于是走了「裸名 → /lib/<名>」那条分支，剥 `...` 没有发生在
                 * 这一步之前。
                 *
                 * 本 fixture 下**指令槽是干净的**（`[1,-1]` 的 langtype = `int64`），
                 * 只有参数定义键被污染——所以这条不是「两边都写」，而是
                 * 「正确的那一边写错了」。形参被引用时指令槽也会被污染，那条
                 * 症状另记（layout 会直接报 TypeError，fixture 加载不到）。
                 */
                EXPECT(type_size == 5U);
                if (type_size != 5U) {
                    fprintf(stderr,
                            "DIAG: parameter key langtype is \"%.*s\" "
                            "(expected \"int64\")\n",
                            (int)type_size, type_text);
                }
                if (type_size == 5U) {
                    EXPECT(memcmp(type_text, "int64", 5U) == 0);
                }
            }
        }
    }

    /*
     * ── 判据 4：同一事实的**第二个可能落点**也不许有 `...` ───────────
     *
     * `...` 有两种可能落点：参数定义键的类型串、指令行签名槽的类型串。
     * 只断言其中一个，就放过了「两边都写」。这条是架构师要求的通用规矩：
     * 凡「同一事实有两个可能落点」的，都要断言**另一个落点为空**。
     */
    check_instruction_signature_slot(&binary_interface, handle,
                                     instruction_signature_path);

    binary_interface.close(handle);
    if (failures != 0) {
        fprintf(stderr, "variadic_purity_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("variadic_purity_test: OK\n");
    return 0;
}
