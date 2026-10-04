#include "rwir_internal.h"

#include <unistd.h>

static int set_value(kvlangKv_t *kv, const char *key, kvlangXvalue_t *value) {
    kvlangKvPair_t pair = {(char *)key, *value};
    char err[256];
    return kvlangKvSet(kv, &pair, 1, err, sizeof err);
}

static int set_i64(kvlangKv_t *kv, const char *key, int64_t value) {
    kvlangXvalue_t xv;
    kvlangXvalueNewInt64(&xv, value);
    int rc = set_value(kv, key, &xv);
    kvlangXvalueFree(&xv);
    return rc;
}

static int64_t get_i64(kvlangKv_t *kv, const char *key) {
    kvlangXvalue_t xv;
    kvlangXvalueZero(&xv);
    kvlangKvGetOne(kv, key, &xv);
    int64_t value = kvlangScalarI64(kvlangXvalueScalar(&xv));
    kvlangXvalueFree(&xv);
    return value;
}

int main(void) {
    char dir[] = "/tmp/kvlang-param-address-XXXXXX";
    char dsn[256];
    if (!mkdtemp(dir))
        return 1;
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv)
        return 1;
    char err[256];
    if (kvlangKvMkindex(kv, "/lib/f/", 0, err, sizeof err) != 0)
        return 1;

    const uint8_t counts[5] = {1, 0, 1, 0, 0};
    const uint8_t read_def[] = {'x', 0, 'i', 'n', 't', '6', '4'};
    const uint8_t write_def[] = {'y', 0, 'i', 'n', 't', '6', '4'};
    const uint8_t op[] = {1, 0, 1, 0, 0, 'n', 'o', 'o', 'p'};
    const uint8_t operand[] = {0, 0, 0, 0, 0, '*', '[', '0', ',', '-', '1', ']'};
    const uint8_t write_operand[] = {0, 0, 0, 0, 0, 'z', 0,
                                     'i', 'n', 't', '6', '4'};
    kvlangXvalue_t anchor, read_type, write_type, opcode, read_operand,
                    write_slot;
    kvlangXvalueNewTlv(&anchor, "rwfunc", counts, sizeof counts, 1);
    kvlangXvalueNewTlv(&read_type, "def langtype", read_def, sizeof read_def, 1);
    kvlangXvalueNewTlv(&write_type, "def langtype", write_def, sizeof write_def, 1);
    kvlangXvalueNewTlv(&opcode, "rwir", op, sizeof op, 1);
    kvlangXvalueNewTlv(&read_operand, "rwir", operand, sizeof operand, 1);
    kvlangXvalueNewTlv(&write_slot, "rwir", write_operand, sizeof write_operand, 1);
    int ok = set_value(kv, "/lib/f/[0,0]", &anchor) == 0 &&
             set_value(kv, "/lib/f.[0,-1]", &read_type) == 0 &&
             set_value(kv, "/lib/f.[0,1]", &write_type) == 0 &&
             set_value(kv, "/lib/f/[1,0]", &opcode) == 0 &&
             set_value(kv, "/lib/f/[1,-1]", &read_operand) == 0 &&
             set_value(kv, "/lib/f/[1,1]", &write_slot) == 0 &&
             set_i64(kv, "/vthread/vt0/[1]/x", 5) == 0 &&
             set_i64(kv, "/vthread/vt0/[1]/y", 3) == 0;
    kvlangXvalueFree(&anchor);
    kvlangXvalueFree(&read_type);
    kvlangXvalueFree(&write_type);
    kvlangXvalueFree(&opcode);
    kvlangXvalueFree(&read_operand);
    kvlangXvalueFree(&write_slot);
    if (!ok)
        return 1;

    kvlangParam_t reads[2] = {{.name = "/lib/f"}, {.name = "x"}};
    kvlangParam_t writes[1] = {{.name = "y"}};
    kvlangRwirInst_t inst = {.reads = reads, .nr = 2, .writes = writes, .nw = 1};
    kvlangFrame_t frame = {.kv = kv, .vtid = "vt0",
                            .pc = "/vthread/vt0/[1]/[1,0]", .inst = &inst};
    if (kvlangCtlCall(&frame) != 0)
        return 1;

    kvlangRwirInst_t decoded;
    const char *pc = "/vthread/vt0/[2]/[1,0]";
    const char *base = "/vthread/vt0/[2]/";
    if (kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = decoded.nr == 1 && decoded.nw == 1 && decoded.reads[0].address &&
         strcmp(decoded.reads[0].name, "/vthread/vt0/[1]/x") == 0 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].val)) == 5 &&
         strcmp(decoded.writes[0].type, "int64") == 0 &&
         strcmp(decoded.writes[0].name, "/vthread/vt0/[2]/z") == 0;
    kvlangRwirInstFree(&decoded);
    kvlangXvalue_t physical;
    kvlangXvalueZero(&physical);
    kvlangKvGetOne(kv, "/vthread/vt0/[2]/[1,-1]", &physical);
    char *physical_target = kvlangXvaluePtrTarget(&physical);
    ok = ok && kvlangXvalueIsPtr(&physical) && physical_target &&
         strcmp(physical_target, "/vthread/vt0/[1]/x") == 0;
    free(physical_target);
    kvlangXvalueFree(&physical);
    kvlangXvalue_t extra;
    kvlangXvalueNewInt64(&extra, 99);
    ok = ok && set_value(kv, "/lib/f/[1,-2]", &extra) == 0;
    kvlangXvalueFree(&extra);
    if (!ok || kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.nr == 1;
    kvlangRwirInstFree(&decoded);

    const char *absolute = "/vthread/vt0/[2]/source";
    uint8_t absolute_operand[5 + 64] = {0};
    memcpy(absolute_operand + 5, absolute, strlen(absolute));
    const uint8_t result_operand[] = {0, 0, 0, 0, 0, 'r', 'e', 's', 'u', 'l', 't'};
    kvlangXvalue_t path_slot, result_slot, path_value;
    kvlangXvalueNewTlv(&path_slot, "rwir", absolute_operand,
                       (uint32_t)(5 + strlen(absolute)), 1);
    kvlangXvalueNewTlv(&result_slot, "rwir", result_operand,
                       sizeof result_operand, 1);
    kvlangXvalueNewTlv(&opcode, "rwir", op, sizeof op, 1);
    kvlangXvalueNewCharUtf8(&path_value, "/wrong");
    ok = ok && set_value(kv, "/lib/f/[2,0]", &opcode) == 0 &&
         set_value(kv, "/lib/f/[2,-1]", &path_slot) == 0 &&
         set_value(kv, "/lib/f/[2,1]", &result_slot) == 0 &&
         set_value(kv, absolute, &path_value) == 0;
    kvlangXvalueFree(&path_slot);
    kvlangXvalueFree(&result_slot);
    kvlangXvalueFree(&opcode);
    kvlangXvalueFree(&path_value);
    const char *path_pc = "/vthread/vt0/[2]/[2,0]";
    if (!ok || kvlangRwirDecode(kv, base, path_pc, &decoded, err, sizeof err) != 0)
        return 1;
    kvlangFrame_t get_frame = {.kv = kv, .vtid = "vt0", .pc = path_pc,
                               .inst = &decoded};
    ok = ok && kvlangCGet(&get_frame) == 0;
    kvlangRwirInstFree(&decoded);
    kvlangXvalue_t result;
    kvlangXvalueZero(&result);
    kvlangKvGetOne(kv, "/vthread/vt0/[2]/result", &result);
    char *result_text = kvlangXvalueValueString(&result);
    ok = ok && strcmp(result_text, "/wrong") == 0;
    free(result_text);
    kvlangXvalueFree(&result);

    const uint8_t any_operand[] = {0, 0, 0, 0, 0, 's', 'o', 'u', 'r', 'c', 'e',
                                   0, 'a', 'n', 'y'};
    kvlangXvalue_t any_slot;
    kvlangXvalueNewTlv(&any_slot, "rwir", any_operand, sizeof any_operand, 1);
    const uint8_t one_read_op[] = {1, 0, 0, 0, 0, 'n', 'o', 'o', 'p'};
    kvlangXvalue_t one_read_opcode;
    kvlangXvalueNewTlv(&one_read_opcode, "rwir", one_read_op,
                       sizeof one_read_op, 1);
    ok = ok && set_value(kv, "/lib/f/[3,0]", &one_read_opcode) == 0 &&
         set_value(kv, "/lib/f/[3,-1]", &any_slot) == 0;
    kvlangXvalueFree(&one_read_opcode);
    kvlangXvalueFree(&any_slot);
    const char *any_pc = "/vthread/vt0/[2]/[3,0]";
    if (!ok || kvlangRwirDecode(kv, base, any_pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.nr == 1 && strcmp(decoded.reads[0].type, "any") == 0;
    kvlangRwirInstFree(&decoded);
    ok = ok && set_i64(kv, absolute, 17) == 0;
    if (kvlangRwirDecode(kv, base, any_pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.nr == 1 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].val)) == 17;
    kvlangRwirInstFree(&decoded);

    kvlangXvalue_t outer, inner;
    kvlangXvalueZero(&outer);
    kvlangXvalueZero(&inner);
    kvlangKvGetOne(kv, "/vthread/vt0/[2]/[0,-1]", &outer);
    if (!kvlangXvalueIsPtr(&outer))
        return 1;
    char *middle = kvlangXvaluePtrTarget(&outer);
    kvlangXvalueFree(&outer);
    kvlangKvGetOne(kv, middle, &inner);
    if (!kvlangXvalueIsPtr(&inner))
        return 1;
    char *source = kvlangXvaluePtrTarget(&inner);
    kvlangXvalueFree(&inner);
    ok = ok && strcmp(source, "/vthread/vt0/[1]/x") == 0;
    free(middle);
    free(source);

    kvlangXvalue_t got;
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangScalarI64(kvlangXvalueScalar(&got)) == 5;
    kvlangXvalueFree(&got);
    ok = ok && set_i64(kv, "/vthread/vt0/[1]/x", 9) == 0;
    kvlangKvDisconnect(kv);
    kv = kvlangKvConnect(dsn);
    if (!kv)
        return 1;
    frame.kv = kv;
    if (kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.nr == 1 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].val)) == 9;
    kvlangRwirInstFree(&decoded);
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangScalarI64(kvlangXvalueScalar(&got)) == 9;
    kvlangXvalueFree(&got);

    char *dest = kvlangBuiltinResolveWriteSlot(kv, "/vthread/vt0/[2]", "*[0,1]");
    ok = ok && strcmp(dest, "/vthread/vt0/[1]/y") == 0;
    ok = ok && set_i64(kv, dest, 11) == 0;
    ok = ok && get_i64(kv, "/vthread/vt0/[1]/y") == 11;
    free(dest);

    reads[1].name = "null";
    if (kvlangCtlCall(&frame) != 0)
        return 1;
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangXvalueNone(&got);
    kvlangXvalueFree(&got);

    kvlangFrame_t child = {.kv = kv, .vtid = "vt0",
                            .pc = "/vthread/vt0/[2]/[1,0]"};
    ok = ok && kvlangCtlReturn(&child) == 0 && !child.persist_failed;
    kvlangKvGetOne(kv, "/vthread/vt0/‥pc", &got);
    char *resumed = kvlangXvalueValueString(&got);
    ok = ok && strcmp(resumed, "/vthread/vt0/[1]/[2,0]") == 0;
    free(resumed);
    kvlangXvalueFree(&got);
    kvlangKvGetOne(kv, "/vthread/vt0/[2]/[0,-1]", &got);
    ok = ok && kvlangXvalueNone(&got);
    kvlangXvalueFree(&got);
    kvlangKvDisconnect(kv);
    return ok ? 0 : 1;
}
