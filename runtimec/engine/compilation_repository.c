
#include "engine/compilation_repository.h"

#include <string.h>

#include "binrwir_compiler/compiler.h"

typedef struct runtimec_compilation_repository_entry {
    runtimec_compilation_repository_key_t key;
    char *function_key_text;
    char *source_position_text;
    binrwir_compilation_t *compilation;
    size_t lease_count;
    bool invalidated;
    bool occupied;
    uint64_t insertion_order;
} runtimec_compilation_repository_entry_t;

struct runtimec_compilation_repository_t {
    runtimec_allocator_t allocator;
    runtimec_compilation_repository_entry_t *entries;
    size_t capacity;
    size_t occupied_count;
    uint64_t next_insertion_order;
};

typedef struct runtimec_compilation_repository_lease_token {
    runtimec_compilation_repository_t *repository;
    runtimec_compilation_repository_entry_t *entry;
} runtimec_compilation_repository_lease_token_t;

static runtimec_status_t runtimec_compilation_repository_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static char *runtimec_compilation_repository_copy_text(
    const runtimec_allocator_t *allocator, const char *data, size_t size) {
    if (data == NULL || size == 0U) {
        return NULL;
    }
    char *copy = runtimec_allocator_allocate(allocator, size + 1U, 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, data, size);
    copy[size] = '\0';
    return copy;
}

static bool runtimec_compilation_repository_key_matches(
    const runtimec_compilation_repository_key_t *left,
    const runtimec_compilation_repository_key_t *right) {
    if (left->rwir_manifest_version != right->rwir_manifest_version ||
        left->canonical_encoding_revision !=
            right->canonical_encoding_revision ||
        left->binrwir_binary_interface_generation !=
            right->binrwir_binary_interface_generation ||
        left->compiler_revision != right->compiler_revision) {
        return false;
    }
    const runtimec_snapshot_t *left_snapshot = &left->snapshot;
    const runtimec_snapshot_t *right_snapshot = &right->snapshot;
    if (left_snapshot->contract_version.major !=
            right_snapshot->contract_version.major ||
        left_snapshot->contract_version.minor !=
            right_snapshot->contract_version.minor ||
        left_snapshot->contract_version.patch !=
            right_snapshot->contract_version.patch ||
        left_snapshot->contract_version.binary_interface !=
            right_snapshot->contract_version.binary_interface) {
        return false;
    }
    const runtimec_source_revision_t *left_revision =
        &left_snapshot->source_revision;
    const runtimec_source_revision_t *right_revision =
        &right_snapshot->source_revision;
    if (left_revision->instance_identifier !=
            right_revision->instance_identifier ||
        left_revision->generation != right_revision->generation ||
        left_revision->schema_version != right_revision->schema_version ||
        left_revision->instruction_set_version !=
            right_revision->instruction_set_version) {
        return false;
    }
    if (left_snapshot->function_key.size != right_snapshot->function_key.size ||
        (left_snapshot->function_key.size != 0U &&
         memcmp(left_snapshot->function_key.data,
                right_snapshot->function_key.data,
                left_snapshot->function_key.size) != 0)) {
        return false;
    }
    return left_snapshot->source_position_text.size ==
               right_snapshot->source_position_text.size &&
           (left_snapshot->source_position_text.size == 0U ||
            memcmp(left_snapshot->source_position_text.data,
                   right_snapshot->source_position_text.data,
                   left_snapshot->source_position_text.size) == 0);
}

static bool runtimec_compilation_repository_key_is_valid(
    const runtimec_compilation_repository_key_t *key) {
    return key != NULL && runtimec_snapshot_is_valid(&key->snapshot) &&
           key->snapshot.source_revision.instance_identifier != 0U;
}

static void runtimec_compilation_repository_entry_reset_views(
    runtimec_compilation_repository_entry_t *entry) {
    entry->key.snapshot.function_key = runtimec_key_view_from_c_string(
        entry->function_key_text);
    entry->key.snapshot.source_position_text =
        runtimec_string_view_from_c_string(entry->source_position_text);
}

static void runtimec_compilation_repository_release_entry(
    runtimec_compilation_repository_t *repository,
    runtimec_compilation_repository_entry_t *entry) {
    if (entry->compilation != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compilation_destroy(entry->compilation, &ignored);
        entry->compilation = NULL;
    }
    if (entry->function_key_text != NULL) {
        (void)runtimec_allocator_deallocate(
            &repository->allocator, entry->function_key_text,
            strlen(entry->function_key_text) + 1U, 1U);
        entry->function_key_text = NULL;
    }
    if (entry->source_position_text != NULL) {
        (void)runtimec_allocator_deallocate(
            &repository->allocator, entry->source_position_text,
            strlen(entry->source_position_text) + 1U, 1U);
        entry->source_position_text = NULL;
    }
    memset(entry, 0, sizeof(*entry));
}

static void runtimec_compilation_repository_reclaim(
    runtimec_compilation_repository_t *repository) {
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *entry =
            &repository->entries[index];
        if (entry->occupied && entry->invalidated && entry->lease_count == 0U) {
            runtimec_compilation_repository_release_entry(repository, entry);
            repository->occupied_count -= 1U;
        }
    }
}

runtimec_status_t runtimec_compilation_repository_create(
    runtimec_allocator_t allocator, size_t capacity,
    runtimec_compilation_repository_t **output_repository,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_repository != NULL) {
        *output_repository = NULL;
    }
    if (!runtimec_allocator_is_valid(&allocator) || capacity == 0U ||
        output_repository == NULL || error == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_INVALID, 1,
            "invalid compilation repository request");
    }
    runtimec_compilation_repository_t *repository =
        runtimec_allocator_allocate(&allocator, sizeof(*repository),
                                    _Alignof(runtimec_compilation_repository_t));
    if (repository == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
            "compilation repository allocation failed");
    }
    memset(repository, 0, sizeof(*repository));
    repository->allocator = allocator;
    repository->capacity = capacity;
    repository->entries = runtimec_allocator_allocate(
        &allocator, capacity * sizeof(*repository->entries),
        _Alignof(runtimec_compilation_repository_entry_t));
    if (repository->entries == NULL) {
        (void)runtimec_allocator_deallocate(&allocator, repository,
                                            sizeof(*repository),
                                            _Alignof(runtimec_compilation_repository_t));
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 3,
            "compilation repository entry table allocation failed");
    }
    memset(repository->entries, 0,
           capacity * sizeof(*repository->entries));
    *output_repository = repository;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_compilation_repository_destroy(
    runtimec_compilation_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (size_t index = 0U; index < repository->capacity; index++) {
        if (repository->entries[index].occupied) {
            runtimec_compilation_repository_release_entry(
                repository, &repository->entries[index]);
        }
    }
    const runtimec_allocator_t allocator = repository->allocator;
    (void)runtimec_allocator_deallocate(
        &allocator, repository->entries,
        repository->capacity * sizeof(*repository->entries),
        _Alignof(runtimec_compilation_repository_entry_t));
    memset(repository, 0, sizeof(*repository));
    return runtimec_allocator_deallocate(
        &allocator, repository, sizeof(*repository),
        _Alignof(runtimec_compilation_repository_t));
}

static runtimec_compilation_repository_entry_t *
runtimec_compilation_repository_acquire_slot(
    runtimec_compilation_repository_t *repository) {
    runtimec_compilation_repository_reclaim(repository);
    for (size_t index = 0U; index < repository->capacity; index++) {
        if (!repository->entries[index].occupied) {
            return &repository->entries[index];
        }
    }
    runtimec_compilation_repository_entry_t *oldest = NULL;
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *entry =
            &repository->entries[index];
        if (entry->lease_count != 0U) {
            continue;
        }
        if (oldest == NULL ||
            entry->insertion_order < oldest->insertion_order) {
            oldest = entry;
        }
    }
    if (oldest != NULL) {
        runtimec_compilation_repository_release_entry(repository, oldest);
        repository->occupied_count -= 1U;
    }
    return oldest;
}

runtimec_status_t runtimec_compilation_repository_find(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lookup_result_t *output_lookup_result,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_lookup_result != NULL) {
        *output_lookup_result =
            RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
    }
    if (output_lease != NULL) {
        *output_lease = (runtimec_compilation_repository_lease_t){0};
    }
    if (repository == NULL || !runtimec_compilation_repository_key_is_valid(key) ||
        output_lookup_result == NULL || output_lease == NULL || error == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_INVALID, 4,
            "invalid compilation repository lookup request");
    }
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *entry =
            &repository->entries[index];
        if (!entry->occupied || entry->compilation == NULL) {
            continue;
        }
        if (!runtimec_compilation_repository_key_matches(&entry->key, key)) {
            continue;
        }
        if (entry->invalidated) {
            
            *output_lookup_result =
                RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        runtimec_compilation_repository_lease_token_t *token =
            runtimec_allocator_allocate(
                &repository->allocator, sizeof(*token),
                _Alignof(runtimec_compilation_repository_lease_token_t));
        if (token == NULL) {
            return runtimec_compilation_repository_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 5,
                "compilation lease allocation failed");
        }
        token->repository = repository;
        token->entry = entry;
        entry->lease_count += 1U;
        output_lease->compilation = entry->compilation;
        output_lease->snapshot = entry->key.snapshot;
        output_lease->token = token;
        *output_lookup_result = RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    *output_lookup_result = RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_compilation_repository_insert(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    binrwir_compilation_t *compilation,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_lease != NULL) {
        *output_lease = (runtimec_compilation_repository_lease_t){0};
    }
    if (repository == NULL || !runtimec_compilation_repository_key_is_valid(key) ||
        compilation == NULL || output_lease == NULL || error == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_INVALID, 6,
            "invalid compilation repository insert request");
    }
    
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *existing =
            &repository->entries[index];
        if (existing->occupied && !existing->invalidated &&
            runtimec_compilation_repository_key_matches(&existing->key,
                                                        key)) {
            return runtimec_compilation_repository_fail(
                error, RUNTIMEC_STATUS_INVALID, 7,
                "compilation for this key is already cached");
        }
    }
    runtimec_compilation_repository_entry_t *entry =
        runtimec_compilation_repository_acquire_slot(repository);
    if (entry == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 8,
            "compilation repository has no free slot");
    }
    char *function_key_text = runtimec_compilation_repository_copy_text(
        &repository->allocator, key->snapshot.function_key.data,
        key->snapshot.function_key.size);
    if (key->snapshot.function_key.size != 0U && function_key_text == NULL) {
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 9,
            "function key copy failed");
    }
    char *source_position_text =
        runtimec_compilation_repository_copy_text(
            &repository->allocator,
            key->snapshot.source_position_text.data,
            key->snapshot.source_position_text.size);
    if (key->snapshot.source_position_text.size != 0U &&
        source_position_text == NULL) {
        if (function_key_text != NULL) {
            (void)runtimec_allocator_deallocate(
                &repository->allocator, function_key_text,
                strlen(function_key_text) + 1U, 1U);
        }
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 10,
            "logical program counter copy failed");
    }
    memset(entry, 0, sizeof(*entry));
    entry->key = *key;
    entry->function_key_text = function_key_text;
    entry->source_position_text = source_position_text;
    runtimec_compilation_repository_entry_reset_views(entry);
    entry->compilation = compilation;
    entry->lease_count = 1U;
    entry->invalidated = false;
    entry->occupied = true;
    entry->insertion_order = repository->next_insertion_order++;
    repository->occupied_count += 1U;

    runtimec_compilation_repository_lease_token_t *token =
        runtimec_allocator_allocate(&repository->allocator, sizeof(*token),
                                    _Alignof(runtimec_compilation_repository_lease_token_t));
    if (token == NULL) {
        runtimec_compilation_repository_release_entry(repository, entry);
        repository->occupied_count -= 1U;
        return runtimec_compilation_repository_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 11,
            "compilation lease allocation failed");
    }
    token->repository = repository;
    token->entry = entry;
    output_lease->compilation = compilation;
    output_lease->snapshot = entry->key.snapshot;
    output_lease->token = token;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_compilation_repository_release(
    runtimec_compilation_repository_t *repository,
    runtimec_compilation_repository_lease_t *lease) {
    if (repository == NULL || lease == NULL || lease->token == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_compilation_repository_lease_token_t *token = lease->token;
    if (token->repository != repository || token->entry == NULL ||
        !token->entry->occupied) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_compilation_repository_entry_t *entry = token->entry;
    if (entry->lease_count != 0U) {
        entry->lease_count -= 1U;
    }
    (void)runtimec_allocator_deallocate(
        &repository->allocator, token, sizeof(*token),
        _Alignof(runtimec_compilation_repository_lease_token_t));
    
    if (entry->invalidated && entry->lease_count == 0U) {
        runtimec_compilation_repository_release_entry(repository, entry);
        repository->occupied_count -= 1U;
    }
    *lease = (runtimec_compilation_repository_lease_t){0};
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_compilation_repository_invalidate(
    runtimec_compilation_repository_t *repository,
    const runtimec_snapshot_t *snapshot) {
    if (repository == NULL || snapshot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *entry =
            &repository->entries[index];
        if (!entry->occupied) {
            continue;
        }
        if (entry->key.snapshot.function_key.size !=
                snapshot->function_key.size ||
            (snapshot->function_key.size != 0U &&
             memcmp(entry->key.snapshot.function_key.data,
                    snapshot->function_key.data,
                    snapshot->function_key.size) != 0)) {
            continue;
        }
        entry->invalidated = true;
    }
    runtimec_compilation_repository_reclaim(repository);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_compilation_repository_clear(
    runtimec_compilation_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (size_t index = 0U; index < repository->capacity; index++) {
        runtimec_compilation_repository_entry_t *entry =
            &repository->entries[index];
        if (!entry->occupied) {
            continue;
        }
        entry->invalidated = true;
    }
    runtimec_compilation_repository_reclaim(repository);
    return RUNTIMEC_STATUS_SUCCESS;
}
