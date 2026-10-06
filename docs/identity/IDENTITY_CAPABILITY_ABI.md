# Aurora Identity Capability ABI Foundation

Status: implementation foundation for the canonical Identity IPC contract.

Aurora Identity privileged IPC must not rely on possession of the service endpoint alone. Callers present narrow capability objects whose type expresses the permitted operation class.

## Capability classes

- `AURORA_CAP_IDENTITY_AUTH` — authorize authentication submission/query/cancellation flows.
- `AURORA_CAP_IDENTITY_CREATE` — authorize identity-creation flows when machine policy also permits creation.
- `AURORA_CAP_IDENTITY_SESSION` — authorize consumption or handling of opaque Identity session grants.

These capability types do not replace policy checks performed by the Identity Service. They form the kernel-enforced authority boundary that prevents a generic IPC endpoint capability from implicitly authorizing privileged Identity operations.

## Delegation and lifetime

Identity authorities are intended to be delegated with the minimum rights required by one operation or bounded workflow. The receiver must not receive `AURORA_RIGHT_TRANSFER` unless an explicit delegation contract requires it.

Ring 3 can release a received or no-longer-required capability using `AURORA_SYS_CAP_REVOKE`. Revocation invalidates that handle in the calling process capability table and advances the slot generation, preventing stale-handle reuse.

## Security invariants

1. Endpoint possession is transport access, not Identity authorization.
2. Authentication, creation, and session consumption remain separate authority classes.
3. Delegated authority is reduced to the minimum rights needed by the receiver.
4. Per-request or short-lived authorities are revoked when their operation ends.
5. Unknown, stale, already-revoked, or zero handles fail closed.
6. The Identity Service still applies machine policy, account state, throttling, and protocol validation after capability validation.

This foundation intentionally does not expose CREATE/AUTH IPC messages yet. The next integration step will define bounded messages, authority transfer, coarse public errors, asynchronous authentication state, cancellation, and opaque session-grant handling.