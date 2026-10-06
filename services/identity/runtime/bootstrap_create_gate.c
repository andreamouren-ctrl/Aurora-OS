#include <stddef.h>

#include <aurora/identity/core.h>
#include <aurora/identity/creation_access_policy.h>
#include <aurora/identity/persistent_store.h>

/*
 * LLD --wrap keeps this bootstrap-only policy at the production runtime
 * boundary without changing the generic Identity core. A future authenticated
 * Administrator creation path can call the real core through a different,
 * purpose-bound authority path.
 */
struct aurora_identity_create_result __real_aurora_identity_create_with_key(
    const struct aurora_identity_core *core,
    const char *candidate_key,
    size_t candidate_key_length
);

struct aurora_identity_create_result __wrap_aurora_identity_create_with_key(
    const struct aurora_identity_core *core,
    const char *candidate_key,
    size_t candidate_key_length
) {
    if (core != NULL && core->store.context != NULL) {
        const struct aurora_identity_persistent_store *store =
            (const struct aurora_identity_persistent_store *)core->store.context;
        size_t identity_count =
            aurora_identity_persistent_store_identity_count(store);

        if (!aurora_identity_pre_auth_create_allowed(identity_count)) {
            struct aurora_identity_create_result denied = {0};
            denied.result = AURORA_IDENTITY_POLICY_ERROR;
            return denied;
        }
    }

    return __real_aurora_identity_create_with_key(
        core,
        candidate_key,
        candidate_key_length);
}
