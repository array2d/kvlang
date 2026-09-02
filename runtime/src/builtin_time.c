// builtin_time —— time·* / time/duration·* 时间与时长 rwir

#include "builtin_internal.h"
#include <time.h>

static int64_t now_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void new_time(kvlangXvalue_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_TIME, r, 8, 1);
}

static void new_duration(kvlangXvalue_t *v, int64_t ns) {
    uint8_t r[8]; memcpy(r, &ns, 8);
    kvlangXvalueNewTlv(v, KVSPACE_KIND_DURATION, r, 8, 1);
}

int kvlangBuiltinTimeNow(kvlangFrame_t *f) {
    kvlangXvalue_t e; new_time(&e, now_nanos());
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinTimeSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time.sub requires 2 time args"); }
    kvlangXvalue_t e; new_duration(&e, kvlangXvalueAsInt64(&in[0]) - kvlangXvalueAsInt64(&in[1]));
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinTimeAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time.add requires time and duration"); }
    kvlangXvalue_t e; new_time(&e, kvlangXvalueAsInt64(&in[0]) + kvlangXvalueAsInt64(&in[1]));
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int64_t dur_scale(const char *op) {
    if (strstr(op, "nanos")) return 1;
    if (strstr(op, "millis")) return 1000000;
    if (strstr(op, "seconds")) return 1000000000;
    if (strstr(op, "minutes")) return 60000000000LL;
    if (strstr(op, "hours")) return 3600000000000LL;
    return 1;
}

int kvlangBuiltinDurFrom(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time/duration.from requires 1 int64 arg"); }
    kvlangXvalue_t e; new_duration(&e, kvlangXvalueAsInt64(&in[0]) * dur_scale(f->inst->opcode));
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurTo(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time/duration.as requires 1 duration arg"); }
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, kvlangXvalueAsInt64(&in[0]) / dur_scale(f->inst->opcode));
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurArith(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time/duration arith requires 2 duration args"); }
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    bool sub = strstr(f->inst->opcode, ".sub") != NULL;
    kvlangXvalue_t e; new_duration(&e, sub ? a - b : a + b);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time/duration cmp requires 2 duration args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    kvlangXvalue_t e; kvlangXvalueNewBool(&e, before ? a < b : a > b);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinTimeCmp(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: time cmp requires 2 args"); }
    bool before = strstr(f->inst->opcode, "before") != NULL;
    int64_t a = kvlangXvalueAsInt64(&in[0]), b = kvlangXvalueAsInt64(&in[1]);
    kvlangXvalue_t e; kvlangXvalueNewBool(&e, before ? a < b : a > b);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
