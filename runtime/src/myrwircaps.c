#include "runtime_internal.h"
#include "rwir_internal.h"
#include "kvlang_runtime.h"

/* Keep the shared queue in KVSpace. */
static int register_vids(kvspace_t *k, const char *opcode) {
    char queue[] = "/vthread/‥vids";
    const char *queue_type = "[]char/utf32·[]char/utf32";
    kvlangXvalue_t root;
    kvlangXvalueZero(&root);
    bool root_exists = kvlangKvspaceGetOne(k, queue, &root) == 0 && !kvlangXvalueNone(&root);
    if (!root_exists) {
        kvlangXvalue_t value;
        kvlangXvalueZero(&value);
        kvlangXvalueNew(&value, queue_type, NULL, 0, 1);
        kvlangKvPair_t pair = {queue, value};
        char err[256];
        int rc = kvlangKvspaceSet(k, &pair, 1, err, sizeof err);
        kvlangXvalueFree(&value);
        if (rc != 0)
            return rc;
    }
    char *base = kvlangKeytreeRwir(opcode);
    char tk[KV_KEY_MAX];
    kvlangKeyFormat(tk, sizeof tk, "%s/vids", base);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    bool exists = (kvlangKvspaceGetOne(k, tk, &cur) == 0 && !kvlangXvalueNone(&cur));
    int rc = 0;
    if (!exists) {
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangXvalueNewPtr(&v, queue_type, queue);
        kvlangKvPair_t p = {tk, v};
        char err[256];
        rc = kvlangKvspaceSet(k, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v);
    }
    free(base);
    return rc;
}

/* 写 [0,x] 签名行槽 = def langtype（body 为该参数 langtype 串）。 */
static void write_signature_slot(kvspace_t *k, const char *base, int x,
                           const char *lt, size_t lt_len) {
    char sk[KV_KEY_MAX];
    kvlangKeyFormat(sk, sizeof sk, "%s/[0,%d]", base, x);
    char *clean = strndup(lt, lt_len);
    kvlangXvalue_t sv;
    kvlangXvalueNewDefLangtype(&sv, clean);
    free(clean);
    kvlangKvPair_t sp = {sk, sv};
    char err[256];
    kvlangKvspaceSet(k, &sp, 1, err, sizeof err);
    kvlangXvalueFree(&sv);
}

/* 注册一条 rwir：/lib/<op> 路由头（仅计数头）+ 各参数落 [0,x] 签名行槽（def langtype）。
 * 参数类型逐条传入（读参 rp[0..read_count]、写参 wp[0..write_count]），不再拼签名串——避免其它 runtime
 * 把「拼接 sig 串」误当注册标准。末读参尾缀 "..." → 变参 arity（落 dynamic 字节）。 */
int kvlangDefRwir(void *kvspace, const char *opcode, const char *const *rp,
                  int32_t nr, const char *const *wp, int32_t nw) {
    kvspace_t k = {kvspace};
    char *base = kvlangKeytreeRwir(opcode);
    char err[256];

    int dynamic = 0;
    size_t last_len = (nr > 0 && rp[nr - 1]) ? strlen(rp[nr - 1]) : 0;
    if (nr > 0 && last_len >= 3 &&
        memcmp(rp[nr - 1] + last_len - 3, "...", 3) == 0) {
        dynamic = 1;
        last_len -= 3;
    }

    kvlangXvalue_t hv;
    kvlangXvalueNewDefRwir(&hv, nr, nw, dynamic);
    kvlangKvPair_t hp = {base, hv};
    int rc = kvlangKvspaceSet(&k, &hp, 1, err, sizeof err);
    kvlangXvalueFree(&hv);

    for (int32_t i = 0; i < nr; i++)
        write_signature_slot(&k, base, -(i + 1), rp[i],
                       (i == nr - 1) ? last_len : strlen(rp[i]));
    for (int32_t i = 0; i < nw; i++)
        write_signature_slot(&k, base, i + 1, wp[i], strlen(wp[i]));

    free(base);
    /* Register the shared KVSpace queue. */
    if (rc == 0)
        rc = register_vids(&k, opcode);
    return rc;
}

/* 精度前缀（int64·add / float32·add …）保留：CapIndex 两级查表——先按完整 opcode 命中特化，
 * 未命中且前缀是 C native 数字 kind 时才剥前缀归到裸 op（如 add），kvlangBuiltin* 按操作数 kind 归约。 */
static int kvlangControlCopy(kvlangFrame_t *f) { return kvlangBuiltinExecuteCopy(f); }

#ifdef KV_COUNT
unsigned long kvCSpec, kvCGen;
#endif

static const kvlangRwirCap myrwircaps[] = {
    /* control / copy：与 native 算子同表，opcode_id 单跳派发 */
    {OP_CALL, kvlangControlCall, false}, {OP_RETURN, kvlangControlReturn, false}, {OP_GOTO, kvlangControlGoto, false},
    {OP_BR, kvlangControlBr, false}, {OP_COPY, kvlangControlCopy, false},
    {"add", kvlangBuiltinAdd, false}, {"+", kvlangBuiltinAdd, false},
    /* layout 特化 opcode：<kind>·<op> 直达对应族的实现（A1 原型）。 */
    {"int64" MEMBER_SEP "add", kvlangBuiltinI64Add, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "sub", kvlangBuiltinI64Sub, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "mul", kvlangBuiltinI64Mul, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "div", kvlangBuiltinI64Div, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "mod", kvlangBuiltinI64Mod, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "bitand", kvlangBuiltinI64Bitand, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "bitor", kvlangBuiltinI64Bitor, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "bitxor", kvlangBuiltinI64Bitxor, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "shl", kvlangBuiltinI64Shl, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "shr", kvlangBuiltinI64Shr, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "max", kvlangBuiltinI64Max, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "min", kvlangBuiltinI64Min, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "neg", kvlangBuiltinI64Neg, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "abs", kvlangBuiltinI64Abs, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "sign", kvlangBuiltinI64Sign, false, KVSPACE_KIND_INT64, 8},
    {"int64" MEMBER_SEP "eq", kvlangBuiltinI64Eq, false, KVSPACE_KIND_BOOL, 1},
    {"int64" MEMBER_SEP "neq", kvlangBuiltinI64Neq, false, KVSPACE_KIND_BOOL, 1},
    {"int64" MEMBER_SEP "lt", kvlangBuiltinI64Lt, false, KVSPACE_KIND_BOOL, 1},
    {"int64" MEMBER_SEP "le", kvlangBuiltinI64Le, false, KVSPACE_KIND_BOOL, 1},
    {"int64" MEMBER_SEP "gt", kvlangBuiltinI64Gt, false, KVSPACE_KIND_BOOL, 1},
    {"int64" MEMBER_SEP "ge", kvlangBuiltinI64Ge, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "add", kvlangBuiltinF64Add, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "sub", kvlangBuiltinF64Sub, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "mul", kvlangBuiltinF64Mul, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "div", kvlangBuiltinF64Div, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "max", kvlangBuiltinF64Max, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "min", kvlangBuiltinF64Min, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "neg", kvlangBuiltinF64Neg, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "abs", kvlangBuiltinF64Abs, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "sign", kvlangBuiltinF64Sign, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "sqrt", kvlangBuiltinF64Sqrt, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "exp", kvlangBuiltinF64Exp, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "log", kvlangBuiltinF64Log, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "pow", kvlangBuiltinF64Pow, false, KVSPACE_KIND_FLOAT64, 8},
    {"float64" MEMBER_SEP "eq", kvlangBuiltinF64Eq, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "neq", kvlangBuiltinF64Neq, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "lt", kvlangBuiltinF64Lt, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "le", kvlangBuiltinF64Le, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "gt", kvlangBuiltinF64Gt, false, KVSPACE_KIND_BOOL, 1},
    {"float64" MEMBER_SEP "ge", kvlangBuiltinF64Ge, false, KVSPACE_KIND_BOOL, 1},
    /* Phase 2 整数族（int64 族见上；结果类型与此表同源）。 */
#define CAP_INT_BASE(ktok, kstr, w, U, kid)                                         \
    {kstr MEMBER_SEP "add", kvlangBuiltin##ktok##Add, false, kstr, w},         \
    {kstr MEMBER_SEP "sub", kvlangBuiltin##ktok##Sub, false, kstr, w},         \
    {kstr MEMBER_SEP "mul", kvlangBuiltin##ktok##Mul, false, kstr, w},         \
    {kstr MEMBER_SEP "div", kvlangBuiltin##ktok##Div, false, kstr, w},         \
    {kstr MEMBER_SEP "mod", kvlangBuiltin##ktok##Mod, false, kstr, w},         \
    {kstr MEMBER_SEP "max", kvlangBuiltin##ktok##Max, false, kstr, w},         \
    {kstr MEMBER_SEP "min", kvlangBuiltin##ktok##Min, false, kstr, w},         \
    {kstr MEMBER_SEP "eq", kvlangBuiltin##ktok##Eq, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "neq", kvlangBuiltin##ktok##Neq, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "lt", kvlangBuiltin##ktok##Lt, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "le", kvlangBuiltin##ktok##Le, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "gt", kvlangBuiltin##ktok##Gt, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "ge", kvlangBuiltin##ktok##Ge, false, KVSPACE_KIND_BOOL, 1},
#define CAP_INT_SIGNED(ktok, kstr, w, U, kid)                                       \
    {kstr MEMBER_SEP "neg", kvlangBuiltin##ktok##Neg, false, kstr, w},         \
    {kstr MEMBER_SEP "abs", kvlangBuiltin##ktok##Abs, false, kstr, w},
#define CAP_INT_U64(ktok, kstr, w, U, kid)                                          \
    {kstr MEMBER_SEP "bitand", kvlangBuiltin##ktok##Bitand, false, kstr, w},   \
    {kstr MEMBER_SEP "bitor", kvlangBuiltin##ktok##Bitor, false, kstr, w},     \
    {kstr MEMBER_SEP "bitxor", kvlangBuiltin##ktok##Bitxor, false, kstr, w},   \
    {kstr MEMBER_SEP "shl", kvlangBuiltin##ktok##Shl, false, kstr, w},         \
    {kstr MEMBER_SEP "shr", kvlangBuiltin##ktok##Shr, false, kstr, w},
    KVLANG_INT_BASE_KINDS(CAP_INT_BASE)
    KVLANG_INT_SIGNED_KINDS(CAP_INT_SIGNED)
    KVLANG_INT_U64_KINDS(CAP_INT_U64)
    /* Phase 2 浮点：float32（float64 族见上）。 */
#define CAP_F32(ktok, kstr, w)                                                 \
    {kstr MEMBER_SEP "add", kvlangBuiltin##ktok##Add, false, kstr, w},         \
    {kstr MEMBER_SEP "sub", kvlangBuiltin##ktok##Sub, false, kstr, w},         \
    {kstr MEMBER_SEP "mul", kvlangBuiltin##ktok##Mul, false, kstr, w},         \
    {kstr MEMBER_SEP "div", kvlangBuiltin##ktok##Div, false, kstr, w},         \
    {kstr MEMBER_SEP "max", kvlangBuiltin##ktok##Max, false, kstr, w},         \
    {kstr MEMBER_SEP "min", kvlangBuiltin##ktok##Min, false, kstr, w},         \
    {kstr MEMBER_SEP "neg", kvlangBuiltin##ktok##Neg, false, kstr, w},         \
    {kstr MEMBER_SEP "abs", kvlangBuiltin##ktok##Abs, false, kstr, w},         \
    {kstr MEMBER_SEP "eq", kvlangBuiltin##ktok##Eq, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "neq", kvlangBuiltin##ktok##Neq, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "lt", kvlangBuiltin##ktok##Lt, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "le", kvlangBuiltin##ktok##Le, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "gt", kvlangBuiltin##ktok##Gt, false, KVSPACE_KIND_BOOL, 1},\
    {kstr MEMBER_SEP "ge", kvlangBuiltin##ktok##Ge, false, KVSPACE_KIND_BOOL, 1},
    KVLANG_FLOAT32_KINDS(CAP_F32)
    {"sub", kvlangBuiltinSub, false}, {"-", kvlangBuiltinSub, false},
    {"mul", kvlangBuiltinMul, false}, {"×", kvlangBuiltinMul, false},
    {"div", kvlangBuiltinDiv, false}, {"÷", kvlangBuiltinDiv, false},
    {"mod", kvlangBuiltinMod, false}, {"%", kvlangBuiltinMod, false},
    {"eq", kvlangBuiltinEq, false}, {"==", kvlangBuiltinEq, false},
    {"neq", kvlangBuiltinNeq, false}, {"!=", kvlangBuiltinNeq, false}, {"≠", kvlangBuiltinNeq, false},
    {"lt", kvlangBuiltinLt, false}, {"<", kvlangBuiltinLt, false},
    {"gt", kvlangBuiltinGt, false}, {">", kvlangBuiltinGt, false},
    {"le", kvlangBuiltinLe, false}, {"<=", kvlangBuiltinLe, false}, {"≤", kvlangBuiltinLe, false},
    {"ge", kvlangBuiltinGe, false}, {">=", kvlangBuiltinGe, false}, {"≥", kvlangBuiltinGe, false},
    {"and", kvlangBuiltinAnd, false}, {"&&", kvlangBuiltinAnd, false},
    {"or", kvlangBuiltinOr, false}, {"||", kvlangBuiltinOr, false},
    {"not", kvlangBuiltinNot, false}, {"!", kvlangBuiltinNot, false},
    {"bitand", kvlangBuiltinBitand, false}, {"&", kvlangBuiltinBitand, false},
    {"bitor", kvlangBuiltinBitor, false}, {"|", kvlangBuiltinBitor, false},
    {"bitxor", kvlangBuiltinBitxor, false}, {"^", kvlangBuiltinBitxor, false},
    {"shl", kvlangBuiltinShl, false}, {"<<", kvlangBuiltinShl, false},
    {"shr", kvlangBuiltinShr, false}, {">>", kvlangBuiltinShr, false},
    {"pow", kvlangBuiltinPow, false},
    {"sqrt", kvlangBuiltinSqrt, false}, {"√", kvlangBuiltinSqrt, false},
    {"exp", kvlangBuiltinExp, false},
    {"log", kvlangBuiltinLog, false},
    {"neg", kvlangBuiltinNeg, false},
    {"abs", kvlangBuiltinAbs, false},
    {"sign", kvlangBuiltinSign, false},
    {"max", kvlangBuiltinMax, false}, {"min", kvlangBuiltinMin, false},
    /* cast */
    {"bool", kvlangBuiltinCastBool, false}, {"int8", kvlangBuiltinCastInt8, false}, {"int16", kvlangBuiltinCastInt16, false},
    {"int32", kvlangBuiltinCastInt32, false}, {"int64", kvlangBuiltinCastInt64, false}, {"uint8", kvlangBuiltinCastUint8, false},
    {"uint16", kvlangBuiltinCastUint16, false}, {"uint32", kvlangBuiltinCastUint32, false}, {"uint64", kvlangBuiltinCastUint64, false},
    {"float32", kvlangBuiltinCastFloat32, false}, {"float64", kvlangBuiltinCastFloat64, false},
    {"char/utf32", kvlangBuiltinCastCharUtf32, false}, {"char/utf8", kvlangBuiltinCastCharUtf8, false}, {"char/ascii", kvlangBuiltinCastCharAscii, false},
    /* collection */
    {"array·new", kvlangBuiltinArray, false}, {"array·fill", kvlangBuiltinArrayFill, false},
    {"array·scatter", kvlangBuiltinScatter, false}, {"array·compact", kvlangBuiltinCompact, false},
    {"array·append", kvlangBuiltinAppend, false}, {"array·slice", kvlangBuiltinSlice, false},
    {"obj", kvlangBuiltinObj, true}, {"map·new", kvlangBuiltinMap, true}, {"struct·new", kvlangBuiltinStructNew, false},
    {"ndarray·numel", kvlangBuiltinNdarrayNumel, false}, {"ndarray·dim", kvlangBuiltinNdarrayDim, false}, {"ndarray·shape", kvlangBuiltinNdarrayShape, false},
    {"xv·at", kvlangBuiltinXvAt, false}, {"xv·set", kvlangBuiltinXvSet, false}, {"xv·reshape", kvlangBuiltinXvReshape, false},
    {"xv·reinterpret", kvlangBuiltinXvReinterpret, false},
    {"xv·langtype", kvlangBuiltinXvLangtype, false}, {"xv·bodylen", kvlangBuiltinXvBodyLen, false},
    {"xv·parselangtype", kvlangBuiltinXvParseLangtype, false},
    {"string·set", kvlangBuiltinStringSet, false}, {"string·char", kvlangBuiltinStringChar, false}, {"string·ord", kvlangBuiltinStringOrd, false},
    {"string·cmp", kvlangBuiltinStringCmp, false}, {"string·find", kvlangBuiltinStringFind, false}, {"string·len", kvlangBuiltinStringLen, false},
    {"string·slice", kvlangBuiltinStringSlice, false}, {"string·concat", kvlangBuiltinStringConcat, false},
    {"string·formatint", kvlangBuiltinStringFormatInt, false}, {"string·formatuint", kvlangBuiltinStringFormatUint, false},
    {"string·parseint", kvlangBuiltinStringParseInt, false}, {"string·parseuint", kvlangBuiltinStringParseUint, false},
    {"string·parsefloat", kvlangBuiltinStringParseFloat, false},
    {"time·now", kvlangBuiltinTimeNow, false}, {"time·sub", kvlangBuiltinTimeSub, false}, {"time·add", kvlangBuiltinTimeAdd, false},
    {"time/duration·nanos", kvlangBuiltinDurationFrom, false}, {"time/duration·millis", kvlangBuiltinDurationFrom, false},
    {"time/duration·seconds", kvlangBuiltinDurationFrom, false}, {"time/duration·minutes", kvlangBuiltinDurationFrom, false},
    {"time/duration·hours", kvlangBuiltinDurationFrom, false},
    {"time/duration·as_nanos", kvlangBuiltinDurationTo, false}, {"time/duration·as_millis", kvlangBuiltinDurationTo, false},
    {"time/duration·as_seconds", kvlangBuiltinDurationTo, false}, {"time/duration·as_minutes", kvlangBuiltinDurationTo, false},
    {"time/duration·as_hours", kvlangBuiltinDurationTo, false},
    {"time/duration·add", kvlangBuiltinDurationArith, false}, {"time/duration·sub", kvlangBuiltinDurationArith, false},
    {"time/duration·before", kvlangBuiltinDurCompare, false}, {"time/duration·after", kvlangBuiltinDurCompare, false},
    {"time·before", kvlangBuiltinTimeCompare, false}, {"time·after", kvlangBuiltinTimeCompare, false},
    {"random·uint64", kvlangBuiltinRandUint64, false}, {"random·int63", kvlangBuiltinRandInt63, false}, {"random·intn", kvlangBuiltinRandIntn, false},
    {"kvspace·get", kvlangBuiltinKvspaceGet, false}, {"kvspace·set", kvlangBuiltinKvspaceSet, false}, {"kvspace·del", kvlangBuiltinKvspaceDelete, false},
    {"kvspace·deltree", kvlangBuiltinKvspaceDeleteTree, false}, {"kvspace·cp", kvlangBuiltinKvspaceCopy, false}, {"kvspace·cpdir", kvlangBuiltinKvspaceCopyTree, false}, {"kvspace·cplist", kvlangBuiltinKvspaceCopyList, false}, {"kvspace·listlen", kvlangBuiltinKvspaceListLength, false}, {"kvspace·listat", kvlangBuiltinKvspaceListAt, false}, {"kvspace·mkindex", kvlangBuiltinKvspaceMakeIndex, false},
    {"kvspace·extindex", kvlangBuiltinKvspaceExtensionIndex, false}, {"kvspace·rmindexext", kvlangBuiltinKvspaceRemoveIndexExtension, false}, {"kvspace·watch", kvlangBuiltinKvspaceWatch, false}, {"kvlang·abs", kvlangBuiltinKvlangAbs, false},
    {"vthread·create", kvlangBuiltinVthreadCreate, false},
    {"vthread·run", kvlangBuiltinVthreadRun, false},
    {"vthread·call", kvlangBuiltinVthreadCall, false},
    {"vthread·sleep", kvlangBuiltinVthreadSleep, false},
    {"vthread·setstatus", kvlangBuiltinVthreadSetStatus, false},
    {"debugger", kvlangBuiltinDebugger, false},
    /* 表尾：非 native（delegated / def rwir / user call）统一路由。 */
    {NULL, kvlangKvcpuRoute, false},
};

static const size_t myrwircaps_n = sizeof(myrwircaps) / sizeof(myrwircaps[0]);

__attribute__((visibility("hidden"))) const kvlangRwirCap *kvlangRwirCaps(void) {
    return myrwircaps;
}
__attribute__((visibility("hidden"))) int kvlangRwirRouteId(void) {
    return (int)myrwircaps_n - 1;
}

/* C 原生数值 langtype 集：仅这些精度的算术走 kvlangBuiltinAdd 等通用实现。
 * int4/fp8/bf16 等 C 表达不了的量化精度不在此列，其 <langtype>·op 走专精 fn 或 handoff。 */
static const char *NUM_KINDS[] = {"int8", "int16", "int32", "int64", "uint8",
                                  "uint16", "uint32", "uint64", "float32", "float64"};
static bool is_num_kind_prefix(const char *op, size_t n) {
    for (size_t i = 0; i < sizeof(NUM_KINDS) / sizeof(NUM_KINDS[0]); i++)
        if (strlen(NUM_KINDS[i]) == n && strncmp(op, NUM_KINDS[i], n) == 0) return true;
    return false;
}

/* rwirtable 两级查找：先查完整 opcode（命中=专精实现，如 fp8·add，或 time·add 等成员算子），
 * 未命中且末段前缀是 C 原生数值 langtype 才拆前缀查裸算子（int64·add → add，通用实现）。
 * 非数值前缀（time/duration·max 等用户 rwfunc）不剥离，两级皆 miss → 非本 runtime native。 */
int kvlangBuiltinCapabilityIndex(const char *opcode) {
    for (size_t i = 0; i < myrwircaps_n; i++)
        if (myrwircaps[i].opcode && strcmp(myrwircaps[i].opcode, opcode) == 0)
            return (int)i;
    const char *dot = strstr(opcode, MEMBER_SEP);
    if (dot && is_num_kind_prefix(opcode, (size_t)(dot - opcode))) {
        const char *bare = dot + MEMBER_SEP_LEN;
        for (size_t i = 0; i < myrwircaps_n; i++)
            if (myrwircaps[i].opcode && strcmp(myrwircaps[i].opcode, bare) == 0)
                return (int)i;
    }
    return -1;
}

/* 该算子是否需在 decode/执行时读回写参旧值（仅 obj/map）。 */
bool kvlangBuiltinLoadWriteValue(int opcode_id) {
    return opcode_id >= 0 && myrwircaps[opcode_id].load_write_value;
}

/* kvlangNotInMyCaps：查 myrwircaps table。opcode 不在本 runtime 能力表内 → true
 * （即须经 /lib/<op> 的 def rwir 路由给能兑现它的其它 runtime，或为用户 rwfunc）。 */
bool kvlangNotInMyCaps(const char *opcode) {
    return kvlangBuiltinCapabilityIndex(opcode) < 0;
}

bool kvlangBuiltinNumericOp(const char *opcode) {
    switch (opcode[0]) {
    case 'a': return strcmp(opcode, "add") == 0 || strcmp(opcode, "abs") == 0;
    case 'b': return strcmp(opcode, "bitand") == 0 || strcmp(opcode, "bitor") == 0 || strcmp(opcode, "bitxor") == 0;
    case 'd': return strcmp(opcode, "div") == 0;
    case 'e': return strcmp(opcode, "eq") == 0 || strcmp(opcode, "exp") == 0;
    case 'g': return strcmp(opcode, "gt") == 0 || strcmp(opcode, "ge") == 0;
    case 'l': return strcmp(opcode, "lt") == 0 || strcmp(opcode, "le") == 0 || strcmp(opcode, "log") == 0;
    case 'm': return strcmp(opcode, "mod") == 0 || strcmp(opcode, "mul") == 0 || strcmp(opcode, "max") == 0 || strcmp(opcode, "min") == 0;
    case 'n': return strcmp(opcode, "neq") == 0 || strcmp(opcode, "neg") == 0;
    case 'p': return strcmp(opcode, "pow") == 0;
    case 's': return strcmp(opcode, "sub") == 0 || strcmp(opcode, "sqrt") == 0 || strcmp(opcode, "shl") == 0 || strcmp(opcode, "shr") == 0 || strcmp(opcode, "sign") == 0;
    }
    return false;
}
