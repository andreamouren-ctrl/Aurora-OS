#ifndef AURORA_IDENTITY_PERSISTENT_STORE_PROTECTED_STATE_H
#define AURORA_IDENTITY_PERSISTENT_STORE_PROTECTED_STATE_H

#include "aurora/identity/machine_secret_protected_state.h"
#include "aurora/identity/persistent_store.h"

#include <stdbool.h>

struct aurora_identity_persistent_store_protected_state {
    struct aurora_identity_protected_state_transport_ops transport;
    bool initialized;
};

bool aurora_identity_persistent_store_protected_state_init(
    struct aurora_identity_persistent_store_protected_state *store,
    const struct aurora_identity_protected_state_transport_ops *transport);

struct aurora_identity_persistent_io_ops
    aurora_identity_persistent_store_protected_state_io_ops(
        struct aurora_identity_persistent_store_protected_state *store);

#endif
