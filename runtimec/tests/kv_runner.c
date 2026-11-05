/*
 * 组件：tests/kv_runner
 * 作用：以 kvlang 语法为单位的用例运行器——把 .kv 源码 layout 进 kvspace、
 *       编成 BinRWIR、执行，并产出三态结论。
 *
 * 为什么不是更多 C 用例：C 用例证明「组件按契约工作」，证明不了「一段 kvlang
 * 程序能被正确执行」。ctest 全绿时 layout 写路径一挂就全挂，因为每个组件都在用
 * 自己的替身数据。kv 用例是唯一以语言语法为单位的判据。
 *
 * 三态（decision-kv-test-corpus.md §4）：
 *   pass     输出与期望逐行相同
 *   fail     跑起来了，输出不符或运行时错误
 *   blocked  执行停在 FOREIGN 标记处——runtimec 没有该能力。
 *            **必须打印 文件名 + pc + 算子名**，否则它就退化成「这条没过」。
 *
 * 第四态：**预期 layout 失败**（语法禁令的负向用例）。文件头写
 *   `// 期望错误: <必须出现在 layout 错误里的子串>`
 * 即表示该用例预期 layout 拒绝，而不是预期跑出某段输出。判定：
 *   layout 失败 且 错误文本含该子串 → **PASS**（计入 pass，并单列 expected_failed 计数）
 *   layout 失败 但 子串不命中        → **FAIL**（贴实际错误首行）
 *   layout 成功                      → **FAIL**（「预期失败却通过了」）
 *
 * 两条硬约束：
 *   1. 子串必须非空。空的 `// 期望错误:` 等于「随便报个错就算过」，会把任意语法错误
 *      都算成通过——那是恒真判据，直接判 FAIL。
 *   2. 计数分开报：`pass N（其中预期失败 M）`。否则「预期失败用例 100% 通过」会被读成
 *      「正向用例也全过」，两种 pass 混在一个数里就没法核。
 *
 * 没有 `// 期望错误:` 的文件行为完全不变（原有三态）。正向用例仍必须有
 * `// 期望输出:`，否则 FAIL——没有期望的用例会永远 pass。
 *
 * ── fail 的五类细分（按**修复方向**排，不按发生顺序）──────────────────
 * fail 不是一类：不同成因指向完全不同的修复动作，混成一个数会把人带错方向。
 *
 *   未观测到     期望非空、实际零行输出。**不是「输出不符」**——是没观测到。
 *                修复方向：查**链路**（算子没执行到 / println 没接上 / 被挡住）。
 *                判据：`actual_count == 0 && expectation->count != 0`。
 *                这一条先于「行数不等」判定，否则「期望 2 行实际 0 行」会被算进
 *                「差了一行」，把人指去查实现。
 *
 *   执行期失败   跑起来了但返回非 SUCCESS（带 status / exit.kind / detail / pc）。
 *                修复方向：查**链路**，看错误码定位到哪一层。
 *
 *   编码期失败   layout 过了但成不了 Program（`vmk_encoder` / `local_types` 报的）。
 *                修复方向：查**编码器**，与执行无关。
 *
 *   期望不符     跑完且行数相等，但某行内容不同。
 *                修复方向：查**实现**或查**期望**——两者要人判，**不得**直接改
 *                期望值去凑（那是「测试是镜子」）。
 *
 *   用例不完整   实际行数**多于**期望。既不是期望错也不是实现错，是**用例没写全**。
 *                修复方向：补用例。
 *
 * 另有 `其中预期失败`：负向用例被 layout 正确拒绝，它计入 pass 而不是 fail，
 * 单列出来是为了不让「负向用例全过」被读成「正向用例也全过」。
 *
 * ── 汇总首行与覆盖率 ──────────────────────────────────────────────
 *   `executed X / blocked Y / fail Z / pass(仅编码) W`
 * X 与 Y **必须同时出现**：只看 X，「5/25」像全过；只看 Y，「20」看不出底下有 5 条
 * 真跑过。合起来才知道「大部分没验」。
 *
 * 覆盖率另有单独一行 `通过 executed X / 总数 N` —— 给人看，不给退出码看。
 * **分母恒为全部用例数**：任何比值的分母若由计算分子的同一段代码挑选，
 * 这个比值就没有信息量（早先用 `executed/(executed+blocked)` 时它恒为 100%）。
 * ctest 的绿只承诺「已执行的都相符」，这一行说的是「已执行的有多少」。
 *
 * `println` 替身放在执行循环**外面**，不进 binrwir_registry：能力表是
 * 「runtimec 能执行什么」的事实源，把只存在于测试的处理器写进去，等于让能力表
 * 说一句不成立的话（routing/README.md §四之二：println 属 byteseek）。
 *
 * 用法：kv_runner <corpus-dir> <layout-lib> <kvspace-lib>
 */

/* 必须写在**任何** include 之前：特性宏先于系统头生效，否则 `<stdio.h>` 已经
 * 按严格 ISO 的可见性展开，`mkstemp`/`fileno` 不可见。
 * 本构建是 `-std=c11`（C_EXTENSIONS OFF），两个函数都是 POSIX，需要这一行。 */
#define _POSIX_C_SOURCE 200809L

#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "binrwir_compiler/compiler.h"
#include "binrwir/specialize.h"
#include "engine/execution_window.h"
#include "engine/session.h"
#include <kvspace/kvspace.h>

#include "contract/kvspace_access.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SKIP_RETURN_CODE 77

/* 路径段最长 255（NAME_MAX），相对路径最多 4 层 + 分隔符，1024 富余。 */
#define CASE_PATH_CAPACITY 1024

/* layout 库的 C 入口：读 .kv 文件、布局进 dsn 指向的 kvspace。
 * 六参：路径、DSN、入口名出参、入口名容量、错误出参、错误容量。 */
typedef int (*layout_file_function_t)(const char *path, const char *data_source,
                                      char *entry, size_t entry_capacity,
                                      char *error, size_t error_capacity);

/* ── 三态计数 ─────────────────────────────────────────────────────── */

typedef struct runner_counts {
    size_t passed;
    size_t failed;
    size_t blocked;
    /* pass 之中「预期 layout 失败且子串命中」的条数。单列的原因见文件头约束 2。 */
    size_t expected_failed;
    /* pass 之中**真执行且输出逐行相同**的条数。没有它就分不清「编过了」与
     * 「跑对了」——那是两件事。 */
    size_t executed;
    /* fail 的两个细分，处置不同：
     *  expectation_wrong —— 跑完了但输出与期望不符。要先判是期望写错还是实现错，
     *                        **不得**直接改期望值去凑（那是「测试是镜子」）。
     *  incomplete        —— 实际输出比期望多/少了行，而期望本身可能就没写全。
     *                        这类是**用例不完整**，既不是期望错也不是实现错。 */
    size_t expectation_wrong;
    size_t incomplete;
    /* **观测为空**：期望有内容，实际一条都没输出。与 expectation_wrong 分开，
     * 因为修复方向相反——前者查链路，后者查实现/期望。 */
    size_t unobserved;
    /* 执行期失败：跑起来但报错（status != SUCCESS）。
     * 与 `unobserved` 的区别：这一条**根本没产出输出**且带了错误码；
     * unobserved 是执行「成功」但什么都没输出。两者都查链路，但一个看错误码、
     * 一个看输出接管——不是同一件事。 */
    size_t execution_failed;
    /* 编码期失败：layout 过了但成不了 Program。查编码器，不是查执行。 */
    size_t encode_failed;
} runner_counts_t;

/* blocked 记录：文件 + pc + 算子名。分组后就得到能力缺口表。 */
typedef struct blocked_entry {
    char file[CASE_PATH_CAPACITY];
    uint32_t pc;
    char operation[64];
} blocked_entry_t;

#define MAX_BLOCKED 512
static blocked_entry_t g_blocked[MAX_BLOCKED];
static size_t g_blocked_count;



/* ── 分配器（与 end_to_end 同形：malloc/free 包装） ────────────────── */

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

static void *runtimec_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return calloc(1U, size);
}

static void runtimec_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/* ── 运行环境 ─────────────────────────────────────────────────────── */

/* 被调函数的入口 source pc。
 *
 * **依据是证据链，不是「约定」**：layout 把函数第 i 条指令落成指令槽
 * `/lib/<fn>/[i,0]`，而 `vmkv/rwir.c:438` 的
 * `output_instruction->logical_program_counter = line` 就是从坐标第一段取的。
 * 实测 `add1` 与 `test` 的槽位都是 `[1,0]` `[2,0]` `[3,0]`，故入口 = 1。
 *
 * **不动态计算**：驱动拿不到被调函数的 Program（encode 在 execution_window
 * 内部发生），硬要算就是造第二条路径。 */
#define KVRUN_CALLEE_ENTRY_SOURCE_PC 1U

/* 一次调用的实参个数上限。签名层没有硬上限，但驱动要在栈上开数组；
 * 超了失败封闭，不动态分配——`add1` 这类用例最多两三个。 */
#define KVRUN_MAX_ARGUMENTS 16U

/* 一帧的执行结果，够调用方判「继续 / 结束 / 失败」。 */
typedef enum frame_outcome {
    FRAME_OUTCOME_INVALID = 0,
    FRAME_OUTCOME_DONE,
    FRAME_OUTCOME_HANDOFF,
    FRAME_OUTCOME_ERROR,
    FRAME_OUTCOME_EXECUTE_FAILED
} frame_outcome_t;

typedef struct runner_environment {
    void *layout_library;
    layout_file_function_t layout_file;
    /* kvspace 走链接期依赖，不再 dlopen；layout 仍走 dlopen（可选构建产物，与本次改动无关）。 */
    /* ── 以下**每条用例**重建 ──────────────────────────────────────
     *
     * store 不能整批共用：上一条用例 layout 进去的 `/lib` 会留在同一个 store 里，
     * 下一条用例就带着别人的函数跑。实测症状是「单独跑通过、整批跑全红」——
     * 而且**只在某些组合下**复发，最难查的一类。
     *
     * 判据：同一组用例，单独跑与整批跑，每条结果必须相同。
     * 因此这三项由 `case_store_open` / `case_store_close` 按用例序号建/拆，
     * 路径带上序号，不共用。 */
    void *kvspace_handle;
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_session_t *session;
    char directory[256];
    char data_source[320];
    unsigned sequence;
} runner_environment_t;

static bool environment_open(runner_environment_t *environment,
                             const char *layout_path,
                             const char *kvspace_path) {
    memset(environment, 0, sizeof(*environment));
    environment->layout_library = dlopen(layout_path, RTLD_NOW | RTLD_LOCAL);
    if (environment->layout_library == NULL) {
        fprintf(stderr, "SKIP: cannot load layout %s: %s\n", layout_path,
                dlerror());
        return false;
    }
    environment->layout_file = (layout_file_function_t)(uintptr_t)dlsym(
        environment->layout_library, "kvlangLayoutFile");
    if (environment->layout_file == NULL) {
        fprintf(stderr, "SKIP: layout library has no kvlangLayoutFile\n");
        return false;
    }
    (void)kvspace_path;
    return true;
}

/*
 * 删掉一套 case store 的**全部**文件，再删目录。
 *
 * shm 后端落五个文件，不是只有 `store.shm`：
 *   store.shm / store.shm.garb / store.shm.gen
 *   store.shm.sbo.data / store.shm.sbo.head
 * 只删 `store.shm` 的后果实测过：每个用例留约 170MB（逻辑大小）残骸，
 * 一条语料跑完攒下 173 个目录。`df` 上不明显（sparse），但目录会一直堆。
 *
 * 名单是显式的，不按前缀通配——通配会连带删掉将来后端加的、语义不同的文件，
 * 而那种误删不会报错。漏项的表现是 /tmp 攒垃圾，看见残留就回来看这个名单。
 *
 * 另有 `capabilities.kv`（我们写的声明文件），一并删。
 */
static void remove_case_store_files(const char *directory) {
    static const char *const files[] = {
        "store.shm", "store.shm.garb", "store.shm.gen",
        "store.shm.sbo.data", "store.shm.sbo.head",
        "capabilities.kv",
    };
    char path[320];
    for (size_t ordinal = 0U; ordinal < sizeof(files) / sizeof(files[0]);
         ++ordinal) {
        snprintf(path, sizeof(path), "%s/%s", directory, files[ordinal]);
        (void)remove(path);
    }
    (void)rmdir(directory);
}

/*
 * 每条用例一套 store + session。
 *
 * 为什么必须按用例隔离：上一条用例 layout 进去的 `/lib`（包括 `.src`、`def rwir`、
 * 指令树）留在同一个 store 里，下一条用例就带着别人的函数跑。实测症状是
 * 「**单独跑通过、整批跑全红**」，而且只在某些组合下复发。
 *
 * 路径带序号，所以两条用例之间零共享。`case_store_close` 无论成败都拆。
 *
 * 返回 false 表示这条用例跑不了（store/session 建不起来），调用方记为 fail 而不是
 * 整批 SKIP——整批 SKIP 会掩盖「跑到第 N 条才坏」这个事实。
 */
static bool case_store_open(runner_environment_t *environment,
                            unsigned case_ordinal) {
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    snprintf(environment->directory, sizeof(environment->directory),
             "/tmp/runtimec-kvrun-%d-%u", (int)getpid(), case_ordinal);
    /* 目录可能残留（上次异常退出），先清干净再建：带着旧 store 跑等于没隔离。 */
    remove_case_store_files(environment->directory);
    if (mkdir(environment->directory, 0700) != 0) {
        fprintf(stderr, "case store: cannot mkdir %s\n", environment->directory);
        return false;
    }
    snprintf(environment->data_source, sizeof(environment->data_source),
             "shm://%s/store.shm", environment->directory);
    environment->kvspace_handle = kvspaceConnect(environment->data_source);
    if (environment->kvspace_handle == NULL) {
        fprintf(stderr, "case store: cannot connect %s\n",
                environment->data_source);
        return false;
    }
    runtimec_kvspace_access_request_t request = {
        .allocator = {.context = NULL,
                      .allocate = test_allocate,
                      .deallocate = test_deallocate},
        .borrowed_kvspace_handle = environment->kvspace_handle,
        .instance_identifier = UINT64_C(1),
    };
    if (runtimec_kvspace_access_create(&request,
                                       &environment->kvspace_access,
                                       &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "case store: adapter create failed\n");
        return false;
    }
    runtimec_session_options_t options;
    memset(&options, 0, sizeof(options));
    options.structure_size = (uint32_t)sizeof(options);
    options.allocator.context = NULL;
    options.allocator.allocate = runtimec_allocate;
    options.allocator.deallocate = runtimec_deallocate;
    /* session 从 options 借句柄（它不 connect、不关闭）。 */
    options.kvspace_handle = environment->kvspace_handle;
    options.compilation_cache_capacity = 64U;
    /* enable_jit=false 时 machine_code_capacity **必须为 0**：
     * session 把「不开 JIT 却要机器码空间」判为非法请求（session.c:92-93）。
     * 骨架阶段只用解释档。 */
    options.machine_code_capacity = 0U;
    options.enable_jit = false;
    if (runtimec_session_create(environment->kvspace_access, &options,
                                &environment->session,
                                &error) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "case store: session create failed: status=%d %s\n",
                (int)error.status, error.message.data ? error.message.data : "");
        return false;
    }
    return true;
}

/* 拆掉本用例的 store + session。**成败都调用**——失败路径不清理，
 * 下一个序号拿不到干净 store（虽然路径不同，但 /tmp 会攒垃圾）。 */
/* 返回 status 而不是 void：生命周期函数若吞掉失败，「收尾出错」就永远看不见。
 * 各步骤都尝试执行（不早退），把任一失败记进返回值——拆一半的 store 比没拆更糟，
 * 所以不能因为第一步失败就跳过后面。 */
static runtimec_status_t case_store_close(runner_environment_t *environment) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (environment->session != NULL) {
        if (runtimec_session_destroy(environment->session) !=
            RUNTIMEC_STATUS_SUCCESS) {
            status = RUNTIMEC_STATUS_ERROR;
        }
        environment->session = NULL;
    }
    if (environment->kvspace_handle != NULL) {
        runtimec_error_t error;
        /* 顺序不可交换：先解借（access 里登记着句柄），再关闭句柄。 */
        if (environment->kvspace_access != NULL) {
            if (runtimec_kvspace_access_destroy(environment->kvspace_access,
                                                &error) !=
                RUNTIMEC_STATUS_SUCCESS) {
                status = RUNTIMEC_STATUS_ERROR;
            }
            environment->kvspace_access = NULL;
        }
        kvspaceClose(environment->kvspace_handle);
        environment->kvspace_handle = NULL;
    }
    if (environment->directory[0] != '\0') {
        remove_case_store_files(environment->directory);
        environment->directory[0] = '\0';
    }
    return status;
}

/*
 * 一帧执行到「不再 CALL」为止，遇到 CALL 就建子帧递归下去。
 *
 * ── 为什么需要它 ──────────────────────────────────────────────────
 * `vmbin/interpreter.c` 执行到 `call` 指令就发 `EXIT_CALL` 交回控制权
 * （带 target_identifier + resume pc），`execution_window` 把它当 running 写进
 * `‥status` 就返回——**它本来就不负责推进**。驱动是缺的那一环。
 *
 * ── 四条依据（每条都能指到代码，不是约定）────────────────────────
 * 1. **不写 `‥lib`**：`contract/kvspace_access.c` 的 `ensure_frame` 已在写，
 *    驱动再写就是同一事实两个写方（两边写同一个值不会报错，直到有一边改了）。
 * 2. **被调入口 pc = 1**：见 `KVRUN_CALLEE_ENTRY_SOURCE_PC` 的注释。
 * 3. **不自己 encode**：`execution_window.c:545-560` 在编译仓库未命中时会
 *    按 `request->function_path` 调 `vmkv_encode`。驱动只需把**被调函数的路径**
 *    放进请求再发一次 execute——有两个编码入口是这条链上最贵的错误。
 * 4. **`‥callpc`/`‥returnpc` = `[stackid][pc]`**（spec `05/01-执行模型:91`）。
 *
 * ── 返回链断裂必须报错，不许静默继续 ────────────────────────────
 * spec `05/01-执行模型:87-88`：帧无 `‥returnpc` 时**必须**报 RuntimeError 并保留该帧。
 * 所以 `resume == PROGRAM_COUNTER_NONE` 不能当成「执行完了」，那是**静默的返回链断裂**，
 * 而它正是崩溃恢复要能发现的东西。
 *
 * `depth` 是当前帧号（根帧 1），子帧 `depth + 1`。**必须递增**：复用同一帧号会让
 * 子帧覆盖调用方帧——那种错不会立刻崩，而是算错。
 */
static frame_outcome_t execute_frame_until_done(
    runner_environment_t *environment, const char *function_path,
    uint32_t source_pc, int32_t depth, const char *frame_segment,
    const char *vthread_identifier,
    const runtimec_argument_description_t *arguments, size_t argument_count,
    unsigned *out_callee_depth, binrwir_execution_exit_t *out_exit) {
    char frame_path[128];
    /*
     * 目录名带唯一序号：`/vthread/{vid}/[{depth}.#{serial}]`。
     * `depth` 保留在最前面，读的人仍能一眼看出层级；序号保证同深度多次调用
     * 不共用目录（见 `g_frame_serial` 的注释）。
     */
    const int frame_length =
        snprintf(frame_path, sizeof(frame_path), "/vthread/%s/[%s]",
                 vthread_identifier, frame_segment);
    if (frame_length < 0 || (size_t)frame_length >= sizeof(frame_path)) {
        return FRAME_OUTCOME_INVALID;
    }
    runtimec_execution_window_request_t request;
    memset(&request, 0, sizeof(request));
    request.function_path = runtimec_string_view_from_c_string(function_path);
    request.vthread_identifier =
        runtimec_string_view_from_c_string(vthread_identifier);
    request.frame_path = runtimec_string_view_from_c_string(frame_path);
    request.stack_identifier = depth;
    request.source_logical_program_counter = source_pc;
    request.arguments = arguments;
    request.argument_count = argument_count;

    runtimec_execution_window_result_t result;
    memset(&result, 0, sizeof(result));
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    /* 窗口的 message 是**线程局部静态缓冲**（`execution_window.c:922`），
     * 下一次失败会覆盖它。所以这里立刻拷走，之后任何再失败的调用都会冲掉原话。 */
    char message_copy[256];
    message_copy[0] = '\0';
    if (runtimec_execution_window_execute(environment->session, &request,
                                          &result, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *out_exit = result.execution_exit;
        if (error.message.data != NULL && error.message.size != 0U) {
            const size_t copy = error.message.size < sizeof(message_copy) - 1U
                                    ? error.message.size
                                    : sizeof(message_copy) - 1U;
            memcpy(message_copy, error.message.data, copy);
            message_copy[copy] = '\0';
        }
        /* 把窗口给的消息带出来：`detail` 是分类码，`message` 才是能定位的那句。
         * 只在 `execute_frame_until_done` 里打一次，不往回调方穿透。 */
        /* `detail_code` 一个字段承载**两个枚举**：窗口自己的诊断码
         * （`execution_window.c:33-50`，6=COMPILE_FAILED）与解释器的诊断码
         * （`interpreter.h:43-51`，6=UNRESOLVED_TARGET）。
         *
         * 分辨办法：**窗口的 fail 路径会把 error 填上**（`execution_window.c:903-933`
         * 那条会写 "instruction execution failed (execution exit detail=…)"），
         * 而窗口自己提前返回的路径不会带这句。所以 `message` 空 = 窗口层失败。
         * 这也是 `execution_window.c:905` 注释里说的「读的人会把两者读错」。 */
        return FRAME_OUTCOME_EXECUTE_FAILED;
    }
    *out_exit = result.execution_exit;
    /* 本帧实际执行的 Program。递归时每帧不同，所以**从结果里取**，
     * 不能存在驱动里当全局——那就是「一个函数只有一个活动帧」的错误假设。 */
    const binrwir_program_t *frame_program = result.program;
    switch (result.execution_exit.kind) {
    case BINRWIR_EXECUTION_EXIT_DONE:
        return FRAME_OUTCOME_DONE;
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return FRAME_OUTCOME_HANDOFF;
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return FRAME_OUTCOME_ERROR;
    case BINRWIR_EXECUTION_EXIT_CALL: {
        /* 返回链断裂：调用方没给 resume 点。spec 要求报错并保留帧，
         * **不许**当成「跑完了」。 */
        if (result.execution_exit.resume_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
            return FRAME_OUTCOME_ERROR;
        }
        /* 被调函数名在符号池里（不含 `/lib/`，见 encoder.c:1222-1226），
         * 补前缀才是它的绝对路径。名字可能已带包前缀（`outer/inner·add1`），
         * 补的是 `/lib/` 这一段，名字原样。 */
        runtimec_string_view_t callee_name =
            runtimec_string_view_make(NULL, 0U);
        runtimec_string_view_t callee_langtype =
            runtimec_string_view_make(NULL, 0U);
        /* `binrwir_program_get_symbol` 返回 **runtimec_status_t**，不是 bool。
         * 写成 `!get_symbol(...)` 会把 SUCCESS(0) 取反成 true，于是每次解析都
         * 判失败——两套返回值语义混用，而且不报错，只是走错分支。 */
        if (binrwir_program_get_symbol(
                frame_program,
                (uint16_t)result.execution_exit.target_identifier,
                &callee_name, &callee_langtype) != RUNTIMEC_STATUS_SUCCESS ||
            callee_name.size == 0U) {
            return FRAME_OUTCOME_ERROR;
        }
        /* **必须用 `%.*s` + size**：`runtimec_string_view_t` 是长度视图，
         * 不保证 NUL 终止。用 `%s` 会把视图后面的字节读进来——实测拼出
         * `/lib/add1rwfuncvint64int64`，于是 source revision 查不到，
         * 而错误只报「不可用」，看起来像 store 坏了。 */
        char callee_path[CASE_PATH_CAPACITY];
        const int callee_length =
            snprintf(callee_path, sizeof(callee_path), "/lib/%.*s",
                     (int)callee_name.size, callee_name.data);
        if (callee_length < 0 ||
            (size_t)callee_length >= sizeof(callee_path)) {
            return FRAME_OUTCOME_ERROR;
        }
        /* ── 物化常量实参到被调帧的 `._lit<i>` ─────────────────────
         *
         * spec `04-layout语义/03-函数:15-17`：字面量实参**在调用绑定期**物化为
         * 被调帧的 `._lit<i>` KV 值，再按地址传给函数。
         * （与 `05-runtime语义/04-函数调用:127-133` 不冲突——那条管同一个 Program
         * 内的普通 rwir，常量内联为 LITERAL 引用、不落临时 key。两个域。）
         *
         * `add1(41)` 是跨帧 rwfunc 调用，`*int64 $x` 是指针绑定需要一个 key，
         * 所以 `41` 必须物化。落点是新帧目录下，所以这里先建目录。
         */
        /*
         * **不在这里预建子帧目录**：帧路径由 `execute_frame_until_done` 自己算
         * （含唯一序号 `g_frame_serial`），执行窗口的 `ensure_frame` 会建它。
         *
         * 曾经在这里按 `/[depth+1]` 先 mkindex：那时两处算的是同一个名字，所以
         * 看不出问题；加了序号之后两处算法不一致，预建的目录和实际用的对不上，
         * 而 `ensure_frame` 又会因「帧上已有另一个函数」被拒——正是它要修的那个
         * 症状。**同一件事两个地方算，就是这个下场。**
         */
        /* 取出 call 指令的读参项（跳过最前的 FUNCTION 那个）。 */
        uint32_t call_instruction_identifier = 0U;
        binrwir_instruction_view_t call_instruction;
        memset(&call_instruction, 0, sizeof(call_instruction));
        if (binrwir_program_find_instruction(
                frame_program,
                result.execution_exit.logical_program_counter,
                &call_instruction_identifier) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_program_get_instruction(frame_program,
                                            call_instruction_identifier,
                                            &call_instruction) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return FRAME_OUTCOME_ERROR;
        }
        /* 驱动只给**描述**，物化归 `execution_window`。
         *
         * 早先这里自己拼 `._lit<i>` 并 `set_value`——那是让缺少 snapshot 的层
         * 去造 key：绕开来会破坏 revision（实测 `function source revision differs`），
         * 硬造又等于抄一份窗口的内部状态。两种都是第二份真相。 */
        runtimec_argument_description_t callee_arguments[KVRUN_MAX_ARGUMENTS];
        char read_local_keys[KVRUN_MAX_ARGUMENTS][128];
        char write_target_keys[KVRUN_MAX_ARGUMENTS][128];
        char write_target_langtypes[KVRUN_MAX_ARGUMENTS][128];
        size_t callee_argument_count = 0U;
        memset(callee_arguments, 0, sizeof(callee_arguments));
        for (size_t ordinal = 0U;
             ordinal < call_instruction.read_arguments.count &&
             callee_argument_count < KVRUN_MAX_ARGUMENTS;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.read_arguments.data[ordinal];
            if (argument->reference_kind == BINRWIR_REFERENCE_FUNCTION) {
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
                runtimec_bytes_view_t wire;
                runtimec_string_view_t literal_langtype;
                memset(&wire, 0, sizeof(wire));
                if (binrwir_program_get_literal(frame_program,
                                                argument->payload, &wire,
                                                &literal_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                    return FRAME_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_LITERAL;
                callee_arguments[callee_argument_count].literal_bytes = wire;
                callee_arguments[callee_argument_count].literal_langtype =
                    literal_langtype;
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
                /* 变量实参（`fib(a)` 的 `a`）：它**已经存在**于调用方帧，
                 * 所以是 EXISTING_KEY，窗口不创建。
                 *
                 * 这正是递归与单层调用的实质差别：单层的实参多是字面量，
                 * 递归的实参是上一层算出来的局部。 */
                runtimec_string_view_t local_name =
                    runtimec_string_view_make(NULL, 0U);
                runtimec_string_view_t local_langtype =
                    runtimec_string_view_make(NULL, 0U);
                if (binrwir_program_get_symbol(frame_program,
                                               (uint16_t)argument->slot,
                                               &local_name, &local_langtype) !=
                        RUNTIMEC_STATUS_SUCCESS ||
                    local_name.size == 0U) {
                    return FRAME_OUTCOME_ERROR;
                }
                const int local_key_written = snprintf(
                    read_local_keys[callee_argument_count],
                    sizeof(read_local_keys[callee_argument_count]),
                    "/vthread/%s/[%d]/%.*s", vthread_identifier, (int)depth,
                    (int)local_name.size, local_name.data);
                if (local_key_written < 0 ||
                    (size_t)local_key_written >=
                        sizeof(read_local_keys[callee_argument_count])) {
                    return FRAME_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        read_local_keys[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            /* 其它 kind（STATIC / PARAMETER_*）本阶段失败封闭。 */
            return FRAME_OUTCOME_ERROR;
        }
        /* 写参：指向调用方帧的槽。此刻可能还不存在（`add1(41) -> v` 的 `v`），
         * 所以描述成 WRITE_TARGET——**要不要创建由窗口判**，驱动不做这个判断。 */
        for (size_t ordinal = 0U;
             ordinal < call_instruction.write_arguments.count &&
             callee_argument_count < KVRUN_MAX_ARGUMENTS;
             ++ordinal) {
            const binrwir_argument_t *argument =
                &call_instruction.write_arguments.data[ordinal];
            /*
             * 写槽可以是**局部**（`add1(41) -> v`），也可以是**本帧自己的形参写位置**
             * （`fib(n1) -> (*a, *b)`）。后者是递归的形态：写回的不是新变量，
             * 而是调用方签名里那个 `*T $name`。
             *
             * 两者的 key **不同形**（见 `binrwir_frame_bindings.h`）：
             *   局部  `/vthread/<vid>/[<depth>]/<name>`
             *   形参  `/vthread/<vid>/[<depth>]/[0,+<ordinal+1>]`
             * 所以这里按 kind 分派，但 key 一律由 `binrwir_frame_*_key` 拼——
             * 拼法只有一处，本驱动不自己拼。
             */
            if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE) {
                /*
                 * 形参写位置：key 由 helper 拼，本驱动不自己拼。
                 * `slot` 是**该方向内的序号**（argidx），从 0 起。
                 */
                char frame_path[128];
                const int frame_length = snprintf(
                    frame_path, sizeof(frame_path), "/vthread/%s/[%s]",
                    vthread_identifier, frame_segment);
                if (frame_length < 0 ||
                    (size_t)frame_length >= sizeof(frame_path)) {
                    return FRAME_OUTCOME_ERROR;
                }
                const size_t parameter_key_size = binrwir_frame_parameter_key(
                    runtimec_string_view_from_c_string(frame_path),
                    argument->slot, true,
                    write_target_keys[callee_argument_count],
                    sizeof(write_target_keys[callee_argument_count]));
                if (parameter_key_size == SIZE_MAX) {
                    return FRAME_OUTCOME_ERROR;
                }
                /*
                 * 形参的 langtype 只能从**调用方 Program 的签名**取：
                 * `slot` 是该方向内的序号，签名表里就是它。
                 */
                runtimec_string_view_t parameter_type =
                    runtimec_string_view_make(NULL, 0U);
                {
                    binrwir_program_view_t view;
                    if (binrwir_program_get_view(frame_program, &view) !=
                            RUNTIMEC_STATUS_SUCCESS ||
                        (size_t)argument->slot >=
                            view.signature.write_parameter_count) {
                        return FRAME_OUTCOME_ERROR;
                    }
                    parameter_type =
                        view.signature.write_parameter_types[argument->slot];
                }
                const int type_written = snprintf(
                    write_target_langtypes[callee_argument_count],
                    sizeof(write_target_langtypes[callee_argument_count]),
                    "%.*s", (int)parameter_type.size, parameter_type.data);
                if (type_written < 0 ||
                    (size_t)type_written >=
                        sizeof(write_target_langtypes[callee_argument_count])) {
                    return FRAME_OUTCOME_ERROR;
                }
                callee_arguments[callee_argument_count].source =
                    RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
                callee_arguments[callee_argument_count].key =
                    runtimec_string_view_from_c_string(
                        write_target_keys[callee_argument_count]);
                callee_arguments[callee_argument_count].literal_langtype =
                    runtimec_string_view_from_c_string(
                        write_target_langtypes[callee_argument_count]);
                callee_argument_count += 1U;
                continue;
            }
            if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
                return FRAME_OUTCOME_ERROR;
            }
            runtimec_string_view_t local_name =
                runtimec_string_view_make(NULL, 0U);
            runtimec_string_view_t local_langtype =
                runtimec_string_view_make(NULL, 0U);
            if (binrwir_program_get_symbol(frame_program,
                                           (uint16_t)argument->slot,
                                           &local_name, &local_langtype) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                local_name.size == 0U) {
                return FRAME_OUTCOME_ERROR;
            }
            const int key_written = snprintf(
                write_target_keys[callee_argument_count],
                sizeof(write_target_keys[callee_argument_count]),
                "/vthread/%s/[%d]/%.*s", vthread_identifier, (int)depth,
                (int)local_name.size, local_name.data);
            if (key_written < 0 ||
                (size_t)key_written >=
                    sizeof(write_target_keys[callee_argument_count])) {
                return FRAME_OUTCOME_ERROR;
            }
            const int langtype_written = snprintf(
                write_target_langtypes[callee_argument_count],
                sizeof(write_target_langtypes[callee_argument_count]), "%.*s",
                (int)local_langtype.size, local_langtype.data);
            if (langtype_written < 0 ||
                (size_t)langtype_written >=
                    sizeof(write_target_langtypes[callee_argument_count])) {
                return FRAME_OUTCOME_ERROR;
            }
            callee_arguments[callee_argument_count].source =
                RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET;
            callee_arguments[callee_argument_count].key =
                runtimec_string_view_from_c_string(
                    write_target_keys[callee_argument_count]);
            callee_arguments[callee_argument_count].literal_langtype =
                runtimec_string_view_from_c_string(
                    write_target_langtypes[callee_argument_count]);
            callee_argument_count += 1U;
        }
        if (out_callee_depth != NULL) {
            *out_callee_depth = (unsigned)depth + 1U;
        }
        /* 递归进入被调帧。**深度递增**——复用同一帧号会让子帧覆盖调用方帧，
         * 那是「能跑一会儿但算错」的形态。 */
        /* 被调函数的 Program 由窗口内部 encode，驱动这里传 NULL：
         * `execute` 会用自己的 Program，并把结果里的 `program` 交回来。
         * **不自己 encode**——两个编码入口是这条链上最贵的错误。 */
        /* 被调帧用自己的 exit 变量：共用一个的话，被调在产出 exit 之前失败时
         * 调用方会读到**上一次**的 exit —— 那是「以看起来合理的值失败」。 */
        binrwir_execution_exit_t callee_exit;
        memset(&callee_exit, 0, sizeof(callee_exit));
        /*
         * 子帧段 = `<depth+1>.<call_site_pc>`。
         *
         * 帧身份必须来自**调用链**，不是计数器：
         *   - 同一调用点重复调用 ⇒ 同一段 ⇒ 复用同一帧（不增长）；
         *   - 不同调用点 ⇒ call_site_pc 不同 ⇒ 区分得开；
         *   - 递归 ⇒ depth 不同 ⇒ 区分得开；
         *   - 跑两遍 ⇒ 完全一样（帧路径可寻址，崩溃恢复靠的就是它）。
         *
         * 曾经只用 `<depth>`：同一深度先后调两个不同函数会撞同一帧路径，
         * `ensure_frame` 因「帧上已有另一个函数」拒绝（detail=13）。改用全局
         * 计数器是错的——不可确定、无法复用、无限增长。
         */
        char callee_frame_segment[64];
        const int callee_segment_length = snprintf(
            callee_frame_segment, sizeof(callee_frame_segment), "%d.%u",
            (int)depth + 1,
            (unsigned)result.execution_exit.logical_program_counter);
        if (callee_segment_length < 0 ||
            (size_t)callee_segment_length >= sizeof(callee_frame_segment)) {
            return FRAME_OUTCOME_ERROR;
        }
        const frame_outcome_t callee_outcome = execute_frame_until_done(
            environment, callee_path, KVRUN_CALLEE_ENTRY_SOURCE_PC,
            depth + 1, callee_frame_segment, vthread_identifier,
            callee_arguments, callee_argument_count, NULL, &callee_exit);
        if (callee_outcome != FRAME_OUTCOME_DONE) {
            /*
             * 子帧没跑完（ERROR / HANDOFF / INVALID）：**必须把它的 exit 交出去**。
             *
             * 曾经这里直接 `return callee_outcome;`——`out_exit` 一个字节都没写，
             * 于是调用方拿到的是**本帧自己那条 CALL 的 exit**（`kind=2 status=0`），
             * 子帧的失败被整个吞掉。症状看起来像「跑到一条 call 就没人接了」，
             * 实际是「子帧失败了，但从没被报告上去」。
             *
             * 实测（2026-11-05）：`fib` 第三层报 `kind=6 status=3 detail=9`，
             * 父帧拿到的却是 `kind=2 status=0`。
             */
            *out_exit = callee_exit;
            return callee_outcome;
        }
        /* 被调回来了：回到调用方的 resume 点。
         * 写参结果**不用搬运**——`*T $name` 是「指向调用方 key，读写沿链直达」
         * （spec 05/04-函数调用 的形态表）。 */
        /* 恢复调用方时**不能**再传本次的实参：`arguments` 是「调用被调方时用的」，
         * 不是调用方自己的。传错会让调用方的帧绑定被重新绑到被调的实参上——
         * 那是「以奇怪的方式跑一会儿然后算错」。
         *
         * 调用方的实参是它被调进来时那份，这里原样传 `arguments` 恰恰是错的：
         * 递归时 `fib(a)` 的 `a` 属于**被调那次**，恢复 fib 自身执行时，
         * 它要用的仍是自己被调时绑的那份。 */
        /* 恢复执行时**必须传本帧自己的实参**（形参 `arguments`）。
         *
         * 曾经传 NULL，理由是「`arguments` 是调用被调方时用的」——那是把两件事
         * 混了：`callee_arguments` 才是「给被调方的」；本帧的 `arguments` 是
         * **它自己被调进来时收到的那份**，恢复续跑必须原样再给一次，
         * 否则 `bind_parameters` 会因实参个数不足直接拒（detail=8）。 */
        return execute_frame_until_done(
            environment, function_path,
            result.execution_exit.resume_logical_program_counter, depth,
            frame_segment, vthread_identifier, arguments, argument_count,
            NULL, out_exit);
    }
    default:
        return FRAME_OUTCOME_INVALID;
    }
}

/*
 * 先让外部能力在 /lib 下存在。
 *
 * 用例里写 `println(...)`，而 layout 的 `check_call_names` 会把未定义的裸名当拼错的
 * 函数拒掉——那条规则是对的，不能为了测试放宽。真实部署里这些名字由扩展在装载期
 * 声明；测试用 layout 自己的 `rwir` 声明语法走同一条路径（write_rwir_decl），
 * 不手工拼 kvspace 字节。
 *
 * `println` 在这里只是**声明**，让 layout 放行；真正执行时它不在 runtimec 的
 * myrwircaps 里，编码后会成为 FOREIGN——这正是 runner 要产出 blocked 的地方。
 * 替身放在执行循环外面（裁决 §5），不进 vmkw_builtin_registry。
 */
static bool declare_external_capabilities(runner_environment_t *environment) {
    /* 新参数文法：`name: T` 已废，声明也是 `*T $name`。
     * 变参标记 `...` 跟在**类型**后（`*[]char/utf32 $s...`），落签名行锚点 `[0,0]`
     * 的 dynamic 字节；这样 `println(1, "x")` 的多实参才过得了 layout 的类型检查。 */
    static const char declarations[] =
        "rwir println(*[]char/utf32 $s...)\n"
        "rwir print(*[]char/utf32 $s...)\n";
    char path[CASE_PATH_CAPACITY];
    snprintf(path, sizeof(path), "%s/capabilities.kv", environment->directory);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    const size_t size = strlen(declarations);
    const bool written = fwrite(declarations, 1U, size, file) == size;
    fclose(file);
    if (!written) {
        return false;
    }
    char entry[256];
    char error_text[512];
    memset(entry, 0, sizeof(entry));
    memset(error_text, 0, sizeof(error_text));
    const int result = environment->layout_file(
        path, environment->data_source, entry, sizeof(entry), error_text,
        sizeof(error_text));
    (void)remove(path);
    if (result != 0) {
        fprintf(stderr, "SKIP: cannot declare external capabilities: %s\n",
                error_text);
        return false;
    }
    return true;
}


/* 从 RWIR 源槽取算子名：`/lib/<fn>/[pc,0]` 的 body 是 5 字节 counts + 文本。
 *
 * 为什么不用 operation_identifier 反查：FOREIGN（0xFFFE）本来就不在能力表里，
 * 表里没有它就等于没有名字。源槽里才是 layout 实际写下的那个名字，也正是
 * blocked 报告要给人看的东西。读不到就写 "?"，不编。
 */
static void operation_name_at(runner_environment_t *environment,
                              const runtimec_snapshot_t *snapshot,
                              const char *function_path, uint32_t pc,
                              char *out, size_t out_capacity) {
    snprintf(out, out_capacity, "?");
    /* 截断是**错误**，不静默截掉：半个路径会让后续「key 不存在」指向错位置。
     * 用 snprintf 的返回值判，它返回「本该写入的长度」。 */
    char key[512];
    const int key_length = snprintf(key, sizeof(key), "%s/[%u,0]",
                                    function_path, pc);
    if (key_length < 0 || (size_t)key_length >= sizeof(key)) {
        fprintf(stderr,
                "FAIL: operation key truncated (%d bytes needed, %zu available): "
                "%s/[%u,0]\n",
                key_length, sizeof(key), function_path, pc);
        return;
    }
    /* 读回的是**完整 wire**：16 字节 head + body。body 前 5 字节是 counts，
     * 之后才是算子名文本。 */
    uint8_t storage[512];
    runtimec_bytes_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    runtimec_error_t error;
    memset(&error, 0, sizeof(error));
    if (runtimec_kvspace_access_read(environment->kvspace_access, snapshot,
                                   runtimec_key_view_from_c_string(key),
                                   &buffer, &error) != RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    /* 长度常量只有一处定义：`kvspace/xvalue_head.h` 的 KVSPACE_XVALUE_HEAD_LEN。
     * 自己再写一份就是第二处真相。 */
    const size_t head = KVSPACE_XVALUE_HEAD_LEN;
    if (buffer.size <= head + 5U) {
        return;
    }
    const uint8_t *text = buffer.data + head + 5U;
    const size_t text_size = buffer.size - head - 5U;
    size_t usable = text_size;
    for (size_t index = 0U; index < text_size; ++index) {
        if (text[index] == '\0') {
            usable = index;
            break;
        }
    }
    if (usable == 0U || usable >= out_capacity) {
        return;
    }
    memcpy(out, text, usable);
    out[usable] = '\0';
}

/* 生命周期收尾与其它组件一致：返回状态而不是 void。清理要继续走完，错误先记下。 */
/* 进程级收尾：只拆 dlopen 的 layout 库。kvspace 是链接期依赖，无需拆。
 * store/session 归 `case_store_close` 管——两者生命周期不同（库整批共用、
 * store 每条用例一换），混在一起写就会漏拆。 */
static runtimec_status_t environment_close(runner_environment_t *environment) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    if (environment->layout_library != NULL) {
        dlclose(environment->layout_library);
    }
    return status;
}

/* ── 用例发现 ─────────────────────────────────────────────────────── */

typedef struct case_list {
    char paths[256][CASE_PATH_CAPACITY];
    size_t count;
} case_list_t;

static bool ends_with_kv(const char *name) {
    const size_t length = strlen(name);
    return length > 3U && strcmp(name + length - 3U, ".kv") == 0;
}

static void collect_cases(const char *root, const char *relative,
                          case_list_t *list) {
    char directory[CASE_PATH_CAPACITY];
    if (relative[0] == '\0') {
        snprintf(directory, sizeof(directory), "%s", root);
    } else {
        snprintf(directory, sizeof(directory), "%s/%s", root, relative);
    }
    DIR *handle = opendir(directory);
    if (handle == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        /* `known-limitations/` 是**停靠区**，不是语料：里面的用例阻塞于已知的
         * 能力缺口（如 head.ptr 恒 0 导致指针参数不可编码），跑起来只会贡献
         * 「一条都不验」的噪声，且会把限制误读成回归。
         *
         * 跳过是**显式列举**目录名，不是按路径猜——名单只有一行，加新停靠区
         * 要改这里，看得见。 */
        if (strcmp(entry->d_name, "known-limitations") == 0) {
            continue;
        }
        char next[CASE_PATH_CAPACITY];
        if (relative[0] == '\0') {
            snprintf(next, sizeof(next), "%s", entry->d_name);
        } else {
            snprintf(next, sizeof(next), "%s/%s", relative, entry->d_name);
        }
        char full[CASE_PATH_CAPACITY * 2U];
        snprintf(full, sizeof(full), "%s/%s", root, next);
        struct stat info;
        if (stat(full, &info) != 0) {
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            collect_cases(root, next, list);
        } else if (ends_with_kv(entry->d_name) && list->count < 256U) {
            snprintf(list->paths[list->count], CASE_PATH_CAPACITY, "%s", next);
            list->count += 1U;
        }
    }
    closedir(handle);
}

/* ── 期望输出解析（与 tutorial/test.py 同约定） ────────────────────── */

typedef struct expectation {
    char lines[16][256];
    size_t count;
    /* `// 期望错误: <子串>`；空串表示「没有这一行」，即正向用例。 */
    char expected_error[256];
} expectation_t;

static void parse_expectations(const char *path, expectation_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    char line[512];
    bool in_block = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *newline = strchr(line, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        /* `// 期望错误: xxx`。先于「期望输出」判定，但两者互不干扰：一个文件要么
         * 是正向（只写期望输出）、要么是负向（只写期望错误），同时写属作者错误，
         * 由 main 里的互斥校验拦下，不在这里替它选一个。 */
        if (strstr(line, "期望错误") != NULL) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *text = colon + 1;
                while (*text == ' ') {
                    text += 1;
                }
                snprintf(out->expected_error, sizeof(out->expected_error), "%s",
                         text);
                continue;
            }
        }
        if (strstr(line, "期望输出") != NULL || strstr(line, "Expected output") != NULL) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *text = colon + 1;
                while (*text == ' ') {
                    text += 1;
                }
                if (*text != '\0' && out->count < 16U) {
                    snprintf(out->lines[out->count], 256, "%s", text);
                    out->count += 1U;
                    in_block = false;
                    continue;
                }
                in_block = true;
                continue;
            }
        }
        if (in_block) {
            if (strncmp(line, "//   ", 5) == 0 || strncmp(line, "// \t", 4) == 0) {
                const char *text = line + (strncmp(line, "//   ", 5) == 0 ? 5 : 4);
                if (*text != '\0' && out->count < 16U) {
                    snprintf(out->lines[out->count], 256, "%s", text);
                    out->count += 1U;
                }
            } else if (strncmp(line, "//", 2) != 0) {
                in_block = false;
            }
        }
    }
    fclose(file);
}

/* 取**第一条 error 行**，没有 error 行再退回第一非空行；写进 output。
 *
 * 为什么不是「第一行」：layout 的 stderr 里 `info:` 行常排在 `error:` 之前
 * （`rwfunc outside lib block — registering under /lib/<name>` 就是典型）。
 * 贴那条 info 行等于什么都没说——它既不是失败原因，也不含任何诊断内容。
 * 报红必须贴的是**真正把 layout 拦下来的那一行**。 */
static void first_error_line(const char *text, char *output, size_t capacity) {
    static const char empty[] = "(空)";

    if (text == NULL || text[0] == '\0') {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    /* 「第一条非空行」：没有 error 行时用它。这个名字直接说它是什么——
     * 禁词表里 `fallback` 被禁，是因为它把「退而求其次」藏进一个词里，
     * 而这里要表达的恰恰是「第二选择是哪个」。 */
    const char *first_nonempty = NULL;
    const char *cursor = text;
    while (*cursor != '\0') {
        const size_t length = strcspn(cursor, "\r\n");
        if (length > 0U) {
            if (strncmp(cursor, "error", 5U) == 0) {
                const size_t copy = length < capacity - 1U ? length : capacity - 1U;
                memcpy(output, cursor, copy);
                output[copy] = '\0';
                return;
            }
            if (first_nonempty == NULL) {
                first_nonempty = cursor;
            }
        }
        cursor += length;
        if (*cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
    }
    if (first_nonempty == NULL) {
        snprintf(output, capacity, "%s", empty);
        return;
    }
    const size_t length = strcspn(first_nonempty, "\r\n");
    const size_t copy = length < capacity - 1U ? length : capacity - 1U;
    memcpy(output, first_nonempty, copy);
    output[copy] = '\0';
}

#define FILENO_STDOUT 1
#define FILENO_STDERR 2

/* 把 stream（stdout/stderr）重定向进临时文件，失败返回 false。
 *
 * 两个用途，同一手法：
 *  · stderr —— layout 的 C ABI 只回一条**汇总串**，真正的诊断走 stderr。
 *    负向用例要匹配的是后者；只比对 ABI 返回串的话，任何语法错误都长得一样，
 *    「预期错误」就成了「报错就行」，那是恒真判据。
 *  · stdout —— 用例的 println 输出写进程 stdout，比对必须逐条抓；
 *    不抓就是所有用例共用一条流，前一条的输出会串到下一条。
 *
 * 都在同一进程内，dup 一份即可，不用 fork。 */
static bool redirect_stream_to(int stream_fd, char *path, size_t capacity,
                               int *saved_fd) {
    snprintf(path, capacity, "/tmp/kv-runner-XXXXXX");
    const int fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }
    fflush(stream_fd == FILENO_STDERR ? stderr : stdout);
    *saved_fd = dup(stream_fd);
    if (*saved_fd < 0) {
        close(fd);
        remove(path);
        return false;
    }
    if (dup2(fd, stream_fd) < 0) {
        close(fd);
        close(*saved_fd);
        remove(path);
        return false;
    }
    close(fd);
    return true;
}

/* 恢复 stream，把捕获到的内容读进 output（截断到容量）。 */
static void restore_stream(int stream_fd, int saved_fd, const char *path,
                           char *output, size_t capacity) {
    fflush(stdout);
    fflush(stderr);
    dup2(saved_fd, stream_fd);
    close(saved_fd);
    output[0] = '\0';
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        size_t read_size = fread(output, 1U, capacity - 1U, file);
        output[read_size] = '\0';
        fclose(file);
    }
    remove(path);
}

/* ── 输出比对：逐行完全相同 ────────────────────────────────────────── */

typedef enum compare_result {
    COMPARE_PASS = 0,
    /* 跑完了，某一行内容不符 —— 期望错或实现错，要人判。 */
    COMPARE_EXPECTATION_WRONG = 1,
    /* 实际行数多于期望，或期望为空 —— 用例本身没写全。 */
    COMPARE_INCOMPLETE = 2,
    /* **观测为空**：期望有内容，实际一条都没输出。
     *
     * 必须与 `COMPARE_EXPECTATION_WRONG` 分开——两者的修复方向**相反**：
     *   观测为空   → 查链路（算子没执行到 / 输出没接上 / 被 FOREIGN 挡住）
     *   内容不符   → 查实现（算错了）或查期望（写错了）
     * 混在一起会把「println 执行不到」读成「println 算错了」，去改实现，白费。
     *
     * 判据：期望非空而实际为零行。它不是「输出不符」，是「没观测到」。 */
    COMPARE_UNOBSERVED = 3
} compare_result_t;

/* 按行切分，剥掉行尾空白。**行尾空白是唯一的归一化**：编辑器与不同 printf
 * 实现会在行尾留空格，那不是语义差异。除此之外不宽容——不去空行、不排序、
 * 不去重、不折叠内部空白。
 *
 * 与 `tutorial/test.py` 的 `_normalize_output_lines` 同一规则（那边已有反向控制
 * 5 条）：多打印不管、顺序错不管、期望 `1` 还能匹到 `12` 的比对不算比对。 */
static size_t split_lines(const char *text, char (*lines)[512],
                          size_t capacity) {
    size_t count = 0U;
    const char *cursor = text;
    while (*cursor != '\0' && count < capacity) {
        size_t length = strcspn(cursor, "\r\n");
        if (length >= sizeof(lines[0])) {
            length = sizeof(lines[0]) - 1U;
        }
        memcpy(lines[count], cursor, length);
        lines[count][length] = '\0';
        /* 剥行尾空白（空格与制表符）。 */
        while (length > 0U &&
               (lines[count][length - 1U] == ' ' ||
                lines[count][length - 1U] == '\t')) {
            lines[count][--length] = '\0';
        }
        count += 1U;
        cursor += strcspn(cursor, "\r\n");
        if (*cursor == '\r') {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
    }
    return count;
}

static compare_result_t compare_output(const expectation_t *expectation,
                                       const char *actual,
                                       const char *case_path) {
    char actual_lines[64][512];
    const size_t actual_count = split_lines(actual, actual_lines, 64U);
    /* **观测为空**要先判，不能并进「行数不等」。
     *
     * 「期望 2 行、实际 0 行」与「期望 2 行、实际 1 行」是两件事：
     * 前者链路没通（算子没执行到），后者执行了但少了一次输出。
     * 修复方向相反——合并会把查链路的人指去改实现。 */
    if (actual_count == 0U && expectation->count != 0U) {
        fprintf(stderr,
                "FAIL %s: 未观测到任何输出（期望 %zu 行）——"
                "查链路，不是查实现\n",
                case_path, expectation->count);
        return COMPARE_UNOBSERVED;
    }
    if (actual_count != expectation->count) {
        /* 行数不等分两种，但要先看是哪边多。
         * 实际比期望多 → 可能是实现多打印（实现错）；也可能是用例只写了部分行
         * （用例不完整）。两者判据不同，报的时候必须把两侧都给出来。 */
        fprintf(stderr,
                "FAIL %s: 行数不等（实际 %zu / 期望 %zu）\n",
                case_path, actual_count, expectation->count);
        for (size_t i = 0U; i < actual_count && i < 4U; ++i) {
            fprintf(stderr, "  实际[%zu]: %s\n", i, actual_lines[i]);
        }
        for (size_t i = 0U; i < expectation->count && i < 4U; ++i) {
            fprintf(stderr, "  期望[%zu]: %s\n", i, expectation->lines[i]);
        }
        return actual_count > expectation->count ? COMPARE_INCOMPLETE
                                                 : COMPARE_EXPECTATION_WRONG;
    }
    for (size_t i = 0U; i < expectation->count; ++i) {
        if (strcmp(expectation->lines[i], actual_lines[i]) != 0) {
            fprintf(stderr, "FAIL %s: 第 %zu 行不同\n  期望: %s\n  实际: %s\n",
                    case_path, i + 1U, expectation->lines[i], actual_lines[i]);
            return COMPARE_EXPECTATION_WRONG;
        }
    }
    return COMPARE_PASS;
}

/* ── 主流程（骨架：尚未接执行） ───────────────────────────────────── */

int main(int argument_count, char **arguments) {
    if (argument_count < 4) {
        fprintf(stderr,
                "usage: kv_runner <corpus-dir> <layout-lib> <kvspace-lib>\n");
        return 2;
    }
    const char *corpus = arguments[1];

    runner_environment_t environment;
    if (!environment_open(&environment, arguments[2], arguments[3])) {
        return SKIP_RETURN_CODE;
    }

    /* 外部能力声明**不在这里**做：它 layout 进 /lib/println，而那需要一套 store。
     * store 已改成按用例建，所以声明也搬进主循环（每套 store 各做一次）。
     * 放在这里的话它会对着一个还没建的 store 跑，直接 SKIP。 */

    case_list_t cases;
    memset(&cases, 0, sizeof(cases));
    collect_cases(corpus, "", &cases);
    if (cases.count == 0U) {
        fprintf(stderr, "no .kv cases under %s\n", corpus);
        if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "warning: environment close reported an error\n");
        }
        return 1;
    }

    runner_counts_t counts;
    memset(&counts, 0, sizeof(counts));
    size_t missing_expectation = 0U;
    size_t layout_ok = 0U;
    for (size_t index = 0U; index < cases.count; ++index) {
        char full[CASE_PATH_CAPACITY * 2U];
        snprintf(full, sizeof(full), "%s/%s", corpus, cases.paths[index]);
        expectation_t expectation;
        parse_expectations(full, &expectation);
        const bool expects_layout_failure = expectation.expected_error[0] != '\0';
        /* ── 每条用例一套 store：上一条的 /lib 不许留到下一条 ──
         *
         * 不隔离的症状是「单独跑通过、整批跑全红」，而且只在某些组合下复发。
         * 这里在**任何 continue 之前**就建好、并用 goto 统一收尾，
         * 否则中途 continue 会漏拆，/tmp 攒垃圾且下一条拿到脏 store。 */
        if (!case_store_open(&environment, (unsigned)index)) {
            fprintf(stderr, "FAIL %s: 无法建立 per-case store\n",
                    cases.paths[index]);
            counts.failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        /* 外部能力声明必须**每套 store 各做一次**：它 layout 进 /lib/println，
         * 换 store 就没了。少做一次的表现是「layout 说未定义函数 println」。 */
        if (!declare_external_capabilities(&environment)) {
            fprintf(stderr, "FAIL %s: 无法声明外部能力\n", cases.paths[index]);
            counts.failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        if (expects_layout_failure && expectation.count != 0U) {
            /* 两类期望互斥。同时写说明作者没决定这条用例测什么；替它选一边
             * 会让另一边永远不被检查。 */
            fprintf(stderr,
                    "FAIL %s: 同时写了 `期望输出:` 与 `期望错误:` —— 一条用例只能是"
                    "正向或负向\n",
                    cases.paths[index]);
            counts.failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        if (!expects_layout_failure && expectation.count == 0U) {
            /* 没有期望的用例会永远 pass —— 那是恒真判据。这里直接算失败。 */
            fprintf(stderr, "FAIL %s: 缺 `// 期望输出:` 头\n", cases.paths[index]);
            missing_expectation += 1U;
            counts.failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        /* layout 到本实例的 store。失败是 fail（跑不起来），不是 blocked。 */
        char entry[256];
        char error_text[512];
        memset(entry, 0, sizeof(entry));
        memset(error_text, 0, sizeof(error_text));
        /* 负向用例要匹配 stderr 里的具体诊断，正向用例不该把 layout 的诊断丢掉：
         * 两种情况都把 stderr 抓下来，正向失败时把它一起打出来。 */
        char captured[4096];
        char capture_path[256];
        capture_path[0] = '\0';
        captured[0] = '\0';
        int saved_stderr = -1;
        const bool capturing =
            redirect_stream_to(FILENO_STDERR, capture_path,
                               sizeof(capture_path), &saved_stderr);
        const int layout_result = environment.layout_file(
            full, environment.data_source, entry, sizeof(entry), error_text,
            sizeof(error_text));
        if (capturing) {
            restore_stream(FILENO_STDERR, saved_stderr, capture_path, captured,
                           sizeof(captured));
        }
        if (expects_layout_failure) {
            /* 负向用例：只看「拒绝」与「拒绝得对」。
             * 空子串在上面的解析里就被当成「没有这一行」——但作者可能写了
             * `// 期望错误:` 后面什么都没有，那时 expected_error 也是空串，
             * 于是变成正向用例、接着因缺期望输出而 FAIL。这条路径是对的：
             * 空子串不能算「报什么错都行」。 */
            if (layout_result == 0) {
                fprintf(stderr, "FAIL %s: 预期 layout 失败，但它通过了\n",
                        cases.paths[index]);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* 子串在**两条**来源里找：ABI 汇总串与捕获到的 stderr。
             * 汇总串只有一类（`parse: error-level diagnostics …`），具体诊断
             * （`rwfunc parameter must be a single type`）只在 stderr 里。 */
            if (strstr(error_text, expectation.expected_error) == NULL &&
                strstr(captured, expectation.expected_error) == NULL) {
                char stderr_line[512];
                first_error_line(captured, stderr_line, sizeof(stderr_line));
                fprintf(stderr,
                        "FAIL %s: layout 失败了，但错误不含 %s\n  实际 stderr 首行：%s\n"
                        "  实际 ABI 串：%s\n",
                        cases.paths[index], expectation.expected_error,
                        stderr_line,
                        error_text[0] != '\0' ? error_text : "(空)");
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            counts.passed += 1U;
            counts.expected_failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        if (layout_result != 0) {
            /* 正向用例失败：摘要 + stderr 首行。只给汇总串等于没说，
             * 同一条红在语法层和类型层的原因完全不同。 */
            char stderr_line[512];
            first_error_line(captured, stderr_line, sizeof(stderr_line));
            fprintf(stderr, "FAIL %s: layout failed: %s\n  实际 stderr 首行：%s\n",
                    cases.paths[index], error_text, stderr_line);
            counts.failed += 1U;
            (void)case_store_close(&environment);
            continue;
        }
        layout_ok += 1U;
        /* 编码 + FOREIGN 扫描：blocked 的判据就在这里。
         * 不执行也能得到 blocked —— 只要编码出来的指令里有 FOREIGN，
         * 就说明 runtimec 没有该能力，且能报出 pc 与算子名。 */
        {
            runtimec_session_runtime_components_t components;
            if (runtimec_session_get_runtime_components(
                    environment.session, &components) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: cannot get components\n",
                        cases.paths[index]);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* 每条用例的入口恒为顶层 `rwfunc test()`（与 tutorial 同约定）。
             * `entry` 是 layout 返回的**初始化入口**（可能是 `init`），不是它。 */
            char function_path[CASE_PATH_CAPACITY];
            snprintf(function_path, sizeof(function_path), "/lib/test");
            vmkv_encode_request_t encode_request;
            runtimec_error_t encode_error;
            memset(&encode_request, 0, sizeof(encode_request));
            memset(&encode_error, 0, sizeof(encode_error));
            encode_request.kvspace_access = environment.kvspace_access;
            encode_request.function_path =
                runtimec_string_view_from_c_string(function_path);
            encode_request.expected_snapshot.contract_version =
                runtimec_contract_version();
            /* revision 必须**从 host 真取**：它由 store 实例与 generation 决定，
             * 自己编一个值会被编码器判为「function source revision differs」
             * ——那是真校验，不该绕。 */
            runtimec_source_revision_reset(
                &encode_request.expected_snapshot.source_revision);
            encode_request.expected_snapshot.function_key =
                runtimec_key_view_from_c_string(function_path);
            /* 快照里的位置文本叫 `source_position_text`（编译缓存键的源码位置），
             * 与执行位置 `runtimec_program_counter_t` 不是同一个概念。 */
            encode_request.expected_snapshot.source_position_text =
                runtimec_string_view_from_c_string("[1,0]");
            if (runtimec_kvspace_access_source_revision(
                    environment.kvspace_access,
                    encode_request.expected_snapshot.function_key,
                    &encode_request.expected_snapshot.source_revision,
                    &encode_error) != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: source revision unavailable\n",
                        cases.paths[index]);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            binrwir_program_t *program = NULL;
            const runtimec_status_t encoded = vmkv_encode(
                components.vmkv_encoder, &encode_request, &program,
                &encode_error);
            if (encoded != RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: encode failed: %s\n",
                        cases.paths[index],
                        encode_error.message.data ? encode_error.message.data
                                                  : "");
                counts.failed += 1U;
                counts.encode_failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* Program 是不透明类型：条数经 get_view，逐条经 get_instruction。
             * 算子名暂缺——operation_identifier 只能回到能力表，而 FOREIGN
             * （0xFFFE）本来就不在表里。下一步从 RWIR 源槽取名字。 */
            binrwir_program_view_t program_view;
            /* CALL 出口的 target_identifier 是**这份** Program 符号池的下标，
             * 解析被调函数名要用同一份。 */
            unsigned foreign = 0U;
            if (binrwir_program_get_view(program, &program_view) !=
                RUNTIMEC_STATUS_SUCCESS) {
                fprintf(stderr, "FAIL %s: get_view failed\n",
                        cases.paths[index]);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* 指令标识符是稠密序号、从 0 起（Program 私有 framing 不暴露起始值，
             * 但标识符区间由 get_view 的 instruction_count 界定）。 */
            for (size_t ordinal = 0U; ordinal < program_view.instruction_count;
                 ++ordinal) {
                binrwir_instruction_view_t view;
                if (binrwir_program_get_instruction(program, (uint32_t)ordinal,
                                                    &view) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                    /* 跳过**一条指令**，不关 store —— 这里是内层循环，
                     * store 还要给后面的指令和本轮执行用。 */
                    continue;
                }
                if (view.instruction->operation_identifier !=
                    BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
                    /* 同上：非 FOREIGN 的指令直接跳过，不关 store。 */
                    continue;
                }
                foreign += 1U;
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = view.source_logical_program_counter;
                    operation_name_at(&environment,
                                      &encode_request.expected_snapshot,
                                      function_path, slot->pc, slot->operation,
                                      sizeof(slot->operation));
                    g_blocked_count += 1U;
                }
            }
            if (foreign > 0U) {
                counts.blocked += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* 无 FOREIGN → 可以执行。
             *
             * 执行段交给 `execute_frame_until_done`：它处理 CALL 的建子帧与返回，
             * `execution_window_execute` 本身不负责推进（它把 CALL 写进 `‥status`
             * 就返回）。 */
            char vthread_identifier[64];
            const int vthread_length = snprintf(
                vthread_identifier, sizeof(vthread_identifier), "%u",
                ++environment.sequence);
            if (vthread_length < 0 ||
                (size_t)vthread_length >= sizeof(vthread_identifier)) {
                fprintf(stderr, "FAIL %s: vthread identifier truncated\n",
                        cases.paths[index]);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            binrwir_execution_exit_t frame_exit;
            memset(&frame_exit, 0, sizeof(frame_exit));
            /* 抓 stdout：println 写进程 stdout，逐条用例必须各自捕获，否则
             * 前一条的输出会串到下一条的比对里。与抓 stderr 同一手法。 */
            char output_path[256];
            output_path[0] = '\0';
            char captured_output[8192];
            captured_output[0] = '\0';
            int saved_stdout = -1;
            const bool capturing_stdout =
                redirect_stream_to(FILENO_STDOUT, output_path,
                                   sizeof(output_path), &saved_stdout);
            const frame_outcome_t frame_outcome = execute_frame_until_done(
                &environment, function_path, 1U, 1, "1",
                vthread_identifier, NULL, 0U, NULL, &frame_exit);
            if (capturing_stdout) {
                restore_stream(FILENO_STDOUT, saved_stdout, output_path,
                               captured_output, sizeof(captured_output));
            }
            if (frame_outcome == FRAME_OUTCOME_EXECUTE_FAILED) {
                /* 只打 `message` 是不够的：它经常是空串（错误由 exit 携带而不是
                 * error 结构），那就成了「execute failed: 」——等于没报。
                 * status 与 exit 的 pc/detail 才是能定位的部分。 */
                fprintf(stderr,
                        "FAIL %s: execute failed status=%d"
                        "（exit.kind=%u status=%d detail=%u pc=%u）\n",
                        cases.paths[index], (int)frame_exit.status,
                        frame_exit.kind, (int)frame_exit.status,
                        frame_exit.detail_code,
                        frame_exit.logical_program_counter);
                counts.failed += 1U;
                counts.execution_failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            if (frame_outcome == FRAME_OUTCOME_HANDOFF) {
                /* 执行到 handoff = 撞上了本 runtime 没有的能力。
                 * **必须带原因串**：不带的话「blocked 21」与「blocked 3」
                 * 看不出区别，blocked 就退化成「这条没过」。 */
                if (g_blocked_count < MAX_BLOCKED) {
                    blocked_entry_t *slot = &g_blocked[g_blocked_count];
                    snprintf(slot->file, sizeof(slot->file), "%s",
                             cases.paths[index]);
                    slot->pc = frame_exit.logical_program_counter;
                    snprintf(slot->operation, sizeof(slot->operation), "%s",
                             "handoff");
                    g_blocked_count += 1U;
                }
                counts.blocked += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            if (frame_outcome == FRAME_OUTCOME_ERROR) {
                /* 跑起来了但运行时错：是 fail，不是 blocked。
                 * 两者分开是因为处置不同——blocked 等能力补齐，fail 是缺陷。 */
                char stderr_line[512];
                first_error_line(captured, stderr_line, sizeof(stderr_line));
                fprintf(stderr,
                        "FAIL %s: 执行报错 status=%d detail=%u pc=%u\n"
                        "  stderr：%s\n",
                        cases.paths[index],
                        (int)frame_exit.status,
                        frame_exit.detail_code,
                        frame_exit.logical_program_counter,
                        stderr_line);
                counts.failed += 1U;
                (void)case_store_close(&environment);
                continue;
            }
            /* 真跑完了：逐行比对。 */
            switch (compare_output(&expectation, captured_output, cases.paths[index])) {
                case COMPARE_PASS:
                    counts.passed += 1U;
                    counts.executed += 1U;
                    break;
                case COMPARE_EXPECTATION_WRONG:
                    counts.failed += 1U;
                    counts.expectation_wrong += 1U;
                    break;
                case COMPARE_INCOMPLETE:
                    counts.failed += 1U;
                    counts.incomplete += 1U;
                    break;
                case COMPARE_UNOBSERVED:
                    /* 与「输出不符」分开计。混在一起会让「链路没通」看起来像
                     * 「实现算错」，把人指向相反的修复方向。
                     *
                     * 并且**打出停止位置**：「未观测到」只说明期望有内容、实际零行，
                     * 它不说明停在哪。停在 call 那条（跨 Program 调用没接上）与停在
                     * 最后一条之后（输出没捕获）处置完全不同。 */
                    counts.failed += 1U;
                    counts.unobserved += 1U;
                    {
                        const binrwir_execution_exit_t *exit_information =
                            &frame_exit;
                        unsigned instruction_count = 0U;
                        if (binrwir_program_get_view(
                                program, &program_view) ==
                            RUNTIMEC_STATUS_SUCCESS) {
                            instruction_count =
                                (unsigned)program_view.instruction_count;
                        }
                        fprintf(stderr,
                                "  停止位置 %s：exit.kind=%u status=%d "
                                "detail=%u pc=%d 指令总数=%u\n",
                                cases.paths[index], exit_information->kind,
                                (int)exit_information->status,
                                exit_information->detail_code,
                                (int)exit_information->logical_program_counter,
                                instruction_count);
                    }
                    break;
                default:
                    counts.failed += 1U;
                    break;
            }
        }
        /* 正常路径也要拆：这是唯一不经过 continue 的出口。
         * 漏了它会让最后一条用例的 store 留到进程结束（/tmp 攒垃圾），
         * 而中间那些因为都走 continue 反倒拆得干净——那种「前面干净、最后一条脏」
         * 最难发现。 */
        (void)case_store_close(&environment);
    }

    /*
     * TODO(骨架)：layout → 编码 → 执行 → 抓输出。
     * 现在只做「发现用例 + 校验期望格式」，**不产出任何 pass 数**——
     * 一个不执行的 runner 若报 PASS，就是又一个恒真判据。
     */
    /* 汇总首行：`executed` 与 `blocked` **必须同时出现**。
     *
     * 只报 executed 的话「5/25」看起来像全过；只报 blocked 的话「20」看不出
     * 底下有 5 条真跑过。两个数挨着摆，第一眼就知道「大部分没验」。
     *
     * 顺序按「可信度」排：executed（跑对）→ blocked（没验）→ fail（红了）→
     * 仅编码（只编过）。抄数字的人从左往右读，先读到的是可信的。 */
    printf("用例 %zu：executed %zu / blocked %zu / fail %zu / pass(仅编码) %zu\n",
           cases.count, counts.executed, counts.blocked, counts.failed,
           counts.passed - counts.executed);
    /* fail 的细分按**修复方向**排，不按发生顺序：
     *   未观测到    → 查链路（输出没接上）
     *   执行期失败  → 查链路（带错误码，看 status/detail）
     *   编码期失败  → 查编码器
     *   期望不符    → 查实现或期望
     *   用例不完整  → 补用例
     *   其中预期失败 → 负向用例正常工作，不是失败 */
    printf("  细分：未观测到 %zu、执行期失败 %zu、编码期失败 %zu、"
           "期望不符 %zu、用例不完整 %zu、其中预期失败 %zu\n",
           counts.unobserved, counts.execution_failed, counts.encode_failed,
           counts.expectation_wrong, counts.incomplete, counts.expected_failed);
    /* 「未观测到」必须单列并带解释：它与「期望不符」的修复方向相反。
     * 不解释的话，看到 21 条 fail 的人会去查实现，而实际该查链路。 */
    if (counts.unobserved != 0U) {
        printf("  注：`未观测到` = 期望有内容但实际零行输出 —— 查**链路**"
               "（算子没执行到/输出没接上），不是查实现。\n");
    }
    /* 覆盖率**单独一行**，给人看、不给退出码看。
     *
     * **分母恒为全部用例数，不许由结果筛**。
     *
     * 早先这里是 `executed / (executed + blocked)`——分子分母由同一段代码挑，
     * 分母自然只含「能跑的那部分」，于是永远 100%。那与「`pass(仅编码)` 不算
     * 验证」是同一个错觉的两种写法：换一行、换一种口径，错觉换个地方发生。
     *
     * 判据：**任何比值的分母若由计算分子的同一段代码挑选，这个比值就没有信息量。**
     * 现在写成 `executed X / 总数 N`，读的人自己能看到「那 N-X 条不在分子里」。 */
    printf("通过 executed %zu / 总数 %zu 条用例\n", counts.executed, cases.count);
    /* `仅编码通过` = pass 里既不是 executed、也不是预期失败的那部分。
     *
     * `counts.passed` 有两处来源：真跑比对通过（同时加 executed）与负向用例被正确
     * 拒绝（同时加 expected_failed）。所以「仅编码通过」必须把**两者都减掉**——
     * 只减 expected_failed 会让它等于 executed，那是又一次「算式看着对、口径错了」。
     * 这个数当前恒为 0，因为 blocked 一出现就不进 pass；留着它是为了将来有
     * 「跑起来了但无输出、也不算失败」的形态时不用改汇总。 */
    printf("  按阶段：编码期失败 %zu、执行期失败 %zu、未观测到 %zu、"
           "期望不符 %zu、用例不完整 %zu、仅编码通过 %zu、预期失败 %zu\n",
           counts.encode_failed, counts.execution_failed, counts.unobserved,
           counts.expectation_wrong, counts.incomplete,
           counts.passed - counts.expected_failed - counts.executed,
           counts.expected_failed);
    if (g_blocked_count > 0U) {
        /* 每一条 blocked 都带原因串。不带的「blocked 20」与「blocked 3」
         * 看不出区别，blocked 就退化成「这条没过」。 */
        printf("blocked 明细（文件 + source pc + 算子名）：\n");
        for (size_t index = 0U; index < g_blocked_count; ++index) {
            printf("  blocked  %s  pc=%u  op=%s\n", g_blocked[index].file,
                   g_blocked[index].pc, g_blocked[index].operation);
        }
    }
    printf("注：`executed` = 真跑且输出逐行相同；`pass(仅编码)` = 只证明编得过，"
           "**不表示输出对**。\n");
    printf("注：`blocked` 是**有名字的能力缺口**，不是失败、也不是跳过——"
           "明细见上，退出码不受它影响。\n");
    printf("注：`其中预期失败` = layout 按文件头 `// 期望错误:` 被正确拒绝的条数，"
           "与「编得过」不是同一类。\n");
    (void)layout_ok;

    if (environment_close(&environment) != RUNTIMEC_STATUS_SUCCESS) {
        fprintf(stderr, "warning: environment close reported an error\n");
    }
    /* 退出码三态（架构师裁决）：
     *
     *   有失败            → 1
     *   executed > 0 且全相符 → 0
     *   executed == 0     → 77（CTest 的 SKIP）
     *
     * 为什么 `executed == 0` 不是 0：那时一条行为都没验（全是 blocked 或只编过），
     * 报 0 就是「永远绿的 runner」——这个文件第 226 行警告过的那个东西。
     * 为什么也不是 1：它没坏，只是今天没验到，制造长期红是另一种噪声。
     * 77 是 CTest 的跳过约定（`runtimec.vmkv.end_to_end` 缺库时也返 77），
     * 它诚实表达「这条用例今天什么都没验」，且**会在 println 接上后自动转成 0/1**——
     * 转换那一刻就是行为层真正开始工作的时刻，谁都看得见。
     *
     * `missing_expectation`（缺 `// 期望输出:` 的用例）算失败：没有期望的用例会永远 pass，
     * 那是恒真判据。 */
    if (missing_expectation != 0U || counts.failed != 0U) {
        return 1;
    }
    if (counts.executed == 0U) {
        fprintf(stderr,
                "SKIP: executed=0 —— 本次没有任何用例真跑到结束并比对输出"
                "（blocked %zu、仅编码 %zu）\n",
                counts.blocked, counts.passed - counts.expected_failed);
        return SKIP_RETURN_CODE;
    }
    return 0;
}
