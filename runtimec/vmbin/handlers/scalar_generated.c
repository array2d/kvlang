
#include "vmbin/handlers/scalar_generated.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "vmbin/handlers/handler_call.h"
#include "vmbin/handlers/scalar_character.h"
#include "vmbin/handlers/scalar_special.h"

static inline void scalar_generated_add_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_float32_float32, scalar_generated_add_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_add_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_float64_float64, scalar_generated_add_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_add_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_int16_float32, scalar_generated_add_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_add_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_int32_float32, scalar_generated_add_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_add_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_int64_float32, scalar_generated_add_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_add_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_int8_float32, scalar_generated_add_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_add_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_uint16_float32, scalar_generated_add_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_add_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_uint32_float32, scalar_generated_add_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_add_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_uint64_float32, scalar_generated_add_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_add_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float32_uint8_float32, scalar_generated_add_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_add_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_float32_float64, scalar_generated_add_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_add_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_float64_float64, scalar_generated_add_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_add_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_int16_float64, scalar_generated_add_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_add_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_int32_float64, scalar_generated_add_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_add_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_int64_float64, scalar_generated_add_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_add_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_int8_float64, scalar_generated_add_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_add_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_uint16_float64, scalar_generated_add_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_add_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_uint32_float64, scalar_generated_add_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_add_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_uint64_float64, scalar_generated_add_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_add_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_float64_uint8_float64, scalar_generated_add_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_add_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_float32_float32, scalar_generated_add_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_add_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_float64_float64, scalar_generated_add_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_add_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a + (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_int16_int16, scalar_generated_add_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_add_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a + (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_int32_int32, scalar_generated_add_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_add_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_int64_int64, scalar_generated_add_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_add_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a + (uint16_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int16_int8_int16, scalar_generated_add_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_add_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_float32_float32, scalar_generated_add_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_add_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_float64_float64, scalar_generated_add_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_add_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a + (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_int16_int32, scalar_generated_add_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_add_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a + (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_int32_int32, scalar_generated_add_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_add_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_int64_int64, scalar_generated_add_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_add_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a + (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int32_int8_int32, scalar_generated_add_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_add_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_float32_float32, scalar_generated_add_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_add_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_float64_float64, scalar_generated_add_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_add_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_int16_int64, scalar_generated_add_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_add_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_int32_int64, scalar_generated_add_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_add_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_int64_int64, scalar_generated_add_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_add_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int64_int8_int64, scalar_generated_add_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_add_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_float32_float32, scalar_generated_add_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_add_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_float64_float64, scalar_generated_add_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_add_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int64_t)*a + (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_int16_int16, scalar_generated_add_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_add_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a + (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_int32_int32, scalar_generated_add_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_add_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a + (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_int64_int64, scalar_generated_add_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_add_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((uint8_t)(int8_t)*a + (uint8_t)(int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_int8_int8_int8, scalar_generated_add_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_add_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_float32_float32, scalar_generated_add_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_add_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_float64_float64, scalar_generated_add_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_add_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a + (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_uint16_uint16, scalar_generated_add_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_add_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a + (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_uint32_uint32, scalar_generated_add_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_add_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a + (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_uint64_uint64, scalar_generated_add_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_add_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint16_uint8_uint16, scalar_generated_add_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_add_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_float32_float32, scalar_generated_add_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_add_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_float64_float64, scalar_generated_add_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_add_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_uint16_uint32, scalar_generated_add_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_add_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a + (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_uint32_uint32, scalar_generated_add_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_add_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a + (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_uint64_uint64, scalar_generated_add_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_add_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint32_uint8_uint32, scalar_generated_add_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_add_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_float32_float32, scalar_generated_add_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_add_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_float64_float64, scalar_generated_add_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_add_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_uint16_uint64, scalar_generated_add_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_add_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_uint32_uint64, scalar_generated_add_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_add_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a + (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_uint64_uint64, scalar_generated_add_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_add_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a + (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint64_uint8_uint64, scalar_generated_add_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_add_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = (float)*a + (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_float32_float32, scalar_generated_add_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_add_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = (double)*a + (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_float64_float64, scalar_generated_add_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_add_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((int64_t)*a + (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_uint16_uint16, scalar_generated_add_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_add_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a + (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_uint32_uint32, scalar_generated_add_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_add_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a + (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_uint64_uint64, scalar_generated_add_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_add_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a + (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_add_uint8_uint8_uint8, scalar_generated_add_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_sub_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_float32_float32, scalar_generated_sub_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_sub_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_float64_float64, scalar_generated_sub_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_sub_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_int16_float32, scalar_generated_sub_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_sub_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_int32_float32, scalar_generated_sub_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_sub_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_int64_float32, scalar_generated_sub_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_sub_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_int8_float32, scalar_generated_sub_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_sub_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_uint16_float32, scalar_generated_sub_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_sub_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_uint32_float32, scalar_generated_sub_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_sub_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_uint64_float32, scalar_generated_sub_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_sub_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float32_uint8_float32, scalar_generated_sub_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_sub_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_float32_float64, scalar_generated_sub_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_sub_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_float64_float64, scalar_generated_sub_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_sub_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_int16_float64, scalar_generated_sub_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_sub_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_int32_float64, scalar_generated_sub_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_sub_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_int64_float64, scalar_generated_sub_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_sub_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_int8_float64, scalar_generated_sub_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_sub_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_uint16_float64, scalar_generated_sub_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_sub_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_uint32_float64, scalar_generated_sub_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_sub_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_uint64_float64, scalar_generated_sub_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_sub_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_float64_uint8_float64, scalar_generated_sub_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_sub_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_float32_float32, scalar_generated_sub_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_sub_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_float64_float64, scalar_generated_sub_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_sub_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a - (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_int16_int16, scalar_generated_sub_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_sub_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a - (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_int32_int32, scalar_generated_sub_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_sub_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_int64_int64, scalar_generated_sub_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_sub_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a - (uint16_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int16_int8_int16, scalar_generated_sub_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_sub_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_float32_float32, scalar_generated_sub_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_sub_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_float64_float64, scalar_generated_sub_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_sub_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a - (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_int16_int32, scalar_generated_sub_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_sub_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a - (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_int32_int32, scalar_generated_sub_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_sub_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_int64_int64, scalar_generated_sub_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_sub_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a - (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int32_int8_int32, scalar_generated_sub_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_sub_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_float32_float32, scalar_generated_sub_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_sub_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_float64_float64, scalar_generated_sub_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_sub_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_int16_int64, scalar_generated_sub_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_sub_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_int32_int64, scalar_generated_sub_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_sub_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_int64_int64, scalar_generated_sub_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_sub_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int64_int8_int64, scalar_generated_sub_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_sub_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_float32_float32, scalar_generated_sub_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_sub_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_float64_float64, scalar_generated_sub_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_sub_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int64_t)*a - (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_int16_int16, scalar_generated_sub_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_sub_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a - (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_int32_int32, scalar_generated_sub_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_sub_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a - (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_int64_int64, scalar_generated_sub_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_sub_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((uint8_t)(int8_t)*a - (uint8_t)(int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_int8_int8_int8, scalar_generated_sub_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_sub_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_float32_float32, scalar_generated_sub_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_sub_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_float64_float64, scalar_generated_sub_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_sub_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a - (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_uint16_uint16, scalar_generated_sub_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_sub_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a - (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_uint32_uint32, scalar_generated_sub_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_sub_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a - (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_uint64_uint64, scalar_generated_sub_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_sub_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint16_uint8_uint16, scalar_generated_sub_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_sub_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_float32_float32, scalar_generated_sub_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_sub_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_float64_float64, scalar_generated_sub_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_sub_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_uint16_uint32, scalar_generated_sub_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_sub_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a - (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_uint32_uint32, scalar_generated_sub_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_sub_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a - (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_uint64_uint64, scalar_generated_sub_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_sub_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint32_uint8_uint32, scalar_generated_sub_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_sub_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_float32_float32, scalar_generated_sub_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_sub_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_float64_float64, scalar_generated_sub_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_sub_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_uint16_uint64, scalar_generated_sub_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_sub_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_uint32_uint64, scalar_generated_sub_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_sub_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a - (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_uint64_uint64, scalar_generated_sub_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_sub_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a - (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint64_uint8_uint64, scalar_generated_sub_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_sub_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = (float)*a - (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_float32_float32, scalar_generated_sub_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_sub_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = (double)*a - (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_float64_float64, scalar_generated_sub_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_sub_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((int64_t)*a - (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_uint16_uint16, scalar_generated_sub_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_sub_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a - (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_uint32_uint32, scalar_generated_sub_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_sub_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a - (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_uint64_uint64, scalar_generated_sub_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_sub_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a - (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_sub_uint8_uint8_uint8, scalar_generated_sub_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_mul_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_float32_float32, scalar_generated_mul_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_mul_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_float64_float64, scalar_generated_mul_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_mul_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_int16_float32, scalar_generated_mul_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_mul_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_int32_float32, scalar_generated_mul_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_mul_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_int64_float32, scalar_generated_mul_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_mul_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_int8_float32, scalar_generated_mul_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_mul_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_uint16_float32, scalar_generated_mul_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_mul_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_uint32_float32, scalar_generated_mul_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_mul_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_uint64_float32, scalar_generated_mul_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_mul_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float32_uint8_float32, scalar_generated_mul_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_mul_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_float32_float64, scalar_generated_mul_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_mul_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_float64_float64, scalar_generated_mul_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_mul_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_int16_float64, scalar_generated_mul_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_mul_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_int32_float64, scalar_generated_mul_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_mul_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_int64_float64, scalar_generated_mul_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_mul_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_int8_float64, scalar_generated_mul_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_mul_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_uint16_float64, scalar_generated_mul_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_mul_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_uint32_float64, scalar_generated_mul_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_mul_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_uint64_float64, scalar_generated_mul_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_mul_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_float64_uint8_float64, scalar_generated_mul_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_mul_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_float32_float32, scalar_generated_mul_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_mul_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_float64_float64, scalar_generated_mul_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_mul_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a * (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_int16_int16, scalar_generated_mul_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_mul_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a * (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_int32_int32, scalar_generated_mul_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_mul_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_int64_int64, scalar_generated_mul_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_mul_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int16_t)*a * (uint16_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int16_int8_int16, scalar_generated_mul_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_mul_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_float32_float32, scalar_generated_mul_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_mul_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_float64_float64, scalar_generated_mul_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_mul_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a * (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_int16_int32, scalar_generated_mul_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_mul_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a * (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_int32_int32, scalar_generated_mul_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_mul_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_int64_int64, scalar_generated_mul_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_mul_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int32_t)*a * (uint32_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int32_int8_int32, scalar_generated_mul_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_mul_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_float32_float32, scalar_generated_mul_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_mul_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_float64_float64, scalar_generated_mul_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_mul_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_int16_int64, scalar_generated_mul_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_mul_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_int32_int64, scalar_generated_mul_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_mul_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_int64_int64, scalar_generated_mul_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_mul_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int64_int8_int64, scalar_generated_mul_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_mul_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_float32_float32, scalar_generated_mul_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_mul_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_float64_float64, scalar_generated_mul_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_mul_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((uint16_t)(int64_t)*a * (uint16_t)(int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_int16_int16, scalar_generated_mul_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_mul_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((uint32_t)(int64_t)*a * (uint32_t)(int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_int32_int32, scalar_generated_mul_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_mul_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((uint64_t)(int64_t)*a * (uint64_t)(int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_int64_int64, scalar_generated_mul_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_mul_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((uint8_t)(int8_t)*a * (uint8_t)(int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_int8_int8_int8, scalar_generated_mul_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_mul_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_float32_float32, scalar_generated_mul_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_mul_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_float64_float64, scalar_generated_mul_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_mul_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a * (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_uint16_uint16, scalar_generated_mul_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_mul_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a * (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_uint32_uint32, scalar_generated_mul_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_mul_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a * (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_uint64_uint64, scalar_generated_mul_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_mul_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint16_uint8_uint16, scalar_generated_mul_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_mul_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_float32_float32, scalar_generated_mul_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_mul_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_float64_float64, scalar_generated_mul_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_mul_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_uint16_uint32, scalar_generated_mul_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_mul_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a * (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_uint32_uint32, scalar_generated_mul_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_mul_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a * (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_uint64_uint64, scalar_generated_mul_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_mul_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint32_uint8_uint32, scalar_generated_mul_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_mul_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_float32_float32, scalar_generated_mul_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_mul_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_float64_float64, scalar_generated_mul_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_mul_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_uint16_uint64, scalar_generated_mul_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_mul_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_uint32_uint64, scalar_generated_mul_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_mul_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a * (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_uint64_uint64, scalar_generated_mul_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_mul_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a * (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint64_uint8_uint64, scalar_generated_mul_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_mul_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = (float)*a * (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_float32_float32, scalar_generated_mul_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_mul_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = (double)*a * (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_float64_float64, scalar_generated_mul_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_mul_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((int64_t)*a * (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_uint16_uint16, scalar_generated_mul_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_mul_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((int64_t)*a * (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_uint32_uint32, scalar_generated_mul_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_mul_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((int64_t)*a * (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_uint64_uint64, scalar_generated_mul_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_mul_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a * (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_mul_uint8_uint8_uint8, scalar_generated_mul_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_div_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_float32_float32, scalar_generated_div_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_div_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_float64_float64, scalar_generated_div_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_div_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_int16_float32, scalar_generated_div_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_div_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_int32_float32, scalar_generated_div_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_div_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_int64_float32, scalar_generated_div_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_div_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_int8_float32, scalar_generated_div_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_div_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_uint16_float32, scalar_generated_div_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_div_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_uint32_float32, scalar_generated_div_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_div_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_uint64_float32, scalar_generated_div_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_div_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float32_uint8_float32, scalar_generated_div_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_div_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_float32_float64, scalar_generated_div_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_div_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_float64_float64, scalar_generated_div_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_div_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_int16_float64, scalar_generated_div_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_div_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_int32_float64, scalar_generated_div_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_div_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_int64_float64, scalar_generated_div_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_div_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_int8_float64, scalar_generated_div_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_div_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_uint16_float64, scalar_generated_div_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_div_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_uint32_float64, scalar_generated_div_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_div_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_uint64_float64, scalar_generated_div_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_div_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_float64_uint8_float64, scalar_generated_div_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_div_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int16_float32_float32, scalar_generated_div_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_div_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int16_float64_float64, scalar_generated_div_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_div_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 16U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int16_int16_int16, scalar_generated_div_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_div_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 16U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int16_int32_int32, scalar_generated_div_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_div_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 16U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int16_int64_int64, scalar_generated_div_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_div_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 16U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int16_int8_int16, scalar_generated_div_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_div_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int32_float32_float32, scalar_generated_div_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_div_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int32_float64_float64, scalar_generated_div_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_div_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int32_int16_int32, scalar_generated_div_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_div_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int32_int32_int32, scalar_generated_div_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_div_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 32U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int32_int64_int64, scalar_generated_div_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_div_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int32_int8_int32, scalar_generated_div_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_div_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int64_float32_float32, scalar_generated_div_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_div_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int64_float64_float64, scalar_generated_div_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_div_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int64_int16_int64, scalar_generated_div_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_div_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int64_int32_int64, scalar_generated_div_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_div_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int64_int64_int64, scalar_generated_div_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_div_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int64_int8_int64, scalar_generated_div_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_div_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int8_float32_float32, scalar_generated_div_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_div_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_int8_float64_float64, scalar_generated_div_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_div_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 8U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int8_int16_int16, scalar_generated_div_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_div_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 8U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int8_int32_int32, scalar_generated_div_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_div_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 8U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int8_int64_int64, scalar_generated_div_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_div_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_divide_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int8_t)runtimec_scalar_wrap_signed(quotient_, 8U, 8U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_int8_int8_int8, scalar_generated_div_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_div_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint16_float32_float32, scalar_generated_div_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_div_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint16_float64_float64, scalar_generated_div_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_div_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint16_uint16_uint16, scalar_generated_div_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_div_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint16_uint32_uint32, scalar_generated_div_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_div_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint16_uint64_uint64, scalar_generated_div_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_div_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint16_uint8_uint16, scalar_generated_div_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_div_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint32_float32_float32, scalar_generated_div_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_div_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint32_float64_float64, scalar_generated_div_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_div_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint32_uint16_uint32, scalar_generated_div_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_div_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint32_uint32_uint32, scalar_generated_div_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_div_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint32_uint64_uint64, scalar_generated_div_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_div_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint32_uint8_uint32, scalar_generated_div_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_div_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint64_float32_float32, scalar_generated_div_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_div_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint64_float64_float64, scalar_generated_div_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_div_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint64_uint16_uint64, scalar_generated_div_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_div_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint64_uint32_uint64, scalar_generated_div_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_div_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint64_uint64_uint64, scalar_generated_div_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_div_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint64_uint8_uint64, scalar_generated_div_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_div_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = (float)*a / (float)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint8_float32_float32, scalar_generated_div_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_div_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = (double)*a / (double)*b;
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_div_uint8_float64_float64, scalar_generated_div_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_div_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint8_uint16_uint16, scalar_generated_div_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_div_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint8_uint32_uint32, scalar_generated_div_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_div_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint8_uint64_uint64, scalar_generated_div_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_div_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_divide_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint8_t)runtimec_scalar_wrap_unsigned(remainder_, 8U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_div_uint8_uint8_uint8, scalar_generated_div_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_mod_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 16U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int16_int16_int16, scalar_generated_mod_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_mod_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 16U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int16_int32_int32, scalar_generated_mod_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_mod_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 16U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int16_int64_int64, scalar_generated_mod_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_mod_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 16U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 16U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int16_int8_int16, scalar_generated_mod_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_mod_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int32_int16_int32, scalar_generated_mod_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_mod_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int32_int32_int32, scalar_generated_mod_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_mod_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 32U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int32_int64_int64, scalar_generated_mod_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_mod_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 32U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 32U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int32_int8_int32, scalar_generated_mod_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_mod_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int64_int16_int64, scalar_generated_mod_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_mod_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int64_int32_int64, scalar_generated_mod_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_mod_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int64_int64_int64, scalar_generated_mod_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_mod_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 64U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 64U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int64_int8_int64, scalar_generated_mod_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_mod_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int16_t)runtimec_scalar_wrap_signed(quotient_, 8U, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int8_int16_int16, scalar_generated_mod_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_mod_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int32_t)runtimec_scalar_wrap_signed(quotient_, 8U, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int8_int32_int32, scalar_generated_mod_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_mod_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int64_t)runtimec_scalar_wrap_signed(quotient_, 8U, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int8_int64_int64, scalar_generated_mod_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_mod_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c, binrwir_execution_exit_t *output_exit) {
    {int64_t quotient_; runtimec_scalar_remainder_signed((int64_t)*a, (int64_t)*b, 8U, &quotient_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (int8_t)runtimec_scalar_wrap_signed(quotient_, 8U, 8U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_int8_int8_int8, scalar_generated_mod_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_mod_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint16_uint16_uint16, scalar_generated_mod_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_mod_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint16_uint32_uint32, scalar_generated_mod_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_mod_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint16_uint64_uint64, scalar_generated_mod_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_mod_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint16_uint8_uint16, scalar_generated_mod_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_mod_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint32_uint16_uint32, scalar_generated_mod_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_mod_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint32_uint32_uint32, scalar_generated_mod_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_mod_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint32_uint64_uint64, scalar_generated_mod_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_mod_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint32_uint8_uint32, scalar_generated_mod_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_mod_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint64_uint16_uint64, scalar_generated_mod_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_mod_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint64_uint32_uint64, scalar_generated_mod_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_mod_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint64_uint64_uint64, scalar_generated_mod_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_mod_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint64_uint8_uint64, scalar_generated_mod_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_mod_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint16_t)runtimec_scalar_wrap_unsigned(remainder_, 16U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint8_uint16_uint16, scalar_generated_mod_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_mod_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint32_t)runtimec_scalar_wrap_unsigned(remainder_, 32U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint8_uint32_uint32, scalar_generated_mod_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_mod_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint64_t)runtimec_scalar_wrap_unsigned(remainder_, 64U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint8_uint64_uint64, scalar_generated_mod_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_mod_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c, binrwir_execution_exit_t *output_exit) {
    {uint64_t remainder_; runtimec_scalar_remainder_unsigned((uint64_t)*a, (uint64_t)*b, &remainder_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *c = (uint8_t)runtimec_scalar_wrap_unsigned(remainder_, 8U);}
}

VMBIN_HANDLER_BINARY_TYPED_EXIT(scalar_generated_mod_uint8_uint8_uint8, scalar_generated_mod_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_max_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_float32_float32, scalar_generated_max_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_max_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_float64_float64, scalar_generated_max_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_max_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_int16_float32, scalar_generated_max_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_max_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_int32_float32, scalar_generated_max_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_max_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_int64_float32, scalar_generated_max_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_max_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_int8_float32, scalar_generated_max_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_max_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_uint16_float32, scalar_generated_max_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_max_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_uint32_float32, scalar_generated_max_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_max_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_uint64_float32, scalar_generated_max_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_max_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float32_uint8_float32, scalar_generated_max_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_max_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_float32_float64, scalar_generated_max_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_max_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_float64_float64, scalar_generated_max_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_max_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_int16_float64, scalar_generated_max_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_max_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_int32_float64, scalar_generated_max_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_max_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_int64_float64, scalar_generated_max_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_max_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_int8_float64, scalar_generated_max_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_max_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_uint16_float64, scalar_generated_max_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_max_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_uint32_float64, scalar_generated_max_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_max_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_uint64_float64, scalar_generated_max_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_max_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_float64_uint8_float64, scalar_generated_max_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_max_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_float32_float32, scalar_generated_max_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_max_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_float64_float64, scalar_generated_max_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_max_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a > (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_int16_int16, scalar_generated_max_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_max_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a > (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_int32_int32, scalar_generated_max_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_max_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_int64_int64, scalar_generated_max_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_max_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a > (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int16_int8_int16, scalar_generated_max_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_max_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_float32_float32, scalar_generated_max_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_max_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_float64_float64, scalar_generated_max_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_max_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a > (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_int16_int32, scalar_generated_max_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_max_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a > (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_int32_int32, scalar_generated_max_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_max_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_int64_int64, scalar_generated_max_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_max_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a > (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int32_int8_int32, scalar_generated_max_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_max_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_float32_float32, scalar_generated_max_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_max_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_float64_float64, scalar_generated_max_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_max_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_int16_int64, scalar_generated_max_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_max_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_int32_int64, scalar_generated_max_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_max_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_int64_int64, scalar_generated_max_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_max_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int64_int8_int64, scalar_generated_max_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_max_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_float32_float32, scalar_generated_max_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_max_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_float64_float64, scalar_generated_max_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_max_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a > (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_int16_int16, scalar_generated_max_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_max_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a > (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_int32_int32, scalar_generated_max_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_max_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a > (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_int64_int64, scalar_generated_max_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_max_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((int8_t)*a > (int8_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_int8_int8_int8, scalar_generated_max_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_max_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_float32_float32, scalar_generated_max_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_max_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_float64_float64, scalar_generated_max_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_max_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a > (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_uint16_uint16, scalar_generated_max_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_max_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a > (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_uint32_uint32, scalar_generated_max_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_max_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_uint64_uint64, scalar_generated_max_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_max_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a > (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint16_uint8_uint16, scalar_generated_max_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_max_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_float32_float32, scalar_generated_max_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_max_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_float64_float64, scalar_generated_max_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_max_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a > (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_uint16_uint32, scalar_generated_max_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_max_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a > (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_uint32_uint32, scalar_generated_max_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_max_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_uint64_uint64, scalar_generated_max_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_max_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a > (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint32_uint8_uint32, scalar_generated_max_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_max_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_float32_float32, scalar_generated_max_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_max_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_float64_float64, scalar_generated_max_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_max_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_uint16_uint64, scalar_generated_max_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_max_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_uint32_uint64, scalar_generated_max_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_max_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_uint64_uint64, scalar_generated_max_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_max_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint64_uint8_uint64, scalar_generated_max_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_max_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_float32_float32, scalar_generated_max_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_max_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_max((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_float64_float64, scalar_generated_max_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_max_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a > (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_uint16_uint16, scalar_generated_max_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_max_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a > (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_uint32_uint32, scalar_generated_max_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_max_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a > (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_uint64_uint64, scalar_generated_max_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_max_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a > (uint8_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_max_uint8_uint8_uint8, scalar_generated_max_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_min_float32_float32_float32_logic(const float *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_float32_float32, scalar_generated_min_float32_float32_float32_logic, float, float, float)

static inline void scalar_generated_min_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_float64_float64, scalar_generated_min_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_min_float32_int16_float32_logic(const float *a, const int16_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_int16_float32, scalar_generated_min_float32_int16_float32_logic, float, int16_t, float)

static inline void scalar_generated_min_float32_int32_float32_logic(const float *a, const int32_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_int32_float32, scalar_generated_min_float32_int32_float32_logic, float, int32_t, float)

static inline void scalar_generated_min_float32_int64_float32_logic(const float *a, const int64_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_int64_float32, scalar_generated_min_float32_int64_float32_logic, float, int64_t, float)

static inline void scalar_generated_min_float32_int8_float32_logic(const float *a, const int8_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_int8_float32, scalar_generated_min_float32_int8_float32_logic, float, int8_t, float)

static inline void scalar_generated_min_float32_uint16_float32_logic(const float *a, const uint16_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_uint16_float32, scalar_generated_min_float32_uint16_float32_logic, float, uint16_t, float)

static inline void scalar_generated_min_float32_uint32_float32_logic(const float *a, const uint32_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_uint32_float32, scalar_generated_min_float32_uint32_float32_logic, float, uint32_t, float)

static inline void scalar_generated_min_float32_uint64_float32_logic(const float *a, const uint64_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_uint64_float32, scalar_generated_min_float32_uint64_float32_logic, float, uint64_t, float)

static inline void scalar_generated_min_float32_uint8_float32_logic(const float *a, const uint8_t *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float32_uint8_float32, scalar_generated_min_float32_uint8_float32_logic, float, uint8_t, float)

static inline void scalar_generated_min_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_float32_float64, scalar_generated_min_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_min_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_float64_float64, scalar_generated_min_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_min_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_int16_float64, scalar_generated_min_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_min_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_int32_float64, scalar_generated_min_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_min_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_int64_float64, scalar_generated_min_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_min_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_int8_float64, scalar_generated_min_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_min_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_uint16_float64, scalar_generated_min_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_min_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_uint32_float64, scalar_generated_min_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_min_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_uint64_float64, scalar_generated_min_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_min_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_float64_uint8_float64, scalar_generated_min_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_min_int16_float32_float32_logic(const int16_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_float32_float32, scalar_generated_min_int16_float32_float32_logic, int16_t, float, float)

static inline void scalar_generated_min_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_float64_float64, scalar_generated_min_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_min_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a < (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_int16_int16, scalar_generated_min_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_min_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a < (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_int32_int32, scalar_generated_min_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_min_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_int64_int64, scalar_generated_min_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_min_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a < (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int16_int8_int16, scalar_generated_min_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_min_int32_float32_float32_logic(const int32_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_float32_float32, scalar_generated_min_int32_float32_float32_logic, int32_t, float, float)

static inline void scalar_generated_min_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_float64_float64, scalar_generated_min_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_min_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a < (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_int16_int32, scalar_generated_min_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_min_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a < (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_int32_int32, scalar_generated_min_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_min_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_int64_int64, scalar_generated_min_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_min_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a < (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int32_int8_int32, scalar_generated_min_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_min_int64_float32_float32_logic(const int64_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_float32_float32, scalar_generated_min_int64_float32_float32_logic, int64_t, float, float)

static inline void scalar_generated_min_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_float64_float64, scalar_generated_min_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_min_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_int16_int64, scalar_generated_min_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_min_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_int32_int64, scalar_generated_min_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_min_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_int64_int64, scalar_generated_min_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_min_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int64_int8_int64, scalar_generated_min_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_min_int8_float32_float32_logic(const int8_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_float32_float32, scalar_generated_min_int8_float32_float32_logic, int8_t, float, float)

static inline void scalar_generated_min_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_float64_float64, scalar_generated_min_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_min_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a < (int16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_int16_int16, scalar_generated_min_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_min_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a < (int32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_int32_int32, scalar_generated_min_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_min_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a < (int64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_int64_int64, scalar_generated_min_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_min_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((int8_t)*a < (int8_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_int8_int8_int8, scalar_generated_min_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_min_uint16_float32_float32_logic(const uint16_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_float32_float32, scalar_generated_min_uint16_float32_float32_logic, uint16_t, float, float)

static inline void scalar_generated_min_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_float64_float64, scalar_generated_min_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_min_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a < (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_uint16_uint16, scalar_generated_min_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_min_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a < (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_uint32_uint32, scalar_generated_min_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_min_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_uint64_uint64, scalar_generated_min_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_min_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a < (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint16_uint8_uint16, scalar_generated_min_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_min_uint32_float32_float32_logic(const uint32_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_float32_float32, scalar_generated_min_uint32_float32_float32_logic, uint32_t, float, float)

static inline void scalar_generated_min_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_float64_float64, scalar_generated_min_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_min_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a < (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_uint16_uint32, scalar_generated_min_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_min_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a < (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_uint32_uint32, scalar_generated_min_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_min_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_uint64_uint64, scalar_generated_min_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_min_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a < (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint32_uint8_uint32, scalar_generated_min_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_min_uint64_float32_float32_logic(const uint64_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_float32_float32, scalar_generated_min_uint64_float32_float32_logic, uint64_t, float, float)

static inline void scalar_generated_min_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_float64_float64, scalar_generated_min_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_min_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_uint16_uint64, scalar_generated_min_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_min_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_uint32_uint64, scalar_generated_min_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_min_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_uint64_uint64, scalar_generated_min_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_min_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint64_uint8_uint64, scalar_generated_min_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_min_uint8_float32_float32_logic(const uint8_t *a, const float *b, float *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_float32_float32, scalar_generated_min_uint8_float32_float32_logic, uint8_t, float, float)

static inline void scalar_generated_min_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = runtimec_scalar_float_min((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_float64_float64, scalar_generated_min_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_min_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a < (uint16_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_uint16_uint16, scalar_generated_min_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_min_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a < (uint32_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_uint32_uint32, scalar_generated_min_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_min_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a < (uint64_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_uint64_uint64, scalar_generated_min_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_min_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a < (uint8_t)*b ? *a : *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_min_uint8_uint8_uint8, scalar_generated_min_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_absolute_float32_float32_logic(const float *a, float *out) {
    *out = fabs(*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_float32_float32, scalar_generated_absolute_float32_float32_logic, float, float)

static inline void scalar_generated_absolute_float64_float64_logic(const double *a, double *out) {
    *out = fabs(*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_float64_float64, scalar_generated_absolute_float64_float64_logic, double, double)

static inline void scalar_generated_absolute_int16_int16_logic(const int16_t *a, int16_t *out) {
    *out = (int16_t)(*a < 0 ? 0 - (uint16_t)*a : (uint16_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_int16_int16, scalar_generated_absolute_int16_int16_logic, int16_t, int16_t)

static inline void scalar_generated_absolute_int32_int32_logic(const int32_t *a, int32_t *out) {
    *out = (int32_t)(*a < 0 ? 0 - (uint32_t)*a : (uint32_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_int32_int32, scalar_generated_absolute_int32_int32_logic, int32_t, int32_t)

static inline void scalar_generated_absolute_int64_int64_logic(const int64_t *a, int64_t *out) {
    *out = (int64_t)(*a < 0 ? 0 - (uint64_t)*a : (uint64_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_int64_int64, scalar_generated_absolute_int64_int64_logic, int64_t, int64_t)

static inline void scalar_generated_absolute_int8_int8_logic(const int8_t *a, int8_t *out) {
    *out = (int8_t)(*a < 0 ? 0 - (uint8_t)*a : (uint8_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_int8_int8, scalar_generated_absolute_int8_int8_logic, int8_t, int8_t)

static inline void scalar_generated_absolute_uint16_uint16_logic(const uint16_t *a, uint16_t *out) {
    *out = *a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_uint16_uint16, scalar_generated_absolute_uint16_uint16_logic, uint16_t, uint16_t)

static inline void scalar_generated_absolute_uint32_uint32_logic(const uint32_t *a, uint32_t *out) {
    *out = *a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_uint32_uint32, scalar_generated_absolute_uint32_uint32_logic, uint32_t, uint32_t)

static inline void scalar_generated_absolute_uint64_uint64_logic(const uint64_t *a, uint64_t *out) {
    *out = *a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_uint64_uint64, scalar_generated_absolute_uint64_uint64_logic, uint64_t, uint64_t)

static inline void scalar_generated_absolute_uint8_uint8_logic(const uint8_t *a, uint8_t *out) {
    *out = *a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_absolute_uint8_uint8, scalar_generated_absolute_uint8_uint8_logic, uint8_t, uint8_t)

static inline void scalar_generated_negation_float32_float32_logic(const float *a, float *out) {
    *out = -*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_float32_float32, scalar_generated_negation_float32_float32_logic, float, float)

static inline void scalar_generated_negation_float64_float64_logic(const double *a, double *out) {
    *out = -*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_float64_float64, scalar_generated_negation_float64_float64_logic, double, double)

static inline void scalar_generated_negation_int16_int16_logic(const int16_t *a, int16_t *out) {
    *out = (int16_t)(0 - (uint16_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_int16_int16, scalar_generated_negation_int16_int16_logic, int16_t, int16_t)

static inline void scalar_generated_negation_int32_int32_logic(const int32_t *a, int32_t *out) {
    *out = (int32_t)(0 - (uint32_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_int32_int32, scalar_generated_negation_int32_int32_logic, int32_t, int32_t)

static inline void scalar_generated_negation_int64_int64_logic(const int64_t *a, int64_t *out) {
    *out = (int64_t)(0 - (uint64_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_int64_int64, scalar_generated_negation_int64_int64_logic, int64_t, int64_t)

static inline void scalar_generated_negation_int8_int8_logic(const int8_t *a, int8_t *out) {
    *out = (int8_t)(0 - (uint8_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_int8_int8, scalar_generated_negation_int8_int8_logic, int8_t, int8_t)

static inline void scalar_generated_negation_uint16_uint16_logic(const uint16_t *a, uint16_t *out) {
    *out = (uint16_t)(0 - (uint16_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_uint16_uint16, scalar_generated_negation_uint16_uint16_logic, uint16_t, uint16_t)

static inline void scalar_generated_negation_uint32_uint32_logic(const uint32_t *a, uint32_t *out) {
    *out = (uint32_t)(0 - (uint32_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_uint32_uint32, scalar_generated_negation_uint32_uint32_logic, uint32_t, uint32_t)

static inline void scalar_generated_negation_uint64_uint64_logic(const uint64_t *a, uint64_t *out) {
    *out = (uint64_t)(0 - (uint64_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_uint64_uint64, scalar_generated_negation_uint64_uint64_logic, uint64_t, uint64_t)

static inline void scalar_generated_negation_uint8_uint8_logic(const uint8_t *a, uint8_t *out) {
    *out = (uint8_t)(0 - (uint8_t)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_negation_uint8_uint8, scalar_generated_negation_uint8_uint8_logic, uint8_t, uint8_t)

static inline void scalar_generated_sign_float32_float32_logic(const float *a, float *out) {
    *out = (float)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_float32_float32, scalar_generated_sign_float32_float32_logic, float, float)

static inline void scalar_generated_sign_float64_float64_logic(const double *a, double *out) {
    *out = (double)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_float64_float64, scalar_generated_sign_float64_float64_logic, double, double)

static inline void scalar_generated_sign_int16_int16_logic(const int16_t *a, int16_t *out) {
    *out = (int16_t)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_int16_int16, scalar_generated_sign_int16_int16_logic, int16_t, int16_t)

static inline void scalar_generated_sign_int32_int32_logic(const int32_t *a, int32_t *out) {
    *out = (int32_t)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_int32_int32, scalar_generated_sign_int32_int32_logic, int32_t, int32_t)

static inline void scalar_generated_sign_int64_int64_logic(const int64_t *a, int64_t *out) {
    *out = (int64_t)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_int64_int64, scalar_generated_sign_int64_int64_logic, int64_t, int64_t)

static inline void scalar_generated_sign_int8_int8_logic(const int8_t *a, int8_t *out) {
    *out = (int8_t)((*a > 0) - (*a < 0));
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_int8_int8, scalar_generated_sign_int8_int8_logic, int8_t, int8_t)

static inline void scalar_generated_sign_uint16_uint16_logic(const uint16_t *a, uint16_t *out) {
    *out = (uint16_t)(*a > 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_uint16_uint16, scalar_generated_sign_uint16_uint16_logic, uint16_t, uint16_t)

static inline void scalar_generated_sign_uint32_uint32_logic(const uint32_t *a, uint32_t *out) {
    *out = (uint32_t)(*a > 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_uint32_uint32, scalar_generated_sign_uint32_uint32_logic, uint32_t, uint32_t)

static inline void scalar_generated_sign_uint64_uint64_logic(const uint64_t *a, uint64_t *out) {
    *out = (uint64_t)(*a > 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_uint64_uint64, scalar_generated_sign_uint64_uint64_logic, uint64_t, uint64_t)

static inline void scalar_generated_sign_uint8_uint8_logic(const uint8_t *a, uint8_t *out) {
    *out = (uint8_t)(*a > 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_sign_uint8_uint8, scalar_generated_sign_uint8_uint8_logic, uint8_t, uint8_t)

static inline void scalar_generated_bitwise_and_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a & (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int16_int16_int16, scalar_generated_bitwise_and_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_and_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a & (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int16_int32_int32, scalar_generated_bitwise_and_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_and_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int16_int64_int64, scalar_generated_bitwise_and_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_and_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a & (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int16_int8_int16, scalar_generated_bitwise_and_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_bitwise_and_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a & (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int32_int16_int32, scalar_generated_bitwise_and_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_bitwise_and_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a & (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int32_int32_int32, scalar_generated_bitwise_and_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_and_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int32_int64_int64, scalar_generated_bitwise_and_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_and_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a & (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int32_int8_int32, scalar_generated_bitwise_and_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_bitwise_and_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int64_int16_int64, scalar_generated_bitwise_and_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_bitwise_and_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int64_int32_int64, scalar_generated_bitwise_and_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_bitwise_and_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int64_int64_int64, scalar_generated_bitwise_and_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_and_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int64_int8_int64, scalar_generated_bitwise_and_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_bitwise_and_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a & (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int8_int16_int16, scalar_generated_bitwise_and_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_and_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a & (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int8_int32_int32, scalar_generated_bitwise_and_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_and_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a & (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int8_int64_int64, scalar_generated_bitwise_and_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_and_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((int8_t)*a & (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_int8_int8_int8, scalar_generated_bitwise_and_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_bitwise_and_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a & (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint16_uint16_uint16, scalar_generated_bitwise_and_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_and_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a & (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint16_uint32_uint32, scalar_generated_bitwise_and_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_and_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint16_uint64_uint64, scalar_generated_bitwise_and_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a & (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint16_uint8_uint16, scalar_generated_bitwise_and_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_bitwise_and_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a & (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint32_uint16_uint32, scalar_generated_bitwise_and_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_bitwise_and_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a & (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint32_uint32_uint32, scalar_generated_bitwise_and_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_and_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint32_uint64_uint64, scalar_generated_bitwise_and_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a & (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint32_uint8_uint32, scalar_generated_bitwise_and_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_bitwise_and_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint64_uint16_uint64, scalar_generated_bitwise_and_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint64_uint32_uint64, scalar_generated_bitwise_and_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint64_uint64_uint64, scalar_generated_bitwise_and_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint64_uint8_uint64, scalar_generated_bitwise_and_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a & (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint8_uint16_uint16, scalar_generated_bitwise_and_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_and_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a & (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint8_uint32_uint32, scalar_generated_bitwise_and_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_and_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a & (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint8_uint64_uint64, scalar_generated_bitwise_and_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_and_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a & (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_and_uint8_uint8_uint8, scalar_generated_bitwise_and_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_bitwise_or_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a | (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int16_int16_int16, scalar_generated_bitwise_or_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_or_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a | (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int16_int32_int32, scalar_generated_bitwise_or_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_or_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int16_int64_int64, scalar_generated_bitwise_or_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_or_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a | (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int16_int8_int16, scalar_generated_bitwise_or_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_bitwise_or_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a | (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int32_int16_int32, scalar_generated_bitwise_or_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_bitwise_or_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a | (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int32_int32_int32, scalar_generated_bitwise_or_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_or_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int32_int64_int64, scalar_generated_bitwise_or_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_or_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a | (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int32_int8_int32, scalar_generated_bitwise_or_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_bitwise_or_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int64_int16_int64, scalar_generated_bitwise_or_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_bitwise_or_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int64_int32_int64, scalar_generated_bitwise_or_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_bitwise_or_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int64_int64_int64, scalar_generated_bitwise_or_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_or_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int64_int8_int64, scalar_generated_bitwise_or_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_bitwise_or_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a | (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int8_int16_int16, scalar_generated_bitwise_or_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_or_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a | (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int8_int32_int32, scalar_generated_bitwise_or_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_or_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a | (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int8_int64_int64, scalar_generated_bitwise_or_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_or_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((int8_t)*a | (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_int8_int8_int8, scalar_generated_bitwise_or_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_bitwise_or_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a | (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint16_uint16_uint16, scalar_generated_bitwise_or_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_or_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a | (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint16_uint32_uint32, scalar_generated_bitwise_or_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_or_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint16_uint64_uint64, scalar_generated_bitwise_or_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a | (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint16_uint8_uint16, scalar_generated_bitwise_or_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_bitwise_or_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a | (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint32_uint16_uint32, scalar_generated_bitwise_or_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_bitwise_or_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a | (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint32_uint32_uint32, scalar_generated_bitwise_or_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_or_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint32_uint64_uint64, scalar_generated_bitwise_or_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a | (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint32_uint8_uint32, scalar_generated_bitwise_or_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_bitwise_or_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint64_uint16_uint64, scalar_generated_bitwise_or_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint64_uint32_uint64, scalar_generated_bitwise_or_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint64_uint64_uint64, scalar_generated_bitwise_or_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint64_uint8_uint64, scalar_generated_bitwise_or_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a | (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint8_uint16_uint16, scalar_generated_bitwise_or_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_or_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a | (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint8_uint32_uint32, scalar_generated_bitwise_or_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_or_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a | (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint8_uint64_uint64, scalar_generated_bitwise_or_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_or_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a | (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_or_uint8_uint8_uint8, scalar_generated_bitwise_or_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_bitwise_xor_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a ^ (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int16_int16_int16, scalar_generated_bitwise_xor_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_xor_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a ^ (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int16_int32_int32, scalar_generated_bitwise_xor_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_xor_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int16_int64_int64, scalar_generated_bitwise_xor_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_xor_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a ^ (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int16_int8_int16, scalar_generated_bitwise_xor_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_bitwise_xor_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a ^ (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int32_int16_int32, scalar_generated_bitwise_xor_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_bitwise_xor_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a ^ (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int32_int32_int32, scalar_generated_bitwise_xor_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_xor_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int32_int64_int64, scalar_generated_bitwise_xor_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_xor_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a ^ (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int32_int8_int32, scalar_generated_bitwise_xor_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_bitwise_xor_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int64_int16_int64, scalar_generated_bitwise_xor_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_bitwise_xor_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int64_int32_int64, scalar_generated_bitwise_xor_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_bitwise_xor_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int64_int64_int64, scalar_generated_bitwise_xor_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_xor_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int64_int8_int64, scalar_generated_bitwise_xor_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_bitwise_xor_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)((int16_t)*a ^ (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int8_int16_int16, scalar_generated_bitwise_xor_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_bitwise_xor_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)((int32_t)*a ^ (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int8_int32_int32, scalar_generated_bitwise_xor_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_bitwise_xor_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)((int64_t)*a ^ (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int8_int64_int64, scalar_generated_bitwise_xor_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_bitwise_xor_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)((int8_t)*a ^ (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_int8_int8_int8, scalar_generated_bitwise_xor_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_bitwise_xor_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a ^ (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint16_uint16_uint16, scalar_generated_bitwise_xor_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_xor_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a ^ (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint16_uint32_uint32, scalar_generated_bitwise_xor_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_xor_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint16_uint64_uint64, scalar_generated_bitwise_xor_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a ^ (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint16_uint8_uint16, scalar_generated_bitwise_xor_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_bitwise_xor_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a ^ (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint32_uint16_uint32, scalar_generated_bitwise_xor_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_bitwise_xor_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a ^ (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint32_uint32_uint32, scalar_generated_bitwise_xor_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_xor_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint32_uint64_uint64, scalar_generated_bitwise_xor_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a ^ (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint32_uint8_uint32, scalar_generated_bitwise_xor_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_bitwise_xor_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint64_uint16_uint64, scalar_generated_bitwise_xor_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint64_uint32_uint64, scalar_generated_bitwise_xor_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint64_uint64_uint64, scalar_generated_bitwise_xor_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint64_uint8_uint64, scalar_generated_bitwise_xor_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)((uint16_t)*a ^ (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint8_uint16_uint16, scalar_generated_bitwise_xor_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_bitwise_xor_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)((uint32_t)*a ^ (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint8_uint32_uint32, scalar_generated_bitwise_xor_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_bitwise_xor_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)((uint64_t)*a ^ (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint8_uint64_uint64, scalar_generated_bitwise_xor_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_bitwise_xor_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)((uint8_t)*a ^ (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_bitwise_xor_uint8_uint8_uint8, scalar_generated_bitwise_xor_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_shift_left_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_left_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int16_int16_int16, scalar_generated_shift_left_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_shift_left_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_left_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int16_int32_int32, scalar_generated_shift_left_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_shift_left_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int16_int64_int64, scalar_generated_shift_left_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_shift_left_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_left_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int16_int8_int16, scalar_generated_shift_left_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_shift_left_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_left_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int32_int16_int32, scalar_generated_shift_left_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_shift_left_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_left_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int32_int32_int32, scalar_generated_shift_left_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_shift_left_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int32_int64_int64, scalar_generated_shift_left_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_shift_left_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_left_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int32_int8_int32, scalar_generated_shift_left_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_shift_left_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int64_int16_int64, scalar_generated_shift_left_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_shift_left_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int64_int32_int64, scalar_generated_shift_left_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_shift_left_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int64_int64_int64, scalar_generated_shift_left_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_shift_left_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int64_int8_int64, scalar_generated_shift_left_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_shift_left_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_left_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int8_int16_int16, scalar_generated_shift_left_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_shift_left_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_left_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int8_int32_int32, scalar_generated_shift_left_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_shift_left_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_left_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int8_int64_int64, scalar_generated_shift_left_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_shift_left_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)runtimec_scalar_shift_left_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_int8_int8_int8, scalar_generated_shift_left_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_shift_left_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint16_uint16_uint16, scalar_generated_shift_left_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_shift_left_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint16_uint32_uint32, scalar_generated_shift_left_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_left_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint16_uint64_uint64, scalar_generated_shift_left_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_left_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint16_uint8_uint16, scalar_generated_shift_left_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_shift_left_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint32_uint16_uint32, scalar_generated_shift_left_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_shift_left_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint32_uint32_uint32, scalar_generated_shift_left_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_left_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint32_uint64_uint64, scalar_generated_shift_left_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_left_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint32_uint8_uint32, scalar_generated_shift_left_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_shift_left_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint64_uint16_uint64, scalar_generated_shift_left_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_shift_left_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint64_uint32_uint64, scalar_generated_shift_left_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_shift_left_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint64_uint64_uint64, scalar_generated_shift_left_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_left_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint64_uint8_uint64, scalar_generated_shift_left_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_shift_left_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint8_uint16_uint16, scalar_generated_shift_left_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_shift_left_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint8_uint32_uint32, scalar_generated_shift_left_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_left_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint8_uint64_uint64, scalar_generated_shift_left_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_left_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)runtimec_scalar_shift_left_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_left_uint8_uint8_uint8, scalar_generated_shift_left_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_shift_right_int16_int16_int16_logic(const int16_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_right_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int16_int16_int16, scalar_generated_shift_right_int16_int16_int16_logic, int16_t, int16_t, int16_t)

static inline void scalar_generated_shift_right_int16_int32_int32_logic(const int16_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_right_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int16_int32_int32, scalar_generated_shift_right_int16_int32_int32_logic, int16_t, int32_t, int32_t)

static inline void scalar_generated_shift_right_int16_int64_int64_logic(const int16_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int16_int64_int64, scalar_generated_shift_right_int16_int64_int64_logic, int16_t, int64_t, int64_t)

static inline void scalar_generated_shift_right_int16_int8_int16_logic(const int16_t *a, const int8_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_right_signed((int16_t)*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int16_int8_int16, scalar_generated_shift_right_int16_int8_int16_logic, int16_t, int8_t, int16_t)

static inline void scalar_generated_shift_right_int32_int16_int32_logic(const int32_t *a, const int16_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_right_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int32_int16_int32, scalar_generated_shift_right_int32_int16_int32_logic, int32_t, int16_t, int32_t)

static inline void scalar_generated_shift_right_int32_int32_int32_logic(const int32_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_right_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int32_int32_int32, scalar_generated_shift_right_int32_int32_int32_logic, int32_t, int32_t, int32_t)

static inline void scalar_generated_shift_right_int32_int64_int64_logic(const int32_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int32_int64_int64, scalar_generated_shift_right_int32_int64_int64_logic, int32_t, int64_t, int64_t)

static inline void scalar_generated_shift_right_int32_int8_int32_logic(const int32_t *a, const int8_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_right_signed((int32_t)*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int32_int8_int32, scalar_generated_shift_right_int32_int8_int32_logic, int32_t, int8_t, int32_t)

static inline void scalar_generated_shift_right_int64_int16_int64_logic(const int64_t *a, const int16_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int64_int16_int64, scalar_generated_shift_right_int64_int16_int64_logic, int64_t, int16_t, int64_t)

static inline void scalar_generated_shift_right_int64_int32_int64_logic(const int64_t *a, const int32_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int64_int32_int64, scalar_generated_shift_right_int64_int32_int64_logic, int64_t, int32_t, int64_t)

static inline void scalar_generated_shift_right_int64_int64_int64_logic(const int64_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int64_int64_int64, scalar_generated_shift_right_int64_int64_int64_logic, int64_t, int64_t, int64_t)

static inline void scalar_generated_shift_right_int64_int8_int64_logic(const int64_t *a, const int8_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int64_t)*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int64_int8_int64, scalar_generated_shift_right_int64_int8_int64_logic, int64_t, int8_t, int64_t)

static inline void scalar_generated_shift_right_int8_int16_int16_logic(const int8_t *a, const int16_t *b, int16_t *c) {
    *c = (int16_t)runtimec_scalar_shift_right_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int8_int16_int16, scalar_generated_shift_right_int8_int16_int16_logic, int8_t, int16_t, int16_t)

static inline void scalar_generated_shift_right_int8_int32_int32_logic(const int8_t *a, const int32_t *b, int32_t *c) {
    *c = (int32_t)runtimec_scalar_shift_right_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int8_int32_int32, scalar_generated_shift_right_int8_int32_int32_logic, int8_t, int32_t, int32_t)

static inline void scalar_generated_shift_right_int8_int64_int64_logic(const int8_t *a, const int64_t *b, int64_t *c) {
    *c = (int64_t)runtimec_scalar_shift_right_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int8_int64_int64, scalar_generated_shift_right_int8_int64_int64_logic, int8_t, int64_t, int64_t)

static inline void scalar_generated_shift_right_int8_int8_int8_logic(const int8_t *a, const int8_t *b, int8_t *c) {
    *c = (int8_t)runtimec_scalar_shift_right_signed((int8_t)*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_int8_int8_int8, scalar_generated_shift_right_int8_int8_int8_logic, int8_t, int8_t, int8_t)

static inline void scalar_generated_shift_right_uint16_uint16_uint16_logic(const uint16_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint16_uint16_uint16, scalar_generated_shift_right_uint16_uint16_uint16_logic, uint16_t, uint16_t, uint16_t)

static inline void scalar_generated_shift_right_uint16_uint32_uint32_logic(const uint16_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint16_uint32_uint32, scalar_generated_shift_right_uint16_uint32_uint32_logic, uint16_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_right_uint16_uint64_uint64_logic(const uint16_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint16_uint64_uint64, scalar_generated_shift_right_uint16_uint64_uint64_logic, uint16_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_right_uint16_uint8_uint16_logic(const uint16_t *a, const uint8_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 16U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint16_uint8_uint16, scalar_generated_shift_right_uint16_uint8_uint16_logic, uint16_t, uint8_t, uint16_t)

static inline void scalar_generated_shift_right_uint32_uint16_uint32_logic(const uint32_t *a, const uint16_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint32_uint16_uint32, scalar_generated_shift_right_uint32_uint16_uint32_logic, uint32_t, uint16_t, uint32_t)

static inline void scalar_generated_shift_right_uint32_uint32_uint32_logic(const uint32_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint32_uint32_uint32, scalar_generated_shift_right_uint32_uint32_uint32_logic, uint32_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_right_uint32_uint64_uint64_logic(const uint32_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint32_uint64_uint64, scalar_generated_shift_right_uint32_uint64_uint64_logic, uint32_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_right_uint32_uint8_uint32_logic(const uint32_t *a, const uint8_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 32U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint32_uint8_uint32, scalar_generated_shift_right_uint32_uint8_uint32_logic, uint32_t, uint8_t, uint32_t)

static inline void scalar_generated_shift_right_uint64_uint16_uint64_logic(const uint64_t *a, const uint16_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint64_uint16_uint64, scalar_generated_shift_right_uint64_uint16_uint64_logic, uint64_t, uint16_t, uint64_t)

static inline void scalar_generated_shift_right_uint64_uint32_uint64_logic(const uint64_t *a, const uint32_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint64_uint32_uint64, scalar_generated_shift_right_uint64_uint32_uint64_logic, uint64_t, uint32_t, uint64_t)

static inline void scalar_generated_shift_right_uint64_uint64_uint64_logic(const uint64_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint64_uint64_uint64, scalar_generated_shift_right_uint64_uint64_uint64_logic, uint64_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_right_uint64_uint8_uint64_logic(const uint64_t *a, const uint8_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 64U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint64_uint8_uint64, scalar_generated_shift_right_uint64_uint8_uint64_logic, uint64_t, uint8_t, uint64_t)

static inline void scalar_generated_shift_right_uint8_uint16_uint16_logic(const uint8_t *a, const uint16_t *b, uint16_t *c) {
    *c = (uint16_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint8_uint16_uint16, scalar_generated_shift_right_uint8_uint16_uint16_logic, uint8_t, uint16_t, uint16_t)

static inline void scalar_generated_shift_right_uint8_uint32_uint32_logic(const uint8_t *a, const uint32_t *b, uint32_t *c) {
    *c = (uint32_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint8_uint32_uint32, scalar_generated_shift_right_uint8_uint32_uint32_logic, uint8_t, uint32_t, uint32_t)

static inline void scalar_generated_shift_right_uint8_uint64_uint64_logic(const uint8_t *a, const uint64_t *b, uint64_t *c) {
    *c = (uint64_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint8_uint64_uint64, scalar_generated_shift_right_uint8_uint64_uint64_logic, uint8_t, uint64_t, uint64_t)

static inline void scalar_generated_shift_right_uint8_uint8_uint8_logic(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    *c = (uint8_t)runtimec_scalar_shift_right_unsigned(*a, (uint64_t)*b, 8U);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_shift_right_uint8_uint8_uint8, scalar_generated_shift_right_uint8_uint8_uint8_logic, uint8_t, uint8_t, uint8_t)

static inline void scalar_generated_square_root_float32_float64_logic(const float *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_float32_float64, scalar_generated_square_root_float32_float64_logic, float, double)

static inline void scalar_generated_square_root_float64_float64_logic(const double *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_float64_float64, scalar_generated_square_root_float64_float64_logic, double, double)

static inline void scalar_generated_square_root_int16_float64_logic(const int16_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_int16_float64, scalar_generated_square_root_int16_float64_logic, int16_t, double)

static inline void scalar_generated_square_root_int32_float64_logic(const int32_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_int32_float64, scalar_generated_square_root_int32_float64_logic, int32_t, double)

static inline void scalar_generated_square_root_int64_float64_logic(const int64_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_int64_float64, scalar_generated_square_root_int64_float64_logic, int64_t, double)

static inline void scalar_generated_square_root_int8_float64_logic(const int8_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_int8_float64, scalar_generated_square_root_int8_float64_logic, int8_t, double)

static inline void scalar_generated_square_root_uint16_float64_logic(const uint16_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_uint16_float64, scalar_generated_square_root_uint16_float64_logic, uint16_t, double)

static inline void scalar_generated_square_root_uint32_float64_logic(const uint32_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_uint32_float64, scalar_generated_square_root_uint32_float64_logic, uint32_t, double)

static inline void scalar_generated_square_root_uint64_float64_logic(const uint64_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_uint64_float64, scalar_generated_square_root_uint64_float64_logic, uint64_t, double)

static inline void scalar_generated_square_root_uint8_float64_logic(const uint8_t *a, double *out) {
    *out = sqrt((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_square_root_uint8_float64, scalar_generated_square_root_uint8_float64_logic, uint8_t, double)

static inline void scalar_generated_exponential_float32_float64_logic(const float *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_float32_float64, scalar_generated_exponential_float32_float64_logic, float, double)

static inline void scalar_generated_exponential_float64_float64_logic(const double *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_float64_float64, scalar_generated_exponential_float64_float64_logic, double, double)

static inline void scalar_generated_exponential_int16_float64_logic(const int16_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_int16_float64, scalar_generated_exponential_int16_float64_logic, int16_t, double)

static inline void scalar_generated_exponential_int32_float64_logic(const int32_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_int32_float64, scalar_generated_exponential_int32_float64_logic, int32_t, double)

static inline void scalar_generated_exponential_int64_float64_logic(const int64_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_int64_float64, scalar_generated_exponential_int64_float64_logic, int64_t, double)

static inline void scalar_generated_exponential_int8_float64_logic(const int8_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_int8_float64, scalar_generated_exponential_int8_float64_logic, int8_t, double)

static inline void scalar_generated_exponential_uint16_float64_logic(const uint16_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_uint16_float64, scalar_generated_exponential_uint16_float64_logic, uint16_t, double)

static inline void scalar_generated_exponential_uint32_float64_logic(const uint32_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_uint32_float64, scalar_generated_exponential_uint32_float64_logic, uint32_t, double)

static inline void scalar_generated_exponential_uint64_float64_logic(const uint64_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_uint64_float64, scalar_generated_exponential_uint64_float64_logic, uint64_t, double)

static inline void scalar_generated_exponential_uint8_float64_logic(const uint8_t *a, double *out) {
    *out = exp((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_exponential_uint8_float64, scalar_generated_exponential_uint8_float64_logic, uint8_t, double)

static inline void scalar_generated_logarithm_float32_float64_logic(const float *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_float32_float64, scalar_generated_logarithm_float32_float64_logic, float, double)

static inline void scalar_generated_logarithm_float64_float64_logic(const double *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_float64_float64, scalar_generated_logarithm_float64_float64_logic, double, double)

static inline void scalar_generated_logarithm_int16_float64_logic(const int16_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_int16_float64, scalar_generated_logarithm_int16_float64_logic, int16_t, double)

static inline void scalar_generated_logarithm_int32_float64_logic(const int32_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_int32_float64, scalar_generated_logarithm_int32_float64_logic, int32_t, double)

static inline void scalar_generated_logarithm_int64_float64_logic(const int64_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_int64_float64, scalar_generated_logarithm_int64_float64_logic, int64_t, double)

static inline void scalar_generated_logarithm_int8_float64_logic(const int8_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_int8_float64, scalar_generated_logarithm_int8_float64_logic, int8_t, double)

static inline void scalar_generated_logarithm_uint16_float64_logic(const uint16_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_uint16_float64, scalar_generated_logarithm_uint16_float64_logic, uint16_t, double)

static inline void scalar_generated_logarithm_uint32_float64_logic(const uint32_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_uint32_float64, scalar_generated_logarithm_uint32_float64_logic, uint32_t, double)

static inline void scalar_generated_logarithm_uint64_float64_logic(const uint64_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_uint64_float64, scalar_generated_logarithm_uint64_float64_logic, uint64_t, double)

static inline void scalar_generated_logarithm_uint8_float64_logic(const uint8_t *a, double *out) {
    *out = log((double)*a);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logarithm_uint8_float64, scalar_generated_logarithm_uint8_float64_logic, uint8_t, double)

static inline void scalar_generated_power_float32_float32_float64_logic(const float *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_float32_float64, scalar_generated_power_float32_float32_float64_logic, float, float, double)

static inline void scalar_generated_power_float32_float64_float64_logic(const float *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_float64_float64, scalar_generated_power_float32_float64_float64_logic, float, double, double)

static inline void scalar_generated_power_float32_int16_float64_logic(const float *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_int16_float64, scalar_generated_power_float32_int16_float64_logic, float, int16_t, double)

static inline void scalar_generated_power_float32_int32_float64_logic(const float *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_int32_float64, scalar_generated_power_float32_int32_float64_logic, float, int32_t, double)

static inline void scalar_generated_power_float32_int64_float64_logic(const float *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_int64_float64, scalar_generated_power_float32_int64_float64_logic, float, int64_t, double)

static inline void scalar_generated_power_float32_int8_float64_logic(const float *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_int8_float64, scalar_generated_power_float32_int8_float64_logic, float, int8_t, double)

static inline void scalar_generated_power_float32_uint16_float64_logic(const float *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_uint16_float64, scalar_generated_power_float32_uint16_float64_logic, float, uint16_t, double)

static inline void scalar_generated_power_float32_uint32_float64_logic(const float *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_uint32_float64, scalar_generated_power_float32_uint32_float64_logic, float, uint32_t, double)

static inline void scalar_generated_power_float32_uint64_float64_logic(const float *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_uint64_float64, scalar_generated_power_float32_uint64_float64_logic, float, uint64_t, double)

static inline void scalar_generated_power_float32_uint8_float64_logic(const float *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float32_uint8_float64, scalar_generated_power_float32_uint8_float64_logic, float, uint8_t, double)

static inline void scalar_generated_power_float64_float32_float64_logic(const double *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_float32_float64, scalar_generated_power_float64_float32_float64_logic, double, float, double)

static inline void scalar_generated_power_float64_float64_float64_logic(const double *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_float64_float64, scalar_generated_power_float64_float64_float64_logic, double, double, double)

static inline void scalar_generated_power_float64_int16_float64_logic(const double *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_int16_float64, scalar_generated_power_float64_int16_float64_logic, double, int16_t, double)

static inline void scalar_generated_power_float64_int32_float64_logic(const double *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_int32_float64, scalar_generated_power_float64_int32_float64_logic, double, int32_t, double)

static inline void scalar_generated_power_float64_int64_float64_logic(const double *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_int64_float64, scalar_generated_power_float64_int64_float64_logic, double, int64_t, double)

static inline void scalar_generated_power_float64_int8_float64_logic(const double *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_int8_float64, scalar_generated_power_float64_int8_float64_logic, double, int8_t, double)

static inline void scalar_generated_power_float64_uint16_float64_logic(const double *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_uint16_float64, scalar_generated_power_float64_uint16_float64_logic, double, uint16_t, double)

static inline void scalar_generated_power_float64_uint32_float64_logic(const double *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_uint32_float64, scalar_generated_power_float64_uint32_float64_logic, double, uint32_t, double)

static inline void scalar_generated_power_float64_uint64_float64_logic(const double *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_uint64_float64, scalar_generated_power_float64_uint64_float64_logic, double, uint64_t, double)

static inline void scalar_generated_power_float64_uint8_float64_logic(const double *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_float64_uint8_float64, scalar_generated_power_float64_uint8_float64_logic, double, uint8_t, double)

static inline void scalar_generated_power_int16_float32_float64_logic(const int16_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_float32_float64, scalar_generated_power_int16_float32_float64_logic, int16_t, float, double)

static inline void scalar_generated_power_int16_float64_float64_logic(const int16_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_float64_float64, scalar_generated_power_int16_float64_float64_logic, int16_t, double, double)

static inline void scalar_generated_power_int16_int16_float64_logic(const int16_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_int16_float64, scalar_generated_power_int16_int16_float64_logic, int16_t, int16_t, double)

static inline void scalar_generated_power_int16_int32_float64_logic(const int16_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_int32_float64, scalar_generated_power_int16_int32_float64_logic, int16_t, int32_t, double)

static inline void scalar_generated_power_int16_int64_float64_logic(const int16_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_int64_float64, scalar_generated_power_int16_int64_float64_logic, int16_t, int64_t, double)

static inline void scalar_generated_power_int16_int8_float64_logic(const int16_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_int8_float64, scalar_generated_power_int16_int8_float64_logic, int16_t, int8_t, double)

static inline void scalar_generated_power_int16_uint16_float64_logic(const int16_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_uint16_float64, scalar_generated_power_int16_uint16_float64_logic, int16_t, uint16_t, double)

static inline void scalar_generated_power_int16_uint32_float64_logic(const int16_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_uint32_float64, scalar_generated_power_int16_uint32_float64_logic, int16_t, uint32_t, double)

static inline void scalar_generated_power_int16_uint64_float64_logic(const int16_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_uint64_float64, scalar_generated_power_int16_uint64_float64_logic, int16_t, uint64_t, double)

static inline void scalar_generated_power_int16_uint8_float64_logic(const int16_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int16_uint8_float64, scalar_generated_power_int16_uint8_float64_logic, int16_t, uint8_t, double)

static inline void scalar_generated_power_int32_float32_float64_logic(const int32_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_float32_float64, scalar_generated_power_int32_float32_float64_logic, int32_t, float, double)

static inline void scalar_generated_power_int32_float64_float64_logic(const int32_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_float64_float64, scalar_generated_power_int32_float64_float64_logic, int32_t, double, double)

static inline void scalar_generated_power_int32_int16_float64_logic(const int32_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_int16_float64, scalar_generated_power_int32_int16_float64_logic, int32_t, int16_t, double)

static inline void scalar_generated_power_int32_int32_float64_logic(const int32_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_int32_float64, scalar_generated_power_int32_int32_float64_logic, int32_t, int32_t, double)

static inline void scalar_generated_power_int32_int64_float64_logic(const int32_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_int64_float64, scalar_generated_power_int32_int64_float64_logic, int32_t, int64_t, double)

static inline void scalar_generated_power_int32_int8_float64_logic(const int32_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_int8_float64, scalar_generated_power_int32_int8_float64_logic, int32_t, int8_t, double)

static inline void scalar_generated_power_int32_uint16_float64_logic(const int32_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_uint16_float64, scalar_generated_power_int32_uint16_float64_logic, int32_t, uint16_t, double)

static inline void scalar_generated_power_int32_uint32_float64_logic(const int32_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_uint32_float64, scalar_generated_power_int32_uint32_float64_logic, int32_t, uint32_t, double)

static inline void scalar_generated_power_int32_uint64_float64_logic(const int32_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_uint64_float64, scalar_generated_power_int32_uint64_float64_logic, int32_t, uint64_t, double)

static inline void scalar_generated_power_int32_uint8_float64_logic(const int32_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int32_uint8_float64, scalar_generated_power_int32_uint8_float64_logic, int32_t, uint8_t, double)

static inline void scalar_generated_power_int64_float32_float64_logic(const int64_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_float32_float64, scalar_generated_power_int64_float32_float64_logic, int64_t, float, double)

static inline void scalar_generated_power_int64_float64_float64_logic(const int64_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_float64_float64, scalar_generated_power_int64_float64_float64_logic, int64_t, double, double)

static inline void scalar_generated_power_int64_int16_float64_logic(const int64_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_int16_float64, scalar_generated_power_int64_int16_float64_logic, int64_t, int16_t, double)

static inline void scalar_generated_power_int64_int32_float64_logic(const int64_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_int32_float64, scalar_generated_power_int64_int32_float64_logic, int64_t, int32_t, double)

static inline void scalar_generated_power_int64_int64_float64_logic(const int64_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_int64_float64, scalar_generated_power_int64_int64_float64_logic, int64_t, int64_t, double)

static inline void scalar_generated_power_int64_int8_float64_logic(const int64_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_int8_float64, scalar_generated_power_int64_int8_float64_logic, int64_t, int8_t, double)

static inline void scalar_generated_power_int64_uint16_float64_logic(const int64_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_uint16_float64, scalar_generated_power_int64_uint16_float64_logic, int64_t, uint16_t, double)

static inline void scalar_generated_power_int64_uint32_float64_logic(const int64_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_uint32_float64, scalar_generated_power_int64_uint32_float64_logic, int64_t, uint32_t, double)

static inline void scalar_generated_power_int64_uint64_float64_logic(const int64_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_uint64_float64, scalar_generated_power_int64_uint64_float64_logic, int64_t, uint64_t, double)

static inline void scalar_generated_power_int64_uint8_float64_logic(const int64_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int64_uint8_float64, scalar_generated_power_int64_uint8_float64_logic, int64_t, uint8_t, double)

static inline void scalar_generated_power_int8_float32_float64_logic(const int8_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_float32_float64, scalar_generated_power_int8_float32_float64_logic, int8_t, float, double)

static inline void scalar_generated_power_int8_float64_float64_logic(const int8_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_float64_float64, scalar_generated_power_int8_float64_float64_logic, int8_t, double, double)

static inline void scalar_generated_power_int8_int16_float64_logic(const int8_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_int16_float64, scalar_generated_power_int8_int16_float64_logic, int8_t, int16_t, double)

static inline void scalar_generated_power_int8_int32_float64_logic(const int8_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_int32_float64, scalar_generated_power_int8_int32_float64_logic, int8_t, int32_t, double)

static inline void scalar_generated_power_int8_int64_float64_logic(const int8_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_int64_float64, scalar_generated_power_int8_int64_float64_logic, int8_t, int64_t, double)

static inline void scalar_generated_power_int8_int8_float64_logic(const int8_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_int8_float64, scalar_generated_power_int8_int8_float64_logic, int8_t, int8_t, double)

static inline void scalar_generated_power_int8_uint16_float64_logic(const int8_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_uint16_float64, scalar_generated_power_int8_uint16_float64_logic, int8_t, uint16_t, double)

static inline void scalar_generated_power_int8_uint32_float64_logic(const int8_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_uint32_float64, scalar_generated_power_int8_uint32_float64_logic, int8_t, uint32_t, double)

static inline void scalar_generated_power_int8_uint64_float64_logic(const int8_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_uint64_float64, scalar_generated_power_int8_uint64_float64_logic, int8_t, uint64_t, double)

static inline void scalar_generated_power_int8_uint8_float64_logic(const int8_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_int8_uint8_float64, scalar_generated_power_int8_uint8_float64_logic, int8_t, uint8_t, double)

static inline void scalar_generated_power_uint16_float32_float64_logic(const uint16_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_float32_float64, scalar_generated_power_uint16_float32_float64_logic, uint16_t, float, double)

static inline void scalar_generated_power_uint16_float64_float64_logic(const uint16_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_float64_float64, scalar_generated_power_uint16_float64_float64_logic, uint16_t, double, double)

static inline void scalar_generated_power_uint16_int16_float64_logic(const uint16_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_int16_float64, scalar_generated_power_uint16_int16_float64_logic, uint16_t, int16_t, double)

static inline void scalar_generated_power_uint16_int32_float64_logic(const uint16_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_int32_float64, scalar_generated_power_uint16_int32_float64_logic, uint16_t, int32_t, double)

static inline void scalar_generated_power_uint16_int64_float64_logic(const uint16_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_int64_float64, scalar_generated_power_uint16_int64_float64_logic, uint16_t, int64_t, double)

static inline void scalar_generated_power_uint16_int8_float64_logic(const uint16_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_int8_float64, scalar_generated_power_uint16_int8_float64_logic, uint16_t, int8_t, double)

static inline void scalar_generated_power_uint16_uint16_float64_logic(const uint16_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_uint16_float64, scalar_generated_power_uint16_uint16_float64_logic, uint16_t, uint16_t, double)

static inline void scalar_generated_power_uint16_uint32_float64_logic(const uint16_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_uint32_float64, scalar_generated_power_uint16_uint32_float64_logic, uint16_t, uint32_t, double)

static inline void scalar_generated_power_uint16_uint64_float64_logic(const uint16_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_uint64_float64, scalar_generated_power_uint16_uint64_float64_logic, uint16_t, uint64_t, double)

static inline void scalar_generated_power_uint16_uint8_float64_logic(const uint16_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint16_uint8_float64, scalar_generated_power_uint16_uint8_float64_logic, uint16_t, uint8_t, double)

static inline void scalar_generated_power_uint32_float32_float64_logic(const uint32_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_float32_float64, scalar_generated_power_uint32_float32_float64_logic, uint32_t, float, double)

static inline void scalar_generated_power_uint32_float64_float64_logic(const uint32_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_float64_float64, scalar_generated_power_uint32_float64_float64_logic, uint32_t, double, double)

static inline void scalar_generated_power_uint32_int16_float64_logic(const uint32_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_int16_float64, scalar_generated_power_uint32_int16_float64_logic, uint32_t, int16_t, double)

static inline void scalar_generated_power_uint32_int32_float64_logic(const uint32_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_int32_float64, scalar_generated_power_uint32_int32_float64_logic, uint32_t, int32_t, double)

static inline void scalar_generated_power_uint32_int64_float64_logic(const uint32_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_int64_float64, scalar_generated_power_uint32_int64_float64_logic, uint32_t, int64_t, double)

static inline void scalar_generated_power_uint32_int8_float64_logic(const uint32_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_int8_float64, scalar_generated_power_uint32_int8_float64_logic, uint32_t, int8_t, double)

static inline void scalar_generated_power_uint32_uint16_float64_logic(const uint32_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_uint16_float64, scalar_generated_power_uint32_uint16_float64_logic, uint32_t, uint16_t, double)

static inline void scalar_generated_power_uint32_uint32_float64_logic(const uint32_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_uint32_float64, scalar_generated_power_uint32_uint32_float64_logic, uint32_t, uint32_t, double)

static inline void scalar_generated_power_uint32_uint64_float64_logic(const uint32_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_uint64_float64, scalar_generated_power_uint32_uint64_float64_logic, uint32_t, uint64_t, double)

static inline void scalar_generated_power_uint32_uint8_float64_logic(const uint32_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint32_uint8_float64, scalar_generated_power_uint32_uint8_float64_logic, uint32_t, uint8_t, double)

static inline void scalar_generated_power_uint64_float32_float64_logic(const uint64_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_float32_float64, scalar_generated_power_uint64_float32_float64_logic, uint64_t, float, double)

static inline void scalar_generated_power_uint64_float64_float64_logic(const uint64_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_float64_float64, scalar_generated_power_uint64_float64_float64_logic, uint64_t, double, double)

static inline void scalar_generated_power_uint64_int16_float64_logic(const uint64_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_int16_float64, scalar_generated_power_uint64_int16_float64_logic, uint64_t, int16_t, double)

static inline void scalar_generated_power_uint64_int32_float64_logic(const uint64_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_int32_float64, scalar_generated_power_uint64_int32_float64_logic, uint64_t, int32_t, double)

static inline void scalar_generated_power_uint64_int64_float64_logic(const uint64_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_int64_float64, scalar_generated_power_uint64_int64_float64_logic, uint64_t, int64_t, double)

static inline void scalar_generated_power_uint64_int8_float64_logic(const uint64_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_int8_float64, scalar_generated_power_uint64_int8_float64_logic, uint64_t, int8_t, double)

static inline void scalar_generated_power_uint64_uint16_float64_logic(const uint64_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_uint16_float64, scalar_generated_power_uint64_uint16_float64_logic, uint64_t, uint16_t, double)

static inline void scalar_generated_power_uint64_uint32_float64_logic(const uint64_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_uint32_float64, scalar_generated_power_uint64_uint32_float64_logic, uint64_t, uint32_t, double)

static inline void scalar_generated_power_uint64_uint64_float64_logic(const uint64_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_uint64_float64, scalar_generated_power_uint64_uint64_float64_logic, uint64_t, uint64_t, double)

static inline void scalar_generated_power_uint64_uint8_float64_logic(const uint64_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint64_uint8_float64, scalar_generated_power_uint64_uint8_float64_logic, uint64_t, uint8_t, double)

static inline void scalar_generated_power_uint8_float32_float64_logic(const uint8_t *a, const float *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_float32_float64, scalar_generated_power_uint8_float32_float64_logic, uint8_t, float, double)

static inline void scalar_generated_power_uint8_float64_float64_logic(const uint8_t *a, const double *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_float64_float64, scalar_generated_power_uint8_float64_float64_logic, uint8_t, double, double)

static inline void scalar_generated_power_uint8_int16_float64_logic(const uint8_t *a, const int16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_int16_float64, scalar_generated_power_uint8_int16_float64_logic, uint8_t, int16_t, double)

static inline void scalar_generated_power_uint8_int32_float64_logic(const uint8_t *a, const int32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_int32_float64, scalar_generated_power_uint8_int32_float64_logic, uint8_t, int32_t, double)

static inline void scalar_generated_power_uint8_int64_float64_logic(const uint8_t *a, const int64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_int64_float64, scalar_generated_power_uint8_int64_float64_logic, uint8_t, int64_t, double)

static inline void scalar_generated_power_uint8_int8_float64_logic(const uint8_t *a, const int8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_int8_float64, scalar_generated_power_uint8_int8_float64_logic, uint8_t, int8_t, double)

static inline void scalar_generated_power_uint8_uint16_float64_logic(const uint8_t *a, const uint16_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_uint16_float64, scalar_generated_power_uint8_uint16_float64_logic, uint8_t, uint16_t, double)

static inline void scalar_generated_power_uint8_uint32_float64_logic(const uint8_t *a, const uint32_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_uint32_float64, scalar_generated_power_uint8_uint32_float64_logic, uint8_t, uint32_t, double)

static inline void scalar_generated_power_uint8_uint64_float64_logic(const uint8_t *a, const uint64_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_uint64_float64, scalar_generated_power_uint8_uint64_float64_logic, uint8_t, uint64_t, double)

static inline void scalar_generated_power_uint8_uint8_float64_logic(const uint8_t *a, const uint8_t *b, double *c) {
    *c = pow((double)*a, (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_power_uint8_uint8_float64, scalar_generated_power_uint8_uint8_float64_logic, uint8_t, uint8_t, double)

static inline void scalar_generated_equal_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_float32_bool, scalar_generated_equal_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_equal_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_float64_bool, scalar_generated_equal_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_equal_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_int16_bool, scalar_generated_equal_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_equal_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_int32_bool, scalar_generated_equal_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_equal_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_int64_bool, scalar_generated_equal_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_equal_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_int8_bool, scalar_generated_equal_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_equal_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_uint16_bool, scalar_generated_equal_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_equal_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_uint32_bool, scalar_generated_equal_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_equal_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_uint64_bool, scalar_generated_equal_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_equal_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float32_uint8_bool, scalar_generated_equal_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_equal_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_float32_bool, scalar_generated_equal_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_equal_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_float64_bool, scalar_generated_equal_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_equal_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_int16_bool, scalar_generated_equal_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_equal_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_int32_bool, scalar_generated_equal_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_equal_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_int64_bool, scalar_generated_equal_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_equal_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_int8_bool, scalar_generated_equal_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_equal_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_uint16_bool, scalar_generated_equal_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_equal_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_uint32_bool, scalar_generated_equal_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_equal_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_uint64_bool, scalar_generated_equal_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_equal_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_float64_uint8_bool, scalar_generated_equal_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_equal_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_float32_bool, scalar_generated_equal_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_equal_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_float64_bool, scalar_generated_equal_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_equal_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a == (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_int16_bool, scalar_generated_equal_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_equal_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a == (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_int32_bool, scalar_generated_equal_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_equal_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_int64_bool, scalar_generated_equal_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_equal_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a == (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_int8_bool, scalar_generated_equal_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_equal_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_uint16_bool, scalar_generated_equal_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_equal_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_uint32_bool, scalar_generated_equal_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_equal_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_uint64_bool, scalar_generated_equal_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_equal_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int16_uint8_bool, scalar_generated_equal_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_equal_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_float32_bool, scalar_generated_equal_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_equal_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_float64_bool, scalar_generated_equal_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_equal_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a == (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_int16_bool, scalar_generated_equal_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_equal_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a == (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_int32_bool, scalar_generated_equal_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_equal_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_int64_bool, scalar_generated_equal_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_equal_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a == (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_int8_bool, scalar_generated_equal_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_equal_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_uint16_bool, scalar_generated_equal_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_equal_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_uint32_bool, scalar_generated_equal_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_equal_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_uint64_bool, scalar_generated_equal_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_equal_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int32_uint8_bool, scalar_generated_equal_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_equal_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_float32_bool, scalar_generated_equal_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_equal_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_float64_bool, scalar_generated_equal_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_equal_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_int16_bool, scalar_generated_equal_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_equal_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_int32_bool, scalar_generated_equal_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_equal_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_int64_bool, scalar_generated_equal_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_equal_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_int8_bool, scalar_generated_equal_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_equal_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_uint16_bool, scalar_generated_equal_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_equal_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_uint32_bool, scalar_generated_equal_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_equal_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_uint64_bool, scalar_generated_equal_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_equal_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int64_uint8_bool, scalar_generated_equal_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_equal_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_float32_bool, scalar_generated_equal_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_equal_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_float64_bool, scalar_generated_equal_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_equal_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a == (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_int16_bool, scalar_generated_equal_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_equal_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a == (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_int32_bool, scalar_generated_equal_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_equal_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a == (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_int64_bool, scalar_generated_equal_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_equal_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a == (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_int8_bool, scalar_generated_equal_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_equal_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_uint16_bool, scalar_generated_equal_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_equal_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_uint32_bool, scalar_generated_equal_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_equal_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_uint64_bool, scalar_generated_equal_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_equal_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_int8_uint8_bool, scalar_generated_equal_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_equal_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_float32_bool, scalar_generated_equal_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_equal_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_float64_bool, scalar_generated_equal_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_equal_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_int16_bool, scalar_generated_equal_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_equal_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_int32_bool, scalar_generated_equal_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_equal_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_int64_bool, scalar_generated_equal_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_equal_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_int8_bool, scalar_generated_equal_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_equal_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a == (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_uint16_bool, scalar_generated_equal_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_equal_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a == (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_uint32_bool, scalar_generated_equal_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_equal_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_uint64_bool, scalar_generated_equal_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_equal_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a == (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint16_uint8_bool, scalar_generated_equal_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_equal_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_float32_bool, scalar_generated_equal_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_equal_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_float64_bool, scalar_generated_equal_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_equal_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_int16_bool, scalar_generated_equal_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_equal_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_int32_bool, scalar_generated_equal_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_equal_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_int64_bool, scalar_generated_equal_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_equal_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_int8_bool, scalar_generated_equal_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_equal_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a == (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_uint16_bool, scalar_generated_equal_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_equal_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a == (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_uint32_bool, scalar_generated_equal_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_equal_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_uint64_bool, scalar_generated_equal_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_equal_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a == (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint32_uint8_bool, scalar_generated_equal_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_equal_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_float32_bool, scalar_generated_equal_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_equal_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_float64_bool, scalar_generated_equal_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_equal_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_int16_bool, scalar_generated_equal_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_equal_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_int32_bool, scalar_generated_equal_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_equal_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_int64_bool, scalar_generated_equal_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_equal_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_int8_bool, scalar_generated_equal_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_equal_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_uint16_bool, scalar_generated_equal_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_equal_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_uint32_bool, scalar_generated_equal_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_equal_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_uint64_bool, scalar_generated_equal_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_equal_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint64_uint8_bool, scalar_generated_equal_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_equal_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_float32_bool, scalar_generated_equal_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_equal_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a == (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_float64_bool, scalar_generated_equal_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_equal_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_int16_bool, scalar_generated_equal_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_equal_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_int32_bool, scalar_generated_equal_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_equal_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_int64_bool, scalar_generated_equal_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_equal_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) == 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_int8_bool, scalar_generated_equal_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_equal_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a == (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_uint16_bool, scalar_generated_equal_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_equal_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a == (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_uint32_bool, scalar_generated_equal_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_equal_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a == (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_uint64_bool, scalar_generated_equal_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_equal_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a == (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_equal_uint8_uint8_bool, scalar_generated_equal_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_not_equal_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_float32_bool, scalar_generated_not_equal_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_not_equal_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_float64_bool, scalar_generated_not_equal_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_not_equal_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_int16_bool, scalar_generated_not_equal_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_not_equal_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_int32_bool, scalar_generated_not_equal_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_not_equal_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_int64_bool, scalar_generated_not_equal_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_not_equal_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_int8_bool, scalar_generated_not_equal_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_not_equal_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_uint16_bool, scalar_generated_not_equal_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_not_equal_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_uint32_bool, scalar_generated_not_equal_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_not_equal_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_uint64_bool, scalar_generated_not_equal_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_not_equal_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float32_uint8_bool, scalar_generated_not_equal_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_not_equal_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_float32_bool, scalar_generated_not_equal_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_not_equal_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_float64_bool, scalar_generated_not_equal_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_not_equal_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_int16_bool, scalar_generated_not_equal_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_not_equal_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_int32_bool, scalar_generated_not_equal_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_not_equal_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_int64_bool, scalar_generated_not_equal_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_not_equal_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_int8_bool, scalar_generated_not_equal_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_not_equal_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_uint16_bool, scalar_generated_not_equal_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_not_equal_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_uint32_bool, scalar_generated_not_equal_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_not_equal_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_uint64_bool, scalar_generated_not_equal_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_not_equal_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_float64_uint8_bool, scalar_generated_not_equal_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_not_equal_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_float32_bool, scalar_generated_not_equal_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_not_equal_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_float64_bool, scalar_generated_not_equal_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_not_equal_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a != (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_int16_bool, scalar_generated_not_equal_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_not_equal_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a != (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_int32_bool, scalar_generated_not_equal_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_not_equal_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_int64_bool, scalar_generated_not_equal_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_not_equal_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a != (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_int8_bool, scalar_generated_not_equal_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_not_equal_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_uint16_bool, scalar_generated_not_equal_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_not_equal_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_uint32_bool, scalar_generated_not_equal_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_not_equal_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_uint64_bool, scalar_generated_not_equal_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_not_equal_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int16_uint8_bool, scalar_generated_not_equal_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_not_equal_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_float32_bool, scalar_generated_not_equal_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_not_equal_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_float64_bool, scalar_generated_not_equal_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_not_equal_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a != (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_int16_bool, scalar_generated_not_equal_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_not_equal_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a != (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_int32_bool, scalar_generated_not_equal_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_not_equal_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_int64_bool, scalar_generated_not_equal_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_not_equal_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a != (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_int8_bool, scalar_generated_not_equal_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_not_equal_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_uint16_bool, scalar_generated_not_equal_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_not_equal_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_uint32_bool, scalar_generated_not_equal_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_not_equal_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_uint64_bool, scalar_generated_not_equal_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_not_equal_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int32_uint8_bool, scalar_generated_not_equal_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_not_equal_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_float32_bool, scalar_generated_not_equal_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_not_equal_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_float64_bool, scalar_generated_not_equal_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_not_equal_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_int16_bool, scalar_generated_not_equal_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_not_equal_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_int32_bool, scalar_generated_not_equal_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_not_equal_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_int64_bool, scalar_generated_not_equal_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_not_equal_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_int8_bool, scalar_generated_not_equal_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_not_equal_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_uint16_bool, scalar_generated_not_equal_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_not_equal_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_uint32_bool, scalar_generated_not_equal_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_not_equal_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_uint64_bool, scalar_generated_not_equal_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_not_equal_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int64_uint8_bool, scalar_generated_not_equal_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_not_equal_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_float32_bool, scalar_generated_not_equal_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_not_equal_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_float64_bool, scalar_generated_not_equal_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_not_equal_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a != (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_int16_bool, scalar_generated_not_equal_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_not_equal_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a != (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_int32_bool, scalar_generated_not_equal_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_not_equal_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a != (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_int64_bool, scalar_generated_not_equal_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_not_equal_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a != (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_int8_bool, scalar_generated_not_equal_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_not_equal_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_uint16_bool, scalar_generated_not_equal_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_not_equal_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_uint32_bool, scalar_generated_not_equal_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_not_equal_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_uint64_bool, scalar_generated_not_equal_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_not_equal_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_int8_uint8_bool, scalar_generated_not_equal_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_not_equal_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_float32_bool, scalar_generated_not_equal_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_not_equal_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_float64_bool, scalar_generated_not_equal_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_not_equal_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_int16_bool, scalar_generated_not_equal_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_not_equal_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_int32_bool, scalar_generated_not_equal_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_not_equal_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_int64_bool, scalar_generated_not_equal_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_not_equal_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_int8_bool, scalar_generated_not_equal_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_not_equal_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a != (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_uint16_bool, scalar_generated_not_equal_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_not_equal_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a != (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_uint32_bool, scalar_generated_not_equal_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_not_equal_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_uint64_bool, scalar_generated_not_equal_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_not_equal_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a != (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint16_uint8_bool, scalar_generated_not_equal_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_not_equal_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_float32_bool, scalar_generated_not_equal_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_not_equal_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_float64_bool, scalar_generated_not_equal_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_not_equal_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_int16_bool, scalar_generated_not_equal_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_not_equal_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_int32_bool, scalar_generated_not_equal_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_not_equal_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_int64_bool, scalar_generated_not_equal_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_not_equal_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_int8_bool, scalar_generated_not_equal_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_not_equal_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a != (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_uint16_bool, scalar_generated_not_equal_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_not_equal_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a != (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_uint32_bool, scalar_generated_not_equal_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_not_equal_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_uint64_bool, scalar_generated_not_equal_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_not_equal_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a != (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint32_uint8_bool, scalar_generated_not_equal_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_not_equal_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_float32_bool, scalar_generated_not_equal_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_not_equal_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_float64_bool, scalar_generated_not_equal_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_not_equal_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_int16_bool, scalar_generated_not_equal_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_not_equal_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_int32_bool, scalar_generated_not_equal_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_not_equal_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_int64_bool, scalar_generated_not_equal_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_not_equal_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_int8_bool, scalar_generated_not_equal_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_not_equal_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_uint16_bool, scalar_generated_not_equal_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_not_equal_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_uint32_bool, scalar_generated_not_equal_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_not_equal_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_uint64_bool, scalar_generated_not_equal_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_not_equal_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint64_uint8_bool, scalar_generated_not_equal_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_not_equal_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_float32_bool, scalar_generated_not_equal_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_not_equal_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a != (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_float64_bool, scalar_generated_not_equal_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_not_equal_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_int16_bool, scalar_generated_not_equal_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_not_equal_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_int32_bool, scalar_generated_not_equal_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_not_equal_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_int64_bool, scalar_generated_not_equal_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_not_equal_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) != 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_int8_bool, scalar_generated_not_equal_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_not_equal_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a != (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_uint16_bool, scalar_generated_not_equal_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_not_equal_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a != (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_uint32_bool, scalar_generated_not_equal_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_not_equal_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a != (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_uint64_bool, scalar_generated_not_equal_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_not_equal_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a != (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_not_equal_uint8_uint8_bool, scalar_generated_not_equal_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_less_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_float32_bool, scalar_generated_less_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_less_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_float64_bool, scalar_generated_less_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_less_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_int16_bool, scalar_generated_less_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_less_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_int32_bool, scalar_generated_less_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_less_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_int64_bool, scalar_generated_less_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_less_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_int8_bool, scalar_generated_less_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_less_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_uint16_bool, scalar_generated_less_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_less_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_uint32_bool, scalar_generated_less_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_less_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_uint64_bool, scalar_generated_less_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_less_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float32_uint8_bool, scalar_generated_less_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_less_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_float32_bool, scalar_generated_less_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_less_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_float64_bool, scalar_generated_less_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_less_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_int16_bool, scalar_generated_less_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_less_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_int32_bool, scalar_generated_less_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_less_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_int64_bool, scalar_generated_less_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_less_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_int8_bool, scalar_generated_less_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_less_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_uint16_bool, scalar_generated_less_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_less_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_uint32_bool, scalar_generated_less_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_less_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_uint64_bool, scalar_generated_less_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_less_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_float64_uint8_bool, scalar_generated_less_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_less_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_float32_bool, scalar_generated_less_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_less_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_float64_bool, scalar_generated_less_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_less_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a < (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_int16_bool, scalar_generated_less_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_less_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a < (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_int32_bool, scalar_generated_less_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_less_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_int64_bool, scalar_generated_less_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_less_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a < (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_int8_bool, scalar_generated_less_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_less_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_uint16_bool, scalar_generated_less_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_less_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_uint32_bool, scalar_generated_less_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_less_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_uint64_bool, scalar_generated_less_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_less_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int16_uint8_bool, scalar_generated_less_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_less_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_float32_bool, scalar_generated_less_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_less_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_float64_bool, scalar_generated_less_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_less_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a < (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_int16_bool, scalar_generated_less_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_less_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a < (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_int32_bool, scalar_generated_less_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_less_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_int64_bool, scalar_generated_less_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_less_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a < (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_int8_bool, scalar_generated_less_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_less_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_uint16_bool, scalar_generated_less_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_less_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_uint32_bool, scalar_generated_less_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_less_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_uint64_bool, scalar_generated_less_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_less_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int32_uint8_bool, scalar_generated_less_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_less_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_float32_bool, scalar_generated_less_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_less_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_float64_bool, scalar_generated_less_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_less_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_int16_bool, scalar_generated_less_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_less_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_int32_bool, scalar_generated_less_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_less_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_int64_bool, scalar_generated_less_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_less_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_int8_bool, scalar_generated_less_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_less_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_uint16_bool, scalar_generated_less_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_less_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_uint32_bool, scalar_generated_less_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_less_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_uint64_bool, scalar_generated_less_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_less_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int64_uint8_bool, scalar_generated_less_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_less_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_float32_bool, scalar_generated_less_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_less_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_float64_bool, scalar_generated_less_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_less_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a < (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_int16_bool, scalar_generated_less_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_less_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a < (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_int32_bool, scalar_generated_less_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_less_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a < (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_int64_bool, scalar_generated_less_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_less_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a < (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_int8_bool, scalar_generated_less_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_less_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_uint16_bool, scalar_generated_less_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_less_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_uint32_bool, scalar_generated_less_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_less_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_uint64_bool, scalar_generated_less_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_less_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_int8_uint8_bool, scalar_generated_less_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_less_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_float32_bool, scalar_generated_less_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_less_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_float64_bool, scalar_generated_less_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_less_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_int16_bool, scalar_generated_less_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_less_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_int32_bool, scalar_generated_less_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_less_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_int64_bool, scalar_generated_less_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_less_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_int8_bool, scalar_generated_less_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_less_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a < (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_uint16_bool, scalar_generated_less_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_less_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a < (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_uint32_bool, scalar_generated_less_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_less_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_uint64_bool, scalar_generated_less_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_less_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a < (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint16_uint8_bool, scalar_generated_less_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_less_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_float32_bool, scalar_generated_less_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_less_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_float64_bool, scalar_generated_less_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_less_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_int16_bool, scalar_generated_less_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_less_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_int32_bool, scalar_generated_less_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_less_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_int64_bool, scalar_generated_less_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_less_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_int8_bool, scalar_generated_less_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_less_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a < (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_uint16_bool, scalar_generated_less_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_less_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a < (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_uint32_bool, scalar_generated_less_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_less_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_uint64_bool, scalar_generated_less_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_less_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a < (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint32_uint8_bool, scalar_generated_less_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_less_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_float32_bool, scalar_generated_less_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_less_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_float64_bool, scalar_generated_less_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_less_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_int16_bool, scalar_generated_less_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_less_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_int32_bool, scalar_generated_less_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_less_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_int64_bool, scalar_generated_less_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_less_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_int8_bool, scalar_generated_less_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_less_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_uint16_bool, scalar_generated_less_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_less_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_uint32_bool, scalar_generated_less_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_less_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_uint64_bool, scalar_generated_less_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_less_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint64_uint8_bool, scalar_generated_less_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_less_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_float32_bool, scalar_generated_less_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_less_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a < (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_float64_bool, scalar_generated_less_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_less_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_int16_bool, scalar_generated_less_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_less_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_int32_bool, scalar_generated_less_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_less_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_int64_bool, scalar_generated_less_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_less_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) < 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_int8_bool, scalar_generated_less_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_less_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a < (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_uint16_bool, scalar_generated_less_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_less_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a < (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_uint32_bool, scalar_generated_less_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_less_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a < (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_uint64_bool, scalar_generated_less_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_less_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a < (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_uint8_uint8_bool, scalar_generated_less_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_less_equal_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_float32_bool, scalar_generated_less_equal_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_less_equal_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_float64_bool, scalar_generated_less_equal_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_less_equal_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_int16_bool, scalar_generated_less_equal_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_less_equal_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_int32_bool, scalar_generated_less_equal_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_less_equal_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_int64_bool, scalar_generated_less_equal_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_less_equal_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_int8_bool, scalar_generated_less_equal_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_less_equal_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_uint16_bool, scalar_generated_less_equal_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_less_equal_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_uint32_bool, scalar_generated_less_equal_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_less_equal_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_uint64_bool, scalar_generated_less_equal_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_less_equal_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float32_uint8_bool, scalar_generated_less_equal_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_less_equal_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_float32_bool, scalar_generated_less_equal_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_less_equal_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_float64_bool, scalar_generated_less_equal_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_less_equal_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_int16_bool, scalar_generated_less_equal_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_less_equal_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_int32_bool, scalar_generated_less_equal_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_less_equal_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_int64_bool, scalar_generated_less_equal_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_less_equal_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_int8_bool, scalar_generated_less_equal_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_less_equal_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_uint16_bool, scalar_generated_less_equal_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_less_equal_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_uint32_bool, scalar_generated_less_equal_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_less_equal_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_uint64_bool, scalar_generated_less_equal_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_less_equal_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_float64_uint8_bool, scalar_generated_less_equal_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_less_equal_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_float32_bool, scalar_generated_less_equal_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_less_equal_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_float64_bool, scalar_generated_less_equal_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_less_equal_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a <= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_int16_bool, scalar_generated_less_equal_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_less_equal_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a <= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_int32_bool, scalar_generated_less_equal_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_less_equal_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_int64_bool, scalar_generated_less_equal_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_less_equal_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a <= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_int8_bool, scalar_generated_less_equal_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_less_equal_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_uint16_bool, scalar_generated_less_equal_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_less_equal_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_uint32_bool, scalar_generated_less_equal_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_less_equal_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_uint64_bool, scalar_generated_less_equal_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_less_equal_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int16_uint8_bool, scalar_generated_less_equal_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_less_equal_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_float32_bool, scalar_generated_less_equal_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_less_equal_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_float64_bool, scalar_generated_less_equal_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_less_equal_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a <= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_int16_bool, scalar_generated_less_equal_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_less_equal_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a <= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_int32_bool, scalar_generated_less_equal_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_less_equal_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_int64_bool, scalar_generated_less_equal_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_less_equal_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a <= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_int8_bool, scalar_generated_less_equal_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_less_equal_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_uint16_bool, scalar_generated_less_equal_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_less_equal_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_uint32_bool, scalar_generated_less_equal_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_less_equal_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_uint64_bool, scalar_generated_less_equal_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_less_equal_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int32_uint8_bool, scalar_generated_less_equal_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_less_equal_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_float32_bool, scalar_generated_less_equal_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_less_equal_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_float64_bool, scalar_generated_less_equal_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_less_equal_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_int16_bool, scalar_generated_less_equal_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_less_equal_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_int32_bool, scalar_generated_less_equal_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_less_equal_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_int64_bool, scalar_generated_less_equal_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_less_equal_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_int8_bool, scalar_generated_less_equal_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_less_equal_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_uint16_bool, scalar_generated_less_equal_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_less_equal_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_uint32_bool, scalar_generated_less_equal_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_less_equal_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_uint64_bool, scalar_generated_less_equal_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_less_equal_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int64_uint8_bool, scalar_generated_less_equal_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_less_equal_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_float32_bool, scalar_generated_less_equal_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_less_equal_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_float64_bool, scalar_generated_less_equal_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_less_equal_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a <= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_int16_bool, scalar_generated_less_equal_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_less_equal_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a <= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_int32_bool, scalar_generated_less_equal_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_less_equal_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a <= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_int64_bool, scalar_generated_less_equal_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_less_equal_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a <= (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_int8_bool, scalar_generated_less_equal_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_less_equal_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_uint16_bool, scalar_generated_less_equal_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_less_equal_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_uint32_bool, scalar_generated_less_equal_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_less_equal_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_uint64_bool, scalar_generated_less_equal_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_less_equal_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_int8_uint8_bool, scalar_generated_less_equal_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_less_equal_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_float32_bool, scalar_generated_less_equal_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_less_equal_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_float64_bool, scalar_generated_less_equal_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_less_equal_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_int16_bool, scalar_generated_less_equal_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_less_equal_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_int32_bool, scalar_generated_less_equal_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_less_equal_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_int64_bool, scalar_generated_less_equal_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_less_equal_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_int8_bool, scalar_generated_less_equal_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_less_equal_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a <= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_uint16_bool, scalar_generated_less_equal_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_less_equal_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a <= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_uint32_bool, scalar_generated_less_equal_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_less_equal_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_uint64_bool, scalar_generated_less_equal_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_less_equal_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a <= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint16_uint8_bool, scalar_generated_less_equal_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_less_equal_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_float32_bool, scalar_generated_less_equal_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_less_equal_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_float64_bool, scalar_generated_less_equal_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_less_equal_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_int16_bool, scalar_generated_less_equal_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_less_equal_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_int32_bool, scalar_generated_less_equal_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_less_equal_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_int64_bool, scalar_generated_less_equal_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_less_equal_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_int8_bool, scalar_generated_less_equal_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_less_equal_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a <= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_uint16_bool, scalar_generated_less_equal_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_less_equal_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a <= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_uint32_bool, scalar_generated_less_equal_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_less_equal_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_uint64_bool, scalar_generated_less_equal_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_less_equal_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a <= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint32_uint8_bool, scalar_generated_less_equal_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_less_equal_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_float32_bool, scalar_generated_less_equal_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_less_equal_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_float64_bool, scalar_generated_less_equal_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_less_equal_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_int16_bool, scalar_generated_less_equal_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_less_equal_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_int32_bool, scalar_generated_less_equal_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_less_equal_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_int64_bool, scalar_generated_less_equal_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_less_equal_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_int8_bool, scalar_generated_less_equal_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_less_equal_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_uint16_bool, scalar_generated_less_equal_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_less_equal_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_uint32_bool, scalar_generated_less_equal_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_less_equal_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_uint64_bool, scalar_generated_less_equal_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_less_equal_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint64_uint8_bool, scalar_generated_less_equal_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_less_equal_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_float32_bool, scalar_generated_less_equal_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_less_equal_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a <= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_float64_bool, scalar_generated_less_equal_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_less_equal_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_int16_bool, scalar_generated_less_equal_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_less_equal_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_int32_bool, scalar_generated_less_equal_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_less_equal_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_int64_bool, scalar_generated_less_equal_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_less_equal_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) <= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_int8_bool, scalar_generated_less_equal_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_less_equal_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a <= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_uint16_bool, scalar_generated_less_equal_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_less_equal_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a <= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_uint32_bool, scalar_generated_less_equal_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_less_equal_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a <= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_uint64_bool, scalar_generated_less_equal_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_less_equal_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a <= (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_less_equal_uint8_uint8_bool, scalar_generated_less_equal_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_greater_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_float32_bool, scalar_generated_greater_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_greater_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_float64_bool, scalar_generated_greater_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_greater_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_int16_bool, scalar_generated_greater_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_greater_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_int32_bool, scalar_generated_greater_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_greater_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_int64_bool, scalar_generated_greater_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_greater_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_int8_bool, scalar_generated_greater_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_greater_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_uint16_bool, scalar_generated_greater_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_greater_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_uint32_bool, scalar_generated_greater_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_greater_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_uint64_bool, scalar_generated_greater_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_greater_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float32_uint8_bool, scalar_generated_greater_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_greater_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_float32_bool, scalar_generated_greater_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_greater_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_float64_bool, scalar_generated_greater_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_greater_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_int16_bool, scalar_generated_greater_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_greater_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_int32_bool, scalar_generated_greater_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_greater_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_int64_bool, scalar_generated_greater_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_greater_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_int8_bool, scalar_generated_greater_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_greater_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_uint16_bool, scalar_generated_greater_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_greater_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_uint32_bool, scalar_generated_greater_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_greater_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_uint64_bool, scalar_generated_greater_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_greater_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_float64_uint8_bool, scalar_generated_greater_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_greater_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_float32_bool, scalar_generated_greater_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_greater_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_float64_bool, scalar_generated_greater_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_greater_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a > (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_int16_bool, scalar_generated_greater_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_greater_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a > (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_int32_bool, scalar_generated_greater_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_greater_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_int64_bool, scalar_generated_greater_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_greater_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a > (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_int8_bool, scalar_generated_greater_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_greater_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_uint16_bool, scalar_generated_greater_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_greater_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_uint32_bool, scalar_generated_greater_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_greater_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_uint64_bool, scalar_generated_greater_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_greater_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int16_uint8_bool, scalar_generated_greater_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_greater_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_float32_bool, scalar_generated_greater_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_greater_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_float64_bool, scalar_generated_greater_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_greater_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a > (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_int16_bool, scalar_generated_greater_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_greater_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a > (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_int32_bool, scalar_generated_greater_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_greater_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_int64_bool, scalar_generated_greater_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_greater_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a > (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_int8_bool, scalar_generated_greater_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_greater_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_uint16_bool, scalar_generated_greater_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_greater_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_uint32_bool, scalar_generated_greater_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_greater_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_uint64_bool, scalar_generated_greater_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_greater_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int32_uint8_bool, scalar_generated_greater_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_greater_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_float32_bool, scalar_generated_greater_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_greater_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_float64_bool, scalar_generated_greater_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_greater_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_int16_bool, scalar_generated_greater_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_greater_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_int32_bool, scalar_generated_greater_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_greater_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_int64_bool, scalar_generated_greater_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_greater_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_int8_bool, scalar_generated_greater_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_greater_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_uint16_bool, scalar_generated_greater_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_greater_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_uint32_bool, scalar_generated_greater_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_greater_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_uint64_bool, scalar_generated_greater_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_greater_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int64_uint8_bool, scalar_generated_greater_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_greater_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_float32_bool, scalar_generated_greater_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_greater_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_float64_bool, scalar_generated_greater_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_greater_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a > (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_int16_bool, scalar_generated_greater_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_greater_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a > (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_int32_bool, scalar_generated_greater_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_greater_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a > (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_int64_bool, scalar_generated_greater_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_greater_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a > (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_int8_bool, scalar_generated_greater_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_greater_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_uint16_bool, scalar_generated_greater_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_greater_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_uint32_bool, scalar_generated_greater_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_greater_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_uint64_bool, scalar_generated_greater_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_greater_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_int8_uint8_bool, scalar_generated_greater_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_greater_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_float32_bool, scalar_generated_greater_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_greater_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_float64_bool, scalar_generated_greater_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_greater_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_int16_bool, scalar_generated_greater_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_greater_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_int32_bool, scalar_generated_greater_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_greater_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_int64_bool, scalar_generated_greater_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_greater_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_int8_bool, scalar_generated_greater_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_greater_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a > (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_uint16_bool, scalar_generated_greater_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_greater_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a > (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_uint32_bool, scalar_generated_greater_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_greater_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_uint64_bool, scalar_generated_greater_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_greater_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a > (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint16_uint8_bool, scalar_generated_greater_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_greater_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_float32_bool, scalar_generated_greater_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_greater_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_float64_bool, scalar_generated_greater_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_greater_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_int16_bool, scalar_generated_greater_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_greater_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_int32_bool, scalar_generated_greater_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_greater_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_int64_bool, scalar_generated_greater_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_greater_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_int8_bool, scalar_generated_greater_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_greater_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a > (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_uint16_bool, scalar_generated_greater_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_greater_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a > (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_uint32_bool, scalar_generated_greater_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_greater_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_uint64_bool, scalar_generated_greater_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_greater_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a > (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint32_uint8_bool, scalar_generated_greater_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_greater_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_float32_bool, scalar_generated_greater_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_greater_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_float64_bool, scalar_generated_greater_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_greater_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_int16_bool, scalar_generated_greater_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_greater_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_int32_bool, scalar_generated_greater_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_greater_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_int64_bool, scalar_generated_greater_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_greater_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_int8_bool, scalar_generated_greater_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_greater_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_uint16_bool, scalar_generated_greater_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_greater_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_uint32_bool, scalar_generated_greater_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_greater_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_uint64_bool, scalar_generated_greater_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_greater_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint64_uint8_bool, scalar_generated_greater_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_greater_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_float32_bool, scalar_generated_greater_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_greater_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a > (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_float64_bool, scalar_generated_greater_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_greater_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_int16_bool, scalar_generated_greater_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_greater_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_int32_bool, scalar_generated_greater_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_greater_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_int64_bool, scalar_generated_greater_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_greater_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) > 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_int8_bool, scalar_generated_greater_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_greater_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a > (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_uint16_bool, scalar_generated_greater_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_greater_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a > (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_uint32_bool, scalar_generated_greater_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_greater_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a > (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_uint64_bool, scalar_generated_greater_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_greater_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a > (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_uint8_uint8_bool, scalar_generated_greater_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_float32_bool, scalar_generated_greater_equal_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_greater_equal_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_float64_bool, scalar_generated_greater_equal_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_greater_equal_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_int16_bool, scalar_generated_greater_equal_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_greater_equal_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_int32_bool, scalar_generated_greater_equal_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_greater_equal_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_int64_bool, scalar_generated_greater_equal_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_greater_equal_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_int8_bool, scalar_generated_greater_equal_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_greater_equal_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_uint16_bool, scalar_generated_greater_equal_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_greater_equal_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_uint32_bool, scalar_generated_greater_equal_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_greater_equal_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_uint64_bool, scalar_generated_greater_equal_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_greater_equal_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float32_uint8_bool, scalar_generated_greater_equal_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_greater_equal_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_float32_bool, scalar_generated_greater_equal_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_greater_equal_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_float64_bool, scalar_generated_greater_equal_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_greater_equal_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_int16_bool, scalar_generated_greater_equal_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_greater_equal_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_int32_bool, scalar_generated_greater_equal_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_greater_equal_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_int64_bool, scalar_generated_greater_equal_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_greater_equal_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_int8_bool, scalar_generated_greater_equal_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_greater_equal_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_uint16_bool, scalar_generated_greater_equal_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_greater_equal_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_uint32_bool, scalar_generated_greater_equal_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_greater_equal_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_uint64_bool, scalar_generated_greater_equal_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_greater_equal_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_float64_uint8_bool, scalar_generated_greater_equal_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_greater_equal_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_float32_bool, scalar_generated_greater_equal_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_greater_equal_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_float64_bool, scalar_generated_greater_equal_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_greater_equal_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a >= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_int16_bool, scalar_generated_greater_equal_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_greater_equal_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a >= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_int32_bool, scalar_generated_greater_equal_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_greater_equal_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_int64_bool, scalar_generated_greater_equal_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_greater_equal_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int16_t)*a >= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_int8_bool, scalar_generated_greater_equal_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_greater_equal_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_uint16_bool, scalar_generated_greater_equal_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_uint32_bool, scalar_generated_greater_equal_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_uint64_bool, scalar_generated_greater_equal_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int16_uint8_bool, scalar_generated_greater_equal_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_float32_bool, scalar_generated_greater_equal_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_greater_equal_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_float64_bool, scalar_generated_greater_equal_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_greater_equal_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int32_t)*a >= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_int16_bool, scalar_generated_greater_equal_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_greater_equal_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a >= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_int32_bool, scalar_generated_greater_equal_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_greater_equal_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_int64_bool, scalar_generated_greater_equal_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_greater_equal_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int32_t)*a >= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_int8_bool, scalar_generated_greater_equal_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_greater_equal_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_uint16_bool, scalar_generated_greater_equal_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_uint32_bool, scalar_generated_greater_equal_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_uint64_bool, scalar_generated_greater_equal_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int32_uint8_bool, scalar_generated_greater_equal_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_float32_bool, scalar_generated_greater_equal_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_greater_equal_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_float64_bool, scalar_generated_greater_equal_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_greater_equal_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_int16_bool, scalar_generated_greater_equal_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_greater_equal_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_int32_bool, scalar_generated_greater_equal_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_greater_equal_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_int64_bool, scalar_generated_greater_equal_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_greater_equal_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_int8_bool, scalar_generated_greater_equal_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_greater_equal_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_uint16_bool, scalar_generated_greater_equal_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_uint32_bool, scalar_generated_greater_equal_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_uint64_bool, scalar_generated_greater_equal_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int64_uint8_bool, scalar_generated_greater_equal_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_float32_bool, scalar_generated_greater_equal_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_greater_equal_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_float64_bool, scalar_generated_greater_equal_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_greater_equal_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((int16_t)*a >= (int16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_int16_bool, scalar_generated_greater_equal_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_greater_equal_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((int32_t)*a >= (int32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_int32_bool, scalar_generated_greater_equal_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_greater_equal_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((int64_t)*a >= (int64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_int64_bool, scalar_generated_greater_equal_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_greater_equal_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((int8_t)*a >= (int8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_int8_bool, scalar_generated_greater_equal_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_greater_equal_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_uint16_bool, scalar_generated_greater_equal_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_uint32_bool, scalar_generated_greater_equal_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_uint64_bool, scalar_generated_greater_equal_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_int8_uint8_bool, scalar_generated_greater_equal_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_float32_bool, scalar_generated_greater_equal_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_greater_equal_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_float64_bool, scalar_generated_greater_equal_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_greater_equal_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_int16_bool, scalar_generated_greater_equal_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_greater_equal_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_int32_bool, scalar_generated_greater_equal_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_greater_equal_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_int64_bool, scalar_generated_greater_equal_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_greater_equal_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_int8_bool, scalar_generated_greater_equal_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_greater_equal_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a >= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_uint16_bool, scalar_generated_greater_equal_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a >= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_uint32_bool, scalar_generated_greater_equal_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_uint64_bool, scalar_generated_greater_equal_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint16_t)*a >= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint16_uint8_bool, scalar_generated_greater_equal_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_float32_bool, scalar_generated_greater_equal_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_greater_equal_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_float64_bool, scalar_generated_greater_equal_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_greater_equal_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_int16_bool, scalar_generated_greater_equal_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_greater_equal_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_int32_bool, scalar_generated_greater_equal_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_greater_equal_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_int64_bool, scalar_generated_greater_equal_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_greater_equal_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_int8_bool, scalar_generated_greater_equal_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_greater_equal_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint32_t)*a >= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_uint16_bool, scalar_generated_greater_equal_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a >= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_uint32_bool, scalar_generated_greater_equal_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_uint64_bool, scalar_generated_greater_equal_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint32_t)*a >= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint32_uint8_bool, scalar_generated_greater_equal_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_float32_bool, scalar_generated_greater_equal_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_greater_equal_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_float64_bool, scalar_generated_greater_equal_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_greater_equal_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_int16_bool, scalar_generated_greater_equal_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_greater_equal_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_int32_bool, scalar_generated_greater_equal_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_greater_equal_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_int64_bool, scalar_generated_greater_equal_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_greater_equal_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_int8_bool, scalar_generated_greater_equal_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_greater_equal_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_uint16_bool, scalar_generated_greater_equal_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_uint32_bool, scalar_generated_greater_equal_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_uint64_bool, scalar_generated_greater_equal_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint64_uint8_bool, scalar_generated_greater_equal_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_greater_equal_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_float32_bool, scalar_generated_greater_equal_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_greater_equal_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((double)*a >= (double)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_float64_bool, scalar_generated_greater_equal_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_greater_equal_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_int16_bool, scalar_generated_greater_equal_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_greater_equal_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_int32_bool, scalar_generated_greater_equal_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_greater_equal_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_int64_bool, scalar_generated_greater_equal_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_greater_equal_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)(runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b) >= 0);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_int8_bool, scalar_generated_greater_equal_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_greater_equal_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((uint16_t)*a >= (uint16_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_uint16_bool, scalar_generated_greater_equal_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_greater_equal_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((uint32_t)*a >= (uint32_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_uint32_bool, scalar_generated_greater_equal_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_greater_equal_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((uint64_t)*a >= (uint64_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_uint64_bool, scalar_generated_greater_equal_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_greater_equal_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((uint8_t)*a >= (uint8_t)*b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_greater_equal_uint8_uint8_bool, scalar_generated_greater_equal_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_logical_and_bool_bool_bool_logic(const bool *a, const bool *b, bool *c) {
    *c = (bool)(*a && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_bool_bool, scalar_generated_logical_and_bool_bool_bool_logic, bool, bool, bool)

static inline void scalar_generated_logical_and_bool_float32_bool_logic(const bool *a, const float *b, bool *c) {
    *c = (bool)(*a && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_float32_bool, scalar_generated_logical_and_bool_float32_bool_logic, bool, float, bool)

static inline void scalar_generated_logical_and_bool_float64_bool_logic(const bool *a, const double *b, bool *c) {
    *c = (bool)(*a && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_float64_bool, scalar_generated_logical_and_bool_float64_bool_logic, bool, double, bool)

static inline void scalar_generated_logical_and_bool_int16_bool_logic(const bool *a, const int16_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_int16_bool, scalar_generated_logical_and_bool_int16_bool_logic, bool, int16_t, bool)

static inline void scalar_generated_logical_and_bool_int32_bool_logic(const bool *a, const int32_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_int32_bool, scalar_generated_logical_and_bool_int32_bool_logic, bool, int32_t, bool)

static inline void scalar_generated_logical_and_bool_int64_bool_logic(const bool *a, const int64_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_int64_bool, scalar_generated_logical_and_bool_int64_bool_logic, bool, int64_t, bool)

static inline void scalar_generated_logical_and_bool_int8_bool_logic(const bool *a, const int8_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_int8_bool, scalar_generated_logical_and_bool_int8_bool_logic, bool, int8_t, bool)

static inline void scalar_generated_logical_and_bool_uint16_bool_logic(const bool *a, const uint16_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_uint16_bool, scalar_generated_logical_and_bool_uint16_bool_logic, bool, uint16_t, bool)

static inline void scalar_generated_logical_and_bool_uint32_bool_logic(const bool *a, const uint32_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_uint32_bool, scalar_generated_logical_and_bool_uint32_bool_logic, bool, uint32_t, bool)

static inline void scalar_generated_logical_and_bool_uint64_bool_logic(const bool *a, const uint64_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_uint64_bool, scalar_generated_logical_and_bool_uint64_bool_logic, bool, uint64_t, bool)

static inline void scalar_generated_logical_and_bool_uint8_bool_logic(const bool *a, const uint8_t *b, bool *c) {
    *c = (bool)(*a && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_bool_uint8_bool, scalar_generated_logical_and_bool_uint8_bool_logic, bool, uint8_t, bool)

static inline void scalar_generated_logical_and_float32_bool_bool_logic(const float *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0.0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_bool_bool, scalar_generated_logical_and_float32_bool_bool_logic, float, bool, bool)

static inline void scalar_generated_logical_and_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_float32_bool, scalar_generated_logical_and_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_logical_and_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_float64_bool, scalar_generated_logical_and_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_logical_and_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_int16_bool, scalar_generated_logical_and_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_logical_and_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_int32_bool, scalar_generated_logical_and_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_logical_and_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_int64_bool, scalar_generated_logical_and_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_logical_and_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_int8_bool, scalar_generated_logical_and_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_logical_and_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_uint16_bool, scalar_generated_logical_and_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_logical_and_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_uint32_bool, scalar_generated_logical_and_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_logical_and_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_uint64_bool, scalar_generated_logical_and_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_logical_and_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float32_uint8_bool, scalar_generated_logical_and_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_logical_and_float64_bool_bool_logic(const double *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0.0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_bool_bool, scalar_generated_logical_and_float64_bool_bool_logic, double, bool, bool)

static inline void scalar_generated_logical_and_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_float32_bool, scalar_generated_logical_and_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_logical_and_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_float64_bool, scalar_generated_logical_and_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_logical_and_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_int16_bool, scalar_generated_logical_and_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_logical_and_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_int32_bool, scalar_generated_logical_and_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_logical_and_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_int64_bool, scalar_generated_logical_and_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_logical_and_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_int8_bool, scalar_generated_logical_and_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_logical_and_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_uint16_bool, scalar_generated_logical_and_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_logical_and_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_uint32_bool, scalar_generated_logical_and_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_logical_and_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_uint64_bool, scalar_generated_logical_and_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_logical_and_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_float64_uint8_bool, scalar_generated_logical_and_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_logical_and_int16_bool_bool_logic(const int16_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_bool_bool, scalar_generated_logical_and_int16_bool_bool_logic, int16_t, bool, bool)

static inline void scalar_generated_logical_and_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_float32_bool, scalar_generated_logical_and_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_logical_and_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_float64_bool, scalar_generated_logical_and_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_logical_and_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_int16_bool, scalar_generated_logical_and_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_logical_and_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_int32_bool, scalar_generated_logical_and_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_logical_and_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_int64_bool, scalar_generated_logical_and_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_logical_and_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_int8_bool, scalar_generated_logical_and_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_logical_and_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_uint16_bool, scalar_generated_logical_and_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_logical_and_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_uint32_bool, scalar_generated_logical_and_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_logical_and_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_uint64_bool, scalar_generated_logical_and_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_logical_and_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int16_uint8_bool, scalar_generated_logical_and_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_logical_and_int32_bool_bool_logic(const int32_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_bool_bool, scalar_generated_logical_and_int32_bool_bool_logic, int32_t, bool, bool)

static inline void scalar_generated_logical_and_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_float32_bool, scalar_generated_logical_and_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_logical_and_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_float64_bool, scalar_generated_logical_and_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_logical_and_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_int16_bool, scalar_generated_logical_and_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_logical_and_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_int32_bool, scalar_generated_logical_and_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_logical_and_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_int64_bool, scalar_generated_logical_and_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_logical_and_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_int8_bool, scalar_generated_logical_and_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_logical_and_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_uint16_bool, scalar_generated_logical_and_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_logical_and_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_uint32_bool, scalar_generated_logical_and_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_logical_and_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_uint64_bool, scalar_generated_logical_and_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_logical_and_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int32_uint8_bool, scalar_generated_logical_and_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_logical_and_int64_bool_bool_logic(const int64_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_bool_bool, scalar_generated_logical_and_int64_bool_bool_logic, int64_t, bool, bool)

static inline void scalar_generated_logical_and_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_float32_bool, scalar_generated_logical_and_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_logical_and_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_float64_bool, scalar_generated_logical_and_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_logical_and_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_int16_bool, scalar_generated_logical_and_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_logical_and_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_int32_bool, scalar_generated_logical_and_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_logical_and_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_int64_bool, scalar_generated_logical_and_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_logical_and_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_int8_bool, scalar_generated_logical_and_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_logical_and_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_uint16_bool, scalar_generated_logical_and_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_logical_and_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_uint32_bool, scalar_generated_logical_and_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_logical_and_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_uint64_bool, scalar_generated_logical_and_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_logical_and_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int64_uint8_bool, scalar_generated_logical_and_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_logical_and_int8_bool_bool_logic(const int8_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_bool_bool, scalar_generated_logical_and_int8_bool_bool_logic, int8_t, bool, bool)

static inline void scalar_generated_logical_and_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_float32_bool, scalar_generated_logical_and_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_logical_and_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_float64_bool, scalar_generated_logical_and_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_logical_and_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_int16_bool, scalar_generated_logical_and_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_logical_and_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_int32_bool, scalar_generated_logical_and_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_logical_and_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_int64_bool, scalar_generated_logical_and_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_logical_and_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_int8_bool, scalar_generated_logical_and_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_logical_and_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_uint16_bool, scalar_generated_logical_and_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_logical_and_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_uint32_bool, scalar_generated_logical_and_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_logical_and_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_uint64_bool, scalar_generated_logical_and_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_logical_and_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_int8_uint8_bool, scalar_generated_logical_and_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_logical_and_uint16_bool_bool_logic(const uint16_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_bool_bool, scalar_generated_logical_and_uint16_bool_bool_logic, uint16_t, bool, bool)

static inline void scalar_generated_logical_and_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_float32_bool, scalar_generated_logical_and_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_logical_and_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_float64_bool, scalar_generated_logical_and_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_logical_and_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_int16_bool, scalar_generated_logical_and_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_logical_and_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_int32_bool, scalar_generated_logical_and_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_logical_and_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_int64_bool, scalar_generated_logical_and_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_logical_and_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_int8_bool, scalar_generated_logical_and_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_logical_and_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_uint16_bool, scalar_generated_logical_and_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_logical_and_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_uint32_bool, scalar_generated_logical_and_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_logical_and_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_uint64_bool, scalar_generated_logical_and_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_logical_and_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint16_uint8_bool, scalar_generated_logical_and_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_logical_and_uint32_bool_bool_logic(const uint32_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_bool_bool, scalar_generated_logical_and_uint32_bool_bool_logic, uint32_t, bool, bool)

static inline void scalar_generated_logical_and_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_float32_bool, scalar_generated_logical_and_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_logical_and_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_float64_bool, scalar_generated_logical_and_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_logical_and_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_int16_bool, scalar_generated_logical_and_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_logical_and_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_int32_bool, scalar_generated_logical_and_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_logical_and_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_int64_bool, scalar_generated_logical_and_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_logical_and_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_int8_bool, scalar_generated_logical_and_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_logical_and_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_uint16_bool, scalar_generated_logical_and_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_logical_and_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_uint32_bool, scalar_generated_logical_and_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_logical_and_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_uint64_bool, scalar_generated_logical_and_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_logical_and_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint32_uint8_bool, scalar_generated_logical_and_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_logical_and_uint64_bool_bool_logic(const uint64_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_bool_bool, scalar_generated_logical_and_uint64_bool_bool_logic, uint64_t, bool, bool)

static inline void scalar_generated_logical_and_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_float32_bool, scalar_generated_logical_and_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_logical_and_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_float64_bool, scalar_generated_logical_and_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_logical_and_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_int16_bool, scalar_generated_logical_and_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_logical_and_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_int32_bool, scalar_generated_logical_and_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_logical_and_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_int64_bool, scalar_generated_logical_and_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_logical_and_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_int8_bool, scalar_generated_logical_and_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_logical_and_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_uint16_bool, scalar_generated_logical_and_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_logical_and_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_uint32_bool, scalar_generated_logical_and_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_logical_and_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_uint64_bool, scalar_generated_logical_and_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_logical_and_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint64_uint8_bool, scalar_generated_logical_and_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_logical_and_uint8_bool_bool_logic(const uint8_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) && *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_bool_bool, scalar_generated_logical_and_uint8_bool_bool_logic, uint8_t, bool, bool)

static inline void scalar_generated_logical_and_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_float32_bool, scalar_generated_logical_and_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_logical_and_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_float64_bool, scalar_generated_logical_and_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_logical_and_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_int16_bool, scalar_generated_logical_and_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_logical_and_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_int32_bool, scalar_generated_logical_and_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_logical_and_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_int64_bool, scalar_generated_logical_and_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_logical_and_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_int8_bool, scalar_generated_logical_and_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_logical_and_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_uint16_bool, scalar_generated_logical_and_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_logical_and_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_uint32_bool, scalar_generated_logical_and_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_logical_and_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_uint64_bool, scalar_generated_logical_and_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_logical_and_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) && (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_and_uint8_uint8_bool, scalar_generated_logical_and_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_logical_or_bool_bool_bool_logic(const bool *a, const bool *b, bool *c) {
    *c = (bool)(*a || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_bool_bool, scalar_generated_logical_or_bool_bool_bool_logic, bool, bool, bool)

static inline void scalar_generated_logical_or_bool_float32_bool_logic(const bool *a, const float *b, bool *c) {
    *c = (bool)(*a || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_float32_bool, scalar_generated_logical_or_bool_float32_bool_logic, bool, float, bool)

static inline void scalar_generated_logical_or_bool_float64_bool_logic(const bool *a, const double *b, bool *c) {
    *c = (bool)(*a || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_float64_bool, scalar_generated_logical_or_bool_float64_bool_logic, bool, double, bool)

static inline void scalar_generated_logical_or_bool_int16_bool_logic(const bool *a, const int16_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_int16_bool, scalar_generated_logical_or_bool_int16_bool_logic, bool, int16_t, bool)

static inline void scalar_generated_logical_or_bool_int32_bool_logic(const bool *a, const int32_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_int32_bool, scalar_generated_logical_or_bool_int32_bool_logic, bool, int32_t, bool)

static inline void scalar_generated_logical_or_bool_int64_bool_logic(const bool *a, const int64_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_int64_bool, scalar_generated_logical_or_bool_int64_bool_logic, bool, int64_t, bool)

static inline void scalar_generated_logical_or_bool_int8_bool_logic(const bool *a, const int8_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_int8_bool, scalar_generated_logical_or_bool_int8_bool_logic, bool, int8_t, bool)

static inline void scalar_generated_logical_or_bool_uint16_bool_logic(const bool *a, const uint16_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_uint16_bool, scalar_generated_logical_or_bool_uint16_bool_logic, bool, uint16_t, bool)

static inline void scalar_generated_logical_or_bool_uint32_bool_logic(const bool *a, const uint32_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_uint32_bool, scalar_generated_logical_or_bool_uint32_bool_logic, bool, uint32_t, bool)

static inline void scalar_generated_logical_or_bool_uint64_bool_logic(const bool *a, const uint64_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_uint64_bool, scalar_generated_logical_or_bool_uint64_bool_logic, bool, uint64_t, bool)

static inline void scalar_generated_logical_or_bool_uint8_bool_logic(const bool *a, const uint8_t *b, bool *c) {
    *c = (bool)(*a || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_bool_uint8_bool, scalar_generated_logical_or_bool_uint8_bool_logic, bool, uint8_t, bool)

static inline void scalar_generated_logical_or_float32_bool_bool_logic(const float *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0.0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_bool_bool, scalar_generated_logical_or_float32_bool_bool_logic, float, bool, bool)

static inline void scalar_generated_logical_or_float32_float32_bool_logic(const float *a, const float *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_float32_bool, scalar_generated_logical_or_float32_float32_bool_logic, float, float, bool)

static inline void scalar_generated_logical_or_float32_float64_bool_logic(const float *a, const double *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_float64_bool, scalar_generated_logical_or_float32_float64_bool_logic, float, double, bool)

static inline void scalar_generated_logical_or_float32_int16_bool_logic(const float *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_int16_bool, scalar_generated_logical_or_float32_int16_bool_logic, float, int16_t, bool)

static inline void scalar_generated_logical_or_float32_int32_bool_logic(const float *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_int32_bool, scalar_generated_logical_or_float32_int32_bool_logic, float, int32_t, bool)

static inline void scalar_generated_logical_or_float32_int64_bool_logic(const float *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_int64_bool, scalar_generated_logical_or_float32_int64_bool_logic, float, int64_t, bool)

static inline void scalar_generated_logical_or_float32_int8_bool_logic(const float *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_int8_bool, scalar_generated_logical_or_float32_int8_bool_logic, float, int8_t, bool)

static inline void scalar_generated_logical_or_float32_uint16_bool_logic(const float *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_uint16_bool, scalar_generated_logical_or_float32_uint16_bool_logic, float, uint16_t, bool)

static inline void scalar_generated_logical_or_float32_uint32_bool_logic(const float *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_uint32_bool, scalar_generated_logical_or_float32_uint32_bool_logic, float, uint32_t, bool)

static inline void scalar_generated_logical_or_float32_uint64_bool_logic(const float *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_uint64_bool, scalar_generated_logical_or_float32_uint64_bool_logic, float, uint64_t, bool)

static inline void scalar_generated_logical_or_float32_uint8_bool_logic(const float *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float32_uint8_bool, scalar_generated_logical_or_float32_uint8_bool_logic, float, uint8_t, bool)

static inline void scalar_generated_logical_or_float64_bool_bool_logic(const double *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0.0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_bool_bool, scalar_generated_logical_or_float64_bool_bool_logic, double, bool, bool)

static inline void scalar_generated_logical_or_float64_float32_bool_logic(const double *a, const float *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_float32_bool, scalar_generated_logical_or_float64_float32_bool_logic, double, float, bool)

static inline void scalar_generated_logical_or_float64_float64_bool_logic(const double *a, const double *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_float64_bool, scalar_generated_logical_or_float64_float64_bool_logic, double, double, bool)

static inline void scalar_generated_logical_or_float64_int16_bool_logic(const double *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_int16_bool, scalar_generated_logical_or_float64_int16_bool_logic, double, int16_t, bool)

static inline void scalar_generated_logical_or_float64_int32_bool_logic(const double *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_int32_bool, scalar_generated_logical_or_float64_int32_bool_logic, double, int32_t, bool)

static inline void scalar_generated_logical_or_float64_int64_bool_logic(const double *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_int64_bool, scalar_generated_logical_or_float64_int64_bool_logic, double, int64_t, bool)

static inline void scalar_generated_logical_or_float64_int8_bool_logic(const double *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_int8_bool, scalar_generated_logical_or_float64_int8_bool_logic, double, int8_t, bool)

static inline void scalar_generated_logical_or_float64_uint16_bool_logic(const double *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_uint16_bool, scalar_generated_logical_or_float64_uint16_bool_logic, double, uint16_t, bool)

static inline void scalar_generated_logical_or_float64_uint32_bool_logic(const double *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_uint32_bool, scalar_generated_logical_or_float64_uint32_bool_logic, double, uint32_t, bool)

static inline void scalar_generated_logical_or_float64_uint64_bool_logic(const double *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_uint64_bool, scalar_generated_logical_or_float64_uint64_bool_logic, double, uint64_t, bool)

static inline void scalar_generated_logical_or_float64_uint8_bool_logic(const double *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0.0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_float64_uint8_bool, scalar_generated_logical_or_float64_uint8_bool_logic, double, uint8_t, bool)

static inline void scalar_generated_logical_or_int16_bool_bool_logic(const int16_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_bool_bool, scalar_generated_logical_or_int16_bool_bool_logic, int16_t, bool, bool)

static inline void scalar_generated_logical_or_int16_float32_bool_logic(const int16_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_float32_bool, scalar_generated_logical_or_int16_float32_bool_logic, int16_t, float, bool)

static inline void scalar_generated_logical_or_int16_float64_bool_logic(const int16_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_float64_bool, scalar_generated_logical_or_int16_float64_bool_logic, int16_t, double, bool)

static inline void scalar_generated_logical_or_int16_int16_bool_logic(const int16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_int16_bool, scalar_generated_logical_or_int16_int16_bool_logic, int16_t, int16_t, bool)

static inline void scalar_generated_logical_or_int16_int32_bool_logic(const int16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_int32_bool, scalar_generated_logical_or_int16_int32_bool_logic, int16_t, int32_t, bool)

static inline void scalar_generated_logical_or_int16_int64_bool_logic(const int16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_int64_bool, scalar_generated_logical_or_int16_int64_bool_logic, int16_t, int64_t, bool)

static inline void scalar_generated_logical_or_int16_int8_bool_logic(const int16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_int8_bool, scalar_generated_logical_or_int16_int8_bool_logic, int16_t, int8_t, bool)

static inline void scalar_generated_logical_or_int16_uint16_bool_logic(const int16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_uint16_bool, scalar_generated_logical_or_int16_uint16_bool_logic, int16_t, uint16_t, bool)

static inline void scalar_generated_logical_or_int16_uint32_bool_logic(const int16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_uint32_bool, scalar_generated_logical_or_int16_uint32_bool_logic, int16_t, uint32_t, bool)

static inline void scalar_generated_logical_or_int16_uint64_bool_logic(const int16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_uint64_bool, scalar_generated_logical_or_int16_uint64_bool_logic, int16_t, uint64_t, bool)

static inline void scalar_generated_logical_or_int16_uint8_bool_logic(const int16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int16_uint8_bool, scalar_generated_logical_or_int16_uint8_bool_logic, int16_t, uint8_t, bool)

static inline void scalar_generated_logical_or_int32_bool_bool_logic(const int32_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_bool_bool, scalar_generated_logical_or_int32_bool_bool_logic, int32_t, bool, bool)

static inline void scalar_generated_logical_or_int32_float32_bool_logic(const int32_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_float32_bool, scalar_generated_logical_or_int32_float32_bool_logic, int32_t, float, bool)

static inline void scalar_generated_logical_or_int32_float64_bool_logic(const int32_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_float64_bool, scalar_generated_logical_or_int32_float64_bool_logic, int32_t, double, bool)

static inline void scalar_generated_logical_or_int32_int16_bool_logic(const int32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_int16_bool, scalar_generated_logical_or_int32_int16_bool_logic, int32_t, int16_t, bool)

static inline void scalar_generated_logical_or_int32_int32_bool_logic(const int32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_int32_bool, scalar_generated_logical_or_int32_int32_bool_logic, int32_t, int32_t, bool)

static inline void scalar_generated_logical_or_int32_int64_bool_logic(const int32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_int64_bool, scalar_generated_logical_or_int32_int64_bool_logic, int32_t, int64_t, bool)

static inline void scalar_generated_logical_or_int32_int8_bool_logic(const int32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_int8_bool, scalar_generated_logical_or_int32_int8_bool_logic, int32_t, int8_t, bool)

static inline void scalar_generated_logical_or_int32_uint16_bool_logic(const int32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_uint16_bool, scalar_generated_logical_or_int32_uint16_bool_logic, int32_t, uint16_t, bool)

static inline void scalar_generated_logical_or_int32_uint32_bool_logic(const int32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_uint32_bool, scalar_generated_logical_or_int32_uint32_bool_logic, int32_t, uint32_t, bool)

static inline void scalar_generated_logical_or_int32_uint64_bool_logic(const int32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_uint64_bool, scalar_generated_logical_or_int32_uint64_bool_logic, int32_t, uint64_t, bool)

static inline void scalar_generated_logical_or_int32_uint8_bool_logic(const int32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int32_uint8_bool, scalar_generated_logical_or_int32_uint8_bool_logic, int32_t, uint8_t, bool)

static inline void scalar_generated_logical_or_int64_bool_bool_logic(const int64_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_bool_bool, scalar_generated_logical_or_int64_bool_bool_logic, int64_t, bool, bool)

static inline void scalar_generated_logical_or_int64_float32_bool_logic(const int64_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_float32_bool, scalar_generated_logical_or_int64_float32_bool_logic, int64_t, float, bool)

static inline void scalar_generated_logical_or_int64_float64_bool_logic(const int64_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_float64_bool, scalar_generated_logical_or_int64_float64_bool_logic, int64_t, double, bool)

static inline void scalar_generated_logical_or_int64_int16_bool_logic(const int64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_int16_bool, scalar_generated_logical_or_int64_int16_bool_logic, int64_t, int16_t, bool)

static inline void scalar_generated_logical_or_int64_int32_bool_logic(const int64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_int32_bool, scalar_generated_logical_or_int64_int32_bool_logic, int64_t, int32_t, bool)

static inline void scalar_generated_logical_or_int64_int64_bool_logic(const int64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_int64_bool, scalar_generated_logical_or_int64_int64_bool_logic, int64_t, int64_t, bool)

static inline void scalar_generated_logical_or_int64_int8_bool_logic(const int64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_int8_bool, scalar_generated_logical_or_int64_int8_bool_logic, int64_t, int8_t, bool)

static inline void scalar_generated_logical_or_int64_uint16_bool_logic(const int64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_uint16_bool, scalar_generated_logical_or_int64_uint16_bool_logic, int64_t, uint16_t, bool)

static inline void scalar_generated_logical_or_int64_uint32_bool_logic(const int64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_uint32_bool, scalar_generated_logical_or_int64_uint32_bool_logic, int64_t, uint32_t, bool)

static inline void scalar_generated_logical_or_int64_uint64_bool_logic(const int64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_uint64_bool, scalar_generated_logical_or_int64_uint64_bool_logic, int64_t, uint64_t, bool)

static inline void scalar_generated_logical_or_int64_uint8_bool_logic(const int64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int64_uint8_bool, scalar_generated_logical_or_int64_uint8_bool_logic, int64_t, uint8_t, bool)

static inline void scalar_generated_logical_or_int8_bool_bool_logic(const int8_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_bool_bool, scalar_generated_logical_or_int8_bool_bool_logic, int8_t, bool, bool)

static inline void scalar_generated_logical_or_int8_float32_bool_logic(const int8_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_float32_bool, scalar_generated_logical_or_int8_float32_bool_logic, int8_t, float, bool)

static inline void scalar_generated_logical_or_int8_float64_bool_logic(const int8_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_float64_bool, scalar_generated_logical_or_int8_float64_bool_logic, int8_t, double, bool)

static inline void scalar_generated_logical_or_int8_int16_bool_logic(const int8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_int16_bool, scalar_generated_logical_or_int8_int16_bool_logic, int8_t, int16_t, bool)

static inline void scalar_generated_logical_or_int8_int32_bool_logic(const int8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_int32_bool, scalar_generated_logical_or_int8_int32_bool_logic, int8_t, int32_t, bool)

static inline void scalar_generated_logical_or_int8_int64_bool_logic(const int8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_int64_bool, scalar_generated_logical_or_int8_int64_bool_logic, int8_t, int64_t, bool)

static inline void scalar_generated_logical_or_int8_int8_bool_logic(const int8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_int8_bool, scalar_generated_logical_or_int8_int8_bool_logic, int8_t, int8_t, bool)

static inline void scalar_generated_logical_or_int8_uint16_bool_logic(const int8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_uint16_bool, scalar_generated_logical_or_int8_uint16_bool_logic, int8_t, uint16_t, bool)

static inline void scalar_generated_logical_or_int8_uint32_bool_logic(const int8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_uint32_bool, scalar_generated_logical_or_int8_uint32_bool_logic, int8_t, uint32_t, bool)

static inline void scalar_generated_logical_or_int8_uint64_bool_logic(const int8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_uint64_bool, scalar_generated_logical_or_int8_uint64_bool_logic, int8_t, uint64_t, bool)

static inline void scalar_generated_logical_or_int8_uint8_bool_logic(const int8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_int8_uint8_bool, scalar_generated_logical_or_int8_uint8_bool_logic, int8_t, uint8_t, bool)

static inline void scalar_generated_logical_or_uint16_bool_bool_logic(const uint16_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_bool_bool, scalar_generated_logical_or_uint16_bool_bool_logic, uint16_t, bool, bool)

static inline void scalar_generated_logical_or_uint16_float32_bool_logic(const uint16_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_float32_bool, scalar_generated_logical_or_uint16_float32_bool_logic, uint16_t, float, bool)

static inline void scalar_generated_logical_or_uint16_float64_bool_logic(const uint16_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_float64_bool, scalar_generated_logical_or_uint16_float64_bool_logic, uint16_t, double, bool)

static inline void scalar_generated_logical_or_uint16_int16_bool_logic(const uint16_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_int16_bool, scalar_generated_logical_or_uint16_int16_bool_logic, uint16_t, int16_t, bool)

static inline void scalar_generated_logical_or_uint16_int32_bool_logic(const uint16_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_int32_bool, scalar_generated_logical_or_uint16_int32_bool_logic, uint16_t, int32_t, bool)

static inline void scalar_generated_logical_or_uint16_int64_bool_logic(const uint16_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_int64_bool, scalar_generated_logical_or_uint16_int64_bool_logic, uint16_t, int64_t, bool)

static inline void scalar_generated_logical_or_uint16_int8_bool_logic(const uint16_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_int8_bool, scalar_generated_logical_or_uint16_int8_bool_logic, uint16_t, int8_t, bool)

static inline void scalar_generated_logical_or_uint16_uint16_bool_logic(const uint16_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_uint16_bool, scalar_generated_logical_or_uint16_uint16_bool_logic, uint16_t, uint16_t, bool)

static inline void scalar_generated_logical_or_uint16_uint32_bool_logic(const uint16_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_uint32_bool, scalar_generated_logical_or_uint16_uint32_bool_logic, uint16_t, uint32_t, bool)

static inline void scalar_generated_logical_or_uint16_uint64_bool_logic(const uint16_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_uint64_bool, scalar_generated_logical_or_uint16_uint64_bool_logic, uint16_t, uint64_t, bool)

static inline void scalar_generated_logical_or_uint16_uint8_bool_logic(const uint16_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint16_uint8_bool, scalar_generated_logical_or_uint16_uint8_bool_logic, uint16_t, uint8_t, bool)

static inline void scalar_generated_logical_or_uint32_bool_bool_logic(const uint32_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_bool_bool, scalar_generated_logical_or_uint32_bool_bool_logic, uint32_t, bool, bool)

static inline void scalar_generated_logical_or_uint32_float32_bool_logic(const uint32_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_float32_bool, scalar_generated_logical_or_uint32_float32_bool_logic, uint32_t, float, bool)

static inline void scalar_generated_logical_or_uint32_float64_bool_logic(const uint32_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_float64_bool, scalar_generated_logical_or_uint32_float64_bool_logic, uint32_t, double, bool)

static inline void scalar_generated_logical_or_uint32_int16_bool_logic(const uint32_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_int16_bool, scalar_generated_logical_or_uint32_int16_bool_logic, uint32_t, int16_t, bool)

static inline void scalar_generated_logical_or_uint32_int32_bool_logic(const uint32_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_int32_bool, scalar_generated_logical_or_uint32_int32_bool_logic, uint32_t, int32_t, bool)

static inline void scalar_generated_logical_or_uint32_int64_bool_logic(const uint32_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_int64_bool, scalar_generated_logical_or_uint32_int64_bool_logic, uint32_t, int64_t, bool)

static inline void scalar_generated_logical_or_uint32_int8_bool_logic(const uint32_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_int8_bool, scalar_generated_logical_or_uint32_int8_bool_logic, uint32_t, int8_t, bool)

static inline void scalar_generated_logical_or_uint32_uint16_bool_logic(const uint32_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_uint16_bool, scalar_generated_logical_or_uint32_uint16_bool_logic, uint32_t, uint16_t, bool)

static inline void scalar_generated_logical_or_uint32_uint32_bool_logic(const uint32_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_uint32_bool, scalar_generated_logical_or_uint32_uint32_bool_logic, uint32_t, uint32_t, bool)

static inline void scalar_generated_logical_or_uint32_uint64_bool_logic(const uint32_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_uint64_bool, scalar_generated_logical_or_uint32_uint64_bool_logic, uint32_t, uint64_t, bool)

static inline void scalar_generated_logical_or_uint32_uint8_bool_logic(const uint32_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint32_uint8_bool, scalar_generated_logical_or_uint32_uint8_bool_logic, uint32_t, uint8_t, bool)

static inline void scalar_generated_logical_or_uint64_bool_bool_logic(const uint64_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_bool_bool, scalar_generated_logical_or_uint64_bool_bool_logic, uint64_t, bool, bool)

static inline void scalar_generated_logical_or_uint64_float32_bool_logic(const uint64_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_float32_bool, scalar_generated_logical_or_uint64_float32_bool_logic, uint64_t, float, bool)

static inline void scalar_generated_logical_or_uint64_float64_bool_logic(const uint64_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_float64_bool, scalar_generated_logical_or_uint64_float64_bool_logic, uint64_t, double, bool)

static inline void scalar_generated_logical_or_uint64_int16_bool_logic(const uint64_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_int16_bool, scalar_generated_logical_or_uint64_int16_bool_logic, uint64_t, int16_t, bool)

static inline void scalar_generated_logical_or_uint64_int32_bool_logic(const uint64_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_int32_bool, scalar_generated_logical_or_uint64_int32_bool_logic, uint64_t, int32_t, bool)

static inline void scalar_generated_logical_or_uint64_int64_bool_logic(const uint64_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_int64_bool, scalar_generated_logical_or_uint64_int64_bool_logic, uint64_t, int64_t, bool)

static inline void scalar_generated_logical_or_uint64_int8_bool_logic(const uint64_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_int8_bool, scalar_generated_logical_or_uint64_int8_bool_logic, uint64_t, int8_t, bool)

static inline void scalar_generated_logical_or_uint64_uint16_bool_logic(const uint64_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_uint16_bool, scalar_generated_logical_or_uint64_uint16_bool_logic, uint64_t, uint16_t, bool)

static inline void scalar_generated_logical_or_uint64_uint32_bool_logic(const uint64_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_uint32_bool, scalar_generated_logical_or_uint64_uint32_bool_logic, uint64_t, uint32_t, bool)

static inline void scalar_generated_logical_or_uint64_uint64_bool_logic(const uint64_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_uint64_bool, scalar_generated_logical_or_uint64_uint64_bool_logic, uint64_t, uint64_t, bool)

static inline void scalar_generated_logical_or_uint64_uint8_bool_logic(const uint64_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint64_uint8_bool, scalar_generated_logical_or_uint64_uint8_bool_logic, uint64_t, uint8_t, bool)

static inline void scalar_generated_logical_or_uint8_bool_bool_logic(const uint8_t *a, const bool *b, bool *c) {
    *c = (bool)((*a != 0) || *b);
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_bool_bool, scalar_generated_logical_or_uint8_bool_bool_logic, uint8_t, bool, bool)

static inline void scalar_generated_logical_or_uint8_float32_bool_logic(const uint8_t *a, const float *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_float32_bool, scalar_generated_logical_or_uint8_float32_bool_logic, uint8_t, float, bool)

static inline void scalar_generated_logical_or_uint8_float64_bool_logic(const uint8_t *a, const double *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0.0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_float64_bool, scalar_generated_logical_or_uint8_float64_bool_logic, uint8_t, double, bool)

static inline void scalar_generated_logical_or_uint8_int16_bool_logic(const uint8_t *a, const int16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_int16_bool, scalar_generated_logical_or_uint8_int16_bool_logic, uint8_t, int16_t, bool)

static inline void scalar_generated_logical_or_uint8_int32_bool_logic(const uint8_t *a, const int32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_int32_bool, scalar_generated_logical_or_uint8_int32_bool_logic, uint8_t, int32_t, bool)

static inline void scalar_generated_logical_or_uint8_int64_bool_logic(const uint8_t *a, const int64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_int64_bool, scalar_generated_logical_or_uint8_int64_bool_logic, uint8_t, int64_t, bool)

static inline void scalar_generated_logical_or_uint8_int8_bool_logic(const uint8_t *a, const int8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_int8_bool, scalar_generated_logical_or_uint8_int8_bool_logic, uint8_t, int8_t, bool)

static inline void scalar_generated_logical_or_uint8_uint16_bool_logic(const uint8_t *a, const uint16_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_uint16_bool, scalar_generated_logical_or_uint8_uint16_bool_logic, uint8_t, uint16_t, bool)

static inline void scalar_generated_logical_or_uint8_uint32_bool_logic(const uint8_t *a, const uint32_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_uint32_bool, scalar_generated_logical_or_uint8_uint32_bool_logic, uint8_t, uint32_t, bool)

static inline void scalar_generated_logical_or_uint8_uint64_bool_logic(const uint8_t *a, const uint64_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_uint64_bool, scalar_generated_logical_or_uint8_uint64_bool_logic, uint8_t, uint64_t, bool)

static inline void scalar_generated_logical_or_uint8_uint8_bool_logic(const uint8_t *a, const uint8_t *b, bool *c) {
    *c = (bool)((*a != 0) || (*b != 0));
}

VMBIN_HANDLER_BINARY_TYPED(scalar_generated_logical_or_uint8_uint8_bool, scalar_generated_logical_or_uint8_uint8_bool_logic, uint8_t, uint8_t, bool)

static inline void scalar_generated_logical_not_bool_bool_logic(const bool *a, bool *out) {
    *out = (bool)!*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_bool_bool, scalar_generated_logical_not_bool_bool_logic, bool, bool)

static inline void scalar_generated_logical_not_float32_bool_logic(const float *a, bool *out) {
    *out = (bool)!(*a != 0.0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_float32_bool, scalar_generated_logical_not_float32_bool_logic, float, bool)

static inline void scalar_generated_logical_not_float64_bool_logic(const double *a, bool *out) {
    *out = (bool)!(*a != 0.0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_float64_bool, scalar_generated_logical_not_float64_bool_logic, double, bool)

static inline void scalar_generated_logical_not_int16_bool_logic(const int16_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_int16_bool, scalar_generated_logical_not_int16_bool_logic, int16_t, bool)

static inline void scalar_generated_logical_not_int32_bool_logic(const int32_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_int32_bool, scalar_generated_logical_not_int32_bool_logic, int32_t, bool)

static inline void scalar_generated_logical_not_int64_bool_logic(const int64_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_int64_bool, scalar_generated_logical_not_int64_bool_logic, int64_t, bool)

static inline void scalar_generated_logical_not_int8_bool_logic(const int8_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_int8_bool, scalar_generated_logical_not_int8_bool_logic, int8_t, bool)

static inline void scalar_generated_logical_not_uint16_bool_logic(const uint16_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_uint16_bool, scalar_generated_logical_not_uint16_bool_logic, uint16_t, bool)

static inline void scalar_generated_logical_not_uint32_bool_logic(const uint32_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_uint32_bool, scalar_generated_logical_not_uint32_bool_logic, uint32_t, bool)

static inline void scalar_generated_logical_not_uint64_bool_logic(const uint64_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_uint64_bool, scalar_generated_logical_not_uint64_bool_logic, uint64_t, bool)

static inline void scalar_generated_logical_not_uint8_bool_logic(const uint8_t *a, bool *out) {
    *out = (bool)!(*a != 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_logical_not_uint8_bool, scalar_generated_logical_not_uint8_bool_logic, uint8_t, bool)

static inline void scalar_generated_cast_bool_bool_bool_logic(const bool *a, bool *out) {
    *out = *a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_bool_bool, scalar_generated_cast_bool_bool_bool_logic, bool, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_bool_char_ascii_char_ascii, runtimec_scalar_value_to_character, bool, (*source_ ? 1U : 0U), RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_bool_char_utf32_char_utf32, runtimec_scalar_value_to_character, bool, (*source_ ? 1U : 0U), RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_bool_char_utf8_char_utf8, runtimec_scalar_value_to_character, bool, (*source_ ? 1U : 0U), RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_bool_float32_float32_logic(const bool *a, float *out) {
    *out = (float)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_float32_float32, scalar_generated_cast_bool_float32_float32_logic, bool, float)

static inline void scalar_generated_cast_bool_float64_float64_logic(const bool *a, double *out) {
    *out = (double)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_float64_float64, scalar_generated_cast_bool_float64_float64_logic, bool, double)

static inline void scalar_generated_cast_bool_int16_int16_logic(const bool *a, int16_t *out) {
    *out = (int16_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_int16_int16, scalar_generated_cast_bool_int16_int16_logic, bool, int16_t)

static inline void scalar_generated_cast_bool_int32_int32_logic(const bool *a, int32_t *out) {
    *out = (int32_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_int32_int32, scalar_generated_cast_bool_int32_int32_logic, bool, int32_t)

static inline void scalar_generated_cast_bool_int64_int64_logic(const bool *a, int64_t *out) {
    *out = (int64_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_int64_int64, scalar_generated_cast_bool_int64_int64_logic, bool, int64_t)

static inline void scalar_generated_cast_bool_int8_int8_logic(const bool *a, int8_t *out) {
    *out = (int8_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_int8_int8, scalar_generated_cast_bool_int8_int8_logic, bool, int8_t)

static inline void scalar_generated_cast_bool_uint16_uint16_logic(const bool *a, uint16_t *out) {
    *out = (uint16_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_uint16_uint16, scalar_generated_cast_bool_uint16_uint16_logic, bool, uint16_t)

static inline void scalar_generated_cast_bool_uint32_uint32_logic(const bool *a, uint32_t *out) {
    *out = (uint32_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_uint32_uint32, scalar_generated_cast_bool_uint32_uint32_logic, bool, uint32_t)

static inline void scalar_generated_cast_bool_uint64_uint64_logic(const bool *a, uint64_t *out) {
    *out = (uint64_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_uint64_uint64, scalar_generated_cast_bool_uint64_uint64_logic, bool, uint64_t)

static inline void scalar_generated_cast_bool_uint8_uint8_logic(const bool *a, uint8_t *out) {
    *out = (uint8_t)(*a ? 1 : 0);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_bool_uint8_uint8, scalar_generated_cast_bool_uint8_uint8_logic, bool, uint8_t)

static inline void scalar_generated_cast_char_ascii_bool_bool_logic(const uint8_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_char_ascii_bool_bool, scalar_generated_cast_char_ascii_bool_bool_logic, uint8_t, bool)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_ascii_char_ascii_char_ascii, runtimec_scalar_character_to_value, string_text_decode_ascii, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_ascii_char_utf32_char_utf32, runtimec_scalar_character_to_value, string_text_decode_ascii, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_ascii_char_utf8_char_utf8, runtimec_scalar_character_to_value, string_text_decode_ascii, RUNTIMEC_CHARACTER_ENCODING_UTF8)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_float32_float32, runtimec_scalar_character_to_number, string_text_decode_ascii, 32U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_float64_float64, runtimec_scalar_character_to_number, string_text_decode_ascii, 64U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_int16_int16, runtimec_scalar_character_to_number, string_text_decode_ascii, 16U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_int32_int32, runtimec_scalar_character_to_number, string_text_decode_ascii, 32U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_int64_int64, runtimec_scalar_character_to_number, string_text_decode_ascii, 64U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_int8_int8, runtimec_scalar_character_to_number, string_text_decode_ascii, 8U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_uint16_uint16, runtimec_scalar_character_to_number, string_text_decode_ascii, 16U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_uint32_uint32, runtimec_scalar_character_to_number, string_text_decode_ascii, 32U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_uint64_uint64, runtimec_scalar_character_to_number, string_text_decode_ascii, 64U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_ascii_uint8_uint8, runtimec_scalar_character_to_number, string_text_decode_ascii, 8U, false, false)

static inline void scalar_generated_cast_char_utf32_bool_bool_logic(const uint32_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_char_utf32_bool_bool, scalar_generated_cast_char_utf32_bool_bool_logic, uint32_t, bool)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf32_char_ascii_char_ascii, runtimec_scalar_character_to_value, string_text_decode_utf32, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf32_char_utf32_char_utf32, runtimec_scalar_character_to_value, string_text_decode_utf32, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf32_char_utf8_char_utf8, runtimec_scalar_character_to_value, string_text_decode_utf32, RUNTIMEC_CHARACTER_ENCODING_UTF8)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_float32_float32, runtimec_scalar_character_to_number, string_text_decode_utf32, 32U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_float64_float64, runtimec_scalar_character_to_number, string_text_decode_utf32, 64U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_int16_int16, runtimec_scalar_character_to_number, string_text_decode_utf32, 16U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_int32_int32, runtimec_scalar_character_to_number, string_text_decode_utf32, 32U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_int64_int64, runtimec_scalar_character_to_number, string_text_decode_utf32, 64U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_int8_int8, runtimec_scalar_character_to_number, string_text_decode_utf32, 8U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_uint16_uint16, runtimec_scalar_character_to_number, string_text_decode_utf32, 16U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_uint32_uint32, runtimec_scalar_character_to_number, string_text_decode_utf32, 32U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_uint64_uint64, runtimec_scalar_character_to_number, string_text_decode_utf32, 64U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf32_uint8_uint8, runtimec_scalar_character_to_number, string_text_decode_utf32, 8U, false, false)

static inline void scalar_generated_cast_char_utf8_bool_bool_logic(const uint32_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_char_utf8_bool_bool, scalar_generated_cast_char_utf8_bool_bool_logic, uint32_t, bool)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf8_char_ascii_char_ascii, runtimec_scalar_character_to_value, string_text_decode_utf8, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf8_char_utf32_char_utf32, runtimec_scalar_character_to_value, string_text_decode_utf8, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_CHARACTER_TO_CHARACTER(scalar_generated_cast_char_utf8_char_utf8_char_utf8, runtimec_scalar_character_to_value, string_text_decode_utf8, RUNTIMEC_CHARACTER_ENCODING_UTF8)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_float32_float32, runtimec_scalar_character_to_number, string_text_decode_utf8, 32U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_float64_float64, runtimec_scalar_character_to_number, string_text_decode_utf8, 64U, false, true)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_int16_int16, runtimec_scalar_character_to_number, string_text_decode_utf8, 16U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_int32_int32, runtimec_scalar_character_to_number, string_text_decode_utf8, 32U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_int64_int64, runtimec_scalar_character_to_number, string_text_decode_utf8, 64U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_int8_int8, runtimec_scalar_character_to_number, string_text_decode_utf8, 8U, true, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_uint16_uint16, runtimec_scalar_character_to_number, string_text_decode_utf8, 16U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_uint32_uint32, runtimec_scalar_character_to_number, string_text_decode_utf8, 32U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_uint64_uint64, runtimec_scalar_character_to_number, string_text_decode_utf8, 64U, false, false)

VMBIN_HANDLER_CHARACTER_TO_NUMBER(scalar_generated_cast_char_utf8_uint8_uint8, runtimec_scalar_character_to_number, string_text_decode_utf8, 8U, false, false)

static inline void scalar_generated_cast_float32_bool_bool_logic(const float *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_bool_bool, scalar_generated_cast_float32_bool_bool_logic, float, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float32_char_ascii_char_ascii, runtimec_scalar_value_to_character, float, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float32_char_utf32_char_utf32, runtimec_scalar_value_to_character, float, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float32_char_utf8_char_utf8, runtimec_scalar_value_to_character, float, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_float32_float32_float32_logic(const float *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_float32_float32_float32, scalar_generated_cast_float32_float32_float32_logic, float, float)

static inline void scalar_generated_cast_float32_float64_float64_logic(const float *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_float32_float64_float64, scalar_generated_cast_float32_float64_float64_logic, float, double)

static inline void scalar_generated_cast_float32_int16_int16_logic(const float *a, int16_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 16U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int16_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_int16_int16, scalar_generated_cast_float32_int16_int16_logic, float, int16_t)

static inline void scalar_generated_cast_float32_int32_int32_logic(const float *a, int32_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 32U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int32_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_int32_int32, scalar_generated_cast_float32_int32_int32_logic, float, int32_t)

static inline void scalar_generated_cast_float32_int64_int64_logic(const float *a, int64_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 64U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int64_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_int64_int64, scalar_generated_cast_float32_int64_int64_logic, float, int64_t)

static inline void scalar_generated_cast_float32_int8_int8_logic(const float *a, int8_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 8U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int8_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_int8_int8, scalar_generated_cast_float32_int8_int8_logic, float, int8_t)

static inline void scalar_generated_cast_float32_uint16_uint16_logic(const float *a, uint16_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 16U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint16_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_uint16_uint16, scalar_generated_cast_float32_uint16_uint16_logic, float, uint16_t)

static inline void scalar_generated_cast_float32_uint32_uint32_logic(const float *a, uint32_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 32U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint32_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_uint32_uint32, scalar_generated_cast_float32_uint32_uint32_logic, float, uint32_t)

static inline void scalar_generated_cast_float32_uint64_uint64_logic(const float *a, uint64_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 64U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint64_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_uint64_uint64, scalar_generated_cast_float32_uint64_uint64_logic, float, uint64_t)

static inline void scalar_generated_cast_float32_uint8_uint8_logic(const float *a, uint8_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 8U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint8_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float32_uint8_uint8, scalar_generated_cast_float32_uint8_uint8_logic, float, uint8_t)

static inline void scalar_generated_cast_float64_bool_bool_logic(const double *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_bool_bool, scalar_generated_cast_float64_bool_bool_logic, double, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float64_char_ascii_char_ascii, runtimec_scalar_value_to_character, double, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float64_char_utf32_char_utf32, runtimec_scalar_value_to_character, double, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_float64_char_utf8_char_utf8, runtimec_scalar_value_to_character, double, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_float64_float32_float32_logic(const double *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_float64_float32_float32, scalar_generated_cast_float64_float32_float32_logic, double, float)

static inline void scalar_generated_cast_float64_float64_float64_logic(const double *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_float64_float64_float64, scalar_generated_cast_float64_float64_float64_logic, double, double)

static inline void scalar_generated_cast_float64_int16_int16_logic(const double *a, int16_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 16U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int16_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_int16_int16, scalar_generated_cast_float64_int16_int16_logic, double, int16_t)

static inline void scalar_generated_cast_float64_int32_int32_logic(const double *a, int32_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 32U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int32_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_int32_int32, scalar_generated_cast_float64_int32_int32_logic, double, int32_t)

static inline void scalar_generated_cast_float64_int64_int64_logic(const double *a, int64_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 64U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int64_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_int64_int64, scalar_generated_cast_float64_int64_int64_logic, double, int64_t)

static inline void scalar_generated_cast_float64_int8_int8_logic(const double *a, int8_t *out, binrwir_execution_exit_t *output_exit) {
    {int64_t converted_; runtimec_scalar_float_to_signed((double)*a, 8U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (int8_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_int8_int8, scalar_generated_cast_float64_int8_int8_logic, double, int8_t)

static inline void scalar_generated_cast_float64_uint16_uint16_logic(const double *a, uint16_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 16U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint16_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_uint16_uint16, scalar_generated_cast_float64_uint16_uint16_logic, double, uint16_t)

static inline void scalar_generated_cast_float64_uint32_uint32_logic(const double *a, uint32_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 32U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint32_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_uint32_uint32, scalar_generated_cast_float64_uint32_uint32_logic, double, uint32_t)

static inline void scalar_generated_cast_float64_uint64_uint64_logic(const double *a, uint64_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 64U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint64_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_uint64_uint64, scalar_generated_cast_float64_uint64_uint64_logic, double, uint64_t)

static inline void scalar_generated_cast_float64_uint8_uint8_logic(const double *a, uint8_t *out, binrwir_execution_exit_t *output_exit) {
    {uint64_t converted_; runtimec_scalar_float_to_unsigned((double)*a, 8U, &converted_, output_exit); if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) { return; } *out = (uint8_t)converted_;}
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_float64_uint8_uint8, scalar_generated_cast_float64_uint8_uint8_logic, double, uint8_t)

static inline void scalar_generated_cast_int16_bool_bool_logic(const int16_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_int16_bool_bool, scalar_generated_cast_int16_bool_bool_logic, int16_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int16_char_ascii_char_ascii, runtimec_scalar_value_to_character, int16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int16_char_utf32_char_utf32, runtimec_scalar_value_to_character, int16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int16_char_utf8_char_utf8, runtimec_scalar_value_to_character, int16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_int16_float32_float32_logic(const int16_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_float32_float32, scalar_generated_cast_int16_float32_float32_logic, int16_t, float)

static inline void scalar_generated_cast_int16_float64_float64_logic(const int16_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_float64_float64, scalar_generated_cast_int16_float64_float64_logic, int16_t, double)

static inline void scalar_generated_cast_int16_int16_int16_logic(const int16_t *a, int16_t *out) {
    *out = (int16_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_int16_int16, scalar_generated_cast_int16_int16_int16_logic, int16_t, int16_t)

static inline void scalar_generated_cast_int16_int32_int32_logic(const int16_t *a, int32_t *out) {
    *out = (int32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_int32_int32, scalar_generated_cast_int16_int32_int32_logic, int16_t, int32_t)

static inline void scalar_generated_cast_int16_int64_int64_logic(const int16_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_int64_int64, scalar_generated_cast_int16_int64_int64_logic, int16_t, int64_t)

static inline void scalar_generated_cast_int16_int8_int8_logic(const int16_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 16U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_int8_int8, scalar_generated_cast_int16_int8_int8_logic, int16_t, int8_t)

static inline void scalar_generated_cast_int16_uint16_uint16_logic(const int16_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_uint16_uint16, scalar_generated_cast_int16_uint16_uint16_logic, int16_t, uint16_t)

static inline void scalar_generated_cast_int16_uint32_uint32_logic(const int16_t *a, uint32_t *out) {
    *out = (uint32_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_uint32_uint32, scalar_generated_cast_int16_uint32_uint32_logic, int16_t, uint32_t)

static inline void scalar_generated_cast_int16_uint64_uint64_logic(const int16_t *a, uint64_t *out) {
    *out = (uint64_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 64U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_uint64_uint64, scalar_generated_cast_int16_uint64_uint64_logic, int16_t, uint64_t)

static inline void scalar_generated_cast_int16_uint8_uint8_logic(const int16_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int16_uint8_uint8, scalar_generated_cast_int16_uint8_uint8_logic, int16_t, uint8_t)

static inline void scalar_generated_cast_int32_bool_bool_logic(const int32_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_int32_bool_bool, scalar_generated_cast_int32_bool_bool_logic, int32_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int32_char_ascii_char_ascii, runtimec_scalar_value_to_character, int32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int32_char_utf32_char_utf32, runtimec_scalar_value_to_character, int32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int32_char_utf8_char_utf8, runtimec_scalar_value_to_character, int32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_int32_float32_float32_logic(const int32_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_float32_float32, scalar_generated_cast_int32_float32_float32_logic, int32_t, float)

static inline void scalar_generated_cast_int32_float64_float64_logic(const int32_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_float64_float64, scalar_generated_cast_int32_float64_float64_logic, int32_t, double)

static inline void scalar_generated_cast_int32_int16_int16_logic(const int32_t *a, int16_t *out) {
    *out = (int16_t)runtimec_scalar_wrap_signed((int64_t)*a, 32U, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_int16_int16, scalar_generated_cast_int32_int16_int16_logic, int32_t, int16_t)

static inline void scalar_generated_cast_int32_int32_int32_logic(const int32_t *a, int32_t *out) {
    *out = (int32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_int32_int32, scalar_generated_cast_int32_int32_int32_logic, int32_t, int32_t)

static inline void scalar_generated_cast_int32_int64_int64_logic(const int32_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_int64_int64, scalar_generated_cast_int32_int64_int64_logic, int32_t, int64_t)

static inline void scalar_generated_cast_int32_int8_int8_logic(const int32_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 32U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_int8_int8, scalar_generated_cast_int32_int8_int8_logic, int32_t, int8_t)

static inline void scalar_generated_cast_int32_uint16_uint16_logic(const int32_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_uint16_uint16, scalar_generated_cast_int32_uint16_uint16_logic, int32_t, uint16_t)

static inline void scalar_generated_cast_int32_uint32_uint32_logic(const int32_t *a, uint32_t *out) {
    *out = (uint32_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_uint32_uint32, scalar_generated_cast_int32_uint32_uint32_logic, int32_t, uint32_t)

static inline void scalar_generated_cast_int32_uint64_uint64_logic(const int32_t *a, uint64_t *out) {
    *out = (uint64_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 64U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_uint64_uint64, scalar_generated_cast_int32_uint64_uint64_logic, int32_t, uint64_t)

static inline void scalar_generated_cast_int32_uint8_uint8_logic(const int32_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int32_uint8_uint8, scalar_generated_cast_int32_uint8_uint8_logic, int32_t, uint8_t)

static inline void scalar_generated_cast_int64_bool_bool_logic(const int64_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_int64_bool_bool, scalar_generated_cast_int64_bool_bool_logic, int64_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int64_char_ascii_char_ascii, runtimec_scalar_value_to_character, int64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int64_char_utf32_char_utf32, runtimec_scalar_value_to_character, int64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int64_char_utf8_char_utf8, runtimec_scalar_value_to_character, int64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_int64_float32_float32_logic(const int64_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_float32_float32, scalar_generated_cast_int64_float32_float32_logic, int64_t, float)

static inline void scalar_generated_cast_int64_float64_float64_logic(const int64_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_float64_float64, scalar_generated_cast_int64_float64_float64_logic, int64_t, double)

static inline void scalar_generated_cast_int64_int16_int16_logic(const int64_t *a, int16_t *out) {
    *out = (int16_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_int16_int16, scalar_generated_cast_int64_int16_int16_logic, int64_t, int16_t)

static inline void scalar_generated_cast_int64_int32_int32_logic(const int64_t *a, int32_t *out) {
    *out = (int32_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_int32_int32, scalar_generated_cast_int64_int32_int32_logic, int64_t, int32_t)

static inline void scalar_generated_cast_int64_int64_int64_logic(const int64_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_int64_int64, scalar_generated_cast_int64_int64_int64_logic, int64_t, int64_t)

static inline void scalar_generated_cast_int64_int8_int8_logic(const int64_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_int8_int8, scalar_generated_cast_int64_int8_int8_logic, int64_t, int8_t)

static inline void scalar_generated_cast_int64_uint16_uint16_logic(const int64_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_uint16_uint16, scalar_generated_cast_int64_uint16_uint16_logic, int64_t, uint16_t)

static inline void scalar_generated_cast_int64_uint32_uint32_logic(const int64_t *a, uint32_t *out) {
    *out = (uint32_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_uint32_uint32, scalar_generated_cast_int64_uint32_uint32_logic, int64_t, uint32_t)

static inline void scalar_generated_cast_int64_uint64_uint64_logic(const int64_t *a, uint64_t *out) {
    *out = (uint64_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 64U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_uint64_uint64, scalar_generated_cast_int64_uint64_uint64_logic, int64_t, uint64_t)

static inline void scalar_generated_cast_int64_uint8_uint8_logic(const int64_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int64_uint8_uint8, scalar_generated_cast_int64_uint8_uint8_logic, int64_t, uint8_t)

static inline void scalar_generated_cast_int8_bool_bool_logic(const int8_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_int8_bool_bool, scalar_generated_cast_int8_bool_bool_logic, int8_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int8_char_ascii_char_ascii, runtimec_scalar_value_to_character, int8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int8_char_utf32_char_utf32, runtimec_scalar_value_to_character, int8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_int8_char_utf8_char_utf8, runtimec_scalar_value_to_character, int8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_int8_float32_float32_logic(const int8_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_float32_float32, scalar_generated_cast_int8_float32_float32_logic, int8_t, float)

static inline void scalar_generated_cast_int8_float64_float64_logic(const int8_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_float64_float64, scalar_generated_cast_int8_float64_float64_logic, int8_t, double)

static inline void scalar_generated_cast_int8_int16_int16_logic(const int8_t *a, int16_t *out) {
    *out = (int16_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_int16_int16, scalar_generated_cast_int8_int16_int16_logic, int8_t, int16_t)

static inline void scalar_generated_cast_int8_int32_int32_logic(const int8_t *a, int32_t *out) {
    *out = (int32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_int32_int32, scalar_generated_cast_int8_int32_int32_logic, int8_t, int32_t)

static inline void scalar_generated_cast_int8_int64_int64_logic(const int8_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_int64_int64, scalar_generated_cast_int8_int64_int64_logic, int8_t, int64_t)

static inline void scalar_generated_cast_int8_int8_int8_logic(const int8_t *a, int8_t *out) {
    *out = (int8_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_int8_int8, scalar_generated_cast_int8_int8_int8_logic, int8_t, int8_t)

static inline void scalar_generated_cast_int8_uint16_uint16_logic(const int8_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_uint16_uint16, scalar_generated_cast_int8_uint16_uint16_logic, int8_t, uint16_t)

static inline void scalar_generated_cast_int8_uint32_uint32_logic(const int8_t *a, uint32_t *out) {
    *out = (uint32_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_uint32_uint32, scalar_generated_cast_int8_uint32_uint32_logic, int8_t, uint32_t)

static inline void scalar_generated_cast_int8_uint64_uint64_logic(const int8_t *a, uint64_t *out) {
    *out = (uint64_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 64U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_uint64_uint64, scalar_generated_cast_int8_uint64_uint64_logic, int8_t, uint64_t)

static inline void scalar_generated_cast_int8_uint8_uint8_logic(const int8_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_int8_uint8_uint8, scalar_generated_cast_int8_uint8_uint8_logic, int8_t, uint8_t)

static inline void scalar_generated_cast_uint16_bool_bool_logic(const uint16_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_uint16_bool_bool, scalar_generated_cast_uint16_bool_bool_logic, uint16_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint16_char_ascii_char_ascii, runtimec_scalar_value_to_character, uint16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint16_char_utf32_char_utf32, runtimec_scalar_value_to_character, uint16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint16_char_utf8_char_utf8, runtimec_scalar_value_to_character, uint16_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_uint16_float32_float32_logic(const uint16_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_float32_float32, scalar_generated_cast_uint16_float32_float32_logic, uint16_t, float)

static inline void scalar_generated_cast_uint16_float64_float64_logic(const uint16_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_float64_float64, scalar_generated_cast_uint16_float64_float64_logic, uint16_t, double)

static inline void scalar_generated_cast_uint16_int16_int16_logic(const uint16_t *a, int16_t *out) {
    *out = (int16_t)runtimec_scalar_wrap_signed((int64_t)*a, 16U, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_int16_int16, scalar_generated_cast_uint16_int16_int16_logic, uint16_t, int16_t)

static inline void scalar_generated_cast_uint16_int32_int32_logic(const uint16_t *a, int32_t *out) {
    *out = (int32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_int32_int32, scalar_generated_cast_uint16_int32_int32_logic, uint16_t, int32_t)

static inline void scalar_generated_cast_uint16_int64_int64_logic(const uint16_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_int64_int64, scalar_generated_cast_uint16_int64_int64_logic, uint16_t, int64_t)

static inline void scalar_generated_cast_uint16_int8_int8_logic(const uint16_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 16U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_int8_int8, scalar_generated_cast_uint16_int8_int8_logic, uint16_t, int8_t)

static inline void scalar_generated_cast_uint16_uint16_uint16_logic(const uint16_t *a, uint16_t *out) {
    *out = (uint16_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_uint16_uint16, scalar_generated_cast_uint16_uint16_uint16_logic, uint16_t, uint16_t)

static inline void scalar_generated_cast_uint16_uint32_uint32_logic(const uint16_t *a, uint32_t *out) {
    *out = (uint32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_uint32_uint32, scalar_generated_cast_uint16_uint32_uint32_logic, uint16_t, uint32_t)

static inline void scalar_generated_cast_uint16_uint64_uint64_logic(const uint16_t *a, uint64_t *out) {
    *out = (uint64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_uint64_uint64, scalar_generated_cast_uint16_uint64_uint64_logic, uint16_t, uint64_t)

static inline void scalar_generated_cast_uint16_uint8_uint8_logic(const uint16_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint16_uint8_uint8, scalar_generated_cast_uint16_uint8_uint8_logic, uint16_t, uint8_t)

static inline void scalar_generated_cast_uint32_bool_bool_logic(const uint32_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_uint32_bool_bool, scalar_generated_cast_uint32_bool_bool_logic, uint32_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint32_char_ascii_char_ascii, runtimec_scalar_value_to_character, uint32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint32_char_utf32_char_utf32, runtimec_scalar_value_to_character, uint32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint32_char_utf8_char_utf8, runtimec_scalar_value_to_character, uint32_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_uint32_float32_float32_logic(const uint32_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_float32_float32, scalar_generated_cast_uint32_float32_float32_logic, uint32_t, float)

static inline void scalar_generated_cast_uint32_float64_float64_logic(const uint32_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_float64_float64, scalar_generated_cast_uint32_float64_float64_logic, uint32_t, double)

static inline void scalar_generated_cast_uint32_int16_int16_logic(const uint32_t *a, int16_t *out) {
    *out = (int16_t)runtimec_scalar_wrap_signed((int64_t)*a, 32U, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_int16_int16, scalar_generated_cast_uint32_int16_int16_logic, uint32_t, int16_t)

static inline void scalar_generated_cast_uint32_int32_int32_logic(const uint32_t *a, int32_t *out) {
    *out = (int32_t)runtimec_scalar_wrap_signed((int64_t)*a, 32U, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_int32_int32, scalar_generated_cast_uint32_int32_int32_logic, uint32_t, int32_t)

static inline void scalar_generated_cast_uint32_int64_int64_logic(const uint32_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_int64_int64, scalar_generated_cast_uint32_int64_int64_logic, uint32_t, int64_t)

static inline void scalar_generated_cast_uint32_int8_int8_logic(const uint32_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 32U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_int8_int8, scalar_generated_cast_uint32_int8_int8_logic, uint32_t, int8_t)

static inline void scalar_generated_cast_uint32_uint16_uint16_logic(const uint32_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_uint16_uint16, scalar_generated_cast_uint32_uint16_uint16_logic, uint32_t, uint16_t)

static inline void scalar_generated_cast_uint32_uint32_uint32_logic(const uint32_t *a, uint32_t *out) {
    *out = (uint32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_uint32_uint32, scalar_generated_cast_uint32_uint32_uint32_logic, uint32_t, uint32_t)

static inline void scalar_generated_cast_uint32_uint64_uint64_logic(const uint32_t *a, uint64_t *out) {
    *out = (uint64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_uint64_uint64, scalar_generated_cast_uint32_uint64_uint64_logic, uint32_t, uint64_t)

static inline void scalar_generated_cast_uint32_uint8_uint8_logic(const uint32_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint32_uint8_uint8, scalar_generated_cast_uint32_uint8_uint8_logic, uint32_t, uint8_t)

static inline void scalar_generated_cast_uint64_bool_bool_logic(const uint64_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_uint64_bool_bool, scalar_generated_cast_uint64_bool_bool_logic, uint64_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint64_char_ascii_char_ascii, runtimec_scalar_value_to_character, uint64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint64_char_utf32_char_utf32, runtimec_scalar_value_to_character, uint64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint64_char_utf8_char_utf8, runtimec_scalar_value_to_character, uint64_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_uint64_float32_float32_logic(const uint64_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_float32_float32, scalar_generated_cast_uint64_float32_float32_logic, uint64_t, float)

static inline void scalar_generated_cast_uint64_float64_float64_logic(const uint64_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_float64_float64, scalar_generated_cast_uint64_float64_float64_logic, uint64_t, double)

static inline void scalar_generated_cast_uint64_int16_int16_logic(const uint64_t *a, int16_t *out) {
    *out = (int16_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_int16_int16, scalar_generated_cast_uint64_int16_int16_logic, uint64_t, int16_t)

static inline void scalar_generated_cast_uint64_int32_int32_logic(const uint64_t *a, int32_t *out) {
    *out = (int32_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_int32_int32, scalar_generated_cast_uint64_int32_int32_logic, uint64_t, int32_t)

static inline void scalar_generated_cast_uint64_int64_int64_logic(const uint64_t *a, int64_t *out) {
    *out = (int64_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 64U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_int64_int64, scalar_generated_cast_uint64_int64_int64_logic, uint64_t, int64_t)

static inline void scalar_generated_cast_uint64_int8_int8_logic(const uint64_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 64U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_int8_int8, scalar_generated_cast_uint64_int8_int8_logic, uint64_t, int8_t)

static inline void scalar_generated_cast_uint64_uint16_uint16_logic(const uint64_t *a, uint16_t *out) {
    *out = (uint16_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 16U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_uint16_uint16, scalar_generated_cast_uint64_uint16_uint16_logic, uint64_t, uint16_t)

static inline void scalar_generated_cast_uint64_uint32_uint32_logic(const uint64_t *a, uint32_t *out) {
    *out = (uint32_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 32U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_uint32_uint32, scalar_generated_cast_uint64_uint32_uint32_logic, uint64_t, uint32_t)

static inline void scalar_generated_cast_uint64_uint64_uint64_logic(const uint64_t *a, uint64_t *out) {
    *out = (uint64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_uint64_uint64, scalar_generated_cast_uint64_uint64_uint64_logic, uint64_t, uint64_t)

static inline void scalar_generated_cast_uint64_uint8_uint8_logic(const uint64_t *a, uint8_t *out) {
    *out = (uint8_t)runtimec_scalar_wrap_unsigned((uint64_t)*a, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint64_uint8_uint8, scalar_generated_cast_uint64_uint8_uint8_logic, uint64_t, uint8_t)

static inline void scalar_generated_cast_uint8_bool_bool_logic(const uint8_t *a, bool *out, binrwir_execution_exit_t *output_exit) {
    (void)a; (void)out; runtimec_scalar_cast_to_bool_invalid(output_exit);
}

VMBIN_HANDLER_UNARY_TYPED_EXIT(scalar_generated_cast_uint8_bool_bool, scalar_generated_cast_uint8_bool_bool_logic, uint8_t, bool)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint8_char_ascii_char_ascii, runtimec_scalar_value_to_character, uint8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_ASCII)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint8_char_utf32_char_utf32, runtimec_scalar_value_to_character, uint8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF32)

VMBIN_HANDLER_UNARY_TO_CHARACTER(scalar_generated_cast_uint8_char_utf8_char_utf8, runtimec_scalar_value_to_character, uint8_t, (uint32_t)(int64_t)*source_, RUNTIMEC_CHARACTER_ENCODING_UTF8)

static inline void scalar_generated_cast_uint8_float32_float32_logic(const uint8_t *a, float *out) {
    *out = (float)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_float32_float32, scalar_generated_cast_uint8_float32_float32_logic, uint8_t, float)

static inline void scalar_generated_cast_uint8_float64_float64_logic(const uint8_t *a, double *out) {
    *out = (double)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_float64_float64, scalar_generated_cast_uint8_float64_float64_logic, uint8_t, double)

static inline void scalar_generated_cast_uint8_int16_int16_logic(const uint8_t *a, int16_t *out) {
    *out = (int16_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_int16_int16, scalar_generated_cast_uint8_int16_int16_logic, uint8_t, int16_t)

static inline void scalar_generated_cast_uint8_int32_int32_logic(const uint8_t *a, int32_t *out) {
    *out = (int32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_int32_int32, scalar_generated_cast_uint8_int32_int32_logic, uint8_t, int32_t)

static inline void scalar_generated_cast_uint8_int64_int64_logic(const uint8_t *a, int64_t *out) {
    *out = (int64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_int64_int64, scalar_generated_cast_uint8_int64_int64_logic, uint8_t, int64_t)

static inline void scalar_generated_cast_uint8_int8_int8_logic(const uint8_t *a, int8_t *out) {
    *out = (int8_t)runtimec_scalar_wrap_signed((int64_t)*a, 8U, 8U);
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_int8_int8, scalar_generated_cast_uint8_int8_int8_logic, uint8_t, int8_t)

static inline void scalar_generated_cast_uint8_uint16_uint16_logic(const uint8_t *a, uint16_t *out) {
    *out = (uint16_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_uint16_uint16, scalar_generated_cast_uint8_uint16_uint16_logic, uint8_t, uint16_t)

static inline void scalar_generated_cast_uint8_uint32_uint32_logic(const uint8_t *a, uint32_t *out) {
    *out = (uint32_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_uint32_uint32, scalar_generated_cast_uint8_uint32_uint32_logic, uint8_t, uint32_t)

static inline void scalar_generated_cast_uint8_uint64_uint64_logic(const uint8_t *a, uint64_t *out) {
    *out = (uint64_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_uint64_uint64, scalar_generated_cast_uint8_uint64_uint64_logic, uint8_t, uint64_t)

static inline void scalar_generated_cast_uint8_uint8_uint8_logic(const uint8_t *a, uint8_t *out) {
    *out = (uint8_t)*a;
}

VMBIN_HANDLER_UNARY_TYPED(scalar_generated_cast_uint8_uint8_uint8, scalar_generated_cast_uint8_uint8_uint8_logic, uint8_t, uint8_t)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_ascii_char_ascii_char_utf8, runtimec_scalar_concat, string_text_decode_ascii, string_text_decode_ascii)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_ascii_char_utf32_char_utf8, runtimec_scalar_concat, string_text_decode_ascii, string_text_decode_utf32)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_ascii_char_utf8_char_utf8, runtimec_scalar_concat, string_text_decode_ascii, string_text_decode_utf8)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf32_char_ascii_char_utf8, runtimec_scalar_concat, string_text_decode_utf32, string_text_decode_ascii)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf32_char_utf32_char_utf8, runtimec_scalar_concat, string_text_decode_utf32, string_text_decode_utf32)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf32_char_utf8_char_utf8, runtimec_scalar_concat, string_text_decode_utf32, string_text_decode_utf8)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf8_char_ascii_char_utf8, runtimec_scalar_concat, string_text_decode_utf8, string_text_decode_ascii)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf8_char_utf32_char_utf8, runtimec_scalar_concat, string_text_decode_utf8, string_text_decode_utf32)

VMBIN_HANDLER_CHARACTER_CONCAT(scalar_generated_concat_char_utf8_char_utf8_char_utf8, runtimec_scalar_concat, string_text_decode_utf8, string_text_decode_utf8)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_ascii_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_ascii, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_ascii_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_utf32, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_ascii_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_utf8, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf32_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_ascii, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf32_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_utf32, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf32_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_utf8, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf8_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_ascii, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf8_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_utf32, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_equal_char_utf8_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_utf8, false)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_ascii_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_ascii, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_ascii_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_utf32, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_ascii_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_ascii, string_text_decode_utf8, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf32_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_ascii, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf32_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_utf32, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf32_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_utf32, string_text_decode_utf8, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf8_char_ascii_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_ascii, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf8_char_utf32_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_utf32, true)

VMBIN_HANDLER_CHARACTER_EQUAL(scalar_generated_character_not_equal_char_utf8_char_utf8_bool, runtimec_scalar_character_equal, string_text_decode_utf8, string_text_decode_utf8, true)

const binrwir_registry_entry_t scalar_generated_entries[] = {
    {0x8000U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_float32_float32},
    {0x8001U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_float64_float64},
    {0x8002U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_int16_float32},
    {0x8003U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_int32_float32},
    {0x8004U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_int64_float32},
    {0x8005U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_int8_float32},
    {0x8006U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_uint16_float32},
    {0x8007U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_uint32_float32},
    {0x8008U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_uint64_float32},
    {0x8009U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float32_uint8_float32},
    {0x800AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_float32_float64},
    {0x800BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_float64_float64},
    {0x800CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_int16_float64},
    {0x800DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_int32_float64},
    {0x800EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_int64_float64},
    {0x800FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_int8_float64},
    {0x8010U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_uint16_float64},
    {0x8011U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_uint32_float64},
    {0x8012U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_uint64_float64},
    {0x8013U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_float64_uint8_float64},
    {0x8014U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_float32_float32},
    {0x8015U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_float64_float64},
    {0x8016U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_int16_int16},
    {0x8017U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_int32_int32},
    {0x8018U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_int64_int64},
    {0x8019U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int16_int8_int16},
    {0x801AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_float32_float32},
    {0x801BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_float64_float64},
    {0x801CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_int16_int32},
    {0x801DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_int32_int32},
    {0x801EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_int64_int64},
    {0x801FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int32_int8_int32},
    {0x8020U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_float32_float32},
    {0x8021U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_float64_float64},
    {0x8022U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_int16_int64},
    {0x8023U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_int32_int64},
    {0x8024U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_int64_int64},
    {0x8025U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int64_int8_int64},
    {0x8026U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_float32_float32},
    {0x8027U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_float64_float64},
    {0x8028U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_int16_int16},
    {0x8029U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_int32_int32},
    {0x802AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_int64_int64},
    {0x802BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_int8_int8_int8},
    {0x802CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_float32_float32},
    {0x802DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_float64_float64},
    {0x802EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_uint16_uint16},
    {0x802FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_uint32_uint32},
    {0x8030U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_uint64_uint64},
    {0x8031U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint16_uint8_uint16},
    {0x8032U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_float32_float32},
    {0x8033U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_float64_float64},
    {0x8034U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_uint16_uint32},
    {0x8035U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_uint32_uint32},
    {0x8036U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_uint64_uint64},
    {0x8037U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint32_uint8_uint32},
    {0x8038U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_float32_float32},
    {0x8039U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_float64_float64},
    {0x803AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_uint16_uint64},
    {0x803BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_uint32_uint64},
    {0x803CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_uint64_uint64},
    {0x803DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint64_uint8_uint64},
    {0x803EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_float32_float32},
    {0x803FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_float64_float64},
    {0x8040U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_uint16_uint16},
    {0x8041U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_uint32_uint32},
    {0x8042U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_uint64_uint64},
    {0x8043U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_add_uint8_uint8_uint8},
    {0x8044U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_float32_float32},
    {0x8045U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_float64_float64},
    {0x8046U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_int16_float32},
    {0x8047U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_int32_float32},
    {0x8048U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_int64_float32},
    {0x8049U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_int8_float32},
    {0x804AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_uint16_float32},
    {0x804BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_uint32_float32},
    {0x804CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_uint64_float32},
    {0x804DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float32_uint8_float32},
    {0x804EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_float32_float64},
    {0x804FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_float64_float64},
    {0x8050U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_int16_float64},
    {0x8051U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_int32_float64},
    {0x8052U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_int64_float64},
    {0x8053U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_int8_float64},
    {0x8054U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_uint16_float64},
    {0x8055U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_uint32_float64},
    {0x8056U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_uint64_float64},
    {0x8057U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_float64_uint8_float64},
    {0x8058U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_float32_float32},
    {0x8059U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_float64_float64},
    {0x805AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_int16_int16},
    {0x805BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_int32_int32},
    {0x805CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_int64_int64},
    {0x805DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int16_int8_int16},
    {0x805EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_float32_float32},
    {0x805FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_float64_float64},
    {0x8060U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_int16_int32},
    {0x8061U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_int32_int32},
    {0x8062U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_int64_int64},
    {0x8063U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int32_int8_int32},
    {0x8064U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_float32_float32},
    {0x8065U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_float64_float64},
    {0x8066U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_int16_int64},
    {0x8067U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_int32_int64},
    {0x8068U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_int64_int64},
    {0x8069U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int64_int8_int64},
    {0x806AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_float32_float32},
    {0x806BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_float64_float64},
    {0x806CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_int16_int16},
    {0x806DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_int32_int32},
    {0x806EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_int64_int64},
    {0x806FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_int8_int8_int8},
    {0x8070U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_float32_float32},
    {0x8071U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_float64_float64},
    {0x8072U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_uint16_uint16},
    {0x8073U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_uint32_uint32},
    {0x8074U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_uint64_uint64},
    {0x8075U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint16_uint8_uint16},
    {0x8076U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_float32_float32},
    {0x8077U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_float64_float64},
    {0x8078U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_uint16_uint32},
    {0x8079U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_uint32_uint32},
    {0x807AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_uint64_uint64},
    {0x807BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint32_uint8_uint32},
    {0x807CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_float32_float32},
    {0x807DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_float64_float64},
    {0x807EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_uint16_uint64},
    {0x807FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_uint32_uint64},
    {0x8080U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_uint64_uint64},
    {0x8081U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint64_uint8_uint64},
    {0x8082U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_float32_float32},
    {0x8083U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_float64_float64},
    {0x8084U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_uint16_uint16},
    {0x8085U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_uint32_uint32},
    {0x8086U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_uint64_uint64},
    {0x8087U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sub_uint8_uint8_uint8},
    {0x8088U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_float32_float32},
    {0x8089U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_float64_float64},
    {0x808AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_int16_float32},
    {0x808BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_int32_float32},
    {0x808CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_int64_float32},
    {0x808DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_int8_float32},
    {0x808EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_uint16_float32},
    {0x808FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_uint32_float32},
    {0x8090U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_uint64_float32},
    {0x8091U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float32_uint8_float32},
    {0x8092U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_float32_float64},
    {0x8093U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_float64_float64},
    {0x8094U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_int16_float64},
    {0x8095U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_int32_float64},
    {0x8096U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_int64_float64},
    {0x8097U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_int8_float64},
    {0x8098U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_uint16_float64},
    {0x8099U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_uint32_float64},
    {0x809AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_uint64_float64},
    {0x809BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_float64_uint8_float64},
    {0x809CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_float32_float32},
    {0x809DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_float64_float64},
    {0x809EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_int16_int16},
    {0x809FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_int32_int32},
    {0x80A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_int64_int64},
    {0x80A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int16_int8_int16},
    {0x80A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_float32_float32},
    {0x80A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_float64_float64},
    {0x80A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_int16_int32},
    {0x80A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_int32_int32},
    {0x80A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_int64_int64},
    {0x80A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int32_int8_int32},
    {0x80A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_float32_float32},
    {0x80A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_float64_float64},
    {0x80AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_int16_int64},
    {0x80ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_int32_int64},
    {0x80ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_int64_int64},
    {0x80ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int64_int8_int64},
    {0x80AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_float32_float32},
    {0x80AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_float64_float64},
    {0x80B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_int16_int16},
    {0x80B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_int32_int32},
    {0x80B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_int64_int64},
    {0x80B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_int8_int8_int8},
    {0x80B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_float32_float32},
    {0x80B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_float64_float64},
    {0x80B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_uint16_uint16},
    {0x80B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_uint32_uint32},
    {0x80B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_uint64_uint64},
    {0x80B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint16_uint8_uint16},
    {0x80BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_float32_float32},
    {0x80BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_float64_float64},
    {0x80BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_uint16_uint32},
    {0x80BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_uint32_uint32},
    {0x80BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_uint64_uint64},
    {0x80BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint32_uint8_uint32},
    {0x80C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_float32_float32},
    {0x80C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_float64_float64},
    {0x80C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_uint16_uint64},
    {0x80C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_uint32_uint64},
    {0x80C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_uint64_uint64},
    {0x80C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint64_uint8_uint64},
    {0x80C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_float32_float32},
    {0x80C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_float64_float64},
    {0x80C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_uint16_uint16},
    {0x80C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_uint32_uint32},
    {0x80CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_uint64_uint64},
    {0x80CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mul_uint8_uint8_uint8},
    {0x80CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_float32_float32},
    {0x80CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_float64_float64},
    {0x80CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_int16_float32},
    {0x80CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_int32_float32},
    {0x80D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_int64_float32},
    {0x80D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_int8_float32},
    {0x80D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_uint16_float32},
    {0x80D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_uint32_float32},
    {0x80D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_uint64_float32},
    {0x80D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float32_uint8_float32},
    {0x80D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_float32_float64},
    {0x80D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_float64_float64},
    {0x80D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_int16_float64},
    {0x80D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_int32_float64},
    {0x80DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_int64_float64},
    {0x80DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_int8_float64},
    {0x80DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_uint16_float64},
    {0x80DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_uint32_float64},
    {0x80DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_uint64_float64},
    {0x80DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_float64_uint8_float64},
    {0x80E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_float32_float32},
    {0x80E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_float64_float64},
    {0x80E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_int16_int16},
    {0x80E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_int32_int32},
    {0x80E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_int64_int64},
    {0x80E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int16_int8_int16},
    {0x80E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_float32_float32},
    {0x80E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_float64_float64},
    {0x80E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_int16_int32},
    {0x80E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_int32_int32},
    {0x80EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_int64_int64},
    {0x80EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int32_int8_int32},
    {0x80ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_float32_float32},
    {0x80EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_float64_float64},
    {0x80EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_int16_int64},
    {0x80EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_int32_int64},
    {0x80F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_int64_int64},
    {0x80F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int64_int8_int64},
    {0x80F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_float32_float32},
    {0x80F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_float64_float64},
    {0x80F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_int16_int16},
    {0x80F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_int32_int32},
    {0x80F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_int64_int64},
    {0x80F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_int8_int8_int8},
    {0x80F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_float32_float32},
    {0x80F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_float64_float64},
    {0x80FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_uint16_uint16},
    {0x80FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_uint32_uint32},
    {0x80FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_uint64_uint64},
    {0x80FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint16_uint8_uint16},
    {0x80FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_float32_float32},
    {0x80FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_float64_float64},
    {0x8100U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_uint16_uint32},
    {0x8101U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_uint32_uint32},
    {0x8102U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_uint64_uint64},
    {0x8103U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint32_uint8_uint32},
    {0x8104U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_float32_float32},
    {0x8105U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_float64_float64},
    {0x8106U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_uint16_uint64},
    {0x8107U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_uint32_uint64},
    {0x8108U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_uint64_uint64},
    {0x8109U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint64_uint8_uint64},
    {0x810AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_float32_float32},
    {0x810BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_float64_float64},
    {0x810CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_uint16_uint16},
    {0x810DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_uint32_uint32},
    {0x810EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_uint64_uint64},
    {0x810FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_div_uint8_uint8_uint8},
    {0x8110U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int16_int16_int16},
    {0x8111U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int16_int32_int32},
    {0x8112U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int16_int64_int64},
    {0x8113U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int16_int8_int16},
    {0x8114U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int32_int16_int32},
    {0x8115U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int32_int32_int32},
    {0x8116U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int32_int64_int64},
    {0x8117U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int32_int8_int32},
    {0x8118U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int64_int16_int64},
    {0x8119U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int64_int32_int64},
    {0x811AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int64_int64_int64},
    {0x811BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int64_int8_int64},
    {0x811CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int8_int16_int16},
    {0x811DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int8_int32_int32},
    {0x811EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int8_int64_int64},
    {0x811FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_int8_int8_int8},
    {0x8120U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint16_uint16_uint16},
    {0x8121U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint16_uint32_uint32},
    {0x8122U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint16_uint64_uint64},
    {0x8123U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint16_uint8_uint16},
    {0x8124U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint32_uint16_uint32},
    {0x8125U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint32_uint32_uint32},
    {0x8126U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint32_uint64_uint64},
    {0x8127U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint32_uint8_uint32},
    {0x8128U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint64_uint16_uint64},
    {0x8129U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint64_uint32_uint64},
    {0x812AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint64_uint64_uint64},
    {0x812BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint64_uint8_uint64},
    {0x812CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint8_uint16_uint16},
    {0x812DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint8_uint32_uint32},
    {0x812EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint8_uint64_uint64},
    {0x812FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_mod_uint8_uint8_uint8},
    {0x8130U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_float32_float32},
    {0x8131U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_float64_float64},
    {0x8132U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_int16_float32},
    {0x8133U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_int32_float32},
    {0x8134U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_int64_float32},
    {0x8135U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_int8_float32},
    {0x8136U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_uint16_float32},
    {0x8137U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_uint32_float32},
    {0x8138U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_uint64_float32},
    {0x8139U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float32_uint8_float32},
    {0x813AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_float32_float64},
    {0x813BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_float64_float64},
    {0x813CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_int16_float64},
    {0x813DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_int32_float64},
    {0x813EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_int64_float64},
    {0x813FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_int8_float64},
    {0x8140U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_uint16_float64},
    {0x8141U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_uint32_float64},
    {0x8142U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_uint64_float64},
    {0x8143U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_float64_uint8_float64},
    {0x8144U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_float32_float32},
    {0x8145U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_float64_float64},
    {0x8146U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_int16_int16},
    {0x8147U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_int32_int32},
    {0x8148U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_int64_int64},
    {0x8149U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int16_int8_int16},
    {0x814AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_float32_float32},
    {0x814BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_float64_float64},
    {0x814CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_int16_int32},
    {0x814DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_int32_int32},
    {0x814EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_int64_int64},
    {0x814FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int32_int8_int32},
    {0x8150U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_float32_float32},
    {0x8151U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_float64_float64},
    {0x8152U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_int16_int64},
    {0x8153U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_int32_int64},
    {0x8154U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_int64_int64},
    {0x8155U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int64_int8_int64},
    {0x8156U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_float32_float32},
    {0x8157U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_float64_float64},
    {0x8158U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_int16_int16},
    {0x8159U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_int32_int32},
    {0x815AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_int64_int64},
    {0x815BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_int8_int8_int8},
    {0x815CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_float32_float32},
    {0x815DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_float64_float64},
    {0x815EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_uint16_uint16},
    {0x815FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_uint32_uint32},
    {0x8160U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_uint64_uint64},
    {0x8161U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint16_uint8_uint16},
    {0x8162U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_float32_float32},
    {0x8163U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_float64_float64},
    {0x8164U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_uint16_uint32},
    {0x8165U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_uint32_uint32},
    {0x8166U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_uint64_uint64},
    {0x8167U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint32_uint8_uint32},
    {0x8168U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_float32_float32},
    {0x8169U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_float64_float64},
    {0x816AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_uint16_uint64},
    {0x816BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_uint32_uint64},
    {0x816CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_uint64_uint64},
    {0x816DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint64_uint8_uint64},
    {0x816EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_float32_float32},
    {0x816FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_float64_float64},
    {0x8170U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_uint16_uint16},
    {0x8171U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_uint32_uint32},
    {0x8172U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_uint64_uint64},
    {0x8173U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_max_uint8_uint8_uint8},
    {0x8174U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_float32_float32},
    {0x8175U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_float64_float64},
    {0x8176U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_int16_float32},
    {0x8177U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_int32_float32},
    {0x8178U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_int64_float32},
    {0x8179U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_int8_float32},
    {0x817AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_uint16_float32},
    {0x817BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_uint32_float32},
    {0x817CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_uint64_float32},
    {0x817DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float32_uint8_float32},
    {0x817EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_float32_float64},
    {0x817FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_float64_float64},
    {0x8180U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_int16_float64},
    {0x8181U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_int32_float64},
    {0x8182U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_int64_float64},
    {0x8183U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_int8_float64},
    {0x8184U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_uint16_float64},
    {0x8185U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_uint32_float64},
    {0x8186U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_uint64_float64},
    {0x8187U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_float64_uint8_float64},
    {0x8188U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_float32_float32},
    {0x8189U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_float64_float64},
    {0x818AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_int16_int16},
    {0x818BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_int32_int32},
    {0x818CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_int64_int64},
    {0x818DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int16_int8_int16},
    {0x818EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_float32_float32},
    {0x818FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_float64_float64},
    {0x8190U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_int16_int32},
    {0x8191U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_int32_int32},
    {0x8192U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_int64_int64},
    {0x8193U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int32_int8_int32},
    {0x8194U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_float32_float32},
    {0x8195U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_float64_float64},
    {0x8196U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_int16_int64},
    {0x8197U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_int32_int64},
    {0x8198U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_int64_int64},
    {0x8199U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int64_int8_int64},
    {0x819AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_float32_float32},
    {0x819BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_float64_float64},
    {0x819CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_int16_int16},
    {0x819DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_int32_int32},
    {0x819EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_int64_int64},
    {0x819FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_int8_int8_int8},
    {0x81A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_float32_float32},
    {0x81A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_float64_float64},
    {0x81A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_uint16_uint16},
    {0x81A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_uint32_uint32},
    {0x81A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_uint64_uint64},
    {0x81A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint16_uint8_uint16},
    {0x81A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_float32_float32},
    {0x81A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_float64_float64},
    {0x81A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_uint16_uint32},
    {0x81A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_uint32_uint32},
    {0x81AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_uint64_uint64},
    {0x81ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint32_uint8_uint32},
    {0x81ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_float32_float32},
    {0x81ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_float64_float64},
    {0x81AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_uint16_uint64},
    {0x81AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_uint32_uint64},
    {0x81B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_uint64_uint64},
    {0x81B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint64_uint8_uint64},
    {0x81B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_float32_float32},
    {0x81B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_float64_float64},
    {0x81B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_uint16_uint16},
    {0x81B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_uint32_uint32},
    {0x81B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_uint64_uint64},
    {0x81B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_min_uint8_uint8_uint8},
    {0x81B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_float32_float32},
    {0x81B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_float64_float64},
    {0x81BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_int16_int16},
    {0x81BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_int32_int32},
    {0x81BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_int64_int64},
    {0x81BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_int8_int8},
    {0x81BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_uint16_uint16},
    {0x81BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_uint32_uint32},
    {0x81C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_uint64_uint64},
    {0x81C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_absolute_uint8_uint8},
    {0x81C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_float32_float32},
    {0x81C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_float64_float64},
    {0x81C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_int16_int16},
    {0x81C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_int32_int32},
    {0x81C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_int64_int64},
    {0x81C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_int8_int8},
    {0x81C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_uint16_uint16},
    {0x81C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_uint32_uint32},
    {0x81CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_uint64_uint64},
    {0x81CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_negation_uint8_uint8},
    {0x81CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_float32_float32},
    {0x81CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_float64_float64},
    {0x81CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_int16_int16},
    {0x81CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_int32_int32},
    {0x81D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_int64_int64},
    {0x81D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_int8_int8},
    {0x81D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_uint16_uint16},
    {0x81D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_uint32_uint32},
    {0x81D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_uint64_uint64},
    {0x81D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_sign_uint8_uint8},
    {0x81D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int16_int16_int16},
    {0x81D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int16_int32_int32},
    {0x81D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int16_int64_int64},
    {0x81D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int16_int8_int16},
    {0x81DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int32_int16_int32},
    {0x81DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int32_int32_int32},
    {0x81DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int32_int64_int64},
    {0x81DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int32_int8_int32},
    {0x81DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int64_int16_int64},
    {0x81DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int64_int32_int64},
    {0x81E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int64_int64_int64},
    {0x81E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int64_int8_int64},
    {0x81E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int8_int16_int16},
    {0x81E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int8_int32_int32},
    {0x81E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int8_int64_int64},
    {0x81E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_int8_int8_int8},
    {0x81E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint16_uint16_uint16},
    {0x81E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint16_uint32_uint32},
    {0x81E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint16_uint64_uint64},
    {0x81E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint16_uint8_uint16},
    {0x81EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint32_uint16_uint32},
    {0x81EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint32_uint32_uint32},
    {0x81ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint32_uint64_uint64},
    {0x81EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint32_uint8_uint32},
    {0x81EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint64_uint16_uint64},
    {0x81EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint64_uint32_uint64},
    {0x81F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint64_uint64_uint64},
    {0x81F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint64_uint8_uint64},
    {0x81F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint8_uint16_uint16},
    {0x81F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint8_uint32_uint32},
    {0x81F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint8_uint64_uint64},
    {0x81F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_and_uint8_uint8_uint8},
    {0x81F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int16_int16_int16},
    {0x81F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int16_int32_int32},
    {0x81F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int16_int64_int64},
    {0x81F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int16_int8_int16},
    {0x81FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int32_int16_int32},
    {0x81FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int32_int32_int32},
    {0x81FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int32_int64_int64},
    {0x81FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int32_int8_int32},
    {0x81FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int64_int16_int64},
    {0x81FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int64_int32_int64},
    {0x8200U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int64_int64_int64},
    {0x8201U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int64_int8_int64},
    {0x8202U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int8_int16_int16},
    {0x8203U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int8_int32_int32},
    {0x8204U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int8_int64_int64},
    {0x8205U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_int8_int8_int8},
    {0x8206U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint16_uint16_uint16},
    {0x8207U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint16_uint32_uint32},
    {0x8208U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint16_uint64_uint64},
    {0x8209U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint16_uint8_uint16},
    {0x820AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint32_uint16_uint32},
    {0x820BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint32_uint32_uint32},
    {0x820CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint32_uint64_uint64},
    {0x820DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint32_uint8_uint32},
    {0x820EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint64_uint16_uint64},
    {0x820FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint64_uint32_uint64},
    {0x8210U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint64_uint64_uint64},
    {0x8211U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint64_uint8_uint64},
    {0x8212U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint8_uint16_uint16},
    {0x8213U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint8_uint32_uint32},
    {0x8214U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint8_uint64_uint64},
    {0x8215U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_or_uint8_uint8_uint8},
    {0x8216U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int16_int16_int16},
    {0x8217U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int16_int32_int32},
    {0x8218U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int16_int64_int64},
    {0x8219U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int16_int8_int16},
    {0x821AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int32_int16_int32},
    {0x821BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int32_int32_int32},
    {0x821CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int32_int64_int64},
    {0x821DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int32_int8_int32},
    {0x821EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int64_int16_int64},
    {0x821FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int64_int32_int64},
    {0x8220U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int64_int64_int64},
    {0x8221U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int64_int8_int64},
    {0x8222U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int8_int16_int16},
    {0x8223U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int8_int32_int32},
    {0x8224U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int8_int64_int64},
    {0x8225U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_int8_int8_int8},
    {0x8226U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint16_uint16_uint16},
    {0x8227U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint16_uint32_uint32},
    {0x8228U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint16_uint64_uint64},
    {0x8229U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint16_uint8_uint16},
    {0x822AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint32_uint16_uint32},
    {0x822BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint32_uint32_uint32},
    {0x822CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint32_uint64_uint64},
    {0x822DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint32_uint8_uint32},
    {0x822EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint64_uint16_uint64},
    {0x822FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint64_uint32_uint64},
    {0x8230U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint64_uint64_uint64},
    {0x8231U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint64_uint8_uint64},
    {0x8232U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint8_uint16_uint16},
    {0x8233U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint8_uint32_uint32},
    {0x8234U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint8_uint64_uint64},
    {0x8235U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_bitwise_xor_uint8_uint8_uint8},
    {0x8236U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int16_int16_int16},
    {0x8237U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int16_int32_int32},
    {0x8238U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int16_int64_int64},
    {0x8239U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int16_int8_int16},
    {0x823AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int32_int16_int32},
    {0x823BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int32_int32_int32},
    {0x823CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int32_int64_int64},
    {0x823DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int32_int8_int32},
    {0x823EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int64_int16_int64},
    {0x823FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int64_int32_int64},
    {0x8240U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int64_int64_int64},
    {0x8241U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int64_int8_int64},
    {0x8242U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int8_int16_int16},
    {0x8243U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int8_int32_int32},
    {0x8244U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int8_int64_int64},
    {0x8245U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_int8_int8_int8},
    {0x8246U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint16_uint16_uint16},
    {0x8247U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint16_uint32_uint32},
    {0x8248U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint16_uint64_uint64},
    {0x8249U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint16_uint8_uint16},
    {0x824AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint32_uint16_uint32},
    {0x824BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint32_uint32_uint32},
    {0x824CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint32_uint64_uint64},
    {0x824DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint32_uint8_uint32},
    {0x824EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint64_uint16_uint64},
    {0x824FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint64_uint32_uint64},
    {0x8250U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint64_uint64_uint64},
    {0x8251U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint64_uint8_uint64},
    {0x8252U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint8_uint16_uint16},
    {0x8253U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint8_uint32_uint32},
    {0x8254U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint8_uint64_uint64},
    {0x8255U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_left_uint8_uint8_uint8},
    {0x8256U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int16_int16_int16},
    {0x8257U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int16_int32_int32},
    {0x8258U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int16_int64_int64},
    {0x8259U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int16_int8_int16},
    {0x825AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int32_int16_int32},
    {0x825BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int32_int32_int32},
    {0x825CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int32_int64_int64},
    {0x825DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int32_int8_int32},
    {0x825EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int64_int16_int64},
    {0x825FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int64_int32_int64},
    {0x8260U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int64_int64_int64},
    {0x8261U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int64_int8_int64},
    {0x8262U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int8_int16_int16},
    {0x8263U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int8_int32_int32},
    {0x8264U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int8_int64_int64},
    {0x8265U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_int8_int8_int8},
    {0x8266U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint16_uint16_uint16},
    {0x8267U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint16_uint32_uint32},
    {0x8268U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint16_uint64_uint64},
    {0x8269U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint16_uint8_uint16},
    {0x826AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint32_uint16_uint32},
    {0x826BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint32_uint32_uint32},
    {0x826CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint32_uint64_uint64},
    {0x826DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint32_uint8_uint32},
    {0x826EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint64_uint16_uint64},
    {0x826FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint64_uint32_uint64},
    {0x8270U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint64_uint64_uint64},
    {0x8271U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint64_uint8_uint64},
    {0x8272U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint8_uint16_uint16},
    {0x8273U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint8_uint32_uint32},
    {0x8274U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint8_uint64_uint64},
    {0x8275U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_shift_right_uint8_uint8_uint8},
    {0x8276U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_float32_float64},
    {0x8277U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_float64_float64},
    {0x8278U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_int16_float64},
    {0x8279U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_int32_float64},
    {0x827AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_int64_float64},
    {0x827BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_int8_float64},
    {0x827CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_uint16_float64},
    {0x827DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_uint32_float64},
    {0x827EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_uint64_float64},
    {0x827FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_square_root_uint8_float64},
    {0x8280U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_float32_float64},
    {0x8281U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_float64_float64},
    {0x8282U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_int16_float64},
    {0x8283U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_int32_float64},
    {0x8284U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_int64_float64},
    {0x8285U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_int8_float64},
    {0x8286U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_uint16_float64},
    {0x8287U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_uint32_float64},
    {0x8288U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_uint64_float64},
    {0x8289U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_exponential_uint8_float64},
    {0x828AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_float32_float64},
    {0x828BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_float64_float64},
    {0x828CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_int16_float64},
    {0x828DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_int32_float64},
    {0x828EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_int64_float64},
    {0x828FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_int8_float64},
    {0x8290U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_uint16_float64},
    {0x8291U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_uint32_float64},
    {0x8292U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_uint64_float64},
    {0x8293U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logarithm_uint8_float64},
    {0x8294U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_float32_float64},
    {0x8295U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_float64_float64},
    {0x8296U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_int16_float64},
    {0x8297U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_int32_float64},
    {0x8298U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_int64_float64},
    {0x8299U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_int8_float64},
    {0x829AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_uint16_float64},
    {0x829BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_uint32_float64},
    {0x829CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_uint64_float64},
    {0x829DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float32_uint8_float64},
    {0x829EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_float32_float64},
    {0x829FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_float64_float64},
    {0x82A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_int16_float64},
    {0x82A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_int32_float64},
    {0x82A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_int64_float64},
    {0x82A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_int8_float64},
    {0x82A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_uint16_float64},
    {0x82A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_uint32_float64},
    {0x82A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_uint64_float64},
    {0x82A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_float64_uint8_float64},
    {0x82A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_float32_float64},
    {0x82A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_float64_float64},
    {0x82AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_int16_float64},
    {0x82ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_int32_float64},
    {0x82ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_int64_float64},
    {0x82ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_int8_float64},
    {0x82AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_uint16_float64},
    {0x82AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_uint32_float64},
    {0x82B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_uint64_float64},
    {0x82B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int16_uint8_float64},
    {0x82B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_float32_float64},
    {0x82B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_float64_float64},
    {0x82B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_int16_float64},
    {0x82B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_int32_float64},
    {0x82B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_int64_float64},
    {0x82B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_int8_float64},
    {0x82B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_uint16_float64},
    {0x82B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_uint32_float64},
    {0x82BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_uint64_float64},
    {0x82BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int32_uint8_float64},
    {0x82BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_float32_float64},
    {0x82BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_float64_float64},
    {0x82BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_int16_float64},
    {0x82BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_int32_float64},
    {0x82C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_int64_float64},
    {0x82C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_int8_float64},
    {0x82C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_uint16_float64},
    {0x82C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_uint32_float64},
    {0x82C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_uint64_float64},
    {0x82C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int64_uint8_float64},
    {0x82C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_float32_float64},
    {0x82C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_float64_float64},
    {0x82C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_int16_float64},
    {0x82C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_int32_float64},
    {0x82CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_int64_float64},
    {0x82CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_int8_float64},
    {0x82CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_uint16_float64},
    {0x82CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_uint32_float64},
    {0x82CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_uint64_float64},
    {0x82CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_int8_uint8_float64},
    {0x82D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_float32_float64},
    {0x82D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_float64_float64},
    {0x82D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_int16_float64},
    {0x82D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_int32_float64},
    {0x82D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_int64_float64},
    {0x82D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_int8_float64},
    {0x82D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_uint16_float64},
    {0x82D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_uint32_float64},
    {0x82D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_uint64_float64},
    {0x82D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint16_uint8_float64},
    {0x82DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_float32_float64},
    {0x82DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_float64_float64},
    {0x82DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_int16_float64},
    {0x82DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_int32_float64},
    {0x82DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_int64_float64},
    {0x82DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_int8_float64},
    {0x82E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_uint16_float64},
    {0x82E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_uint32_float64},
    {0x82E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_uint64_float64},
    {0x82E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint32_uint8_float64},
    {0x82E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_float32_float64},
    {0x82E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_float64_float64},
    {0x82E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_int16_float64},
    {0x82E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_int32_float64},
    {0x82E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_int64_float64},
    {0x82E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_int8_float64},
    {0x82EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_uint16_float64},
    {0x82EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_uint32_float64},
    {0x82ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_uint64_float64},
    {0x82EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint64_uint8_float64},
    {0x82EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_float32_float64},
    {0x82EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_float64_float64},
    {0x82F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_int16_float64},
    {0x82F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_int32_float64},
    {0x82F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_int64_float64},
    {0x82F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_int8_float64},
    {0x82F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_uint16_float64},
    {0x82F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_uint32_float64},
    {0x82F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_uint64_float64},
    {0x82F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_power_uint8_uint8_float64},
    {0x82F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_float32_bool},
    {0x82F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_float64_bool},
    {0x82FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_int16_bool},
    {0x82FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_int32_bool},
    {0x82FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_int64_bool},
    {0x82FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_int8_bool},
    {0x82FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_uint16_bool},
    {0x82FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_uint32_bool},
    {0x8300U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_uint64_bool},
    {0x8301U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float32_uint8_bool},
    {0x8302U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_float32_bool},
    {0x8303U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_float64_bool},
    {0x8304U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_int16_bool},
    {0x8305U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_int32_bool},
    {0x8306U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_int64_bool},
    {0x8307U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_int8_bool},
    {0x8308U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_uint16_bool},
    {0x8309U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_uint32_bool},
    {0x830AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_uint64_bool},
    {0x830BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_float64_uint8_bool},
    {0x830CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_float32_bool},
    {0x830DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_float64_bool},
    {0x830EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_int16_bool},
    {0x830FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_int32_bool},
    {0x8310U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_int64_bool},
    {0x8311U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_int8_bool},
    {0x8312U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_uint16_bool},
    {0x8313U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_uint32_bool},
    {0x8314U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_uint64_bool},
    {0x8315U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int16_uint8_bool},
    {0x8316U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_float32_bool},
    {0x8317U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_float64_bool},
    {0x8318U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_int16_bool},
    {0x8319U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_int32_bool},
    {0x831AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_int64_bool},
    {0x831BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_int8_bool},
    {0x831CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_uint16_bool},
    {0x831DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_uint32_bool},
    {0x831EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_uint64_bool},
    {0x831FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int32_uint8_bool},
    {0x8320U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_float32_bool},
    {0x8321U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_float64_bool},
    {0x8322U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_int16_bool},
    {0x8323U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_int32_bool},
    {0x8324U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_int64_bool},
    {0x8325U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_int8_bool},
    {0x8326U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_uint16_bool},
    {0x8327U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_uint32_bool},
    {0x8328U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_uint64_bool},
    {0x8329U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int64_uint8_bool},
    {0x832AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_float32_bool},
    {0x832BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_float64_bool},
    {0x832CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_int16_bool},
    {0x832DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_int32_bool},
    {0x832EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_int64_bool},
    {0x832FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_int8_bool},
    {0x8330U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_uint16_bool},
    {0x8331U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_uint32_bool},
    {0x8332U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_uint64_bool},
    {0x8333U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_int8_uint8_bool},
    {0x8334U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_float32_bool},
    {0x8335U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_float64_bool},
    {0x8336U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_int16_bool},
    {0x8337U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_int32_bool},
    {0x8338U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_int64_bool},
    {0x8339U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_int8_bool},
    {0x833AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_uint16_bool},
    {0x833BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_uint32_bool},
    {0x833CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_uint64_bool},
    {0x833DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint16_uint8_bool},
    {0x833EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_float32_bool},
    {0x833FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_float64_bool},
    {0x8340U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_int16_bool},
    {0x8341U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_int32_bool},
    {0x8342U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_int64_bool},
    {0x8343U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_int8_bool},
    {0x8344U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_uint16_bool},
    {0x8345U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_uint32_bool},
    {0x8346U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_uint64_bool},
    {0x8347U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint32_uint8_bool},
    {0x8348U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_float32_bool},
    {0x8349U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_float64_bool},
    {0x834AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_int16_bool},
    {0x834BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_int32_bool},
    {0x834CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_int64_bool},
    {0x834DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_int8_bool},
    {0x834EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_uint16_bool},
    {0x834FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_uint32_bool},
    {0x8350U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_uint64_bool},
    {0x8351U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint64_uint8_bool},
    {0x8352U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_float32_bool},
    {0x8353U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_float64_bool},
    {0x8354U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_int16_bool},
    {0x8355U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_int32_bool},
    {0x8356U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_int64_bool},
    {0x8357U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_int8_bool},
    {0x8358U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_uint16_bool},
    {0x8359U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_uint32_bool},
    {0x835AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_uint64_bool},
    {0x835BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_equal_uint8_uint8_bool},
    {0x835CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_float32_bool},
    {0x835DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_float64_bool},
    {0x835EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_int16_bool},
    {0x835FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_int32_bool},
    {0x8360U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_int64_bool},
    {0x8361U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_int8_bool},
    {0x8362U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_uint16_bool},
    {0x8363U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_uint32_bool},
    {0x8364U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_uint64_bool},
    {0x8365U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float32_uint8_bool},
    {0x8366U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_float32_bool},
    {0x8367U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_float64_bool},
    {0x8368U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_int16_bool},
    {0x8369U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_int32_bool},
    {0x836AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_int64_bool},
    {0x836BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_int8_bool},
    {0x836CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_uint16_bool},
    {0x836DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_uint32_bool},
    {0x836EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_uint64_bool},
    {0x836FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_float64_uint8_bool},
    {0x8370U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_float32_bool},
    {0x8371U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_float64_bool},
    {0x8372U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_int16_bool},
    {0x8373U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_int32_bool},
    {0x8374U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_int64_bool},
    {0x8375U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_int8_bool},
    {0x8376U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_uint16_bool},
    {0x8377U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_uint32_bool},
    {0x8378U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_uint64_bool},
    {0x8379U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int16_uint8_bool},
    {0x837AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_float32_bool},
    {0x837BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_float64_bool},
    {0x837CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_int16_bool},
    {0x837DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_int32_bool},
    {0x837EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_int64_bool},
    {0x837FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_int8_bool},
    {0x8380U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_uint16_bool},
    {0x8381U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_uint32_bool},
    {0x8382U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_uint64_bool},
    {0x8383U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int32_uint8_bool},
    {0x8384U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_float32_bool},
    {0x8385U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_float64_bool},
    {0x8386U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_int16_bool},
    {0x8387U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_int32_bool},
    {0x8388U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_int64_bool},
    {0x8389U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_int8_bool},
    {0x838AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_uint16_bool},
    {0x838BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_uint32_bool},
    {0x838CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_uint64_bool},
    {0x838DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int64_uint8_bool},
    {0x838EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_float32_bool},
    {0x838FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_float64_bool},
    {0x8390U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_int16_bool},
    {0x8391U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_int32_bool},
    {0x8392U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_int64_bool},
    {0x8393U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_int8_bool},
    {0x8394U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_uint16_bool},
    {0x8395U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_uint32_bool},
    {0x8396U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_uint64_bool},
    {0x8397U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_int8_uint8_bool},
    {0x8398U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_float32_bool},
    {0x8399U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_float64_bool},
    {0x839AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_int16_bool},
    {0x839BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_int32_bool},
    {0x839CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_int64_bool},
    {0x839DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_int8_bool},
    {0x839EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_uint16_bool},
    {0x839FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_uint32_bool},
    {0x83A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_uint64_bool},
    {0x83A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint16_uint8_bool},
    {0x83A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_float32_bool},
    {0x83A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_float64_bool},
    {0x83A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_int16_bool},
    {0x83A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_int32_bool},
    {0x83A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_int64_bool},
    {0x83A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_int8_bool},
    {0x83A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_uint16_bool},
    {0x83A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_uint32_bool},
    {0x83AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_uint64_bool},
    {0x83ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint32_uint8_bool},
    {0x83ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_float32_bool},
    {0x83ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_float64_bool},
    {0x83AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_int16_bool},
    {0x83AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_int32_bool},
    {0x83B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_int64_bool},
    {0x83B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_int8_bool},
    {0x83B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_uint16_bool},
    {0x83B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_uint32_bool},
    {0x83B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_uint64_bool},
    {0x83B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint64_uint8_bool},
    {0x83B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_float32_bool},
    {0x83B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_float64_bool},
    {0x83B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_int16_bool},
    {0x83B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_int32_bool},
    {0x83BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_int64_bool},
    {0x83BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_int8_bool},
    {0x83BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_uint16_bool},
    {0x83BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_uint32_bool},
    {0x83BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_uint64_bool},
    {0x83BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_not_equal_uint8_uint8_bool},
    {0x83C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_float32_bool},
    {0x83C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_float64_bool},
    {0x83C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_int16_bool},
    {0x83C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_int32_bool},
    {0x83C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_int64_bool},
    {0x83C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_int8_bool},
    {0x83C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_uint16_bool},
    {0x83C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_uint32_bool},
    {0x83C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_uint64_bool},
    {0x83C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float32_uint8_bool},
    {0x83CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_float32_bool},
    {0x83CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_float64_bool},
    {0x83CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_int16_bool},
    {0x83CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_int32_bool},
    {0x83CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_int64_bool},
    {0x83CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_int8_bool},
    {0x83D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_uint16_bool},
    {0x83D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_uint32_bool},
    {0x83D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_uint64_bool},
    {0x83D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_float64_uint8_bool},
    {0x83D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_float32_bool},
    {0x83D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_float64_bool},
    {0x83D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_int16_bool},
    {0x83D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_int32_bool},
    {0x83D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_int64_bool},
    {0x83D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_int8_bool},
    {0x83DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_uint16_bool},
    {0x83DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_uint32_bool},
    {0x83DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_uint64_bool},
    {0x83DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int16_uint8_bool},
    {0x83DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_float32_bool},
    {0x83DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_float64_bool},
    {0x83E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_int16_bool},
    {0x83E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_int32_bool},
    {0x83E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_int64_bool},
    {0x83E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_int8_bool},
    {0x83E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_uint16_bool},
    {0x83E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_uint32_bool},
    {0x83E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_uint64_bool},
    {0x83E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int32_uint8_bool},
    {0x83E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_float32_bool},
    {0x83E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_float64_bool},
    {0x83EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_int16_bool},
    {0x83EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_int32_bool},
    {0x83ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_int64_bool},
    {0x83EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_int8_bool},
    {0x83EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_uint16_bool},
    {0x83EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_uint32_bool},
    {0x83F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_uint64_bool},
    {0x83F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int64_uint8_bool},
    {0x83F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_float32_bool},
    {0x83F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_float64_bool},
    {0x83F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_int16_bool},
    {0x83F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_int32_bool},
    {0x83F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_int64_bool},
    {0x83F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_int8_bool},
    {0x83F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_uint16_bool},
    {0x83F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_uint32_bool},
    {0x83FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_uint64_bool},
    {0x83FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_int8_uint8_bool},
    {0x83FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_float32_bool},
    {0x83FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_float64_bool},
    {0x83FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_int16_bool},
    {0x83FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_int32_bool},
    {0x8400U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_int64_bool},
    {0x8401U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_int8_bool},
    {0x8402U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_uint16_bool},
    {0x8403U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_uint32_bool},
    {0x8404U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_uint64_bool},
    {0x8405U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint16_uint8_bool},
    {0x8406U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_float32_bool},
    {0x8407U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_float64_bool},
    {0x8408U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_int16_bool},
    {0x8409U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_int32_bool},
    {0x840AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_int64_bool},
    {0x840BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_int8_bool},
    {0x840CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_uint16_bool},
    {0x840DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_uint32_bool},
    {0x840EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_uint64_bool},
    {0x840FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint32_uint8_bool},
    {0x8410U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_float32_bool},
    {0x8411U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_float64_bool},
    {0x8412U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_int16_bool},
    {0x8413U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_int32_bool},
    {0x8414U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_int64_bool},
    {0x8415U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_int8_bool},
    {0x8416U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_uint16_bool},
    {0x8417U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_uint32_bool},
    {0x8418U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_uint64_bool},
    {0x8419U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint64_uint8_bool},
    {0x841AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_float32_bool},
    {0x841BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_float64_bool},
    {0x841CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_int16_bool},
    {0x841DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_int32_bool},
    {0x841EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_int64_bool},
    {0x841FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_int8_bool},
    {0x8420U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_uint16_bool},
    {0x8421U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_uint32_bool},
    {0x8422U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_uint64_bool},
    {0x8423U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_uint8_uint8_bool},
    {0x8424U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_float32_bool},
    {0x8425U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_float64_bool},
    {0x8426U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_int16_bool},
    {0x8427U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_int32_bool},
    {0x8428U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_int64_bool},
    {0x8429U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_int8_bool},
    {0x842AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_uint16_bool},
    {0x842BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_uint32_bool},
    {0x842CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_uint64_bool},
    {0x842DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float32_uint8_bool},
    {0x842EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_float32_bool},
    {0x842FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_float64_bool},
    {0x8430U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_int16_bool},
    {0x8431U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_int32_bool},
    {0x8432U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_int64_bool},
    {0x8433U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_int8_bool},
    {0x8434U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_uint16_bool},
    {0x8435U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_uint32_bool},
    {0x8436U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_uint64_bool},
    {0x8437U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_float64_uint8_bool},
    {0x8438U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_float32_bool},
    {0x8439U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_float64_bool},
    {0x843AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_int16_bool},
    {0x843BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_int32_bool},
    {0x843CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_int64_bool},
    {0x843DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_int8_bool},
    {0x843EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_uint16_bool},
    {0x843FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_uint32_bool},
    {0x8440U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_uint64_bool},
    {0x8441U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int16_uint8_bool},
    {0x8442U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_float32_bool},
    {0x8443U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_float64_bool},
    {0x8444U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_int16_bool},
    {0x8445U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_int32_bool},
    {0x8446U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_int64_bool},
    {0x8447U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_int8_bool},
    {0x8448U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_uint16_bool},
    {0x8449U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_uint32_bool},
    {0x844AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_uint64_bool},
    {0x844BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int32_uint8_bool},
    {0x844CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_float32_bool},
    {0x844DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_float64_bool},
    {0x844EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_int16_bool},
    {0x844FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_int32_bool},
    {0x8450U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_int64_bool},
    {0x8451U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_int8_bool},
    {0x8452U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_uint16_bool},
    {0x8453U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_uint32_bool},
    {0x8454U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_uint64_bool},
    {0x8455U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int64_uint8_bool},
    {0x8456U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_float32_bool},
    {0x8457U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_float64_bool},
    {0x8458U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_int16_bool},
    {0x8459U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_int32_bool},
    {0x845AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_int64_bool},
    {0x845BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_int8_bool},
    {0x845CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_uint16_bool},
    {0x845DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_uint32_bool},
    {0x845EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_uint64_bool},
    {0x845FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_int8_uint8_bool},
    {0x8460U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_float32_bool},
    {0x8461U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_float64_bool},
    {0x8462U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_int16_bool},
    {0x8463U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_int32_bool},
    {0x8464U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_int64_bool},
    {0x8465U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_int8_bool},
    {0x8466U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_uint16_bool},
    {0x8467U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_uint32_bool},
    {0x8468U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_uint64_bool},
    {0x8469U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint16_uint8_bool},
    {0x846AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_float32_bool},
    {0x846BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_float64_bool},
    {0x846CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_int16_bool},
    {0x846DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_int32_bool},
    {0x846EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_int64_bool},
    {0x846FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_int8_bool},
    {0x8470U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_uint16_bool},
    {0x8471U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_uint32_bool},
    {0x8472U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_uint64_bool},
    {0x8473U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint32_uint8_bool},
    {0x8474U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_float32_bool},
    {0x8475U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_float64_bool},
    {0x8476U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_int16_bool},
    {0x8477U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_int32_bool},
    {0x8478U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_int64_bool},
    {0x8479U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_int8_bool},
    {0x847AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_uint16_bool},
    {0x847BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_uint32_bool},
    {0x847CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_uint64_bool},
    {0x847DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint64_uint8_bool},
    {0x847EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_float32_bool},
    {0x847FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_float64_bool},
    {0x8480U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_int16_bool},
    {0x8481U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_int32_bool},
    {0x8482U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_int64_bool},
    {0x8483U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_int8_bool},
    {0x8484U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_uint16_bool},
    {0x8485U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_uint32_bool},
    {0x8486U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_uint64_bool},
    {0x8487U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_less_equal_uint8_uint8_bool},
    {0x8488U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_float32_bool},
    {0x8489U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_float64_bool},
    {0x848AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_int16_bool},
    {0x848BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_int32_bool},
    {0x848CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_int64_bool},
    {0x848DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_int8_bool},
    {0x848EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_uint16_bool},
    {0x848FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_uint32_bool},
    {0x8490U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_uint64_bool},
    {0x8491U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float32_uint8_bool},
    {0x8492U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_float32_bool},
    {0x8493U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_float64_bool},
    {0x8494U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_int16_bool},
    {0x8495U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_int32_bool},
    {0x8496U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_int64_bool},
    {0x8497U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_int8_bool},
    {0x8498U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_uint16_bool},
    {0x8499U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_uint32_bool},
    {0x849AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_uint64_bool},
    {0x849BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_float64_uint8_bool},
    {0x849CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_float32_bool},
    {0x849DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_float64_bool},
    {0x849EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_int16_bool},
    {0x849FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_int32_bool},
    {0x84A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_int64_bool},
    {0x84A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_int8_bool},
    {0x84A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_uint16_bool},
    {0x84A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_uint32_bool},
    {0x84A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_uint64_bool},
    {0x84A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int16_uint8_bool},
    {0x84A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_float32_bool},
    {0x84A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_float64_bool},
    {0x84A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_int16_bool},
    {0x84A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_int32_bool},
    {0x84AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_int64_bool},
    {0x84ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_int8_bool},
    {0x84ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_uint16_bool},
    {0x84ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_uint32_bool},
    {0x84AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_uint64_bool},
    {0x84AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int32_uint8_bool},
    {0x84B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_float32_bool},
    {0x84B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_float64_bool},
    {0x84B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_int16_bool},
    {0x84B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_int32_bool},
    {0x84B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_int64_bool},
    {0x84B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_int8_bool},
    {0x84B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_uint16_bool},
    {0x84B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_uint32_bool},
    {0x84B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_uint64_bool},
    {0x84B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int64_uint8_bool},
    {0x84BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_float32_bool},
    {0x84BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_float64_bool},
    {0x84BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_int16_bool},
    {0x84BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_int32_bool},
    {0x84BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_int64_bool},
    {0x84BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_int8_bool},
    {0x84C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_uint16_bool},
    {0x84C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_uint32_bool},
    {0x84C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_uint64_bool},
    {0x84C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_int8_uint8_bool},
    {0x84C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_float32_bool},
    {0x84C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_float64_bool},
    {0x84C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_int16_bool},
    {0x84C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_int32_bool},
    {0x84C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_int64_bool},
    {0x84C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_int8_bool},
    {0x84CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_uint16_bool},
    {0x84CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_uint32_bool},
    {0x84CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_uint64_bool},
    {0x84CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint16_uint8_bool},
    {0x84CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_float32_bool},
    {0x84CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_float64_bool},
    {0x84D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_int16_bool},
    {0x84D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_int32_bool},
    {0x84D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_int64_bool},
    {0x84D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_int8_bool},
    {0x84D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_uint16_bool},
    {0x84D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_uint32_bool},
    {0x84D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_uint64_bool},
    {0x84D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint32_uint8_bool},
    {0x84D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_float32_bool},
    {0x84D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_float64_bool},
    {0x84DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_int16_bool},
    {0x84DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_int32_bool},
    {0x84DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_int64_bool},
    {0x84DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_int8_bool},
    {0x84DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_uint16_bool},
    {0x84DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_uint32_bool},
    {0x84E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_uint64_bool},
    {0x84E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint64_uint8_bool},
    {0x84E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_float32_bool},
    {0x84E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_float64_bool},
    {0x84E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_int16_bool},
    {0x84E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_int32_bool},
    {0x84E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_int64_bool},
    {0x84E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_int8_bool},
    {0x84E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_uint16_bool},
    {0x84E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_uint32_bool},
    {0x84EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_uint64_bool},
    {0x84EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_uint8_uint8_bool},
    {0x84ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_float32_bool},
    {0x84EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_float64_bool},
    {0x84EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_int16_bool},
    {0x84EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_int32_bool},
    {0x84F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_int64_bool},
    {0x84F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_int8_bool},
    {0x84F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_uint16_bool},
    {0x84F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_uint32_bool},
    {0x84F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_uint64_bool},
    {0x84F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float32_uint8_bool},
    {0x84F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_float32_bool},
    {0x84F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_float64_bool},
    {0x84F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_int16_bool},
    {0x84F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_int32_bool},
    {0x84FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_int64_bool},
    {0x84FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_int8_bool},
    {0x84FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_uint16_bool},
    {0x84FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_uint32_bool},
    {0x84FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_uint64_bool},
    {0x84FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_float64_uint8_bool},
    {0x8500U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_float32_bool},
    {0x8501U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_float64_bool},
    {0x8502U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_int16_bool},
    {0x8503U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_int32_bool},
    {0x8504U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_int64_bool},
    {0x8505U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_int8_bool},
    {0x8506U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_uint16_bool},
    {0x8507U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_uint32_bool},
    {0x8508U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_uint64_bool},
    {0x8509U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int16_uint8_bool},
    {0x850AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_float32_bool},
    {0x850BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_float64_bool},
    {0x850CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_int16_bool},
    {0x850DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_int32_bool},
    {0x850EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_int64_bool},
    {0x850FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_int8_bool},
    {0x8510U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_uint16_bool},
    {0x8511U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_uint32_bool},
    {0x8512U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_uint64_bool},
    {0x8513U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int32_uint8_bool},
    {0x8514U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_float32_bool},
    {0x8515U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_float64_bool},
    {0x8516U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_int16_bool},
    {0x8517U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_int32_bool},
    {0x8518U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_int64_bool},
    {0x8519U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_int8_bool},
    {0x851AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_uint16_bool},
    {0x851BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_uint32_bool},
    {0x851CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_uint64_bool},
    {0x851DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int64_uint8_bool},
    {0x851EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_float32_bool},
    {0x851FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_float64_bool},
    {0x8520U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_int16_bool},
    {0x8521U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_int32_bool},
    {0x8522U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_int64_bool},
    {0x8523U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_int8_bool},
    {0x8524U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_uint16_bool},
    {0x8525U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_uint32_bool},
    {0x8526U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_uint64_bool},
    {0x8527U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_int8_uint8_bool},
    {0x8528U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_float32_bool},
    {0x8529U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_float64_bool},
    {0x852AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_int16_bool},
    {0x852BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_int32_bool},
    {0x852CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_int64_bool},
    {0x852DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_int8_bool},
    {0x852EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_uint16_bool},
    {0x852FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_uint32_bool},
    {0x8530U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_uint64_bool},
    {0x8531U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint16_uint8_bool},
    {0x8532U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_float32_bool},
    {0x8533U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_float64_bool},
    {0x8534U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_int16_bool},
    {0x8535U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_int32_bool},
    {0x8536U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_int64_bool},
    {0x8537U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_int8_bool},
    {0x8538U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_uint16_bool},
    {0x8539U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_uint32_bool},
    {0x853AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_uint64_bool},
    {0x853BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint32_uint8_bool},
    {0x853CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_float32_bool},
    {0x853DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_float64_bool},
    {0x853EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_int16_bool},
    {0x853FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_int32_bool},
    {0x8540U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_int64_bool},
    {0x8541U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_int8_bool},
    {0x8542U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_uint16_bool},
    {0x8543U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_uint32_bool},
    {0x8544U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_uint64_bool},
    {0x8545U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint64_uint8_bool},
    {0x8546U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_float32_bool},
    {0x8547U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_float64_bool},
    {0x8548U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_int16_bool},
    {0x8549U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_int32_bool},
    {0x854AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_int64_bool},
    {0x854BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_int8_bool},
    {0x854CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_uint16_bool},
    {0x854DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_uint32_bool},
    {0x854EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_uint64_bool},
    {0x854FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_greater_equal_uint8_uint8_bool},
    {0x8550U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_bool_bool},
    {0x8551U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_float32_bool},
    {0x8552U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_float64_bool},
    {0x8553U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_int16_bool},
    {0x8554U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_int32_bool},
    {0x8555U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_int64_bool},
    {0x8556U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_int8_bool},
    {0x8557U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_uint16_bool},
    {0x8558U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_uint32_bool},
    {0x8559U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_uint64_bool},
    {0x855AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_bool_uint8_bool},
    {0x855BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_bool_bool},
    {0x855CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_float32_bool},
    {0x855DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_float64_bool},
    {0x855EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_int16_bool},
    {0x855FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_int32_bool},
    {0x8560U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_int64_bool},
    {0x8561U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_int8_bool},
    {0x8562U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_uint16_bool},
    {0x8563U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_uint32_bool},
    {0x8564U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_uint64_bool},
    {0x8565U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float32_uint8_bool},
    {0x8566U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_bool_bool},
    {0x8567U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_float32_bool},
    {0x8568U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_float64_bool},
    {0x8569U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_int16_bool},
    {0x856AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_int32_bool},
    {0x856BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_int64_bool},
    {0x856CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_int8_bool},
    {0x856DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_uint16_bool},
    {0x856EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_uint32_bool},
    {0x856FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_uint64_bool},
    {0x8570U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_float64_uint8_bool},
    {0x8571U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_bool_bool},
    {0x8572U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_float32_bool},
    {0x8573U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_float64_bool},
    {0x8574U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_int16_bool},
    {0x8575U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_int32_bool},
    {0x8576U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_int64_bool},
    {0x8577U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_int8_bool},
    {0x8578U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_uint16_bool},
    {0x8579U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_uint32_bool},
    {0x857AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_uint64_bool},
    {0x857BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int16_uint8_bool},
    {0x857CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_bool_bool},
    {0x857DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_float32_bool},
    {0x857EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_float64_bool},
    {0x857FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_int16_bool},
    {0x8580U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_int32_bool},
    {0x8581U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_int64_bool},
    {0x8582U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_int8_bool},
    {0x8583U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_uint16_bool},
    {0x8584U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_uint32_bool},
    {0x8585U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_uint64_bool},
    {0x8586U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int32_uint8_bool},
    {0x8587U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_bool_bool},
    {0x8588U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_float32_bool},
    {0x8589U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_float64_bool},
    {0x858AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_int16_bool},
    {0x858BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_int32_bool},
    {0x858CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_int64_bool},
    {0x858DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_int8_bool},
    {0x858EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_uint16_bool},
    {0x858FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_uint32_bool},
    {0x8590U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_uint64_bool},
    {0x8591U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int64_uint8_bool},
    {0x8592U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_bool_bool},
    {0x8593U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_float32_bool},
    {0x8594U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_float64_bool},
    {0x8595U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_int16_bool},
    {0x8596U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_int32_bool},
    {0x8597U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_int64_bool},
    {0x8598U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_int8_bool},
    {0x8599U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_uint16_bool},
    {0x859AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_uint32_bool},
    {0x859BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_uint64_bool},
    {0x859CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_int8_uint8_bool},
    {0x859DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_bool_bool},
    {0x859EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_float32_bool},
    {0x859FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_float64_bool},
    {0x85A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_int16_bool},
    {0x85A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_int32_bool},
    {0x85A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_int64_bool},
    {0x85A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_int8_bool},
    {0x85A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_uint16_bool},
    {0x85A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_uint32_bool},
    {0x85A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_uint64_bool},
    {0x85A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint16_uint8_bool},
    {0x85A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_bool_bool},
    {0x85A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_float32_bool},
    {0x85AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_float64_bool},
    {0x85ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_int16_bool},
    {0x85ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_int32_bool},
    {0x85ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_int64_bool},
    {0x85AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_int8_bool},
    {0x85AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_uint16_bool},
    {0x85B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_uint32_bool},
    {0x85B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_uint64_bool},
    {0x85B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint32_uint8_bool},
    {0x85B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_bool_bool},
    {0x85B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_float32_bool},
    {0x85B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_float64_bool},
    {0x85B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_int16_bool},
    {0x85B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_int32_bool},
    {0x85B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_int64_bool},
    {0x85B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_int8_bool},
    {0x85BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_uint16_bool},
    {0x85BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_uint32_bool},
    {0x85BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_uint64_bool},
    {0x85BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint64_uint8_bool},
    {0x85BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_bool_bool},
    {0x85BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_float32_bool},
    {0x85C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_float64_bool},
    {0x85C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_int16_bool},
    {0x85C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_int32_bool},
    {0x85C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_int64_bool},
    {0x85C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_int8_bool},
    {0x85C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_uint16_bool},
    {0x85C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_uint32_bool},
    {0x85C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_uint64_bool},
    {0x85C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_and_uint8_uint8_bool},
    {0x85C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_bool_bool},
    {0x85CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_float32_bool},
    {0x85CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_float64_bool},
    {0x85CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_int16_bool},
    {0x85CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_int32_bool},
    {0x85CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_int64_bool},
    {0x85CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_int8_bool},
    {0x85D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_uint16_bool},
    {0x85D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_uint32_bool},
    {0x85D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_uint64_bool},
    {0x85D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_bool_uint8_bool},
    {0x85D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_bool_bool},
    {0x85D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_float32_bool},
    {0x85D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_float64_bool},
    {0x85D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_int16_bool},
    {0x85D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_int32_bool},
    {0x85D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_int64_bool},
    {0x85DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_int8_bool},
    {0x85DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_uint16_bool},
    {0x85DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_uint32_bool},
    {0x85DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_uint64_bool},
    {0x85DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float32_uint8_bool},
    {0x85DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_bool_bool},
    {0x85E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_float32_bool},
    {0x85E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_float64_bool},
    {0x85E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_int16_bool},
    {0x85E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_int32_bool},
    {0x85E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_int64_bool},
    {0x85E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_int8_bool},
    {0x85E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_uint16_bool},
    {0x85E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_uint32_bool},
    {0x85E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_uint64_bool},
    {0x85E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_float64_uint8_bool},
    {0x85EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_bool_bool},
    {0x85EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_float32_bool},
    {0x85ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_float64_bool},
    {0x85EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_int16_bool},
    {0x85EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_int32_bool},
    {0x85EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_int64_bool},
    {0x85F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_int8_bool},
    {0x85F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_uint16_bool},
    {0x85F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_uint32_bool},
    {0x85F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_uint64_bool},
    {0x85F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int16_uint8_bool},
    {0x85F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_bool_bool},
    {0x85F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_float32_bool},
    {0x85F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_float64_bool},
    {0x85F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_int16_bool},
    {0x85F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_int32_bool},
    {0x85FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_int64_bool},
    {0x85FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_int8_bool},
    {0x85FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_uint16_bool},
    {0x85FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_uint32_bool},
    {0x85FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_uint64_bool},
    {0x85FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int32_uint8_bool},
    {0x8600U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_bool_bool},
    {0x8601U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_float32_bool},
    {0x8602U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_float64_bool},
    {0x8603U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_int16_bool},
    {0x8604U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_int32_bool},
    {0x8605U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_int64_bool},
    {0x8606U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_int8_bool},
    {0x8607U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_uint16_bool},
    {0x8608U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_uint32_bool},
    {0x8609U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_uint64_bool},
    {0x860AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int64_uint8_bool},
    {0x860BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_bool_bool},
    {0x860CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_float32_bool},
    {0x860DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_float64_bool},
    {0x860EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_int16_bool},
    {0x860FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_int32_bool},
    {0x8610U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_int64_bool},
    {0x8611U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_int8_bool},
    {0x8612U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_uint16_bool},
    {0x8613U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_uint32_bool},
    {0x8614U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_uint64_bool},
    {0x8615U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_int8_uint8_bool},
    {0x8616U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_bool_bool},
    {0x8617U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_float32_bool},
    {0x8618U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_float64_bool},
    {0x8619U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_int16_bool},
    {0x861AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_int32_bool},
    {0x861BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_int64_bool},
    {0x861CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_int8_bool},
    {0x861DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_uint16_bool},
    {0x861EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_uint32_bool},
    {0x861FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_uint64_bool},
    {0x8620U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint16_uint8_bool},
    {0x8621U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_bool_bool},
    {0x8622U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_float32_bool},
    {0x8623U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_float64_bool},
    {0x8624U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_int16_bool},
    {0x8625U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_int32_bool},
    {0x8626U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_int64_bool},
    {0x8627U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_int8_bool},
    {0x8628U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_uint16_bool},
    {0x8629U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_uint32_bool},
    {0x862AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_uint64_bool},
    {0x862BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint32_uint8_bool},
    {0x862CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_bool_bool},
    {0x862DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_float32_bool},
    {0x862EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_float64_bool},
    {0x862FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_int16_bool},
    {0x8630U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_int32_bool},
    {0x8631U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_int64_bool},
    {0x8632U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_int8_bool},
    {0x8633U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_uint16_bool},
    {0x8634U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_uint32_bool},
    {0x8635U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_uint64_bool},
    {0x8636U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint64_uint8_bool},
    {0x8637U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_bool_bool},
    {0x8638U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_float32_bool},
    {0x8639U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_float64_bool},
    {0x863AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_int16_bool},
    {0x863BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_int32_bool},
    {0x863CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_int64_bool},
    {0x863DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_int8_bool},
    {0x863EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_uint16_bool},
    {0x863FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_uint32_bool},
    {0x8640U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_uint64_bool},
    {0x8641U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_or_uint8_uint8_bool},
    {0x8642U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_bool_bool},
    {0x8643U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_float32_bool},
    {0x8644U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_float64_bool},
    {0x8645U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_int16_bool},
    {0x8646U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_int32_bool},
    {0x8647U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_int64_bool},
    {0x8648U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_int8_bool},
    {0x8649U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_uint16_bool},
    {0x864AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_uint32_bool},
    {0x864BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_uint64_bool},
    {0x864CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_logical_not_uint8_bool},
    {0x864DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_bool_bool},
    {0x864EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_char_ascii_char_ascii},
    {0x864FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_char_utf32_char_utf32},
    {0x8650U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_char_utf8_char_utf8},
    {0x8651U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_float32_float32},
    {0x8652U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_float64_float64},
    {0x8653U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_int16_int16},
    {0x8654U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_int32_int32},
    {0x8655U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_int64_int64},
    {0x8656U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_int8_int8},
    {0x8657U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_uint16_uint16},
    {0x8658U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_uint32_uint32},
    {0x8659U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_uint64_uint64},
    {0x865AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_bool_uint8_uint8},
    {0x865BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_bool_bool},
    {0x865CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_char_ascii_char_ascii},
    {0x865DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_char_utf32_char_utf32},
    {0x865EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_char_utf8_char_utf8},
    {0x865FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_float32_float32},
    {0x8660U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_float64_float64},
    {0x8661U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_int16_int16},
    {0x8662U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_int32_int32},
    {0x8663U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_int64_int64},
    {0x8664U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_int8_int8},
    {0x8665U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_uint16_uint16},
    {0x8666U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_uint32_uint32},
    {0x8667U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_uint64_uint64},
    {0x8668U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_ascii_uint8_uint8},
    {0x8669U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_bool_bool},
    {0x866AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_char_ascii_char_ascii},
    {0x866BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_char_utf32_char_utf32},
    {0x866CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_char_utf8_char_utf8},
    {0x866DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_float32_float32},
    {0x866EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_float64_float64},
    {0x866FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_int16_int16},
    {0x8670U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_int32_int32},
    {0x8671U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_int64_int64},
    {0x8672U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_int8_int8},
    {0x8673U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_uint16_uint16},
    {0x8674U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_uint32_uint32},
    {0x8675U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_uint64_uint64},
    {0x8676U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf32_uint8_uint8},
    {0x8677U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_bool_bool},
    {0x8678U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_char_ascii_char_ascii},
    {0x8679U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_char_utf32_char_utf32},
    {0x867AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_char_utf8_char_utf8},
    {0x867BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_float32_float32},
    {0x867CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_float64_float64},
    {0x867DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_int16_int16},
    {0x867EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_int32_int32},
    {0x867FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_int64_int64},
    {0x8680U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_int8_int8},
    {0x8681U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_uint16_uint16},
    {0x8682U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_uint32_uint32},
    {0x8683U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_uint64_uint64},
    {0x8684U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_char_utf8_uint8_uint8},
    {0x8685U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_bool_bool},
    {0x8686U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_char_ascii_char_ascii},
    {0x8687U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_char_utf32_char_utf32},
    {0x8688U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_char_utf8_char_utf8},
    {0x8689U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_float32_float32},
    {0x868AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_float64_float64},
    {0x868BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_int16_int16},
    {0x868CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_int32_int32},
    {0x868DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_int64_int64},
    {0x868EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_int8_int8},
    {0x868FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_uint16_uint16},
    {0x8690U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_uint32_uint32},
    {0x8691U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_uint64_uint64},
    {0x8692U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float32_uint8_uint8},
    {0x8693U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_bool_bool},
    {0x8694U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_char_ascii_char_ascii},
    {0x8695U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_char_utf32_char_utf32},
    {0x8696U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_char_utf8_char_utf8},
    {0x8697U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_float32_float32},
    {0x8698U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_float64_float64},
    {0x8699U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_int16_int16},
    {0x869AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_int32_int32},
    {0x869BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_int64_int64},
    {0x869CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_int8_int8},
    {0x869DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_uint16_uint16},
    {0x869EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_uint32_uint32},
    {0x869FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_uint64_uint64},
    {0x86A0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_float64_uint8_uint8},
    {0x86A1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_bool_bool},
    {0x86A2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_char_ascii_char_ascii},
    {0x86A3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_char_utf32_char_utf32},
    {0x86A4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_char_utf8_char_utf8},
    {0x86A5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_float32_float32},
    {0x86A6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_float64_float64},
    {0x86A7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_int16_int16},
    {0x86A8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_int32_int32},
    {0x86A9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_int64_int64},
    {0x86AAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_int8_int8},
    {0x86ABU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_uint16_uint16},
    {0x86ACU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_uint32_uint32},
    {0x86ADU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_uint64_uint64},
    {0x86AEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int16_uint8_uint8},
    {0x86AFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_bool_bool},
    {0x86B0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_char_ascii_char_ascii},
    {0x86B1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_char_utf32_char_utf32},
    {0x86B2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_char_utf8_char_utf8},
    {0x86B3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_float32_float32},
    {0x86B4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_float64_float64},
    {0x86B5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_int16_int16},
    {0x86B6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_int32_int32},
    {0x86B7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_int64_int64},
    {0x86B8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_int8_int8},
    {0x86B9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_uint16_uint16},
    {0x86BAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_uint32_uint32},
    {0x86BBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_uint64_uint64},
    {0x86BCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int32_uint8_uint8},
    {0x86BDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_bool_bool},
    {0x86BEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_char_ascii_char_ascii},
    {0x86BFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_char_utf32_char_utf32},
    {0x86C0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_char_utf8_char_utf8},
    {0x86C1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_float32_float32},
    {0x86C2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_float64_float64},
    {0x86C3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_int16_int16},
    {0x86C4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_int32_int32},
    {0x86C5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_int64_int64},
    {0x86C6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_int8_int8},
    {0x86C7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_uint16_uint16},
    {0x86C8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_uint32_uint32},
    {0x86C9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_uint64_uint64},
    {0x86CAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int64_uint8_uint8},
    {0x86CBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_bool_bool},
    {0x86CCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_char_ascii_char_ascii},
    {0x86CDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_char_utf32_char_utf32},
    {0x86CEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_char_utf8_char_utf8},
    {0x86CFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_float32_float32},
    {0x86D0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_float64_float64},
    {0x86D1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_int16_int16},
    {0x86D2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_int32_int32},
    {0x86D3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_int64_int64},
    {0x86D4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_int8_int8},
    {0x86D5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_uint16_uint16},
    {0x86D6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_uint32_uint32},
    {0x86D7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_uint64_uint64},
    {0x86D8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_int8_uint8_uint8},
    {0x86D9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_bool_bool},
    {0x86DAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_char_ascii_char_ascii},
    {0x86DBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_char_utf32_char_utf32},
    {0x86DCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_char_utf8_char_utf8},
    {0x86DDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_float32_float32},
    {0x86DEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_float64_float64},
    {0x86DFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_int16_int16},
    {0x86E0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_int32_int32},
    {0x86E1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_int64_int64},
    {0x86E2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_int8_int8},
    {0x86E3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_uint16_uint16},
    {0x86E4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_uint32_uint32},
    {0x86E5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_uint64_uint64},
    {0x86E6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint16_uint8_uint8},
    {0x86E7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_bool_bool},
    {0x86E8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_char_ascii_char_ascii},
    {0x86E9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_char_utf32_char_utf32},
    {0x86EAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_char_utf8_char_utf8},
    {0x86EBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_float32_float32},
    {0x86ECU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_float64_float64},
    {0x86EDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_int16_int16},
    {0x86EEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_int32_int32},
    {0x86EFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_int64_int64},
    {0x86F0U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_int8_int8},
    {0x86F1U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_uint16_uint16},
    {0x86F2U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_uint32_uint32},
    {0x86F3U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_uint64_uint64},
    {0x86F4U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint32_uint8_uint8},
    {0x86F5U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_bool_bool},
    {0x86F6U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_char_ascii_char_ascii},
    {0x86F7U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_char_utf32_char_utf32},
    {0x86F8U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_char_utf8_char_utf8},
    {0x86F9U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_float32_float32},
    {0x86FAU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_float64_float64},
    {0x86FBU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_int16_int16},
    {0x86FCU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_int32_int32},
    {0x86FDU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_int64_int64},
    {0x86FEU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_int8_int8},
    {0x86FFU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_uint16_uint16},
    {0x8700U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_uint32_uint32},
    {0x8701U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_uint64_uint64},
    {0x8702U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint64_uint8_uint8},
    {0x8703U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_bool_bool},
    {0x8704U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_char_ascii_char_ascii},
    {0x8705U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_char_utf32_char_utf32},
    {0x8706U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_char_utf8_char_utf8},
    {0x8707U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_float32_float32},
    {0x8708U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_float64_float64},
    {0x8709U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_int16_int16},
    {0x870AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_int32_int32},
    {0x870BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_int64_int64},
    {0x870CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_int8_int8},
    {0x870DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_uint16_uint16},
    {0x870EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_uint32_uint32},
    {0x870FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_uint64_uint64},
    {0x8710U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_cast_uint8_uint8_uint8},
    {0x8711U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_ascii_char_ascii_char_utf8},
    {0x8712U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_ascii_char_utf32_char_utf8},
    {0x8713U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_ascii_char_utf8_char_utf8},
    {0x8714U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf32_char_ascii_char_utf8},
    {0x8715U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf32_char_utf32_char_utf8},
    {0x8716U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf32_char_utf8_char_utf8},
    {0x8717U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf8_char_ascii_char_utf8},
    {0x8718U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf8_char_utf32_char_utf8},
    {0x8719U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_concat_char_utf8_char_utf8_char_utf8},
    {0x871AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_ascii_char_ascii_bool},
    {0x871BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_ascii_char_utf32_bool},
    {0x871CU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_ascii_char_utf8_bool},
    {0x871DU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf32_char_ascii_bool},
    {0x871EU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf32_char_utf32_bool},
    {0x871FU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf32_char_utf8_bool},
    {0x8720U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf8_char_ascii_bool},
    {0x8721U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf8_char_utf32_bool},
    {0x8722U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_equal_char_utf8_char_utf8_bool},
    {0x8723U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_ascii_char_ascii_bool},
    {0x8724U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_ascii_char_utf32_bool},
    {0x8725U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_ascii_char_utf8_bool},
    {0x8726U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf32_char_ascii_bool},
    {0x8727U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf32_char_utf32_bool},
    {0x8728U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf32_char_utf8_bool},
    {0x8729U, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf8_char_ascii_bool},
    {0x872AU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf8_char_utf32_bool},
    {0x872BU, BINRWIR_REGISTRY_FLAG_NONE, scalar_generated_character_not_equal_char_utf8_char_utf8_bool},
};

const size_t scalar_generated_entry_count =
    sizeof(scalar_generated_entries) / sizeof(scalar_generated_entries[0]);

void runtimec_scalar_probe_add_int64_int32_int64(const int64_t *a, const int32_t *b, int64_t *c) {
    scalar_generated_add_int64_int32_int64_logic(a, b, c);
}

void runtimec_scalar_probe_mul_int32_int32_int32(const int32_t *a, const int32_t *b, int32_t *c) {
    scalar_generated_mul_int32_int32_int32_logic(a, b, c);
}

void runtimec_scalar_probe_min_int64_int32_int64(const int64_t *a, const int32_t *b, int64_t *c) {
    scalar_generated_min_int64_int32_int64_logic(a, b, c);
}

void runtimec_scalar_probe_max_int32_int32_int32(const int32_t *a, const int32_t *b, int32_t *c) {
    scalar_generated_max_int32_int32_int32_logic(a, b, c);
}

void runtimec_scalar_probe_less_int64_uint64_bool(const int64_t *a, const uint64_t *b, bool *c) {
    scalar_generated_less_int64_uint64_bool_logic(a, b, c);
}

void runtimec_scalar_probe_equal_float64_int8_bool(const double *a, const int8_t *b, bool *c) {
    scalar_generated_equal_float64_int8_bool_logic(a, b, c);
}

void runtimec_scalar_probe_logical_and_uint8_float64_bool(const uint8_t *a, const double *b, bool *c) {
    scalar_generated_logical_and_uint8_float64_bool_logic(a, b, c);
}

void runtimec_scalar_probe_bitwise_or_int8_int64_int64(const int8_t *a, const int64_t *b, int64_t *c) {
    scalar_generated_bitwise_or_int8_int64_int64_logic(a, b, c);
}

void runtimec_scalar_probe_square_root_uint64_float64(const uint64_t *a, double *out) {
    scalar_generated_square_root_uint64_float64_logic(a, out);
}

void runtimec_scalar_probe_negation_int64_int64(const int64_t *a, int64_t *out) {
    scalar_generated_negation_int64_int64_logic(a, out);
}

void runtimec_scalar_probe_absolute_int8_int8(const int8_t *a, int8_t *out) {
    scalar_generated_absolute_int8_int8_logic(a, out);
}

void runtimec_scalar_probe_sign_float64_float64(const double *a, double *out) {
    scalar_generated_sign_float64_float64_logic(a, out);
}

void runtimec_scalar_probe_shift_left_int8_int8_int8(const int8_t *a, const int8_t *b, int8_t *c) {
    scalar_generated_shift_left_int8_int8_int8_logic(a, b, c);
}

void runtimec_scalar_probe_shift_right_int64_int8_int64(const int64_t *a, const int8_t *b, int64_t *c) {
    scalar_generated_shift_right_int64_int8_int64_logic(a, b, c);
}

void runtimec_scalar_probe_cast_int64_int8_int8(const int64_t *a, int8_t *out) {
    scalar_generated_cast_int64_int8_int8_logic(a, out);
}

void runtimec_scalar_probe_cast_uint8_int8_int8(const uint8_t *a, int8_t *out) {
    scalar_generated_cast_uint8_int8_int8_logic(a, out);
}

void runtimec_scalar_probe_cast_int8_uint8_uint8(const int8_t *a, uint8_t *out) {
    scalar_generated_cast_int8_uint8_uint8_logic(a, out);
}

void runtimec_scalar_probe_div_int16_int16_int16(const int16_t *a, const int16_t *b, int16_t *c, binrwir_execution_exit_t *output_exit) {
    scalar_generated_div_int16_int16_int16_logic(a, b, c, output_exit);
}

void runtimec_scalar_probe_div_int64_int64_int64(const int64_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    scalar_generated_div_int64_int64_int64_logic(a, b, c, output_exit);
}

void runtimec_scalar_probe_div_float64_float64_float64(const double *a, const double *b, double *c) {
    scalar_generated_div_float64_float64_float64_logic(a, b, c);
}

void runtimec_scalar_probe_mod_int64_int64_int64(const int64_t *a, const int64_t *b, int64_t *c, binrwir_execution_exit_t *output_exit) {
    scalar_generated_mod_int64_int64_int64_logic(a, b, c, output_exit);
}

void runtimec_scalar_probe_mod_uint8_uint8_uint8(const uint8_t *a, const uint8_t *b, uint8_t *c, binrwir_execution_exit_t *output_exit) {
    scalar_generated_mod_uint8_uint8_uint8_logic(a, b, c, output_exit);
}

void runtimec_scalar_probe_add_float32_int64_float32(const float *a, const int64_t *b, float *c) {
    scalar_generated_add_float32_int64_float32_logic(a, b, c);
}

void runtimec_scalar_probe_min_int32_float32_float32(const int32_t *a, const float *b, float *c) {
    scalar_generated_min_int32_float32_float32_logic(a, b, c);
}

void runtimec_scalar_probe_mul_float32_uint64_float32(const float *a, const uint64_t *b, float *c) {
    scalar_generated_mul_float32_uint64_float32_logic(a, b, c);
}

void runtimec_scalar_probe_sub_int8_int8_int8(const int8_t *a, const int8_t *b, int8_t *c) {
    scalar_generated_sub_int8_int8_int8_logic(a, b, c);
}

void runtimec_scalar_probe_add_int8_int8_int8(const int8_t *a, const int8_t *b, int8_t *c) {
    scalar_generated_add_int8_int8_int8_logic(a, b, c);
}

void runtimec_scalar_probe_sub_uint8_uint8_uint8(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    scalar_generated_sub_uint8_uint8_uint8_logic(a, b, c);
}

void runtimec_scalar_probe_add_float32_float32_float32(const float *a, const float *b, float *c) {
    scalar_generated_add_float32_float32_float32_logic(a, b, c);
}

void runtimec_scalar_probe_less_int32_int32_bool(const int32_t *a, const int32_t *b, bool *c) {
    scalar_generated_less_int32_int32_bool_logic(a, b, c);
}

void runtimec_scalar_probe_equal_int64_int64_bool(const int64_t *a, const int64_t *b, bool *c) {
    scalar_generated_equal_int64_int64_bool_logic(a, b, c);
}

void runtimec_scalar_probe_bitwise_and_uint8_uint8_uint8(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    scalar_generated_bitwise_and_uint8_uint8_uint8_logic(a, b, c);
}

void runtimec_scalar_probe_logical_or_float64_float64_bool(const double *a, const double *b, bool *c) {
    scalar_generated_logical_or_float64_float64_bool_logic(a, b, c);
}

void runtimec_scalar_probe_logical_not_int64_bool(const int64_t *a, bool *out) {
    scalar_generated_logical_not_int64_bool_logic(a, out);
}

void runtimec_scalar_probe_shift_left_uint8_uint8_uint8(const uint8_t *a, const uint8_t *b, uint8_t *c) {
    scalar_generated_shift_left_uint8_uint8_uint8_logic(a, b, c);
}

void runtimec_scalar_probe_shift_right_int8_int8_int8(const int8_t *a, const int8_t *b, int8_t *c) {
    scalar_generated_shift_right_int8_int8_int8_logic(a, b, c);
}

void runtimec_scalar_probe_cast_float64_int8_int8(const double *a, int8_t *out, binrwir_execution_exit_t *output_exit) {
    scalar_generated_cast_float64_int8_int8_logic(a, out, output_exit);
}

void runtimec_scalar_probe_cast_float32_uint8_uint8(const float *a, uint8_t *out, binrwir_execution_exit_t *output_exit) {
    scalar_generated_cast_float32_uint8_uint8_logic(a, out, output_exit);
}

