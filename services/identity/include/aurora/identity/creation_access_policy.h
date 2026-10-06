#ifndef AURORA_IDENTITY_CREATION_ACCESS_POLICY_H
#define AURORA_IDENTITY_CREATION_ACCESS_POLICY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The unauthenticated login/setup path may create exactly the bootstrap
 * identity. Once any persistent identity exists, later creation must travel
 * through an authenticated machine-policy/Administrator authorization path.
 */
bool aurora_identity_pre_auth_create_allowed(
    size_t persistent_identity_count
);

#endif
