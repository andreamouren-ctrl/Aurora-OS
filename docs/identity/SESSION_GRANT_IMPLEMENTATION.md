# Aurora Identity One-Time Session Grant Implementation

Status: **live Ring 3 issue/consume path integrated**
Version: **0.3**

This document records the executable Aurora Identity session-grant contract and its live Ring 3 consume boundary. Session Manager integration is now implemented.

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

The low-level grant module does **not** decide whether a caller is allowed to request a session. The Aurora Identity Service calls it only after successful authentication and policy checks.

Likewise, possession of a token is not the only production authorization requirement. The Ring 3 Identity Service consume endpoint requires the dedicated `AURORA_CAP_IDENTITY_SESSION` capability with `CONTROL`; the delegated receiver handle must not retain `TRANSFER`.

## 4. Token representation

The bearer token is:

```text
32 random bytes
```

It is generated through `aurora_identity_random_ops`; the live Identity runtime binds this to the controlled DRBG/entropy path.

The token must never be logged, persisted in ordinary identity records, written to crash telemetry, or exposed to applications.

## 5. Backend representation

The backend receives a derived `token_tag`, not the raw bearer token.

`derive_token_tag()` is an explicit provider boundary. The live runtime binds this operation to the Identity cryptographic provider; deterministic stand-ins remain test-only.

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

## 10. Ring 3 consume protocol

Protocol v4 adds:

- `CONSUME_SESSION_GRANT` request carrying only the 32-byte opaque grant;
- exactly one transferred `AURORA_CAP_IDENTITY_SESSION` authority;
- immediate revocation of the received request authority after validation;
- `SESSION_GRANT_RESULT` returning only a coarse state/public error and, on success, the bound 16-byte `user_id`;
- no credential, verifier, token tag, Aurora Key, or raw grant is returned.

Missing/expired/replayed grants collapse to the same public rejected state. Storage/crypto failures remain fail-closed.

## 11. Current test coverage

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

## 12. Live integration status

The following former integration blockers are now implemented:

- Ring 3 Session Manager;
- one-time grant consumption by the Session Manager;
- stable `user_id` binding;
- persistent profile capability issuance after successful binding;
- live boot/login integration;
- logout revocation;
- lock/unlock using a fresh same-user Identity grant;
- replay rejection at the consume boundary;
- credential material remains inside the Identity boundary.

The normal pre-session presentation is still framebuffer-based; compositor-backed Identity UI remains a later M4/G6 milestone.

## 13. Remaining hardening

Production readiness still requires:

1. broader fuzzing and race/fault injection around grant issue/consume;
2. security/audit event integration;
3. real-hardware entropy/time/storage validation;
4. strict generation invalidation across every Identity Service restart path;
5. continued proof that no credential or verifier material crosses into Session Manager/Desktop components.
