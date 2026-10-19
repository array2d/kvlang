#include "runtime_internal.h"
#include "rwir_internal.h"
#include "kvlang_runtime.h"

typedef int (*kvlangBuiltinFn)(kvlangFrame_t *f);

/* Keep the shared queue in KVSpace. */
static int register_vids(kvlangKv_t *k, const char *opcode) {
    char queue[] = "/vthread/‥vids";
    const char *queue_type = "[]char/utf32·[]char/utf32";
    kvlangXvalue_t root;
    kvlangXvalueZero(&root);
    bool root_exists = kvlangKvGetOne(k, queue, &root) == 0 && !kvlangXvalueNone(&root);
    kvlangXvalueFree(&root);
    if (!root_exists) {
        kvlangXvalue_t value;
        kvlangXvalueZero(&value);
        kvlangXvalueNewTlv(&value, queue_type, NULL, 0, 1);
        kvlangKvPair_t pair = {queue, value};
        char err[256];
        int rc = kvlangKvSet(k, &pair, 1, err, sizeof err);
        kvlangXvalueFree(&value);
        if (rc != 0)
            return rc;
    }
    char *base = kvlangKeytreeRwir(opcode);
    char tk[KV_KEY_MAX];
    kvlangKeyFmt(tk, sizeof tk, "%s/vids", base);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    bool exists = (kvlangKvGetOne(k, tk, &cur) == 0 && !kvlangXvalueNone(&cur));
    kvlangXvalueFree(&cur);
    int rc = 0;
    if (!exists) {
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangXvalueNewPtr(&v, queue_type, queue);
        kvlangKvPair_t p = {tk, v};
        char err[256];
        rc = kvlangKvSet(k, &p, 1, err, sizeof err);
        kvlangXvalueFree(&v);
    }
    free(base);
    return rc;
}

/* 写 [0,x] 签名行槽 = def langtype（body 为该参数 langtype 串）。 */
static void write_sig_slot(kvlangKv_t *k, const char *base, int x,
                           const char *lt, size_t lt_len) {
    char sk[KV_KEY_MAX];
    kvlangKeyFmt(sk, sizeof sk, "%s/[0,%d]", base, x);
    char *clean = strndup(lt, lt_len);
    kvlangXvalue_t sv;
    kvlangXvalueNewDefLangtype(&sv, clean);
    free(clean);
    kvlangKvPair_t sp = {sk, sv};
    char err[256];
    kvlangKvSet(k, &sp, 1, err, sizeof err);
    kvlangXvalueFree(&sv);
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
    /* Register the shared KVSpace queue. */
    if (rc == 0)
        rc = register_vids(&k, opcode);
    return rc;
}

/* 精度前缀（int64·add / float32·add …）保留：CapIndex 两级查表——先按完整 opcode 命中特化，
 * 未命中且前缀是 C native 数字 kind 时才剥前缀归到裸 op（如 add），kvlangBuiltin* 按操作数 kind 归约。 */
static int kvlangCtlCopy(kvlangFrame_t *f) { return kvlangBuiltinExecuteCopy(f); }

static const struct {
    const char *op;
    kvlangBuiltinFn fn;
    bool load_write_value;
} myrwircaps[] = {
    /* control / copy：与 native 算子同表，op_id 单跳派发 */
    {OP_CALL, kvlangCtlCall, false}, {OP_RETURN, kvlangCtlReturn, false}, {OP_GOTO, kvlangCtlGoto, false},
    {OP_BR, kvlangCtlBr, false}, {OP_COPY, kvlangCtlCopy, false},
    {"add", kvlangBuiltinAdd, false}, {"+", kvlangBuiltinAdd, false},
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
    {"float32", kvlangBuiltinCastF32, false}, {"float64", kvlangBuiltinCastF64, false},
    {"char/utf32", kvlangBuiltinCastChar32, false}, {"char/utf8", kvlangBuiltinCastChar8, false}, {"char/ascii", kvlangBuiltinCastCharAscii, false},
    /* collection */
    {"array", kvlangBuiltinArray, false}, {"array·fill", kvlangBuiltinArrayFill, false},
    {"array·scatter", kvlangBuiltinScatter, false}, {"array·compact", kvlangBuiltinCompact, false},
    {"array·append", kvlangBuiltinAppend, false}, {"array·slice", kvlangBuiltinSlice, false},
    {"obj", kvlangBuiltinObj, true}, {"map", kvlangBuiltinMap, true}, {"struct·new", kvlangBuiltinStructNew, false},
    {"ndarray·numel", kvlangBuiltinNdarrayNumel, false}, {"ndarray·dim", kvlangBuiltinNdarrayDim, false}, {"ndarray·shape", kvlangBuiltinNdarrayShape, false},
    {"xv·at", kvlangBuiltinXvAt, false}, {"xv·set", kvlangBuiltinXvSet, false}, {"xv·reshape", kvlangBuiltinXvReshape, false},
    {"xv·reinterpret", kvlangBuiltinXvReinterpret, false},
    {"xv·langtype", kvlangBuiltinXvLangtype, false}, {"xv·bodylen", kvlangBuiltinXvBodylen, false},
    {"xv·parselangtype", kvlangBuiltinXvParselangtype, false},
    {"string·set", kvlangBuiltinStringSet, false}, {"string·char", kvlangBuiltinStringChar, false}, {"string·ord", kvlangBuiltinStringOrd, false},
    {"string·cmp", kvlangBuiltinStringCmp, false}, {"string·find", kvlangBuiltinStringFind, false}, {"string·len", kvlangBuiltinStringLen, false},
    {"string·slice", kvlangBuiltinStringSlice, false}, {"string·concat", kvlangBuiltinStringConcat, false},
    {"string·formatint", kvlangBuiltinStringFormatInt, false}, {"string·formatuint", kvlangBuiltinStringFormatUint, false},
    {"string·parseint", kvlangBuiltinStringParseInt, false}, {"string·parseuint", kvlangBuiltinStringParseUint, false},
    {"string·parsefloat", kvlangBuiltinStringParseFloat, false},
    {"time·now", kvlangBuiltinTimeNow, false}, {"time·sub", kvlangBuiltinTimeSub, false}, {"time·add", kvlangBuiltinTimeAdd, false},
    {"time/duration·nanos", kvlangBuiltinDurFrom, false}, {"time/duration·millis", kvlangBuiltinDurFrom, false},
    {"time/duration·seconds", kvlangBuiltinDurFrom, false}, {"time/duration·minutes", kvlangBuiltinDurFrom, false},
    {"time/duration·hours", kvlangBuiltinDurFrom, false},
    {"time/duration·as_nanos", kvlangBuiltinDurTo, false}, {"time/duration·as_millis", kvlangBuiltinDurTo, false},
    {"time/duration·as_seconds", kvlangBuiltinDurTo, false}, {"time/duration·as_minutes", kvlangBuiltinDurTo, false},
    {"time/duration·as_hours", kvlangBuiltinDurTo, false},
    {"time/duration·add", kvlangBuiltinDurArith, false}, {"time/duration·sub", kvlangBuiltinDurArith, false},
    {"time/duration·before", kvlangBuiltinDurCmp, false}, {"time/duration·after", kvlangBuiltinDurCmp, false},
    {"time·before", kvlangBuiltinTimeCmp, false}, {"time·after", kvlangBuiltinTimeCmp, false},
    {"random·uint64", kvlangBuiltinRandUint64, false}, {"random·int63", kvlangBuiltinRandInt63, false}, {"random·intn", kvlangBuiltinRandIntn, false},
    {"kvspace·get", kvlangCGet, false}, {"kvspace·set", kvlangCSet, false}, {"kvspace·del", kvlangCDel, false},
    {"kvspace·deltree", kvlangCDelTree, false}, {"kvspace·cp", kvlangCCp, false}, {"kvspace·cpdir", kvlangCCpTree, false}, {"kvspace·cplist", kvlangCCpList, false}, {"kvspace·list", kvlangCList, false}, {"kvspace·listlen", kvlangCListLen, false}, {"kvspace·listn", kvlangCListN, false}, {"kvspace·mkindex", kvlangCMkindex, false},
    {"kvspace·extindex", kvlangCExtIndex, false}, {"kvspace·rmindexext", kvlangCRmIndexExt, false}, {"kvspace·watch", kvlangCWatch, false}, {"kvlang·abs", kvlangCAbs, false},
    {"vthread·create", kvlangBuiltinVthreadCreate, false},
    {"vthread·run", kvlangBuiltinVthreadRun, false},
    {"vthread·call", kvlangBuiltinVthreadCall, false},
    {"vthread·sleep", kvlangBuiltinVthreadSleep, false},
    {"vthread·setstatus", kvlangBuiltinVthreadSetstatus, false},
    {"debugger", kvlangBuiltinDebugger, false},
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

/* 该算子是否需在 decode/执行时读回写参旧值（仅 obj/map）。 */
bool kvlangBuiltinLoadWriteValue(int op_id) {
    return op_id >= 0 && myrwircaps[op_id].load_write_value;
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
