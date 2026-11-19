
#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>
#include <kvlangxvalue/kvlangxvalue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *store;
static unsigned key_sequence;

static const char *next_key(void) {
    static char buffer[32];
    snprintf(buffer, sizeof buffer, "/golden/%u", key_sequence++);
    return buffer;
}

static uint64_t wire_total(const uint8_t *wire) {
    return kvspaceXvalueHeadTotalLen((const kvspaceXvalueHead *)(const void *)wire);
}

static int failures;

#define REQUIRE(condition, name)                                              \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "golden: %s failed\n", (name));                   \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static void emit(const char *name, const uint8_t *wire, uint64_t length) {
    printf("%s %llu ", name, (unsigned long long)length);
    for (uint64_t index = 0; index < length; ++index) {
        printf("%02x", wire[index]);
    }
    printf("\n");
}

static void emit_scalar_int64(void) {
    const uint8_t body[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *wire = kvlangXvalueNewT(store, next_key(), body, 8, "int64");
    REQUIRE(wire != NULL, "int64");
    if (wire != NULL)
        emit("scalar-int64", wire, wire_total(wire));
}

static void emit_scalar_bool(void) {
    const uint8_t body[1] = {1};
    uint8_t *wire = kvlangXvalueNewT(store, next_key(), body, 1, "bool");
    REQUIRE(wire != NULL, "bool");
    if (wire != NULL)
        emit("scalar-bool", wire, wire_total(wire));
}

static void emit_slack_utf8(void) {
    const uint8_t body[7] = {0xe4, 0xbd, 0xa0, 0xf0, 0x9f, 0x98, 0x80};
    uint8_t *wire = kvlangXvalueNewBufferArray(store, next_key(), body,
                                               sizeof body, "[]char/utf8");
    REQUIRE(wire != NULL, "utf8");
    if (wire != NULL)
        emit("slack-utf8", wire, wire_total(wire));
}

static void emit_slack_no_langtype(void) {
    const uint8_t body[3] = {'a', 'b', 'c'};
    uint8_t *wire = kvlangXvalueNewT(store, next_key(), body,
                                     sizeof body, "");
    REQUIRE(wire != NULL, "no-langtype");
    if (wire != NULL)
        emit("slack-no-langtype", wire, wire_total(wire));
}

static void emit_tensor(void) {
    const uint64_t dims[2] = {2, 3};
    uint8_t body[24];
    memset(body, 0xab, sizeof body);
    uint8_t *wire = kvlangXvalueNewCompactArray(store, next_key(), dims, 2, body,
                                                sizeof body, "[2,3]float32");
    REQUIRE(wire != NULL, "tensor");
    if (wire != NULL)
        emit("tensor", wire, wire_total(wire));
}

static void emit_ptr(void) {
    uint8_t *wire = kvlangXvalueNewPtr(store, next_key(),
                                       (const uint8_t *)"/lib/f", 6,
                                       "[]char/utf8");
    REQUIRE(wire != NULL, "ptr");
    if (wire != NULL)
        emit("ptr", wire, wire_total(wire));
}

static void emit_reserve(void) {
    uint8_t *wire = kvlangXvalueHeadReserve(store, next_key(), 4, "[]u8");
    REQUIRE(wire != NULL, "reserve");
    if (wire != NULL)
        emit("reserve", wire, wire_total(wire));
}

int main(void) {
    char dsn[128];
    char path[128];
    snprintf(path, sizeof path, "/tmp/kvlangxvalue-golden-%ld", (long)getpid());
    snprintf(dsn, sizeof dsn, "shm://%s", path);
    unlink(path);
    store = kvspaceConnect(dsn);
    if (!store) {
        fprintf(stderr, "cannot connect %s\n", dsn);
        return 1;
    }
    emit_scalar_int64();
    emit_scalar_bool();
    emit_slack_utf8();
    emit_slack_no_langtype();
    emit_tensor();
    emit_ptr();
    emit_reserve();
    kvspaceClose(store);
    unlink(path);
    if (failures != 0) {
        fprintf(stderr, "xvalue_wire_golden: %d construction failure(s)\n",
                failures);
        return 1;
    }
    return 0;
}
