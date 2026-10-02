#ifndef AURORA_IDENTITY_ROTATION_H
#define AURORA_IDENTITY_ROTATION_H

#include "aurora/identity/core.h"

enum aurora_identity_rotation_store_result {
    AURORA_IDENTITY_ROTATION_STORE_OK = 0,
    AURORA_IDENTITY_ROTATION_STORE_CURRENT_NOT_FOUND,
    AURORA_IDENTITY_ROTATION_STORE_CONFLICT,
    AURORA_IDENTITY_ROTATION_STORE_ERROR
};

/*
 * Rotation authorization is deliberately outside this low-level core.
 * The future Aurora Identity Service must require an authenticated context
 * and policy-required re-authentication before invoking this operation.
 */
struct aurora_identity_rotation_store_ops {
    void *context;

    /*
     * Atomically replace one existing Aurora Key credential.
     *
     * The backend must verify that current_credential_id belongs to user_id,
     * enforce replacement lookup-tag/credential-id uniqueness, and publish
     * the replacement only if the old credential can be retired in the same
     * transaction. On any non-OK result, the old credential remains valid.
     */
    enum aurora_identity_rotation_store_result (*replace_key_credential)(
        void *context,
        const struct aurora_identity_user_id *user_id,
        const struct aurora_identity_credential_id *current_credential_id,
        const struct aurora_identity_key_record *replacement);
};

struct aurora_identity_rotation_context {
    const struct aurora_identity_core *core;
    struct aurora_identity_rotation_store_ops store;
};

struct aurora_identity_rotation_result {
    enum aurora_identity_result result;
    struct aurora_identity_credential_id new_credential_id;
};

struct aurora_identity_rotation_result aurora_identity_rotate_key(
    const struct aurora_identity_rotation_context *context,
    const struct aurora_identity_user_id *user_id,
    const struct aurora_identity_credential_id *current_credential_id,
    const char *new_key,
    size_t new_key_length);

#endif
