#ifndef RUNTIMEC_CONTRACT_SNAPSHOT_H
#define RUNTIMEC_CONTRACT_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_CONTRACT_VERSION_MAJOR 1U
#define RUNTIMEC_CONTRACT_VERSION_MINOR 0U
#define RUNTIMEC_CONTRACT_VERSION_PATCH 0U
#define RUNTIMEC_CONTRACT_BINARY_INTERFACE_VERSION 1U

typedef struct runtimec_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t binary_interface;
} runtimec_version_t;

typedef struct runtimec_source_revision {
    uint64_t instance_identifier;
    uint64_t generation;
    uint32_t schema_version;
    uint32_t instruction_set_version;
} runtimec_source_revision_t;

typedef struct runtimec_snapshot {
    runtimec_version_t contract_version;
    runtimec_source_revision_t source_revision;
    runtimec_key_view_t function_key;
    
    runtimec_string_view_t source_position_text;
} runtimec_snapshot_t;

runtimec_version_t runtimec_contract_version(void);

void runtimec_source_revision_reset(runtimec_source_revision_t *revision);

void runtimec_snapshot_reset(runtimec_snapshot_t *snapshot);

bool runtimec_snapshot_is_valid(const runtimec_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
