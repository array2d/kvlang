/*
 * 组件：host/kvspace_binary_interface 的实现
 * 作用：符号名清单与整表绑定。绑定全有或全无：任何一个符号缺失都把输出清零
 *       并失败，绝不发布部分可用的表。
 * 联系：host/kvspace_adapter 只消费绑定成功的表；测试用自定义 resolver 提供
 *       替身，真实运行时由调用方（public_interface 或宿主）绑定真实库。
 * 约束：不创建、不关闭 kvspace 句柄，不理解任何语言语义。
 */

#include "host/kvspace_binary_interface.h"

#include <string.h>

/*
 * 符号名清单。顺序必须与 runtimec_kvspace_binary_interface_t 的字段顺序完全
 * 一致；两份清单的一致性由 _Static_assert 与符号数量断言共同保证。
 */
static const char *const runtimec_kvspace_symbol_names[] = {
    "kvspaceConnect",     "kvspaceClose",   "kvspaceGet",
    "kvspaceSetValue",    "kvspaceSetPart", "kvspaceDel",
    "kvspaceListLen",     "kvspaceListAt",  "kvspaceWatch",
    "kvspaceViewQuiesce", "kvspaceViewAck", "kvspaceMkindex",
    "kvspaceXvalueHeadNewSlack",
};

enum { RUNTIMEC_KVSPACE_SYMBOL_COUNT =
           sizeof(runtimec_kvspace_symbol_names) /
           sizeof(runtimec_kvspace_symbol_names[0]) };

/* 表字段数量必须等于符号数量：任一侧增减都会在这里失败。 */
_Static_assert(
    RUNTIMEC_KVSPACE_SYMBOL_COUNT == 13U,
    "kvspace binary interface symbol count must match its table fields");

/* 缺失符号的错误文本：借用，指向本文件的静态缓冲，随进程存活。 */
static char runtimec_kvspace_missing_symbol_text[128];

size_t runtimec_kvspace_binary_interface_symbol_count(void) {
    return (size_t)RUNTIMEC_KVSPACE_SYMBOL_COUNT;
}

const char *runtimec_kvspace_binary_interface_symbol_name(size_t index) {
    if (index >= (size_t)RUNTIMEC_KVSPACE_SYMBOL_COUNT) {
        return NULL;
    }
    return runtimec_kvspace_symbol_names[index];
}

void runtimec_kvspace_binary_interface_reset(
    runtimec_kvspace_binary_interface_t *binary_interface) {
    if (binary_interface != NULL) {
        memset(binary_interface, 0, sizeof(*binary_interface));
    }
}

bool runtimec_kvspace_binary_interface_is_complete(
    const runtimec_kvspace_binary_interface_t *binary_interface) {
    return binary_interface != NULL && binary_interface->connect != NULL &&
           binary_interface->close != NULL && binary_interface->get != NULL &&
           binary_interface->set_value != NULL &&
           binary_interface->set_part != NULL &&
           binary_interface->del != NULL &&
           binary_interface->list_size != NULL &&
           binary_interface->list_at != NULL &&
           binary_interface->watch != NULL &&
           binary_interface->view_quiesce != NULL &&
           binary_interface->view_ack != NULL &&
           binary_interface->mkindex != NULL &&
           binary_interface->new_slack != NULL;
}

runtimec_status_t runtimec_kvspace_binary_interface_bind(
    const runtimec_kvspace_symbol_resolver_t *resolver,
    runtimec_kvspace_binary_interface_t *output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    runtimec_kvspace_binary_interface_reset(output);
    if (resolver == NULL || resolver->resolve == NULL || output == NULL ||
        error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_kvspace_symbol_address_t addresses[RUNTIMEC_KVSPACE_SYMBOL_COUNT];
    for (size_t index = 0U; index < (size_t)RUNTIMEC_KVSPACE_SYMBOL_COUNT;
         index++) {
        addresses[index] =
            resolver->resolve(resolver->context,
                              runtimec_kvspace_symbol_names[index]);
        if (addresses[index] == NULL) {
            runtimec_kvspace_binary_interface_reset(output);
            static const char prefix[] = "missing kvspace symbol: ";
            const char *name = runtimec_kvspace_symbol_names[index];
            const size_t prefix_size = sizeof(prefix) - 1U;
            size_t name_size = strlen(name);
            size_t capacity = sizeof(runtimec_kvspace_missing_symbol_text);
            if (prefix_size + name_size + 1U > capacity) {
                name_size = capacity - prefix_size - 1U;
            }
            memcpy(runtimec_kvspace_missing_symbol_text, prefix, prefix_size);
            memcpy(runtimec_kvspace_missing_symbol_text + prefix_size, name,
                   name_size);
            runtimec_kvspace_missing_symbol_text[prefix_size + name_size] =
                '\0';
            runtimec_error_set(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                RUNTIMEC_ERROR_DOMAIN_HOST, 1,
                runtimec_string_view_from_c_string(
                    runtimec_kvspace_missing_symbol_text),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
    }
    output->connect = (void *(*)(const char *))addresses[0];
    output->close = (void (*)(void *))addresses[1];
    output->get = (int (*)(void *, const char *, int, uint8_t **,
                           uint32_t *))addresses[2];
    output->set_value = (int (*)(void *, const char *, const uint8_t *,
                                 uint32_t, char *, uint32_t))addresses[3];
    output->set_part = (int (*)(void *, const char *, uint32_t,
                                const uint8_t *, uint32_t, char *,
                                uint32_t))addresses[4];
    output->del = (int (*)(void *, const char *const *, uint32_t, char *,
                           uint32_t))addresses[5];
    output->list_size = (int (*)(void *, const char *, int, int,
                                 int32_t *))addresses[6];
    output->list_at = (int (*)(void *, const char *, int, int, int32_t,
                               uint8_t *, uint32_t, uint32_t *))addresses[7];
    output->watch = (int (*)(void *, const char *, const uint8_t *, uint32_t,
                             uint64_t, uint8_t **, uint32_t *))addresses[8];
    output->view_quiesce = (int (*)(void *, int))addresses[9];
    output->view_ack = (int (*)(void *, uint32_t *))addresses[10];
    output->mkindex = (int (*)(void *, const char *, uint32_t, char *,
                               uint32_t))addresses[11];
    output->new_slack = (int (*)(const uint8_t *, uint64_t, uint64_t,
                                 const char *, uint8_t **,
                                 uint64_t *))addresses[12];
    return RUNTIMEC_STATUS_SUCCESS;
}
