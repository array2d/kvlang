// rwir_time —— time·* / time/duration·* 时间与时长 rwir

#include "rwir_internal.h"
#include <time.h>

static int64_t now_nanoseconds(void) {
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
    kvlangXvalue_t e; new_time(&e, now_nanoseconds());
    int rc = kvlangBuiltinEmitResult(f, &e);
    return rc;
}

int kvlangBuiltinTimeSub(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time.sub requires 2 time args"); }
    kvlangXvalue_t e; new_duration(&e, kvlangScalarI64(kvlangXvalueScalar(&in[0])) - kvlangScalarI64(kvlangXvalueScalar(&in[1])));
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinTimeAdd(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time.add requires time and duration"); }
    kvlangXvalue_t e; new_time(&e, kvlangScalarI64(kvlangXvalueScalar(&in[0])) + kvlangScalarI64(kvlangXvalueScalar(&in[1])));
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

static int64_t duration_scale(const char *op) {
    if (strstr(op, "nanos")) return 1;
    if (strstr(op, "millis")) return 1000000;
    if (strstr(op, "seconds")) return 1000000000;
    if (strstr(op, "minutes")) return 60000000000LL;
    if (strstr(op, "hours")) return 3600000000000LL;
    return 1;
}

int kvlangBuiltinDurationFrom(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time/duration.from requires 1 int64 arg"); }
    kvlangXvalue_t e; new_duration(&e, kvlangScalarI64(kvlangXvalueScalar(&in[0])) * duration_scale(f->instruction->opcode));
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurationTo(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time/duration.as requires 1 duration arg"); }
    int rc = kvlangBuiltinEmitInt64(f, kvlangScalarI64(kvlangXvalueScalar(&in[0])) / duration_scale(f->instruction->opcode));kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurationArith(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time/duration arith requires 2 duration args"); }
    int64_t a = kvlangScalarI64(kvlangXvalueScalar(&in[0])), b = kvlangScalarI64(kvlangXvalueScalar(&in[1]));
    bool sub = strstr(f->instruction->opcode, MEMBER_SEP "sub") != NULL;
    kvlangXvalue_t e; new_duration(&e, sub ? a - b : a + b);
    int rc = kvlangBuiltinEmitResult(f, &e);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinDurCompare(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time/duration cmp requires 2 duration args"); }
    bool before = strstr(f->instruction->opcode, "before") != NULL;
    int64_t a = kvlangScalarI64(kvlangXvalueScalar(&in[0])), b = kvlangScalarI64(kvlangXvalueScalar(&in[1]));
    int rc = kvlangBuiltinEmitBool(f, before ? a < b : a > b);kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinTimeCompare(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 2) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetError(f, "TypeError: time cmp requires 2 args"); }
    bool before = strstr(f->instruction->opcode, "before") != NULL;
    int64_t a = kvlangScalarI64(kvlangXvalueScalar(&in[0])), b = kvlangScalarI64(kvlangXvalueScalar(&in[1]));
    int rc = kvlangBuiltinEmitBool(f, before ? a < b : a > b);kvlangBuiltinFreeInputs(in, n);
    return rc;
}
