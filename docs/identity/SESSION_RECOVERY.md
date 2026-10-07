# Aurora Identity Session and Recovery

Status: **Canonical design**
Version: **0.2**

## 1. Session objective

Authentication proves that a caller is allowed to act as a stable Aurora `user_id`. It does not directly grant applications access to that user's files or secrets.

A successful authentication therefore transitions through a separate **Session Manager** boundary.

## 2. Session bootstrap

Canonical flow:

```text
Aurora Identity App / fallback UI
        |
        v
Aurora Identity Service
        |
        | successful authentication
        v
one-time session grant
        |
        v
Aurora Session Manager
        |
        +-> bind user_id
        +-> open profile
        +-> issue user capabilities
        +-> start desktop/session services
        v
SESSION_ACTIVE
```

The Session Manager never receives the Aurora Key, Identity Drive secret, recovery credential, or private authenticator key.

### Current profile-bootstrap implementation

After a one-time grant is consumed successfully, the Ring 3 Session Manager asks the kernel profile mechanism to open or create the persistent profile backing directory for the authenticated stable `user_id`.

The current v1 backing layout is:

```text
/system/users/<stable-user-id-hex>
```

The path is storage representation only; it is **not** authorization. The Session Manager receives a capability-gated profile handle and transfers a reduced `AURORA_CAP_FILE` authority to the session bridge. The capability is validated against the same authenticated `user_id` before the session becomes active.

Aurora Key, display name, or a guessed path can never substitute for that capability.

## 3. Session grant

A normal login session grant is:

- opaque;
- bound to one `user_id`;
- short-lived;
- single-use;
- non-replayable after consumption;
- issued only by Aurora Identity Service;
- accepted only by trusted Session Manager paths.

The exact implementation may use cryptographic tokens, capability objects, or a hybrid model depending on final IPC/session architecture.

## 4. Session states

```text
SESSION_STARTING
 -> ACTIVE
 -> LOCKED
 -> ACTIVE
 -> LOGGING_OUT
 -> TERMINATED
```

Exceptional states may include:

- `RECOVERY_REQUIRED`;
- `PROFILE_UNAVAILABLE`;
- `SESSION_SERVICE_FAILURE`.

Authentication success alone does not guarantee session startup if profile/storage initialization fails.

## 5. Locking

Locking removes interactive access while preserving the underlying session according to Session Manager policy.

Unlock requires fresh authentication through Aurora Identity.

The lock screen may display the current user's presentation because the active session identity is already known locally; this does not change the cold-boot no-enumeration rule.

## 6. Re-authentication

Sensitive operations can require a fresh proof without locking the whole desktop.

Examples:

- change Aurora Key;
- enroll/revoke Identity Drive;
- change recovery configuration;
- authorize high-impact system administration;
- expose a protected secret.

The resulting proof is:

- purpose-bound;
- short-lived;
- non-reusable for unrelated operations.

## 7. Logout

Logout should:

1. stop accepting new user interactions;
2. terminate/revoke session-scoped capabilities;
3. close user-owned services according to policy;
4. flush required user state;
5. clear transient identity proofs;
6. return to pre-session Aurora Identity login.

A logout must not leave a reusable session grant behind.

## 8. Recovery philosophy

Recovery is a separate authentication path. Aurora never recovers the original Aurora Key from a reversible database copy.

After successful recovery, the normal action is to create a **new** Aurora Key and revoke/retire the previous verifier.

## 9. Recovery methods

Aurora Identity may support:

### Local recovery credential

A separately generated high-entropy recovery secret stored by the user offline. Aurora stores only a protected verifier/public representation.

### Trusted hardware authenticator

A previously enrolled secure key may authorize recovery if machine policy permits.

### Trusted device approval

Future federation may allow another enrolled Aurora device to approve recovery cryptographically.

### Managed administrator recovery

Enterprise/managed devices may delegate recovery to a separately privileged administrator identity.

### Encrypted recovery escrow

Optional future feature. It must be explicit, encrypted, revocable where possible, and never mandatory for local Aurora use.

## 10. Recovery credential properties

A recovery credential should:

- be independent from Aurora Key;
- have high entropy;
- be shown/generated only through a protected setup flow;
- be individually revocable/replaceable;
- trigger security audit events when used;
- normally force Aurora Key rotation after successful use.

## 11. Recovery state machine

```text
RECOVERY_IDLE
 -> METHOD_SELECTION
 -> RECOVERY_AUTHENTICATING

RECOVERY_AUTHENTICATING
 -> RECOVERY_SUCCESS
 -> RECOVERY_FAILED
 -> RECOVERY_THROTTLED

RECOVERY_SUCCESS
 -> CREDENTIAL_ROTATION_REQUIRED
 -> SECURITY_REVIEW
 -> SESSION_STARTING or LOGIN_IDLE
```

`SECURITY_REVIEW` may prompt the user to review/revoke lost authenticators.

## 12. Lost Aurora Key

If the Aurora Key is lost:

1. user enters Recovery Mode;
2. completes an enrolled recovery method;
3. Aurora authenticates recovery without exposing the old verifier;
4. a new Aurora Key is created;
5. old Aurora Key credential is revoked;
6. user reviews Identity Drives/secure keys;
7. normal login resumes.

## 13. Lost Identity Drive

If a drive is lost:

1. authenticate using Aurora Key or another authenticator;
2. open Aurora Identity -> Access Devices;
3. revoke the lost `authenticator_id`;
4. optionally review security activity;
5. enroll a replacement.

The user's `user_id` and profile remain unchanged.

## 14. All credentials lost

If all local credentials are lost and no recovery method exists, Aurora must not bypass authentication merely to preserve convenience.

Possible outcomes depend on machine policy and future installation design:

- restore from a separately protected backup/recovery mechanism;
- managed administrator recovery;
- profile remains inaccessible;
- destructive reset/reinstallation as last resort.

This is an intentional fail-closed behavior.

## 15. Recovery environment

Aurora's recovery environment should be capable of starting a minimal Aurora Identity path independently from the normal desktop compositor.

It may provide:

- credential/recovery authentication;
- identity database integrity checks;
- controlled rollback/restore;
- profile repair operations;
- credential rotation after successful recovery.

It must not expose a generic unauthenticated shell simply because normal login failed.

## 16. Multi-session future

The architecture permits later support for:

- fast user switching;
- multiple local interactive sessions;
- remote sessions;
- per-session lock state.

Each session remains bound to a stable `user_id` and receives independently scoped capabilities.

## 17. Acceptance criteria

The first authenticated-session milestone is complete when:

- Identity Service can issue a one-time grant;
- Session Manager validates and consumes it once;
- profile capabilities are bound to the correct `user_id`;
- replay fails;
- lock/unlock requires fresh Aurora Identity authentication;
- logout revokes session access and returns to login;
- no credential material crosses into ordinary application processes;
- recovery cannot bypass identity policy.
