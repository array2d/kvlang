#include "rwir_internal.h"

#include <unistd.h>

static int set_value(kvspace_t *kv, const char *key, kvlangXvalue_t *value) {
    kvlangKvPair_t pair = {(char *)key, *value};
    char err[256];
    return kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
}

static int set_i64(kvspace_t *kv, const char *key, int64_t value) {
    kvlangXvalue_t xv;
    kvlangXvalueNewInt64(&xv, value);
    int rc = set_value(kv, key, &xv);
    return rc;
}

static int64_t get_i64(kvspace_t *kv, const char *key) {
    kvlangXvalue_t xv;
    kvlangXvalueZero(&xv);
    kvlangKvspaceGetOne(kv, key, &xv);
    int64_t value = kvlangScalarI64(kvlangXvalueScalar(&xv));
    return value;
}

int main(void) {
    char dir[] = "/tmp/kvlang-param-address-XXXXXX";
    char dsn[256];
    if (!mkdtemp(dir))
        return 1;
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        return 1;
    char err[256];
    if (kvlangKvspaceMakeIndex(kv, "/lib/f/", 0, err, sizeof err) != 0)
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
    kvlangXvalueNew(&anchor, "rwfunc", counts, sizeof counts, 1);
    kvlangXvalueNew(&read_type, "def langtype", read_def, sizeof read_def, 1);
    kvlangXvalueNew(&write_type, "def langtype", write_def, sizeof write_def, 1);
    kvlangXvalueNew(&opcode, "rwir", op, sizeof op, 1);
    kvlangXvalueNew(&read_operand, "rwir", operand, sizeof operand, 1);
    kvlangXvalueNew(&write_slot, "rwir", write_operand, sizeof write_operand, 1);
    int ok = set_value(kv, "/lib/f/[0,0]", &anchor) == 0 &&
             set_value(kv, "/lib/f.[0,-1]", &read_type) == 0 &&
             set_value(kv, "/lib/f.[0,1]", &write_type) == 0 &&
             set_value(kv, "/lib/f/[1,0]", &opcode) == 0 &&
             set_value(kv, "/lib/f/[1,-1]", &read_operand) == 0 &&
             set_value(kv, "/lib/f/[1,1]", &write_slot) == 0 &&
             set_i64(kv, "/vthread/vt0/[1]/x", 5) == 0 &&
             set_i64(kv, "/vthread/vt0/[1]/y", 3) == 0;
    if (!ok)
        return 1;

    kvlangParam_t reads[2] = {{.name = "/lib/f"}, {.name = "x"}};
    kvlangParam_t writes[1] = {{.name = "y"}};
    kvlangRwirInst_t inst = {.reads = reads, .read_count = 2, .writes = writes, .write_count = 1};
    kvlangFrame_t frame = {.kvspace = kv, .vthread_id = "vt0",
                            .pc = "/vthread/vt0/[1]/[1,0]", .instruction = &inst};
    if (kvlangControlCall(&frame) != 0)
        return 1;

    kvlangRwirInst_t decoded;
    const char *pc = "/vthread/vt0/[2]/[1,0]";
    const char *base = "/vthread/vt0/[2]/";
    if (kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = decoded.read_count == 1 && decoded.write_count == 1 && decoded.reads[0].is_address &&
         strcmp(decoded.reads[0].name, "/vthread/vt0/[1]/x") == 0 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].value)) == 5 &&
         strcmp(decoded.writes[0].type, "int64") == 0 &&
         strcmp(decoded.writes[0].name, "/vthread/vt0/[2]/z") == 0;
    kvlangRwirInstructionFree(&decoded);
    kvlangXvalue_t physical;
    kvlangXvalueZero(&physical);
    kvlangKvspaceGetOne(kv, "/vthread/vt0/[2]/[1,-1]", &physical);
    char *physical_target = kvlangXvaluePtrTarget(&physical);
    ok = ok && kvlangXvalueIsPtr(&physical) && physical_target &&
         strcmp(physical_target, "/vthread/vt0/[1]/x") == 0;
    free(physical_target);
    kvlangXvalue_t extra;
    kvlangXvalueNewInt64(&extra, 99);
    ok = ok && set_value(kv, "/lib/f/[1,-2]", &extra) == 0;
    if (!ok || kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.read_count == 1;
    kvlangRwirInstructionFree(&decoded);

    const char *absolute = "/vthread/vt0/[2]/source";
    uint8_t absolute_operand[5 + 64] = {0};
    memcpy(absolute_operand + 5, absolute, strlen(absolute));
    const uint8_t result_operand[] = {0, 0, 0, 0, 0, 'r', 'e', 's', 'u', 'l', 't'};
    kvlangXvalue_t path_slot, result_slot, path_value;
    kvlangXvalueNew(&path_slot, "rwir", absolute_operand,
                       (uint32_t)(5 + strlen(absolute)), 1);
    kvlangXvalueNew(&result_slot, "rwir", result_operand,
                       sizeof result_operand, 1);
    kvlangXvalueNew(&opcode, "rwir", op, sizeof op, 1);
    kvlangXvalueNewCharUtf8(&path_value, "/wrong");
    ok = ok && set_value(kv, "/lib/f/[2,0]", &opcode) == 0 &&
         set_value(kv, "/lib/f/[2,-1]", &path_slot) == 0 &&
         set_value(kv, "/lib/f/[2,1]", &result_slot) == 0 &&
         set_value(kv, absolute, &path_value) == 0;
    const char *path_pc = "/vthread/vt0/[2]/[2,0]";
    if (!ok || kvlangRwirDecode(kv, base, path_pc, &decoded, err, sizeof err) != 0)
        return 1;
    kvlangFrame_t get_frame = {.kvspace = kv, .vthread_id = "vt0", .pc = path_pc,
                               .instruction = &decoded};
    ok = ok && kvlangBuiltinKvspaceGet(&get_frame) == 0;
    kvlangRwirInstructionFree(&decoded);
    kvlangXvalue_t result;
    kvlangXvalueZero(&result);
    kvlangKvspaceGetOne(kv, "/vthread/vt0/[2]/result", &result);
    char *result_text = kvlangXvalueValueString(&result);
    ok = ok && strcmp(result_text, "/wrong") == 0;
    free(result_text);

    const uint8_t any_operand[] = {0, 0, 0, 0, 0, 's', 'o', 'u', 'r', 'c', 'e',
                                   0, 'a', 'n', 'y'};
    kvlangXvalue_t any_slot;
    kvlangXvalueNew(&any_slot, "rwir", any_operand, sizeof any_operand, 1);
    const uint8_t one_read_op[] = {1, 0, 0, 0, 0, 'n', 'o', 'o', 'p'};
    kvlangXvalue_t one_read_opcode;
    kvlangXvalueNew(&one_read_opcode, "rwir", one_read_op,
                       sizeof one_read_op, 1);
    ok = ok && set_value(kv, "/lib/f/[3,0]", &one_read_opcode) == 0 &&
         set_value(kv, "/lib/f/[3,-1]", &any_slot) == 0;
    const char *any_pc = "/vthread/vt0/[2]/[3,0]";
    if (!ok || kvlangRwirDecode(kv, base, any_pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.read_count == 1 && strcmp(decoded.reads[0].type, "any") == 0;
    kvlangRwirInstructionFree(&decoded);
    ok = ok && set_i64(kv, absolute, 17) == 0;
    if (kvlangRwirDecode(kv, base, any_pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.read_count == 1 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].value)) == 17;
    kvlangRwirInstructionFree(&decoded);

    kvlangXvalue_t outer, inner;
    kvlangXvalueZero(&outer);
    kvlangXvalueZero(&inner);
    kvlangKvspaceGetOne(kv, "/vthread/vt0/[2]/[0,-1]", &outer);
    if (!kvlangXvalueIsPtr(&outer))
        return 1;
    char *middle = kvlangXvaluePtrTarget(&outer);
    kvlangKvspaceGetOne(kv, middle, &inner);
    if (!kvlangXvalueIsPtr(&inner))
        return 1;
    char *source = kvlangXvaluePtrTarget(&inner);
    ok = ok && strcmp(source, "/vthread/vt0/[1]/x") == 0;
    free(middle);
    free(source);

    kvlangXvalue_t got;
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangScalarI64(kvlangXvalueScalar(&got)) == 5;
    ok = ok && set_i64(kv, "/vthread/vt0/[1]/x", 9) == 0;
    kvlangKvspaceDisconnect(kv);
    kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        return 1;
    frame.kvspace = kv;
    if (kvlangRwirDecode(kv, base, pc, &decoded, err, sizeof err) != 0)
        return 1;
    ok = ok && decoded.read_count == 1 &&
         kvlangScalarI64(kvlangXvalueScalar(&decoded.reads[0].value)) == 9;
    kvlangRwirInstructionFree(&decoded);
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangScalarI64(kvlangXvalueScalar(&got)) == 9;

    char *dest = kvlangBuiltinResolveWriteSlot(kv, "/vthread/vt0/[2]", "*[0,1]");
    ok = ok && strcmp(dest, "/vthread/vt0/[1]/y") == 0;
    ok = ok && set_i64(kv, dest, 11) == 0;
    ok = ok && get_i64(kv, "/vthread/vt0/[1]/y") == 11;
    free(dest);

    reads[1].name = "null";
    if (kvlangControlCall(&frame) != 0)
        return 1;
    kvlangBuiltinResolveReadValue(kv, "/vthread/vt0/[2]", "*[0,-1]", NULL, &got);
    ok = ok && kvlangXvalueNone(&got);

    kvlangFrame_t child = {.kvspace = kv, .vthread_id = "vt0",
                            .pc = "/vthread/vt0/[2]/[1,0]"};
    ok = ok && kvlangControlReturn(&child) == 0 && !child.persist_failed;
    kvlangKvspaceGetOne(kv, "/vthread/vt0/‥pc", &got);
    char *resumed = kvlangXvalueValueString(&got);
    ok = ok && strcmp(resumed, "/vthread/vt0/[1]/[2,0]") == 0;
    free(resumed);
    kvlangKvspaceGetOne(kv, "/vthread/vt0/[2]/[0,-1]", &got);
    ok = ok && kvlangXvalueNone(&got);
    kvlangKvspaceDisconnect(kv);
    return ok ? 0 : 1;
}
