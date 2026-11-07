/* The C interface must be directly consumable from C++17. */

#include <type_traits>

#include <kvlang/runtimec.h>

static_assert(std::is_same_v<
              decltype(&kvlang_runtime_create),
              runtimec_status_t (*)(const kvlang_runtime_create_request_t *,
                                    kvlang_runtime_t **,
                                    kvlang_runtime_error_t *)>);
static_assert(std::is_same_v<
              decltype(&kvlang_runtime_destroy),
              runtimec_status_t (*)(kvlang_runtime_t *,
                                    kvlang_runtime_error_t *)>);
static_assert(std::is_same_v<
              decltype(&kvlang_runtime_execute),
              runtimec_status_t (*)(kvlang_runtime_t *,
                                    const kvlang_runtime_execute_request_t *,
                                    kvlang_runtime_execute_result_t *,
                                    kvlang_runtime_error_t *)>);

int main() {
    return 0;
}
