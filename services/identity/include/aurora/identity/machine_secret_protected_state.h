#ifndef AURORA_IDENTITY_MACHINE_SECRET_PROTECTED_STATE_H
#define AURORA_IDENTITY_MACHINE_SECRET_PROTECTED_STATE_H

#include "aurora/identity/machine_secret.h"
#include "aurora/identity/protected_state_transport.h"

#include <stdbool.h>

struct aurora_identity_machine_secret_protected_state_store {
    struct aurora_identity_protected_state_transport_ops transport;
    bool initialized;
};

bool aurora_identity_machine_secret_protected_state_store_init(
    struct aurora_identity_machine_secret_protected_state_store *store,
    const struct aurora_identity_protected_state_transport_ops *transport);

struct aurora_identity_machine_secret_store_ops
    aurora_identity_machine_secret_protected_state_store_ops(
        struct aurora_identity_machine_secret_protected_state_store *store);

#endif
