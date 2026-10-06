#include <aurora/identity/creation_access_policy.h>

bool aurora_identity_pre_auth_create_allowed(
    size_t persistent_identity_count
) {
    return persistent_identity_count == 0u;
}
