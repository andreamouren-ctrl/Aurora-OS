# Aurora Identity Core

Status: **Identity core + live Ring 3 service integrated; not yet production-certified**

This directory contains the security-sensitive core of Aurora Identity **and** its integrated live Ring 3 service foundation. The active OS login path uses the isolated Identity Service for credential verification and authenticated session grants, with protected AuroraFS system state, bounded IPC and a separate Session Manager/User Session Host. The compositor-backed secure pre-session Identity System App, recovery/authenticator breadth and production-security certification are **not yet complete**.

The platform continues to separate privileged Identity policy and credential state from kernel mechanisms and untrusted UI; framebuffer login remains an independent bootstrap/recovery path.

## Implemented foundations

The isolated layer now implements:

- canonical Aurora Key normalization (`[A-Z0-9]{12,32}` with spaces/hyphens accepted as visual separators);
- stable opaque `user_id` and independent `credential_id` types;
- versioned Argon2id parameter metadata contract;
- identity/credential record state (`active`, `disabled`, `recovery-required`);
- persistent identity roles (`administrator`, `standard user`, `guest`) with `unassigned` reserved for the pre-commit boundary;
- configurable progressive authentication throttling;
- authentication flow with protected lookup, verifier delegation, durable failure escalation, volatile monotonic deadlines, and fail-closed backend handling;
- atomic identity + first Aurora Key creation;
- first-user bootstrap role assignment inside the persistent-store transaction;
- bounded retry for reserved all-zero random identifiers;
- duplicate Aurora Key preflight plus mandatory commit-time uniqueness handling;
- atomic Aurora Key rotation while keeping stable `user_id` unchanged;
- one-time bounded session-grant core;
- a versioned dual-slot persistent store with close/reopen tests, rollback to the previous valid generation, corruption detection, and POSIX durable publication;
- SHA-256 and HMAC-SHA256 primitives;
- constant-time byte comparison helper;
- HMAC-DRBG with explicit instantiate/reseed/generate lifecycle;
- domain-separated HMAC tags for Aurora Key lookup and Session Grant tokens;
- an Identity random-provider adapter backed by an already-instantiated HMAC-DRBG;
- a bounded Argon2id verifier provider backed by the pinned official Argon2 reference implementation;
- RFC 9106 Argon2id known-answer validation;
- create-once 256-bit machine Identity root-secret provisioning;
- redundant machine-secret replicas with fail-closed corruption/conflict handling;
- domain-separated derivation of the persistent Aurora Key lookup-HMAC key from the machine root secret;
- secret-buffer clearing helpers and deterministic host tests.

## Transactional identity creation and bootstrap role

`aurora_identity_create_with_key()` constructs a new identity and first Aurora Key without depending on a concrete database.

Conceptual flow:

```text
candidate Aurora Key
 -> normalize
 -> derive protected lookup tag
 -> preflight uniqueness check
 -> generate nonzero user_id
 -> generate nonzero credential_id
 -> generate random salt
 -> derive versioned Argon2id verifier
 -> submit identity(role=UNASSIGNED) + first key
 -> persistent store assigns bootstrap role in the commit
 -> publish success
```

The persistent backend is authoritative for bootstrap role assignment:

- if the committed identity set is empty, the new identity becomes `ADMINISTRATOR`;
- otherwise a newly created persistent identity becomes `STANDARD_USER`;
- callers cannot self-assign `ADMINISTRATOR` through the creation contract;
- role assignment, identity publication, and first credential publication happen in the same transaction.

This prevents two concurrent first-user attempts from both deciding independently that they are the first administrator.

## Persistent store schema v2

The current host-tested persistent engine uses two complete snapshot slots. Every durable mutation is staged, serialized, written to the inactive slot, and only then becomes the live state. On reopen, the newest valid generation wins; if the newest slot is damaged, the previous valid generation can be selected.

Schema v2 persists:

- stable identity ID, status, role, policy version, and record version;
- credential ID and user binding;
- opaque lookup tag;
- KDF metadata, salt, verifier, and credential status;
- durable failed-attempt count.

Raw Aurora Keys are never stored.

### Reboot-safe throttling

A monotonic timestamp belongs to one boot/service clock epoch and therefore must not be serialized for reuse after reboot.

The persistent backend stores `failed_attempts` but **does not serialize `throttle_until_ms`**. After reopen, the authentication core sees the durable escalation level and re-arms one penalty in the new monotonic epoch before another verifier attempt is allowed. Re-arming the same failure count changes only volatile memory and does not create another disk generation.

## Crypto foundation

The crypto foundation provides SHA-256, HMAC-SHA256, a constant-time comparison helper, HMAC-DRBG, and the isolated Argon2id verifier provider.

Aurora OS now also has a kernel entropy seed foundation that qualifies RDSEED with startup/continuous health checks and fails closed when a trusted seed source is unavailable. The Identity service still needs its future Ring 3 handoff and reseed lifecycle before live production use.

The provider derives the Aurora Key lookup tag as a domain-separated HMAC-SHA256 value under a dedicated protected lookup key. A copied identity database therefore does not expose the plain deterministic hash oracle that the original design explicitly prohibited.

For verifier derivation/checking, Aurora pins `P-H-C/phc-winner-argon2` at commit `f57e61e19229e23c4445b85494dbf7c07de721cb` and wraps it behind `aurora_identity_crypto_ops`. Stored KDF metadata is checked against explicit memory/time/parallelism/salt/verifier bounds before Argon2 work is allowed.

See `docs/identity/CRYPTO_FOUNDATION.md`, `docs/identity/ARGON2ID_PROVIDER.md`, and `docs/ENTROPY.md`.

## Machine Identity root secret

`machine_secret.c` implements create-once provisioning of a 256-bit root secret. Normal startup follows a strict rule:

```text
no secret replicas -> provision once
valid replica      -> load existing secret
corrupt replicas   -> fail closed
conflicting valid replicas -> fail closed
```

Corruption never causes automatic regeneration. Replacing the root secret without a coordinated lookup-tag migration would make existing Aurora Key records unreachable.

The root secret derives the persistent lookup key as:

```text
HMAC-SHA256(machine_root_secret, "AURORA.IDENTITY.LOOKUP-KEY.V1")
```

The POSIX adapter validates create-once durability, owner-only permissions, close/reopen stability, and replica recovery. It is not the final Aurora Protected System State adapter and does not claim hardware sealing or offline-disk confidentiality.

See `docs/identity/MACHINE_SECRET_PROVISIONING.md`.

## Test providers and persistence adapters

The deterministic test inputs/providers under `tests/` are validation-only. They are not suitable for production authentication or DRBG seeding.

`persistent_store_posix.c` proves real Identity database close/reopen durability in CI.

`machine_secret_posix.c` proves create-once machine-secret publication with owner-only files, file `fsync`, atomic no-clobber publication, and directory `fsync`.

Neither POSIX adapter is the final AuroraFS protected-system-state adapter.

Tests cover, among other cases:

- successful identity + first credential publication and immediate authentication;
- duplicate/race rejection and no partial publication;
- Aurora Key rotation atomicity;
- session-grant issue/consume behavior;
- first identity becoming administrator and later identities becoming standard users;
- reboot-safe throttling;
- persistent-store corruption fallback/fail-closed behavior;
- SHA-256/HMAC/HMAC-DRBG known-answer validation;
- RFC 9106 Argon2id v=19 validation;
- Argon2id resource-bound and parameter-version rejection;
- first machine-secret provisioning and close/reopen stability;
- stable lookup-key derivation across reopen;
- proof that an existing machine secret does not invoke RNG again;
- recovery from one damaged machine-secret replica;
- fail-closed handling when no valid machine-secret replica remains;
- RNG failure leaving machine-secret state unprovisioned.

## Not implemented yet

This layer still deliberately does not provide:

- Ring 3 handoff from the kernel entropy seed service into the Identity DRBG lifecycle;
- calibrated production Argon2id creation parameters for Aurora hardware classes;
- Aurora-native Protected System State adapter for the machine secret and Identity database;
- capability-authorized machine-secret access restricted to the Identity Service;
- hardware sealing/encrypted-at-rest protection against an offline raw-disk attacker;
- safe machine-root-secret rotation/migration;
- Ring 3 Aurora Identity Service lifecycle;
- capability-authorized Identity IPC transport;
- production Session Manager/profile bootstrap;
- recovery credentials and Identity Drive integration;
- live login integration;
- graphical System App UI.

The existing framebuffer login remains a bootstrap/recovery prototype and must not claim production authentication.

## Build and test

The Argon2 reference implementation is a pinned Git submodule. A fresh checkout must initialize submodules before running Identity tests:

```sh
git submodule update --init --recursive
```

From the repository root:

```sh
make identity-test
```

or directly:

```sh
make -C services/identity test
```

Aurora-owned host code is compiled as strict C11 with warning-as-error flags. The pinned Argon2 reference source is built separately with `ARGON2_NO_THREADS` for the isolated validation path.

## Integration gate

The isolated Identity implementation should only be connected to the real Aurora OS login path after at least:

1. a protected service-owned AuroraFS system-state namespace/capability exists;
2. the Ring 3 Identity Service can obtain seed material through a capability-authorized entropy handoff and run the DRBG reseed lifecycle;
3. the machine root secret is provisioned through an Aurora-native protected-state adapter;
4. Argon2id parameters are calibrated for Aurora hardware targets and bound into production policy;
5. the Identity database uses the Aurora-native durable adapter;
6. the Ring 3 Identity Service lifecycle and capability-authorized IPC transport exist;
7. the Session Manager can consume non-replayable session grants and bootstrap the correct profile/session context.
