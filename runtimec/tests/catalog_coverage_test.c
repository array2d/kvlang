/*
 * 能力目录的「声称」与 runtime 的「实际」必须一致。
 *
 * ── 判据（2026-11-05 修订，架构师据代码裁定）─────────────────────────
 *
 * 对每条 catalog 能力行，**二者之一**成立即算 resolved：
 *
 *   1. **基础 opid 已注册** —— `registry_find(基础 opid)` 成功；或
 *   2. **该基础 opid 在特化表里至少有一行，且那行指定的特化 opid 已注册** ——
 *      `registry_find(特化 opid)` 成功。
 *
 * 只要**至少一行**特化命中就算 resolved，不要求全部命中：某些类型组合可能
 * 故意不实现（例如 char 族走手写特例）。
 *
 * ── 为什么不再要求「基础 opid 必须注册」────────────────────────────
 *
 * `engine/execution_window.c:572` 在**生产路径上无条件调用**
 * `binrwir_binfunc_specialize`（编码之后、编译之前）。所以能特化的算子，
 * 基础 opid **到不了 `binrwir_registry_find`**——它先被改写成 `0x8xxx`。
 *
 * 旧判据（「基础 opid 必须注册」）因此断言了一个**不产生行为的属性**：
 * `scalar_entries` 那 36 条基础 handler 是**行为测试夹具**，不是产品分派路径。
 *
 * 实证（2026-11-05 14:2x，独立探针）：`pow` 在 `scalar_generated_entries`
 * 里有 **100/100** 条特化已注册，但没有基础条目——按旧判据它被报成「缺失」，
 * 按新判据它是 resolved。
 *
 * ── 为什么仍然需要这条测试 ───────────────────────────────────────────
 *
 * `generate_rwir_catalog.py:40` 的 `REGISTRY_SOURCE` 指向
 * `runtime/src/myrwircaps.c` —— 那个**不可构建**的旧 runtime 的文件。
 * 目录的「已注册」一侧读的是它，而真正执行的是 session 装进
 * `binrwir_registry_t` 的条目。**两套「注册」互不校验**，所以「目录说 0 gaps」
 * 与「运行时查不到」可以同时为真。本测试就是那道校验。
 *
 * ── 为什么用 session，不自建 registry ─────────────────────────────────
 *
 * 自建 registry 会绕过 `engine/session.c` 的实际装载——而缺口正是藏在
 * 「session 装了什么」这个缝里。判据要验的是「session 装完之后有没有」，
 * 不是「某个模块的条目表里有没有」。
 *
 * ── 依赖 ──────────────────────────────────────────────────────────────
 *
 * 真 kvspace 库。缺库返回 77 跳过（跳过只给「没装依赖」）。
 *
 * ── 边界 ──────────────────────────────────────────────────────────────
 *
 * 本用例**不改** `REGISTRY_SOURCE`。把目录的「已注册」来源换成 runtimec 的注册表
 * 是个更大的决定，会牵动 1836 条特化与别名表。这里只把事实钉住。
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <kvspace/kvspace.h>

#include "binrwir/instruction.h"
#include "binrwir/registry.h"
#include "contract/kvspace_access.h"
#include "contract/status.h"
#include "engine/session.h"
#include "vmkv/rwir_manifest.h"
#include "vmbin/core_operations.h"

#define SKIP_RETURN_CODE 77

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/*
 * 5 条 core 由 `interpreter.c` 的 switch 直接分派，**不进 registry**。
 * 它们是「目录里有、registry 里没有」的合法情形，必须排除；不排除会把
 * 这条用例变成「core 也算缺口」的假红。
 */
static bool is_core_dispatch(binrwir_operation_identifier_t identifier) {
    return identifier == VMBIN_OPERATION_IDENTIFIER_COPY ||
           identifier == VMBIN_OPERATION_IDENTIFIER_CALL ||
           identifier == VMBIN_OPERATION_IDENTIFIER_RETURN ||
           identifier == VMBIN_OPERATION_IDENTIFIER_GOTO ||
           identifier == VMBIN_OPERATION_IDENTIFIER_BRANCH;
}


/*
 * ── 声明式缺口清单：**计划内、尚未落地** ──────────────────────────────
 *
 * 「一条永远红的门禁会退化成噪音」与「直接豁免等于开洞」是同一件事的两面。
 * 这里取第三种：**列出我们知道的缺口，并让清单自己失效**。
 *
 * 三条判据（缺一不可，代码在 main 的循环里）：
 *
 *   1. **未声明且缺失** → 红。抓漂移：目录加了一行、runtime 没跟上。
 *   2. **已声明但已实现** → 红，提示「删掉这条声明」。这就是自失效，
 *      防止实现做完了清单还挂着，规则被悄悄放宽。
 *   3. **每条必须带可核对的来源**，不是「待办」「下阶段」。
 *
 * 进清单的唯一条件：**这一条能在既有计划里指到具体的线**。指不到计划项的缺口
 * **不进清单、直接红**——「没有计划的缺口」本来就该一直红着提醒。
 *
 * 先例：`check_handler_size.py` 的 `REVIEWED_OVERSIZE` 与它的 selftest 僵尸检测
 * （`reviewed-oversize zombie: ... is still exempted but now only N lines`）。
 *
 * 所以本门禁守的**不是「全都实现了」**，是：
 *   「声称有而实际没有的，恰好只有我们声明过的那一批」。
 *
 * ── 进清单的唯一条件：**能指到一条既有计划线** ─────────────────────
 *
 * 不是「实现难度」，也不是「严不严重」。难度体现在计划线叫什么名字，
 * 不体现在进不进清单。两条规则冲突时看**「我们知不知道它缺」**：
 *
 *   | 状态                       | 含义         | 处置                    |
 *   |----------------------------|--------------|-------------------------|
 *   | 缺、**知道**缺、有排期      | 计划内未落地 | **进清单**（带计划线）   |
 *   | 缺、**不知道**缺            | 漂移         | 红，等我们决定           |
 *   | 声称有、实际没有、**无排期** | bug          | 红，必须修               |
 *
 * 「红着可以，红得没名分不行」——没有归属的永久红会被学会忽略。
 *
 * ── 刻意**不在**清单里的 2 条 ─────────────────────────────────────
 *
 * 它们是真正的 bug（声称有、实际没有、**且无排期**）：
 *
 *   0x0014 `pow`        —— 已派 coding5（与 sqrt/exp/log 同族）。
 *                          注意按修订判据它**已 resolved**：100 条特化全注册，
 *                          只是 session 还没装 `scalar_generated_entries`。
 *                          所以它其实连红都不该红。
 *   0x002A `ptreq·not`  —— 已派 coding5（`ptreq` 的取反）。
 *
 */
typedef struct declared_gap {
    binrwir_operation_identifier_t operation_identifier;
    const char *operator_name;
    const char *source; /* 可核对的计划来源，不写日期、不写下阶段 */
} declared_gap_t;

static const declared_gap_t kDeclaredGaps[] = {
    {0x0106U, "compactarray·shape", "看板 §九 B 线容器"},
    {0x0130U, "xv·at", "容器线 coding4"},
    {0x0131U, "xv·set", "容器线 coding4"},
    {0x0132U, "xv·reshape", "容器线 coding4"},
    {0x0133U, "xv·reinterpret", "容器线 coding4"},
    {0x0134U, "xv·langtype", "容器线 coding4"},
    {0x0135U, "xv·bodylen", "容器线 coding4"},
    {0x0140U, "ndarray·numel", "容器线 coding4"},
    {0x0141U, "ndarray·dim", "容器线 coding4"},
    {0x0142U, "ndarray·shape", "容器线 coding4"},
    /* 安装签名族：读侧零操作数、写侧有值、`result_rule = IDENTITY`。
     * 特化表里连一行都没有是**必然**（特化是「按操作数类型选行」，它们没有
     * 操作数），不是遗漏。补实现时不能照 `scalar_transcendental` 模板。 */
    /*
     * ── 已从本清单移除：`kvspace/key·abs`（0x01A2）────────────────────
     *
     * 曾以「指针一等值立项」挂在这里。2026-11-06 该 handler 随 head ABI 收敛
     * 一起落地（`host.c` 的 `runtimec_host_key_abs`），本用例随即报出
     * `STALE-DECLARATION  0x01A2`——正是自失效那条判据在起作用。按机制删掉
     * 声明，不是把清单放宽：它现在由「缺就红」那条规则覆盖。
     *
     * 同批注册、同样**不曾**进过清单的两条（记在这里防重复入册）：
     *   `kvspace/key·tostr` 0x01A3、`vthread·pc` 0x0185。
     */
};

/* 该 opid 是否在声明清单里；命中时返回那条实体（自失效检查要看它）。 */
static const declared_gap_t *find_declared_gap(
    binrwir_operation_identifier_t identifier) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < sizeof(kDeclaredGaps) / sizeof(kDeclaredGaps[0]);
         ++ordinal) {
        if (kDeclaredGaps[ordinal].operation_identifier == identifier) {
            return &kDeclaredGaps[ordinal];
        }
    }
    return NULL;
}


/*
 * 任一特化行已注册即算该基础 opid 可执行。
 *
 * 为什么「至少一行」而不是「全部」：某些类型组合可能故意不实现（char 族走
 * 手写特例就是这种）。要求全部会在那些组合上报假缺失。
 */
static bool has_registered_specialization(
    const binrwir_registry_t *registry,
    binrwir_operation_identifier_t base_identifier, size_t *output_rows) {
    size_t ordinal;
    size_t rows = 0U;

    *output_rows = 0U;
    for (ordinal = 0U; ordinal < BINRWIR_SPECIALIZATION_TABLE_COUNT; ++ordinal) {
        const binrwir_specialization_t *row = &binrwir_specializations[ordinal];
        const binrwir_registry_entry_t *entry = NULL;

        if (row->base_operation_identifier != base_identifier) {
            continue;
        }
        rows += 1U;
        if (binrwir_registry_find(registry,
                                  row->specialized_operation_identifier,
                                  &entry) == RUNTIMEC_STATUS_SUCCESS) {
            *output_rows = rows;
            return true;
        }
    }
    *output_rows = rows;
    return false;
}

int main(int argument_count, char **arguments) {
    const char *kvspace_library = argument_count > 1 ? arguments[1] : NULL;
    char directory[256];
    char data_source_name[320];

    if (kvspace_library == NULL) {
        fprintf(stderr, "SKIP: need the kvspace library path\n");
        return SKIP_RETURN_CODE;
    }
    void *library = dlopen(kvspace_library, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "SKIP: cannot load %s\n", kvspace_library);
        return SKIP_RETURN_CODE;
    }

        runtimec_error_t error;

    runtimec_error_reset(&error);

    snprintf(directory, sizeof(directory),
             "/home/peng.li24/github.com/array2d/kvlang/workspace/tmp/coding2/"
             "catalog-coverage-%d",
             (int)getpid());
    (void)mkdir(directory, 0700);
    snprintf(data_source_name, sizeof(data_source_name), "shm://%s/store.shm",
             directory);
    void *handle = kvspaceConnect(data_source_name);
    if (handle == NULL) {
        fprintf(stderr, "SKIP: cannot connect %s\n", data_source_name);
        return SKIP_RETURN_CODE;
    }

    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    const runtimec_kvspace_access_request_t adapter_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = handle,
        .instance_identifier = UINT64_C(1),
    };
    runtimec_kvspace_access_t *access = NULL;
    if (runtimec_kvspace_access_create(&adapter_request, &access, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL: adapter create failed\n");
        return 1;
    }

    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = sizeof(options);
    options.allocator = allocator;
    options.compilation_cache_capacity = 4U;
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    runtimec_session_t *session = NULL;
    if (runtimec_session_create(access, &options, &session, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "FAIL: session create failed: %.*s\n",
                (int)error.message.size,
                error.message.data != NULL ? error.message.data : "");
        runtimec_kvspace_access_destroy(access, &error);
        return 1;
    }

    /*
     * 取 session **实际装载出来**的那张表。
     *
     * 不自建 registry：自建就绕过了 `session.c` 的装载路径，而 `pow` 正是藏在
     * 「目录说有、session 没装」这个缝里。
     */
    runtimec_session_runtime_components_t components;
    memset(&components, 0, sizeof(components));
    if (runtimec_session_get_runtime_components(session, &components) !=
            RUNTIMEC_STATUS_SUCCESS ||
        components.builtin_registry == NULL) {
        fprintf(stderr, "FAIL: session exposes no builtin registry\n");
        runtimec_session_destroy(session);
        runtimec_kvspace_access_destroy(access, &error);
        return 1;
    }
    const binrwir_registry_t *registry = components.builtin_registry;

    /*
     * 遍历目录的**全部**能力行。`VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT` 由
     * `rwir_catalog.def` 展开而成，所以这个循环天然跟着目录走——加一行能力，
     * 这里自动多查一条，不会漏。
     */
    size_t missing_count = 0U;
    size_t resolved_base_count = 0U;
    size_t resolved_specialized_count = 0U;
    size_t declared_count = 0U;
    size_t stale_count = 0U;
    size_t claimed_count = 0U;
    size_t core_count = 0U;
    size_t row_count = 0U;
    vmkv_rwir_capability_identifier_t identifier;

    for (identifier = 0; identifier < VMKV_RWIR_CAPABILITY_IDENTIFIER_LIMIT;
         ++identifier) {
        const vmkv_rwir_capability_t *capability =
            vmkv_rwir_manifest_capability(identifier);
        const binrwir_registry_entry_t *entry = NULL;
        const declared_gap_t *declared_here = NULL;
        runtimec_status_t status;

        if (capability == NULL || capability->operator_name.size == 0U) {
            continue;
        }
        row_count += 1U;
        if (is_core_dispatch(capability->operation_identifier)) {
            core_count += 1U;
            continue;
        }
        claimed_count += 1U;
        status = binrwir_registry_find(registry,
                                       capability->operation_identifier,
                                       &entry);
        declared_here = find_declared_gap(capability->operation_identifier);
        /*
         * 判据（修订版）：**基础已注册** 或 **至少一条特化已注册**。
         *
         * 后者的依据是生产路径无条件调用 `binrwir_binfunc_specialize`
         * （`execution_window.c:572`）——基础 opid 到不了 `registry_find`。
         * 只看基础会把 `pow` 这类「100 条特化全注册、基础没注册」的算子
         * 误报成缺失。
         */
        {
            size_t specialization_rows = 0U;
            const bool base_registered =
                status == RUNTIMEC_STATUS_SUCCESS;
            const bool specialized_registered =
                !base_registered &&
                has_registered_specialization(registry,
                                              capability->operation_identifier,
                                              &specialization_rows);

            if (base_registered || specialized_registered) {
                if (base_registered) {
                    resolved_base_count += 1U;
                } else {
                    resolved_specialized_count += 1U;
                }
                /*
                 * 自失效：声明了却已经实现 —— 红，提示删声明。
                 * 漏掉这条，清单就变成永久白名单：实现做完了它还挂着，
                 * 规则被悄悄放宽。
                 */
                if (declared_here != NULL) {
                    fprintf(stderr,
                            "STALE-DECLARATION  0x%04X  %.*s  is declared as a "
                            "gap (\"%s\") but the runtime already resolves it "
                            "(%s); remove that declaration\n",
                            (unsigned)capability->operation_identifier,
                            (int)capability->operator_name.size,
                            capability->operator_name.data,
                            declared_here->source,
                            base_registered ? "base opid"
                                            : "a specialized opid");
                    stale_count += 1U;
                }
                continue;
            }
            /*
             * 真缺：基础没注册，特化表里连一行都没有，或行有但一条都没注册。
             * 分清这两种，因为修法不同——前者是「这条算子根本没实现」，
             * 后者是「实现了但加载器没装那张表」。
             */
            if (declared_here != NULL) {
                declared_count += 1U;
                continue;
            }
            fprintf(stderr,
                    "MISSING  0x%04X  %.*s  (base opid not registered; "
                    "specialization table has %zu row(s) for it, none "
                    "registered; not in the declared-gap list)\n",
                    (unsigned)capability->operation_identifier,
                    (int)capability->operator_name.size,
                    capability->operator_name.data, specialization_rows);
            missing_count += 1U;
        }
    }

    printf("catalog_coverage_test: catalog has %zu row(s) (%zu core, "
           "%zu through the registry); runtime registry holds %zu entr(ies)\n",
           row_count, core_count, claimed_count, registry->entry_count);
    /*
     * 三个数字要一起报，别压成「N 条红」：
     *   resolved  = 真能执行的
     *   declared  = 计划内、尚未落地（不红）
     *   missing   = 未声明且缺失（红，这是漂移）
     * 只有放在一起才读得出「声称 125 条，实际能执行多少」。
     */
    /*
     * 三类分开报，别压成一个「N 条红」：
     *   resolved(base)       —— 基础 opid 已注册
     *   resolved(特化)       —— 基础没注册，但特化表里有已注册的行（生产路径走这条）
     *   真的缺               —— 两者都没有
     * 第三个数才是缺口。
     */
    printf("catalog_coverage_test: %zu resolved(base), %zu resolved(only via "
           "specialization), %zu declared as planned gaps, %zu really missing, "
           "%zu stale declarations\n",
           resolved_base_count, resolved_specialized_count, declared_count,
           missing_count, stale_count);

    runtimec_session_destroy(session);
    runtimec_kvspace_access_destroy(access, &error);
    kvspaceClose(handle);

    if (missing_count != 0U) {
        fprintf(stderr,
                "catalog_coverage_test: %zu capability row(s) are claimed by "
                "the catalog, absent from the runtime, AND not declared as "
                "planned gaps\n",
                missing_count);
    }
    if (stale_count != 0U) {
        fprintf(stderr,
                "catalog_coverage_test: %zu declared gap(s) are already "
                "implemented; the declarations have gone stale\n",
                stale_count);
    }
    if (missing_count != 0U || stale_count != 0U) {
        return 1;
    }
    printf("catalog_coverage_test: OK (every claimed row either resolves or is "
           "a declared planned gap)\n");
    return 0;
}
