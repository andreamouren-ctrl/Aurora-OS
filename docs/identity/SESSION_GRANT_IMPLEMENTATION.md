# Aurora Identity One-Time Session Grant Implementation

Status: **isolated implementation foundation**
Version: **0.1**

This document records the first executable Aurora Identity session-grant contract. It remains intentionally isolated from the live Aurora OS login/session path.

## 1. Purpose

Successful authentication proves a stable `user_id`, but it must not expose the Aurora Key or other authenticator secret to Session Manager.

Aurora Identity therefore issues an opaque bearer grant that Session Manager can consume once to bootstrap the authenticated profile.

## 2. Implemented properties

The current isolated module guarantees:

- 256-bit opaque random bearer token;
- token bound to exactly one stable `user_id`;
- configurable short TTL with a hard five-minute upper bound;
- non-zero token requirement;
- backend uniqueness enforcement;
- bounded retry on an improbable token-tag conflict;
- raw token is not persisted by the grant store;
- atomic consume operation;
- successful consume invalidates the grant before returning;
- expired grants are invalidated;
- replay after success fails;
- replay after expiry fails;
- all sensitive temporary buffers are cleared before returning;
- fail-closed behavior for RNG, cryptographic, clock and backend failures.

## 3. Trust boundary

`aurora_identity_session_grant_issue()` accepts an already-authenticated `user_id`.

The low-level grant module does **not** decide whether a caller is allowed to request a session. The future Aurora Identity Service is responsible for calling it only after successful authentication and policy checks.

Likewise, possession of a token is not the only production authorization requirement. The future IPC endpoint must also require the dedicated Session Manager capability (`identity.session.consume-grant`) described by the canonical IPC design.

## 4. Token representation

The bearer token is:

```text
32 random bytes
```

It is generated through `aurora_identity_random_ops`, which must eventually be backed by Aurora's reviewed CSPRNG.

The token must never be logged, persisted in ordinary identity records, written to crash telemetry, or exposed to applications.

## 5. Backend representation

The backend receives a derived `token_tag`, not the raw bearer token.

`derive_token_tag()` is an explicit provider boundary. Production must bind this operation to a reviewed cryptographic primitive. The test implementation is deterministic and test-only.

Because the bearer token itself carries 256 bits of CSPRNG entropy, this design does not rely on human-memorable-secret properties. The tag still exists to avoid retaining live bearer tokens in the backend.

## 6. Grant record

Current transient record:

```text
SessionGrantRecord
- user_id
- token_tag
- issued_at_ms
- expires_at_ms
- record_version
```

It deliberately contains no Aurora Key, authenticator secret, recovery secret or profile capability.

## 7. Issue flow

```text
authenticated user_id
        |
        v
validate grant policy
        |
        v
read monotonic time
        |
        v
generate 256-bit random token
        |
        v
derive token_tag
        |
        v
publish transient grant record
        |
        +-- conflict -> bounded regenerate/retry
        |
        v
return raw bearer token + expiry
```

A failed issue operation returns no usable token.

## 8. Consume flow

```text
Session Manager bearer token
        |
        v
derive token_tag
        |
        v
read monotonic time
        |
        v
atomic backend consume(tag, now)
        |
        +-- missing -> NOT_FOUND
        +-- expired -> invalidate + EXPIRED
        +-- backend failure -> fail closed
        |
        v
return bound user_id
```

The critical rule is that lookup and invalidation are one backend operation. A read-then-delete implementation would permit a race in which two consumers could observe the same valid grant.

## 9. Lifetime and reboot behavior

Session grants are transient authorization handoff objects, not durable identity state.

Production grant storage must therefore be cleared when:

- Aurora reboots;
- Identity Service restarts into a new generation;
- the grant expires;
- the grant is consumed;
- a security policy explicitly revokes outstanding grants.

Grant records must not be restored from normal identity database backups.

## 10. Current test coverage

Host-side tests verify:

- issue -> consume success;
- returned `user_id` binding;
- replay rejection;
- expiry rejection and invalidation;
- retry after an all-zero RNG candidate;
- retry after uniqueness conflict;
- invalid `user_id` rejection;
- TTL policy enforcement;
- RNG failure;
- token-tag derivation failure;
- issue-store failure;
- consume-store failure;
- zero bearer token rejection.

All providers in the host test are stand-ins and are not production cryptography or storage.

## 11. Deliberately not implemented yet

This milestone does not implement:

- production CSPRNG;
- production token-tag cryptographic provider;
- Aurora-native transient grant store;
- Identity Service Ring 3 process;
- Session Manager Ring 3 process;
- capability-authorized IPC;
- profile opening/capability issuance;
- live boot/login integration;
- lock/unlock session lifecycle;
- graphical UI.

## 12. Integration gate

The module can be connected to the real login path only after:

1. Aurora Identity Service has a real process/service lifecycle;
2. secure RNG and crypto providers are production-ready;
3. a capability-protected IPC transport exists;
4. Session Manager can consume the grant and bind the resulting `user_id` to profile startup;
5. successful consumption is tested against replay/race scenarios;
6. no credential material crosses the Identity Service boundary.
