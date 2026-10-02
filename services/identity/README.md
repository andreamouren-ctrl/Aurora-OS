# Aurora Identity Core

Status: **isolated implementation foundation with persistent store**

This directory contains the implementation layer of Aurora Identity that is intentionally **not yet wired into Aurora OS login/session startup**.

The goal is to build and verify security-sensitive identity logic behind explicit platform interfaces before binding it to the real Ring 3 service lifecycle, protected AuroraFS system state, IPC transport, compositor UI, or Session Manager.

## Implemented foundations

The isolated implementation now includes:

- canonical Aurora Key normalization (`[A-Z0-9]{12,32}` with spaces/hyphens accepted as visual separators);
- stable opaque `user_id` and independent `credential_id`;
- versioned Argon2id metadata contract;
- identity/credential states (`active`, `disabled`, `recovery-required`);
- configurable progressive throttling;
- authentication flow with opaque lookup-tag resolution and fail-closed backend handling;
- explicit provider boundaries for crypto, secure randomness and monotonic time;
- atomic identity + first Aurora Key creation;
- atomic Aurora Key rotation while preserving stable `user_id`;
- opaque one-time Session Grants with bounded lifetime and atomic consumption;
- a versioned persistent Identity Store with real close/reopen persistence;
- dual-slot transactional publication and corruption recovery;
- durable throttle state;
- explicit little-endian disk serialization independent of host ABI;
- deterministic host tests for security/control flow plus POSIX filesystem persistence tests.

## Transactional identity creation

`aurora_identity_create_with_key()` performs account creation through one atomic store boundary:

```text
candidate Aurora Key
 -> normalize
 -> derive protected lookup tag
 -> preflight uniqueness
 -> generate user_id
 -> generate credential_id
 -> generate salt
 -> derive verifier
 -> atomic store create(identity + first credential)
```

The store enforces uniqueness again at commit time. Preflight lookup is not trusted as the correctness boundary.

## Aurora Key rotation

The rotation module replaces the active Aurora Key credential without changing the human identity.

```text
authorized user_id + current credential_id
 -> normalize replacement Key
 -> derive new lookup tag
 -> generate new credential_id
 -> generate fresh salt/verifier
 -> atomic credential replacement
```

On any non-success result, the old committed credential remains the intended visible state.

Authorization/re-authentication is deliberately a higher Identity Service responsibility and is not bypassed by the low-level rotation core.

## One-time Session Grants

Successful authentication can later be converted by Identity Service into an opaque Session Grant.

The implemented grant core provides:

- 256-bit opaque bearer token;
- one stable `user_id` binding;
- short configurable TTL with hard maximum;
- transient storage contract;
- no raw bearer token persistence;
- atomic consume;
- replay rejection;
- expiry rejection;
- fail-closed RNG/crypto/clock/backend behavior.

The live Session Manager is not connected yet.

## Persistent Identity Store

`persistent_store.c` is the first real persistent backend for the isolated Identity implementation.

Schema v1 stores the current security-critical subset:

- stable Identity records;
- Aurora Key verifier records;
- KDF metadata;
- salt/verifier bytes;
- opaque lookup tags;
- credential status;
- failed-attempt count;
- throttle deadline.

The format is explicitly serialized. C structs are never dumped directly to disk.

### Dual-slot publication

The backend alternates complete snapshots between two durable slots.

Each transaction:

1. copies the current state into staging;
2. applies the mutation only to staging;
3. increments the generation;
4. serializes and validates the complete image;
5. publishes the inactive slot atomically;
6. updates live memory only after successful publication.

On reopen, both slots are validated and the highest valid generation is selected.

Existing corrupt data is never silently interpreted as a first-run empty database.

### Host POSIX adapter

`persistent_store_posix.c` is the current executable durable adapter used by host CI tests.

It uses:

- owner-only `0600` slot files;
- complete temporary-file write;
- file `fsync()`;
- atomic `rename()`;
- parent-directory `fsync()`.

This proves real process-close/process-reopen persistence and transaction behavior.

It is **not** the final Aurora protected-storage adapter.

The final adapter will bind the same `read_slot()` / `write_slot_atomic()` contract to AuroraFS protected system state and capability ownership.

### Integrity

Schema v1 uses CRC32 to detect accidental corruption and malformed/partial images.

CRC32 is not cryptographic authentication. Tamper protection, authenticated storage metadata and at-rest encryption remain future protected-storage/crypto work.

## Bounded state

The first schema deliberately bounds local storage to 32 identities and 32 Aurora Key credential records.

This keeps memory use, parsing and recovery work bounded while the security architecture is still being established.

A later schema can migrate to scalable indexed storage without changing the Identity Service API.

## Secure randomness and cryptography boundary

The implementation does not promote test cryptography into production.

Production is still blocked on reviewed implementations for:

- secure CSPRNG;
- Argon2id;
- keyed opaque lookup-tag PRF;
- Session Grant tag derivation;
- future storage authentication/encryption keys.

Test providers remain isolated under `tests/`.

## Build and test

From the repository root:

```sh
make identity-test
```

or:

```sh
make -C services/identity test
```

The host build is ordinary C11 and intentionally independent from the kernel.

The persistent-store test exercises:

```text
empty open
 -> create
 -> reopen
 -> lookup
 -> persist throttle
 -> reopen
 -> rotate credential
 -> reopen
 -> verify old/new lookup behavior
 -> add another identity
 -> inject failed durable write
 -> prove visible generation unchanged
 -> corrupt newest slot
 -> recover previous valid generation
 -> corrupt all slots
 -> fail closed
```

## Still not connected / not production-complete

The implementation still deliberately lacks:

- production Argon2id;
- production keyed lookup-tag primitive;
- production cryptographic RNG;
- AuroraFS protected system-state adapter;
- authenticated/encrypted Identity database at rest;
- Ring 3 Identity Service process lifecycle;
- capability-authorized Identity IPC;
- Session Manager/profile bootstrap;
- recovery credentials;
- Aurora Identity Drive implementation;
- live boot/login integration;
- graphical System App.

## Integration gate

The core should only replace the bootstrap login path after at least:

1. protected service-owned durable system state exists;
2. the persistent store has an AuroraFS durable adapter;
3. reviewed CSPRNG/KDF/lookup primitives exist;
4. Ring 3 Identity Service lifecycle and capability IPC exist;
5. Session Manager can consume grants and bind profile capabilities;
6. crash/recovery tests pass on the Aurora-native storage path.

Until then, the framebuffer login remains a bootstrap/recovery prototype and must not claim production authentication.
