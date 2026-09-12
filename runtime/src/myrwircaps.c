#include "runtime_internal.h"
#include "rwir_internal.h"
#include "kvlang_runtime.h"

typedef int (*kvlangBuiltinFn)(kvlangFrame_t *f);

/* 共享队列根：第一个 rwir 的 /lib/<opcode>/vids 绝对路径。 */
static char *g_first_vids = NULL;

/* 建立 rwir 的 vids 队列：第一个 rwir 是真实 strkeymap，后续是 Ptr 指向第一个。
 * 幂等：vids 已存在则跳过——否则脏 kvspace 上重复注册时，Set 经路径穿透会把首队列
 * 改成自指 Ptr，令 resolve_path 死循环（父子 kvlang 共享同一 redis 的挂起根因）。 */
static void register_vids(kvlangKv_t *k, const char *opcode) {
    char *base = kvlangKeytreeRwir(opcode);
    kvlangStrbuf_t tk;
    kvlangStrbufInit(&tk);
    kvlangStrbufPuts(&tk, base);
    kvlangStrbufPuts(&tk, "/vids");
    bool first = (g_first_vids == NULL);
    if (first)
        g_first_vids = strdup(tk.p);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    bool exists =
        (kvlangKvGetOne(k, tk.p, &cur) == 0 && !kvlangXvalueNone(&cur));
    kvlangXvalueFree(&cur);
    if (!exists) {
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        int32_t dims[1] = {0};
        if (first)
            kvlangXvalueNewTlvDims(&v, KVSPACE_KIND_MAP, (const uint8_t *)"", 0,
                                   dims, 1);
        else
            kvlangXvalueNewPtr(&v, KVSPACE_KIND_MAP, g_first_vids);
        kvlangKvPair_t p = {tk.p, v};
        char err[256];
        kvlangKvSet(k, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v);
    }
    kvlangStrbufFree(&tk);
    free(base);
}

/* 写 [0,x] 签名行槽 = def langtype（body 为该参数 langtype 串）。 */
static void write_sig_slot(kvlangKv_t *k, const char *base, int x,
                           const char *lt, size_t lt_len) {
    kvlangStrbuf_t sk;
    kvlangStrbufInit(&sk);
    kvlangStrbufPrintf(&sk, "%s/[0,%d]", base, x);
    char *clean = strndup(lt, lt_len);
    kvlangXvalue_t sv;
    kvlangXvalueNewDefLangtype(&sv, clean);
    free(clean);
    kvlangKvPair_t sp = {sk.p, sv};
    char err[256];
    kvlangKvSet(k, &sp, 1, err, sizeof err);
    kvlangXvalueFree(&sv);
    kvlangStrbufFree(&sk);
}

/* 注册一条 rwir：/lib/<op> 路由头（仅计数头）+ 各参数落 [0,x] 签名行槽（def langtype）。
 * 参数类型逐条传入（读参 rp[0..nr]、写参 wp[0..nw]），不再拼签名串——避免其它 runtime
 * 把「拼接 sig 串」误当注册标准。末读参尾缀 "..." → 变参 arity（落 dynamic 字节）。 */
int kvlangDefRwir(void *kvspace, const char *opcode, const char *const *rp,
                  int32_t nr, const char *const *wp, int32_t nw) {
    kvlangKv_t k = {kvspace};
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
    int rc = kvlangKvSet(&k, &hp, 1, err, sizeof err);
    kvlangXvalueFree(&hv);

    for (int32_t i = 0; i < nr; i++)
        write_sig_slot(&k, base, -(i + 1), rp[i],
                       (i == nr - 1) ? last_len : strlen(rp[i]));
    for (int32_t i = 0; i < nw; i++)
        write_sig_slot(&k, base, i + 1, wp[i], strlen(wp[i]));

    free(base);
    /* 建立共享 vids 队列（第一个真实 strkeymap，后续 Ptr 指向它；幂等查 kvspace）。 */
    register_vids(&k, opcode);
    return rc;
}

/* 精度前缀（int64·add / float32·add …）保留：CapIndex 两级查表——先按完整 opcode 命中特化，
 * 未命中且前缀是 C native 数字 kind 时才剥前缀归到裸 op（如 add），kvlangBuiltin* 按操作数 kind 归约。 */
static int kvlangCtlCopy(kvlangFrame_t *f) { return kvlangBuiltinExecuteCopy(f->kv, f->vtid, f->pc, f->inst); }

static const struct { const char *op; kvlangBuiltinFn fn; } myrwircaps[] = {
    /* control / copy：与 native 算子同表，op_id 单跳派发 */
    {OP_CALL, kvlangCtlCall}, {OP_RETURN, kvlangCtlReturn}, {OP_GOTO, kvlangCtlGoto},
    {OP_BR, kvlangCtlBr}, {OP_COPY, kvlangCtlCopy},
    {"add", kvlangBuiltinAdd}, {"+", kvlangBuiltinAdd},
    {"sub", kvlangBuiltinSub}, {"-", kvlangBuiltinSub},
    {"mul", kvlangBuiltinMul}, {"×", kvlangBuiltinMul},
    {"div", kvlangBuiltinDiv}, {"÷", kvlangBuiltinDiv},
    {"mod", kvlangBuiltinMod}, {"%", kvlangBuiltinMod},
    {"eq", kvlangBuiltinEq}, {"==", kvlangBuiltinEq},
    {"neq", kvlangBuiltinNeq}, {"!=", kvlangBuiltinNeq}, {"≠", kvlangBuiltinNeq},
    {"lt", kvlangBuiltinLt}, {"<", kvlangBuiltinLt},
    {"gt", kvlangBuiltinGt}, {">", kvlangBuiltinGt},
    {"le", kvlangBuiltinLe}, {"<=", kvlangBuiltinLe}, {"≤", kvlangBuiltinLe},
    {"ge", kvlangBuiltinGe}, {">=", kvlangBuiltinGe}, {"≥", kvlangBuiltinGe},
    {"and", kvlangBuiltinAnd}, {"&&", kvlangBuiltinAnd},
    {"or", kvlangBuiltinOr}, {"||", kvlangBuiltinOr},
    {"not", kvlangBuiltinNot}, {"!", kvlangBuiltinNot},
    {"bitand", kvlangBuiltinBitand}, {"&", kvlangBuiltinBitand},
    {"bitor", kvlangBuiltinBitor}, {"|", kvlangBuiltinBitor},
    {"bitxor", kvlangBuiltinBitxor}, {"^", kvlangBuiltinBitxor},
    {"shl", kvlangBuiltinShl}, {"<<", kvlangBuiltinShl},
    {"shr", kvlangBuiltinShr}, {">>", kvlangBuiltinShr},
    {"pow", kvlangBuiltinPow},
    {"sqrt", kvlangBuiltinSqrt}, {"√", kvlangBuiltinSqrt},
    {"exp", kvlangBuiltinExp},
    {"log", kvlangBuiltinLog},
    {"neg", kvlangBuiltinNeg},
    {"abs", kvlangBuiltinAbs},
    {"sign", kvlangBuiltinSign},
    {"max", kvlangBuiltinMax}, {"min", kvlangBuiltinMin},
    /* cast */
    {"bool", kvlangBuiltinCastBool}, {"int8", kvlangBuiltinCastInt8}, {"int16", kvlangBuiltinCastInt16},
    {"int32", kvlangBuiltinCastInt32}, {"int64", kvlangBuiltinCastInt64}, {"uint8", kvlangBuiltinCastUint8},
    {"uint16", kvlangBuiltinCastUint16}, {"uint32", kvlangBuiltinCastUint32}, {"uint64", kvlangBuiltinCastUint64},
    {"float32", kvlangBuiltinCastF32}, {"float64", kvlangBuiltinCastF64},
    {"char/utf32", kvlangBuiltinCastChar32}, {"char/utf8", kvlangBuiltinCastChar8}, {"char/ascii", kvlangBuiltinCastCharAscii},
    /* collection */
    {"array", kvlangBuiltinArray}, {"array·fill", kvlangBuiltinArrayFill},
    {"array·scatter", kvlangBuiltinScatter}, {"array·compact", kvlangBuiltinCompact},
    {"array·append", kvlangBuiltinAppend}, {"array·slice", kvlangBuiltinSlice},
    {"obj", kvlangBuiltinObj}, {"map", kvlangBuiltinMap}, {"struct·new", kvlangBuiltinStructNew},
    {"ndarray·numel", kvlangBuiltinNdarrayNumel}, {"ndarray·dim", kvlangBuiltinNdarrayDim}, {"ndarray·shape", kvlangBuiltinNdarrayShape},
    {"xv·at", kvlangBuiltinXvAt}, {"xv·set", kvlangBuiltinXvSet}, {"xv·reshape", kvlangBuiltinXvReshape},
    {"xv·reinterpret", kvlangBuiltinXvReinterpret},
    {"xv·langtype", kvlangBuiltinXvLangtype}, {"xv·bodylen", kvlangBuiltinXvBodylen},
    {"string·set", kvlangBuiltinStringSet}, {"string·char", kvlangBuiltinStringChar}, {"string·ord", kvlangBuiltinStringOrd},
    {"string·cmp", kvlangBuiltinStringCmp}, {"string·find", kvlangBuiltinStringFind}, {"string·len", kvlangBuiltinStringLen},
    {"string·slice", kvlangBuiltinStringSlice}, {"string·concat", kvlangBuiltinStringConcat},
    {"string·formatint", kvlangBuiltinStringFormatInt}, {"string·formatuint", kvlangBuiltinStringFormatUint},
    {"string·parseint", kvlangBuiltinStringParseInt}, {"string·parseuint", kvlangBuiltinStringParseUint},
    {"time·now", kvlangBuiltinTimeNow}, {"time·sub", kvlangBuiltinTimeSub}, {"time·add", kvlangBuiltinTimeAdd},
    {"time/duration·nanos", kvlangBuiltinDurFrom}, {"time/duration·millis", kvlangBuiltinDurFrom},
    {"time/duration·seconds", kvlangBuiltinDurFrom}, {"time/duration·minutes", kvlangBuiltinDurFrom},
    {"time/duration·hours", kvlangBuiltinDurFrom},
    {"time/duration·as_nanos", kvlangBuiltinDurTo}, {"time/duration·as_millis", kvlangBuiltinDurTo},
    {"time/duration·as_seconds", kvlangBuiltinDurTo}, {"time/duration·as_minutes", kvlangBuiltinDurTo},
    {"time/duration·as_hours", kvlangBuiltinDurTo},
    {"time/duration·add", kvlangBuiltinDurArith}, {"time/duration·sub", kvlangBuiltinDurArith},
    {"time/duration·before", kvlangBuiltinDurCmp}, {"time/duration·after", kvlangBuiltinDurCmp},
    {"time·before", kvlangBuiltinTimeCmp}, {"time·after", kvlangBuiltinTimeCmp},
    {"random·uint64", kvlangBuiltinRandUint64}, {"random·int63", kvlangBuiltinRandInt63}, {"random·intn", kvlangBuiltinRandIntn},
    {"kvspace·get", kvlangCGet}, {"kvspace·set", kvlangCSet}, {"kvspace·del", kvlangCDel},
    {"kvspace·deltree", kvlangCDelTree}, {"kvspace·cp", kvlangCCp}, {"kvspace·cpdir", kvlangCCpTree}, {"kvspace·cplist", kvlangCCpList}, {"kvspace·list", kvlangCList}, {"kvspace·listlen", kvlangCListLen}, {"kvspace·listn", kvlangCListN}, {"kvspace·mkindex", kvlangCMkindex},
    {"kvspace·extindex", kvlangCExtIndex}, {"kvspace·rmindexext", kvlangCRmIndexExt}, {"kvspace·watch", kvlangCWatch}, {"kvlang·abs", kvlangCAbs},
    {"vthread·create", kvlangBuiltinVthreadCreate},
    {"vthread·run", kvlangBuiltinVthreadRun},
    {"vthread·call", kvlangBuiltinVthreadCall},
    {"vthread·sleep", kvlangBuiltinVthreadSleep},
    {"vthread·setstatus", kvlangBuiltinVthreadSetstatus},
    {"debugger", kvlangBuiltinDebugger},
};

static const size_t myrwircaps_n = sizeof(myrwircaps) / sizeof(myrwircaps[0]);

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
int kvlangBuiltinCapIndex(const char *opcode) {
    for (size_t i = 0; i < myrwircaps_n; i++)
        if (strcmp(myrwircaps[i].op, opcode) == 0) return (int)i;
    const char *dot = strstr(opcode, MEMBER_SEP);
    if (dot && is_num_kind_prefix(opcode, (size_t)(dot - opcode))) {
        const char *bare = dot + MEMBER_SEP_LEN;
        for (size_t i = 0; i < myrwircaps_n; i++)
            if (strcmp(myrwircaps[i].op, bare) == 0) return (int)i;
    }
    return -1;
}

/* notinmycaps：查 myrwircaps table。opcode 不在本 runtime 能力表内 → true
 * （即须经 /lib/<op> 的 def rwir 路由给能兑现它的其它 runtime，或为用户 rwfunc）。 */
bool notinmycaps(const char *opcode) {
    return kvlangBuiltinCapIndex(opcode) < 0;
}

bool kvlangBuiltinNumOp(const char *opcode) {
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

int kvlangBuiltinNative(kvlangFrame_t *f) {
    int i = f->inst->op_id >= 0 ? f->inst->op_id : kvlangBuiltinCapIndex(f->inst->opcode);
    if (i >= 0) return myrwircaps[i].fn(f);
    return kvlangBuiltinSetErr(f, "unknown builtin op: %s", f->inst->opcode);
}
