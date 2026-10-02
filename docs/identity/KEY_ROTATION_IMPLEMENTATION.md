# Aurora Key Rotation — Implementation Contract

Status: **Implemented isolated core contract**  
Version: **0.1**

## Purpose

Aurora Key rotation replaces one Aurora Key credential without changing the stable human identity.

The operation is implemented in the isolated Aurora Identity core and is intentionally not yet connected to the live Aurora OS login path.

## Security boundary

The low-level rotation function does **not** decide whether a caller is authorized to rotate a credential.

The future Aurora Identity Service must require:

- an authenticated session/context;
- the `identity.credential.manage-self` capability or an explicitly stronger administrative/recovery capability;
- fresh re-authentication when policy requires it;
- a target `user_id` and current `credential_id` derived from trusted service state, never from untrusted UI assertions alone.

The rotation backend independently verifies that the current credential belongs to the supplied stable `user_id` before committing.

## Rotation sequence

```text
authorized user/context
        |
        v
normalize replacement Aurora Key
        |
        v
derive opaque keyed lookup tag
        |
        v
reject an already-existing lookup tag
        |
        v
generate new credential_id
        |
        v
generate fresh random salt
        |
        v
derive new Argon2id verifier through crypto provider
        |
        v
atomic store transaction
  - verify current credential belongs to user_id
  - enforce new lookup/id uniqueness
  - retire old credential
  - publish replacement credential
        |
        v
return new credential_id
```

## Required invariants

1. `user_id` never changes during ordinary Aurora Key rotation.
2. The replacement receives a new `credential_id`.
3. The replacement receives a fresh salt.
4. The replacement verifier is derived using the active versioned KDF policy.
5. The old Aurora Key stops resolving only after the replacement is durably committed.
6. Any storage, RNG, policy, or cryptographic failure leaves the old credential valid.
7. Rotating to an already-enrolled Aurora Key is rejected.
8. The backend re-checks uniqueness during commit to close the race between preflight lookup and publication.
9. Failure/throttle counters do not migrate into the replacement credential.
10. No raw Aurora Key is persisted or written to ordinary logs.

## Atomic store contract

The isolated module exposes `replace_key_credential()` through `aurora_identity_rotation_store_ops`.

The future persistent backend must perform the following in one transaction:

- locate the current `credential_id`;
- verify its `user_id` matches the requested identity;
- ensure the replacement lookup tag is unique;
- ensure the replacement `credential_id` is unique;
- retire/delete the old active Aurora Key credential according to schema policy;
- insert/publish the replacement;
- commit atomically.

On `CURRENT_NOT_FOUND`, `CONFLICT`, or `ERROR`, no replacement becomes visible and the existing credential remains unchanged.

## Current implementation

Code:

- `services/identity/include/aurora/identity/rotation.h`
- `services/identity/src/rotation.c`
- `services/identity/tests/test_identity_rotation.c`

The production Argon2id provider, production keyed lookup PRF, CSPRNG, and durable protected database are still separate dependency gates. Tests use deterministic test-only primitives and must never be linked into production authentication.

## Verification

The host-side test suite verifies at least:

- successful rotation preserves `user_id`;
- a new `credential_id` is issued;
- the old Key no longer resolves after a successful commit;
- the new Key authenticates the same identity;
- same-Key rotation is rejected without writing;
- a mismatched current credential fails without changing state;
- commit-time uniqueness conflict preserves the old credential;
- RNG and verifier-derivation failures preserve the old credential;
- malformed replacement Keys are rejected before publication.

## Integration gate

This implementation remains isolated until Aurora has the real Identity Service authorization/re-authentication path and a protected transactional identity store. The framebuffer login is not changed by this milestone.
