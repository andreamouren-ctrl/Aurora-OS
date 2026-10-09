#ifndef AURORA_IDENTITY_AUDIT_STORE_PROTECTED_STATE_H
#define AURORA_IDENTITY_AUDIT_STORE_PROTECTED_STATE_H

#include "aurora/identity/audit_store.h"
#include "aurora/identity/protected_state_transport.h"

#include <stdbool.h>

struct aurora_identity_audit_protected_state_store {
    struct aurora_identity_protected_state_transport_ops transport;
    bool initialized;
};

bool aurora_identity_audit_protected_state_store_init(
    struct aurora_identity_audit_protected_state_store *store,
    const struct aurora_identity_protected_state_transport_ops *transport);

struct aurora_identity_audit_io_ops
    aurora_identity_audit_protected_state_io_ops(
        struct aurora_identity_audit_protected_state_store *store);

#endif
