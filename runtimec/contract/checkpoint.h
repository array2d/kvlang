#ifndef RUNTIMEC_CONTRACT_CHECKPOINT_H
#define RUNTIMEC_CONTRACT_CHECKPOINT_H

#include <stdbool.h>
#include <stdint.h>

#include "contract/error.h"
#include "contract/program_counter.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_checkpoint {
    runtimec_version_t contract_version;
    runtimec_source_revision_t source_revision; 
    uint64_t sequence;                          
    runtimec_string_view_t runtime_identifier;
    runtimec_string_view_t vthread_identifier;
    runtimec_string_view_t frame_identifier;
    runtimec_program_counter_t logical_program_counter;
    runtimec_string_view_t persistent_status;   
    runtimec_status_t outcome;                  
    runtimec_error_t error;                     
} runtimec_checkpoint_t;

void runtimec_checkpoint_reset(runtimec_checkpoint_t *checkpoint);

bool runtimec_checkpoint_is_valid(const runtimec_checkpoint_t *checkpoint);

#ifdef __cplusplus
}
#endif

#endif
