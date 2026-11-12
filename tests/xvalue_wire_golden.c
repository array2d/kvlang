
#include <kvspace/kvspace_xvalue.h>
#include <kvlang/xvalue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void emit_none(void) {
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewNone(&wire, &length) == 0, "none");
    if (wire != NULL) {
        emit("none", wire, length);
        free(wire);
    }
}

static void emit_scalar_int64(void) {
    const uint8_t body[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewScalar(body, 8,
                                       "int64", &wire, &length) == 0,
            "int64");
    if (wire != NULL) {
        emit("scalar-int64", wire, length);
        free(wire);
    }
}

static void emit_scalar_bool(void) {
    const uint8_t body[1] = {1};
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewScalar(body, 1, "bool",
                                       &wire, &length) == 0,
            "bool");
    if (wire != NULL) {
        emit("scalar-bool", wire, length);
        free(wire);
    }
}

static void emit_slack_utf8(void) {
    const uint8_t body[7] = {0xe4, 0xbd, 0xa0, 0xf0, 0x9f, 0x98, 0x80};
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewSlack(body, sizeof body, "[]char/utf8",
                                      &wire, &length) == 0,
            "utf8");
    if (wire != NULL) {
        emit("slack-utf8", wire, length);
        free(wire);
    }
}

static void emit_slack_no_langtype(void) {
    const uint8_t body[3] = {'a', 'b', 'c'};
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewSlack(body, sizeof body, "", &wire, &length) == 0,
            "no-langtype");
    if (wire != NULL) {
        emit("slack-no-langtype", wire, length);
        free(wire);
    }
}

static void emit_tensor(void) {
    const uint64_t dims[2] = {2, 3};
    uint8_t body[24];
    uint8_t *wire = NULL;
    uint64_t length = 0;

    memset(body, 0xab, sizeof body);
    REQUIRE(kvlangXvalueHeadNewTensor(dims, 2, body, sizeof body,
                                       "[2,3]float32", &wire, &length) == 0,
            "tensor");
    if (wire != NULL) {
        emit("tensor", wire, length);
        free(wire);
    }
}

static void emit_ptr(void) {
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewPtr((const uint8_t *)"/lib/f", 6, "[]char/utf8",
                                    &wire, &length) == 0,
            "ptr");
    if (wire != NULL) {
        emit("ptr", wire, length);
        free(wire);
    }
}

static void emit_ext(void) {
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadNewExt((const uint8_t *)"s3://b/k", 8, "", &wire,
                                    &length) == 0,
            "ext");
    if (wire != NULL) {
        emit("ext", wire, length);
        free(wire);
    }
}

static void emit_reserve(void) {
    uint8_t *wire = NULL;
    uint64_t length = 0;

    REQUIRE(kvlangXvalueHeadReserve(4, "[]u8",
                                     &wire, &length) == 0,
            "reserve");
    if (wire != NULL) {
        emit("reserve", wire, length);
        free(wire);
    }
}

int main(void) {
    emit_none();
    emit_scalar_int64();
    emit_scalar_bool();
    emit_slack_utf8();
    emit_slack_no_langtype();
    emit_tensor();
    emit_ptr();
    emit_ext();
    emit_reserve();
    if (failures != 0) {
        fprintf(stderr, "xvalue_wire_golden: %d construction failure(s)\n",
                failures);
        return 1;
    }
    return 0;
}
