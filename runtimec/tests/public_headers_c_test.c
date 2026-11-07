/* The only public C header must expose the structured runtimec interface. */

#include <kvlang/runtimec.h>

#define RUNTIMEC_ASSERT_FUNCTION_TYPE(function_name, function_pointer_type)       \
    _Static_assert(_Generic(&(function_name), function_pointer_type: 1,           \
                            default: 0),                                          \
                   "unexpected declaration for " #function_name)

typedef runtimec_status_t (*runtimec_create_function)(
    const kvlang_runtime_create_request_t *, kvlang_runtime_t **,
    kvlang_runtime_error_t *);
typedef runtimec_status_t (*runtimec_destroy_function)(
    kvlang_runtime_t *, kvlang_runtime_error_t *);
typedef runtimec_status_t (*runtimec_execute_function)(
    kvlang_runtime_t *, const kvlang_runtime_execute_request_t *,
    kvlang_runtime_execute_result_t *, kvlang_runtime_error_t *);

RUNTIMEC_ASSERT_FUNCTION_TYPE(kvlang_runtime_create, runtimec_create_function);
RUNTIMEC_ASSERT_FUNCTION_TYPE(kvlang_runtime_destroy, runtimec_destroy_function);
RUNTIMEC_ASSERT_FUNCTION_TYPE(kvlang_runtime_execute, runtimec_execute_function);

_Static_assert(RUNTIMEC_STATUS_SUCCESS == 0,
               "the only successful status must remain zero");
_Static_assert(sizeof(kvlang_runtime_t *) == sizeof(void *),
               "the runtime handle must remain opaque and pointer-sized");

int main(void) {
    return 0;
}
