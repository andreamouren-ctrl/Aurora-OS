# Aurora Identity IPC Protocol

Status: **Canonical protocol draft**
Version: **0.1**

## 1. Purpose

This document defines the logical IPC contract between Aurora Identity components. It is intentionally transport-agnostic: the final binary ABI may evolve, but authorization, message semantics, bounds, versioning, cancellation, and secret-handling rules are canonical.

Primary participants:

- Aurora Identity System App;
- Bootstrap / Recovery Login Surface;
- Aurora Identity Service;
- Session Manager;
- removable-media / authenticator broker;
- explicitly authorized recovery or administrative components.

## 2. Protocol invariants

1. Every privileged operation requires an explicit capability appropriate to the operation.
2. Caller process identity alone is not authorization.
3. Requests and replies are strictly bounded in size.
4. Credential bytes are never echoed in replies or diagnostics.
5. Long operations are asynchronous and cancellable.
6. Every message carries a protocol version.
7. Unknown fields or message types fail safely.
8. Session grants and re-authentication proofs are opaque to ordinary callers.
9. Sensitive handles are non-transferable unless the capability contract explicitly allows delegation.
10. Malformed messages must not crash the service or kernel.

## 3. Logical message envelope

Conceptual envelope:

```text
IdentityMessage
- protocol_version
- message_type
- request_id
- flags
- payload_length
- payload
```

`request_id` is unique within one caller endpoint and is used for cancellation and asynchronous result retrieval.

The final transport should use fixed-width integer fields, explicit byte order, checked arithmetic, and hard maximum lengths.

## 4. Capability classes

Canonical logical capabilities:

```text
identity.auth.submit
identity.auth.query
identity.auth.cancel
identity.identity.create
identity.credential.manage-self
identity.authenticator.manage-self
identity.recovery.begin
identity.recovery.complete
identity.session.consume-grant
identity.policy.admin
identity.audit.read-self
```

A component receives only the capabilities required by its role.

The System App must not automatically receive administrative policy capabilities.

## 5. Authentication operations

### BEGIN_KEY_AUTH

Input:

```text
- normalized-or-normalizable candidate key buffer
- authentication context
```

Output:

```text
- request_id
- state = PENDING | THROTTLED | REJECTED
```

The key buffer is consumed for verification and must be cleared promptly after processing.

### BEGIN_AUTHENTICATOR_AUTH

Input:

```text
- brokered authenticator handle / descriptor
- authentication context
```

The Identity Service must not receive broad arbitrary removable-filesystem access merely to authenticate one drive.

### QUERY_AUTH

Output states:

```text
PENDING
WAITING_FOR_DEVICE
WAITING_FOR_PIN
AUTH_FAILED
AUTH_THROTTLED
CREATION_AVAILABLE
AUTH_SUCCESS
SERVICE_ERROR
CANCELLED
```

`AUTH_SUCCESS` returns an opaque one-time session grant handle, not credential information.

### CANCEL_AUTH

Cancellation is idempotent. Cancelling a completed request must not re-open or duplicate it.

## 6. Identity creation operations

Creation requires a short-lived creation token produced by an authentication attempt when machine policy permits self-service local creation.

Logical operations:

```text
BEGIN_IDENTITY_CREATE
COMMIT_IDENTITY_CREATE
CANCEL_IDENTITY_CREATE
```

Creation must be transactional. A crash between begin and commit cannot leave a login-capable half-created identity.

## 7. Credential-management operations

Self-management operations require an authenticated session context and, for sensitive changes, a fresh purpose-bound re-authentication proof.

Logical operations:

```text
ROTATE_AURORA_KEY
LIST_AUTHENTICATORS
ENROLL_AUTHENTICATOR
REVOKE_AUTHENTICATOR
RENAME_AUTHENTICATOR
```

Secret material is never returned by listing operations.

## 8. Session grant contract

A normal login success yields a one-time grant consumed by Session Manager.

Required properties:

- bound to exactly one `user_id`;
- short-lived;
- single-use;
- replay-resistant;
- unusable by unauthorized callers;
- invalidated after successful consumption or expiry;
- contains no raw Aurora Key or authenticator secret.

Session Manager must present both the grant and the correct `identity.session.consume-grant` capability.

## 9. Re-authentication proofs

Sensitive actions such as changing Aurora Key, enrolling/revoking authenticators, or altering recovery policy may require re-authentication.

A re-authentication proof is:

- bound to one authenticated `user_id`;
- purpose-bound;
- short-lived;
- preferably single-use;
- rejected if presented for a different action.

Example purposes:

```text
ROTATE_PRIMARY_KEY
ENROLL_AUTHENTICATOR
REVOKE_AUTHENTICATOR
CHANGE_RECOVERY_POLICY
EXPORT_RECOVERY_MATERIAL
```

## 10. Error model

Public protocol errors remain coarse:

```text
INVALID_REQUEST
UNAUTHORIZED
UNSUPPORTED_VERSION
BUSY
THROTTLED
AUTH_FAILED
DEVICE_INVALID
DEVICE_REMOVED
POLICY_DENIED
SERVICE_UNAVAILABLE
STORAGE_FAILURE
INTERNAL_FAILURE
```

The protocol must avoid returning an error that unnecessarily confirms whether a specific unknown credential maps to an existing account.

## 11. Message bounds

The implementation must define compile-time/runtime maximums for:

- message envelope size;
- Aurora Key candidate size;
- label/display-name size;
- authenticator descriptor size;
- audit page size;
- concurrent pending requests per caller;
- total pending requests service-wide.

Length fields are validated before allocation or copy. Integer overflow checks are mandatory.

## 12. Versioning

Protocol compatibility uses a major/minor model or equivalent explicit version pair.

Rules:

- incompatible layout/semantic changes increment major version;
- backward-compatible optional additions increment minor version;
- callers and service negotiate only versions both support;
- unsupported major versions fail closed;
- stored credential/database versioning is independent from IPC protocol versioning.

## 13. Concurrency and cancellation

The service may process multiple callers concurrently, but serialization is required around conflicting writes to the same identity.

Examples:

- two key rotations for the same identity cannot commit concurrently;
- revoke and authenticate races resolve atomically;
- the same one-time grant cannot be consumed twice;
- device removal cancels operations depending on that device.

## 14. Secret-memory rules

Credential-bearing IPC buffers must:

- never be logged;
- not be copied into general-purpose debug traces;
- be bounded;
- be cleared after use where practical;
- not be retained by the UI after request submission longer than necessary;
- eventually integrate with protected/locked memory mechanisms when Aurora provides them.

## 15. Recovery and fallback compatibility

The framebuffer fallback UI may use the same logical authentication protocol once user-space services are available. If the normal compositor is unavailable, transport and authorization semantics must remain unchanged.

The fallback UI does not gain broader privilege simply because it is used during recovery.

## 16. Acceptance criteria

The IPC contract is ready for implementation when:

- all operations map to explicit capabilities;
- every request/reply has strict bounds;
- async completion and cancellation are defined;
- session grants are single-use and opaque;
- re-authentication proofs are purpose-bound;
- error behavior does not create obvious account-enumeration leaks;
- protocol version negotiation is implemented;
- malformed-message tests and fuzzing exist;
- no test credential appears in logs or crash output.
