# Aurora Identity Core

Status: **isolated implementation foundation with persistent host store and crypto foundation**

This directory contains the implementation layer of Aurora Identity that is intentionally **not yet wired into Aurora OS login/session startup**.

The goal is to build and verify the security-sensitive identity logic behind explicit platform interfaces before binding it to the real Ring 3 service lifecycle, protected AuroraFS system state, production entropy, IPC transport, compositor UI, or Session Manager.

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

This preserves throttling across reboot without comparing timestamps from unrelated monotonic-clock epochs.

## Crypto foundation

The crypto foundation provides SHA-256, HMAC-SHA256, a constant-time comparison helper, HMAC-DRBG, and the isolated Argon2id verifier provider.

The DRBG is deliberately **not** an entropy source. Production use requires Aurora to supply reviewed unpredictable entropy and nonce material before instantiation/reseed. Deterministic seeds exist only in tests.

The provider derives the Aurora Key lookup tag as a domain-separated HMAC-SHA256 value under a dedicated protected lookup key. A copied database therefore does not expose the plain deterministic hash oracle that the original design explicitly prohibited.

The provider also uses a separate key and domain for transient Session Grant token tags. Key reuse between these protocols is forbidden.

For verifier derivation/checking, Aurora pins `P-H-C/phc-winner-argon2` at commit `f57e61e19229e23c4445b85494dbf7c07de721cb` and wraps it behind `aurora_identity_crypto_ops`. Stored KDF metadata is checked against explicit memory/time/parallelism/salt/verifier bounds before Argon2 work is allowed. Unsupported parameter versions fail closed.

The lookup HMAC key must remain stable across reboot so existing lookup tags remain reproducible. Provisioning, protected persistence, rotation, and migration of that key belong to future Protected System State work.

See `docs/identity/CRYPTO_FOUNDATION.md` and `docs/identity/ARGON2ID_PROVIDER.md` for the complete boundaries and remaining gates.

## Test providers and persistence adapter

The deterministic test inputs/providers under `tests/` are validation-only. They are not suitable for production authentication or DRBG seeding.

`persistent_store_posix.c` is a host adapter used to prove real close/reopen durability in CI. It writes owner-only slot files, fsyncs complete images, atomically renames them into place, and fsyncs the parent directory. It is **not** the final AuroraFS protected-system-state adapter.

Tests cover, among other cases:

- successful identity + first credential publication;
- immediate authentication;
- duplicate/race rejection and no partial publication;
- random/verifier/backend failure handling;
- Aurora Key rotation atomicity;
- session-grant issue/consume behavior;
- first identity becoming administrator;
- later identity becoming standard user;
- rejection of caller-forged administrator role;
- durable failure-count persistence with non-persistent monotonic deadline;
- volatile throttle re-arm without another disk generation;
- durable-write failure rollback;
- newest-slot corruption fallback;
- fail-closed behavior when no valid snapshot remains;
- SHA-256 and HMAC-SHA256 known-answer vectors;
- deterministic HMAC-DRBG known-answer output;
- domain-separated lookup/session-tag vectors;
- provider-backed random generation and state clearing;
- RFC 9106 Argon2id v=19 vector;
- Argon2id provider derive/verify success and mismatch paths;
- rejection of unsupported or resource-excessive KDF metadata.

## Not implemented yet

This layer still deliberately does not provide:

- reviewed Aurora kernel/platform entropy collection and DRBG seeding path;
- calibrated production Argon2id creation parameters for Aurora hardware classes;
- protected provisioning/storage/rotation of the persistent lookup HMAC key;
- protected AuroraFS system-state namespace/capability;
- authenticated/encrypted database-at-rest protection;
- Aurora-native durable slot adapter;
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

The host build uses ordinary C11 and has no dependency on the kernel. Aurora-owned code is compiled with strict warning-as-error flags. The pinned Argon2 reference source is built separately with `ARGON2_NO_THREADS` for the isolated host validation path.

## Integration gate

The isolated Identity implementation should only be connected to the real Aurora OS login path after at least:

1. protected service-owned durable system state exists;
2. Aurora has a reviewed production entropy source and secure DRBG seeding/reseeding path;
3. Argon2id parameters are calibrated for Aurora hardware targets and bound into production policy;
4. persistent lookup-HMAC key provisioning/rotation exists;
5. an AuroraFS durable-slot adapter preserves the store's atomic publication contract;
6. the Ring 3 Identity Service lifecycle and capability-authorized IPC transport exist;
7. the Session Manager can consume non-replayable session grants and bootstrap the correct profile/session context.
