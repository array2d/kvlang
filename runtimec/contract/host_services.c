/* host 能力表的纯生命周期、形状校验与 V.body 地址推导。 */

#include "contract/host_services.h"

/*
 * 唯一的「基址 + 8」实现。零基址不是有效地址；加法会越过 UINT64_MAX 时
 * 直接失败，不回绕。调用方拿到 NONE 必须立即失败，不得把 0 当作合法 body。
 */
runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address) {
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        xvalue_address >
            UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    }
    return xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

/*
 * `+8` 的逆。零地址与下溢（值体地址小于固定头长度）都不是有效基址，一律
 * 返回 RUNTIMEC_XVALUE_ADDRESS_NONE；不做回绕，也不查存储。
 */
runtimec_xvalue_address_t
runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    return body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

/* 值头 storetype 只能是这四个档位；其它值说明地址根本不是值头。 */
static int runtimec_value_header_store_type_is_known(uint8_t store_type) {
    return store_type == KVSPACE_XVALUE_ST_SHORT ||
           store_type == KVSPACE_XVALUE_ST_SLACK ||
           store_type == KVSPACE_XVALUE_ST_LARGE ||
           store_type == KVSPACE_XVALUE_ST_EXT;
}

/*
 * `+8` 的逆运算：值体地址往前挪固定头长度就是值头，长度从值头读。
 * 零地址、下溢（body < 头长度）、storetype 不可信、长度超 uint32 都返回 0。
 */
uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return 0U;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(uintptr_t)(
        body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    if (!runtimec_value_header_store_type_is_known(head->storetype)) {
        return 0U;
    }
    const uint64_t body_length = kvspaceXvalueHeadLen(head);
    return body_length > UINT32_MAX ? 0U : (uint32_t)body_length;
}

void runtimec_host_value_header_reset(runtimec_host_value_header_t *header) {
    if (header != NULL) {
        header->value_size = 0U;
        header->value_revision = 0U;
        header->flags = 0U;
        header->reserved = 0U;
    }
}

void runtimec_handoff_request_reset(runtimec_handoff_request_t *request) {
    if (request != NULL) {
        request->operation = runtimec_string_view_make(NULL, 0U);
        runtimec_checkpoint_reset(&request->checkpoint);
    }
}

bool runtimec_handoff_request_is_valid(
    const runtimec_handoff_request_t *request) {
    return request != NULL && request->operation.size != 0U &&
           runtimec_string_view_is_valid(request->operation) &&
           runtimec_checkpoint_is_valid(&request->checkpoint);
}

void runtimec_host_services_initialize(runtimec_host_services_t *services) {
    if (services != NULL) {
        services->binary_interface_version =
            RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION;
        services->context = NULL;
        runtimec_allocator_reset(&services->allocator);
        services->read = NULL;
        services->read_header = NULL;
        services->write = NULL;
        services->write_partial = NULL;
        services->remove = NULL;
        services->list = NULL;
        services->watch = NULL;
        services->signal = NULL;
        services->source_revision = NULL;
        services->resolve_static_xvalue_address = NULL;
        services->resolve_frame_xvalue_address = NULL;
        services->ensure_frame = NULL;
        services->checkpoint = NULL;
        services->handoff = NULL;
    }
}

bool runtimec_host_services_is_valid(
    const runtimec_host_services_t *services) {
    return services != NULL &&
           services->binary_interface_version ==
               RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION &&
           runtimec_allocator_is_valid(&services->allocator);
}
