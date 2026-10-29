#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "codeseg.hpp"
#include "status.hpp"

#include <kvspace/kvspace.h>

#include "value.hpp"

// 双栈映射：kvspace 明文栈（string key + head 携带的 xvalue）+ runtimec2 私有
// 二进制栈（vmbin）。本层把 xvaluebodyview 等优化技术统一组织：langtype 枚举化、
// 寄存器（FReg）/帧（FrameCtx）、key/xvalue 偏移量、PC 双 int32。
// kvspace 保持 string 形态（明文栈契约），vmbin 侧零 string 判断
// （编译期/init 预编码 + 槽直寻址）。
//
// 明文栈 ←→ 二进制栈 一一对应：
//   kindexpr 串      ↔  LangType 枚举
//   /vthread/<vid>/[<d>]/[<s0>,<axis>]  ↔  FrameId + SlotOff（2×int32）
//   head+body xvalue ↔  body 纯数据（FReg 值缓存，无 head）

namespace kv::rt::bin {

// langtype 枚举：与明文栈 kindexpr 一一对应（init 期映射表，热路径零 string）。
// 覆盖 scalar + 容器 + 扩展；kindexpr 串 → 枚举经映射表查一次（首次），此后常数。
enum class LangType : std::uint8_t {
    None = 0,
    // 标量
    Int8, Int16, Int32, Int64,
    Uint8, Uint16, Uint32, Uint64,
    Float32, Float64,
    Bool,
    CharUtf8, CharUtf32, CharAscii,
    // 容器/复合（dims 在 SlotDesc，kind 仅类别）
    Array, StringKeyMap, StructRef,
    // 扩展世界（@ 前缀，真实 xvalue 在扩展）
    Ext,
};

// PC：2×int32（frame_id + s0），不再字符串。
struct PcBin {
    std::uint32_t frame_id;
    std::uint32_t s0;
};

// 槽引用：帧内指令槽（frame_id 运行时定，slot_off 编译期常数）。
struct SlotOff {
    std::int32_t s0;   // 指令行
    std::int8_t axis;  // 0=opcode, -k 读, +k 写
};

// 帧基址（FrameKey）：/vthread/<vid>/[<depth>] 预拼接，帧入口一次。
struct FrameKey {
    char base[64];
    std::uint16_t base_len;
    std::uint32_t vid;
    std::uint32_t depth;
};

// kindexpr 串 → LangType 枚举（剥 dims 前缀；init/首次调用后热路径零 string）。
// None = 未知/复合（StringKeyMap/Ext 单独识别）。

// ── 二进制栈性能统计 ──
struct OpStat {
    unsigned long long n = 0, handler = 0, loop = 0;
};

struct Prof {
    unsigned long long handler = 0, loop = 0, steps = 0;
    unsigned long long pre = 0, loads = 0, frames = 0, ctrl = 0, fb = 0;
    unsigned long long f_slot = 0, f_raw = 0, w_fast = 0, w_fb = 0;
    unsigned long long rdtsc = 0;  // 单次 now() 的探针成本（校准值）
    std::vector<OpStat> ops;
};

// 二进制栈（VmBin）：收拢纯 bin 态（prof 统计）+ 方法（langtype 映射/探针/纯运算）。
// 明文逻辑（帧装载/回写）在 vmkv 方法，经 vmkv.bin.xxx() 调本层纯 bin 能力。
struct VmBin {
    // ── 数据（纯 bin 态）──
    Prof prof;
    bool trace_on = false;
    unsigned long long trace_n = 200;
    unsigned long long rdtsc_cost = 0;
#ifdef RC2_PROF
    static constexpr bool prof_on = true;
#else
    static constexpr bool prof_on = false;
#endif

    // ── 方法：langtype 映射 ──
    LangType langtype_of(const char* kindexpr, std::size_t len);
    std::uint8_t elem_width_of(LangType t);
    bool is_scalar(LangType t);

    // ── 方法：性能探针 ──
    unsigned long long now();
    void prof_tick(std::uint16_t id, std::uint64_t h, std::uint64_t l);
    void prof_init();
    void prof_report();
};

// 帧寄存器：kvspace 明文槽的**派生视图**（不是第二事实源）。
// - body/cap 指向 shm 槽（借用），fast 缓存纯 u64/布尔值；
// - 任何写（本 runtime 或外部）都先写回 kvspace 明文槽再更新 fast，
//   槽被删/重建/换 Ptr → 缓存失效重建（frame-entry 解析 + view 失效）。
struct FReg {
    std::string key;
    std::string desc;  // Ptr 描述符原样（惰性解析用；非 Ptr 为空）
    const RegDesc* desc_ref = nullptr;  // 段级描述符指针（普通变量惰性 key 用，零 string 拷贝）
    kvspaceRef_t ref{};
    bool resolved = false;
    kvspaceXvalueBodyView_t view{};
    bool view_ok = false;
    std::uint8_t* body = nullptr;  // view 的 body 指针（槽搬迁/写后失效）
    std::uint64_t cap = 0;
    bool sc_ok = false;  // 标量 kind/width 已缓存（写后失效）
    Scalar::K sck = Scalar::K::None;
    std::uint8_t scw = 0;
    // 热路径快缓存：已确认 Int/8 时直存 u64；已确认 Bool/1 时直存 b。
    bool fast_i64 = false;
    std::int64_t fv = 0;
    bool fast_bool = false;
    bool fb = false;
    // 跨帧内存别名（纯 runtime rwfunc 递归调用的参数边界，免 kvspace arg 槽）：
    // src_ptr：读参 → 调用方 reg（只读转发）；dst_ptr：写参 → 调用方 reg（写转发）。
    FReg* src_ptr = nullptr;
    FReg* dst_ptr = nullptr;
    // 成员 base 缓存：base 槽值是否为 Ptr（member_read/write 热路径——binary_trees/
    // hash_table 动态键 `L·*k` 每次免 kvspace read_wire_raw 检查）。写后失效。
    bool base_ptr = false;
    bool base_ptr_known = false;
    // compact 数组布局缓存（xv·at/set 首次 decode 后复用：dims/esz/kind 恒定，
    // quicksort 1728 次 at + 1492 次 set 免每次 strtoll/substr 解析）。
    std::uint32_t arr_esz = 0;
    std::int32_t arr_dims[8] = {};
    std::uint32_t arr_ndim = 0;
    std::uint8_t arr_kind = 0;  // Scalar::K（元素类别）+ 高位置 is_char
};

// 单帧执行上下文（二进制栈帧）。
struct FrameCtx {
    void* h = nullptr;
    std::string vid;
    int depth = 0;
    std::string frame;    // /vthread/<vid>/[depth]
    std::string func_dir;    // /lib/<name>
    std::string frame_dir_hint;  // 段缓存已持有的函数目录（复用，避免二次构造/查询）
    std::vector<FReg> regs;  // 帧地址表（帧入口一次解析）
    // 跨帧参数别名表（定长栈数组，段 nr/nw ≤ 16）：arg_src[-k-1]=读参 k 槽的调用方 reg，
    // arg_dst[k-1]=写参 k 槽的调用方 reg。避免每次 call 的 vector 堆分配（fib 递归热点）。
    FReg* arg_src[16] = {};
    int arg_src_n = 0;
    FReg* arg_dst[16] = {};
    int arg_dst_n = 0;
    // 纯内存递归帧：callee 段无 native/handoff → 无观察边界 → 帧系统变量
    // （returnpc/lib）与帧生命周期（DelTree/Mkindex）全部省略。return 直接
    // Returned（stop_pc 空，Call 分支捕获后 continue），pc 从不指向此帧。
    bool mem_frame = false;
    // 外部可观察：vthread·run 驱动的子链（printstack 检视需要帧 ‥lib/extindex）。
    // 顶层 vthread（bootstrap/test）链默认不可观察——mem_frame 保持零 kvspace
    // 性能（fib/iops 热路径）。do_call 子帧继承父链。
    bool observable = false;
};

// 单帧执行结果：Ok=入口帧走到段尾/return（vthread done）；
// Returned=子帧 return 弹出（stop_pc 为调用方返回地址）；Stop=遇到他人 op。
enum class RunResult : std::uint8_t { Ok = 0, Stop, Returned, Error };

// ── int64 快路径：模板特化（编译期内联单指令，无函数指针间接跳）。
template <std::uint8_t O>
inline std::int64_t fast_i64(std::int64_t a, std::int64_t b) {
    if constexpr (O == 1)  return a + b;                        // Add
    if constexpr (O == 2)  return a - b;                        // Sub
    if constexpr (O == 3)  return a * b;                        // Mul
    if constexpr (O == 4)  return b ? a / b : 0;                // Div（0 哨兵）
    if constexpr (O == 5)  return b ? a % b : 0;                // Mod
    if constexpr (O == 6)  return a > b ? a : b;                // Max
    if constexpr (O == 7)  return a < b ? a : b;                // Min
    if constexpr (O == 8)  return a & b;                        // BitAnd
    if constexpr (O == 9)  return a | b;                        // BitOr
    if constexpr (O == 10) return a ^ b;                        // BitXor
    if constexpr (O == 11) return (std::int64_t)((std::uint64_t)a << (b & 63)); // Shl
    if constexpr (O == 12) return a >> (b & 63);                // Shr
    if constexpr (O == 13) return a == b ? 1 : 0;               // Eq
    if constexpr (O == 14) return a != b ? 1 : 0;               // Neq
    if constexpr (O == 15) return a < b ? 1 : 0;                // Lt
    if constexpr (O == 16) return a <= b ? 1 : 0;               // Le
    if constexpr (O == 17) return a > b ? 1 : 0;                // Gt
    if constexpr (O == 18) return a >= b ? 1 : 0;               // Ge
    return 0;
}

}  // namespace kv::rt::bin
