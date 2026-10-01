# Aurora Identity Service Specification

Status: **Canonical design**
Version: **0.2**

## 1. Role

Aurora Identity Service is the privileged user-space authority for local identities and authentication policy.

It owns no presentation. It exposes a narrow IPC contract consumed by Aurora Identity System App, the bootstrap/recovery login surface, Session Manager, and explicitly authorized administrative/recovery components.

## 2. Responsibilities

The service owns:

- identity record lifecycle;
- Aurora Key verifier derivation and verification;
- authenticator enrollment and revocation;
- Aurora Identity Drive validation;
- recovery credential validation;
- authentication throttling;
- machine account-creation policy;
- credential migration;
- authenticated-session grant issuance;
- security audit events without secret disclosure;
- transactional identity database migrations.

The service does not own:

- low-level keyboard/USB drivers;
- framebuffer/compositor drawing;
- general filesystem UI;
- desktop/session rendering;
- application permission prompts unrelated to identity.

## 3. Service lifecycle

Conceptual startup:

```text
START
 -> acquire required capabilities
 -> open protected identity storage
 -> verify/migrate schema
 -> initialize secure RNG client
 -> restore rate-limit state
 -> publish IPC endpoint
 -> READY
```

If protected storage cannot be opened safely, the service enters a restricted failure state and must not issue normal session grants.

## 4. IPC trust model

Every request is authorized by caller capabilities and operation context.

Caller identity alone is insufficient; possession of the correct capability is required.

Credential-bearing requests are bounded and short-lived.

The service must not include credential bytes in ordinary error responses.

## 5. Conceptual IPC operations

Names below describe semantics, not final ABI symbols.

### Authentication

```text
BeginAuroraKeyAuthentication(candidate_key)
BeginAuthenticatorAuthentication(authenticator_descriptor)
CancelAuthentication(request_id)
GetAuthenticationState(request_id)
```

Possible terminal results:

```text
AUTH_SUCCESS(session_grant)
AUTH_FAILED
AUTH_THROTTLED(retry_after)
CREATION_AVAILABLE(creation_token)
SERVICE_UNAVAILABLE
```

`CREATION_AVAILABLE` is returned only when machine policy permits unknown-credential local user creation.

### Identity creation

```text
BeginIdentityCreation(creation_token, profile_options)
ConfirmIdentityCreation(request_id)
CancelIdentityCreation(request_id)
```

Creation must be transactional. Partial records must not become login-capable identities.

### Credential management

```text
RotateAuroraKey(authenticated_context, new_key)
EnrollAuthenticator(authenticated_context, descriptor)
RevokeAuthenticator(authenticated_context, authenticator_id)
ListAuthenticators(authenticated_context)
```

Returned authenticator metadata must exclude secret material.

### Session and re-authentication

```text
IssueSessionGrant(auth_result)
BeginReauthentication(authenticated_context, purpose)
ValidateSensitiveActionProof(proof, purpose)
```

Re-authentication proofs should be narrowly scoped to the requested sensitive action and expire quickly.

### Recovery

```text
BeginRecovery(recovery_method)
CompleteRecovery(request_id, proof)
RotateCredentialAfterRecovery(...)
```

Recovery may be unavailable in ordinary mode if the machine has no enrolled recovery method.

## 6. Authentication request state machine

```text
NEW
 -> VALIDATING_INPUT
 -> POLICY_CHECK
 -> THROTTLE_CHECK
 -> VERIFYING

VERIFYING
 -> SUCCESS
 -> FAILED
 -> CREATION_AVAILABLE
 -> CANCELLED
 -> ERROR

SUCCESS
 -> SESSION_GRANT_ISSUED
 -> COMPLETE
```

Device-backed flows add states such as `WAITING_FOR_DEVICE`, `DEVICE_REMOVED`, or `WAITING_FOR_PIN`.

## 7. Aurora Key flow

1. Receive bounded candidate credential from authorized caller.
2. Normalize/validate using canonical Aurora Key rules.
3. Check global and credential-context throttling policy.
4. Resolve candidate verifier without leaking enumeration state to caller.
5. Derive Argon2id verifier with stored parameters.
6. Compare safely.
7. On success, opportunistically migrate verifier parameters if needed.
8. Update last-login/security metadata transactionally.
9. Issue one-time session grant.
10. Clear candidate credential buffer.

## 8. Identity Drive flow

1. Receive a brokered authenticator descriptor/container handle.
2. Validate container format/version and size.
3. Resolve `authenticator_id` enrollment.
4. Verify integrity/proof according to authenticator class.
5. Enforce revocation, machine-binding, PIN, and throttling policy.
6. Issue session grant on success.
7. Do not retain removable-media secret material longer than required.

The service should operate on handles or narrowly scoped buffers provided by the storage/device broker instead of broad access to arbitrary removable filesystems where possible.

## 9. Session grant contract

A session grant is opaque outside the Identity Service/Session Manager trust boundary.

Required properties:

- cryptographically unguessable or protected by trusted capability semantics;
- bound to one `user_id`;
- short-lived;
- single-consumption for normal login;
- non-replayable after logout/consumption;
- purpose-bound where re-authentication grants are used;
- contains no raw credential material.

## 10. Account creation policy

Machine policy controls whether an unknown Aurora Key may create a user.

Possible policies:

- `OPEN_LOCAL_CREATION`;
- `OWNER_APPROVAL_REQUIRED`;
- `ADMIN_ONLY`;
- `CREATION_DISABLED`.

The first personal-desktop implementation may default to open local creation during first setup and a user-configurable policy afterward, but this remains a product-policy decision outside the kernel.

## 11. Rate-limit model

The service owns persisted throttling state.

The initial implementation should support:

- progressive delays;
- machine-wide pressure limits;
- authenticator-specific failure state;
- reboot-persistent timestamps/counters once storage exists;
- monotonic-time handling where possible;
- bounded failure metadata to prevent database growth attacks.

## 12. Database migration

Schema and credential formats are versioned independently.

Migrations must:

- run before the service publishes full readiness;
- be transactional;
- preserve last-known-valid state on failure;
- never require plaintext recovery of Aurora Keys;
- record a non-secret migration audit event.

## 13. Administrative and managed systems

Future managed-machine support may expose separate privileged policy APIs for:

- disabling identities;
- forcing credential rotation;
- restricting authenticator classes;
- requiring multi-factor authentication;
- disabling local self-service creation;
- enforcing recovery policy.

These operations require stronger capabilities than self-management APIs.

## 14. Error model

UI-facing errors are intentionally coarse:

- authentication failed;
- authentication temporarily throttled;
- device invalid/unsupported;
- service unavailable;
- recovery required.

Detailed internal diagnostics may be written to secure system logs only if they do not contain secrets or provide unsafe account-enumeration detail to untrusted callers.

## 15. Concurrency

The service must handle concurrent requests safely:

- only one transactional credential rotation per identity at a time;
- authenticator revocation races resolve atomically;
- session grants cannot be double-issued from a single-use result;
- drive removal cancels affected pending operations;
- database migration excludes ordinary write operations.

## 16. Acceptance criteria

The first functional Identity Service milestone requires:

- isolated Ring 3 execution;
- protected IPC endpoint;
- persistent storage backend;
- secure RNG;
- Aurora Key verifier path;
- persisted rate limiting;
- account creation transaction;
- one-time session grant issuance;
- no raw secret material in logs;
- crash recovery tests;
- successful offline authentication and session bootstrap.
