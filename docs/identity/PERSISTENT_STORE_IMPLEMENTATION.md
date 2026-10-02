# Aurora Identity Persistent Store Implementation

Status: **Implemented isolated persistence foundation**
Version: **0.1**

## 1. Purpose

This milestone turns the Aurora Identity core store contract into a real persistent, reopenable and transaction-oriented storage engine without wiring Aurora Identity into the live Aurora OS login path yet.

The implementation lives under `services/identity/` and remains portable. The final Aurora-native adapter will bind the same durable-slot contract to protected AuroraFS system state once the Ring 3 service/storage boundary is ready.

## 2. Storage model

The first implemented format is a bounded, versioned **dual-slot snapshot store**.

Each durable database image contains:

- format magic;
- global schema version;
- monotonic generation number;
- identity count;
- Aurora Key credential count;
- serialized stable Identity records;
- serialized Aurora Key verifier records;
- persisted throttle state;
- CRC32 corruption-detection checksum.

Raw Aurora Keys are never serialized.

The store currently supports the security-critical subset already required by the isolated core:

- stable `user_id`;
- identity status;
- identity policy and record versions;
- `credential_id`;
- keyed opaque `lookup_tag`;
- KDF metadata;
- salt;
- verifier;
- credential status;
- failed-attempt count;
- throttle deadline.

## 3. Why two slots

The logical database alternates between slot 0 and slot 1.

For every successful transaction:

1. copy the currently visible state into a staging state;
2. apply the requested mutation only to the staging copy;
3. increment the generation;
4. serialize the entire new image;
5. publish it atomically into the inactive slot;
6. only after durable publication succeeds, replace the live in-memory state.

Therefore a failed write does not expose a partially mutated in-memory database and does not intentionally invalidate the previous committed slot.

At reopen, Aurora validates both slots and chooses the valid image with the highest generation.

If the newest slot is corrupt but the previous slot is valid, the store can recover the previous valid generation.

If existing slot data is present but no valid image remains, open returns `CORRUPT`. It never silently treats corruption as first-run empty state.

## 4. On-disk portability

The file format is serialized explicitly in little-endian byte form.

It does not write C structs directly to disk. This avoids dependence on:

- compiler padding;
- enum size;
- host pointer width;
- `size_t` width;
- ABI-specific structure layout.

The format has a global schema version and fixed validation rules.

Unsupported schema versions fail closed with `UNSUPPORTED_SCHEMA`.

## 5. Transactional operations implemented

### Create identity + first Aurora Key

The persistent backend implements the existing `create_identity_with_key()` contract.

Commit-time checks enforce:

- unique `user_id`;
- unique `credential_id`;
- unique lookup tag;
- key record references the exact new `user_id`;
- capacity bounds;
- record structural validity.

Identity and first credential become visible in the same snapshot generation.

### Authentication lookup

Lookup by opaque Aurora Key tag reads the persisted credential record.

The stored record includes the verifier/KDF data required by the authentication core.

### Persistent throttle state

Authentication failure state is now durable:

- failed attempt count;
- throttle deadline.

A reboot/reopen therefore does not automatically erase throttling for a known credential.

The current core contract models one active Aurora Key credential per local identity. The persistent backend fails closed if the state would make that update ambiguous.

### Aurora Key rotation

The backend implements the existing atomic rotation contract.

Rotation:

- verifies `current_credential_id` belongs to the requested `user_id`;
- checks new credential ID uniqueness;
- checks new lookup-tag uniqueness;
- replaces the old credential only in the staged state;
- commits the replacement in one new snapshot generation.

On failure, the old committed credential remains the visible state.

## 6. Bounded state

Schema v1 intentionally has bounded capacities:

- 32 stable local identities;
- 32 Aurora Key verifier records.

This is not intended to be Aurora's final large-scale account database limit.

The bound is deliberate for the first security-critical implementation because it provides:

- bounded memory use;
- bounded parsing;
- bounded startup work;
- no attacker-driven unbounded allocation;
- simple corruption validation.

A future schema migration may introduce scalable indexed storage without changing the higher-level Identity Service contracts.

## 7. Integrity model

The current CRC32 detects accidental corruption and malformed/partial images.

CRC32 is **not** a cryptographic authenticator and must not be described as tamper protection.

Future protected system-state work may add:

- authenticated storage metadata;
- machine-bound MAC keys;
- storage encryption;
- TPM/secure-element-backed wrapping;
- AuroraFS-integrated integrity metadata.

The Aurora Key verifier remains memory-hard regardless of at-rest encryption.

## 8. Host POSIX durable adapter

`persistent_store_posix.c` provides the current executable persistence adapter used by CI tests.

For each slot publication it:

1. creates/truncates a temporary file with owner-only mode `0600`;
2. writes the complete serialized image;
3. `fsync()`s the file;
4. atomically renames the temporary file over the target slot;
5. `fsync()`s the parent directory.

This adapter exists to validate real close/reopen persistence and transaction behavior on CI hosts.

It is not the final Aurora OS protected-storage adapter.

## 9. AuroraFS integration boundary

The store deliberately depends only on:

```text
read_slot(slot)
write_slot_atomic(slot, complete_image)
```

The final Aurora-native adapter can therefore map one complete slot publication to AuroraFS copy-on-write/durable inode publication instead of duplicating Identity transaction logic.

This avoids binding the Identity database format directly to kernel-internal filesystem structures.

The Aurora adapter must ultimately ensure:

- only Identity Service has the direct storage capability;
- atomic complete-image publication;
- durable ordering/barriers;
- protected system-state path/namespace;
- no ordinary user/application write access;
- corruption is surfaced, not auto-reset.

## 10. Concurrency model

The current persistent store is **single-writer service-owned state**.

It is not a multi-process shared database.

The future Aurora Identity Service is the sole owner and serializes mutations before calling the store.

This is intentional: ordinary applications must never open and mutate the Identity database directly.

Future service concurrency may add an internal lock/transaction queue without changing the persistent format.

## 11. Recovery behavior

Open behavior is explicit:

- no slot exists -> `EMPTY`;
- at least one valid supported slot -> newest valid generation opens;
- existing slots but none valid -> `CORRUPT`;
- recognized image with unsupported schema -> `UNSUPPORTED_SCHEMA`;
- underlying I/O failure -> `IO_ERROR`.

A damaged database is never silently replaced by a fresh empty store.

## 12. Tests

The persistent-store CI test exercises real filesystem persistence:

```text
empty open
 -> create identity + credential
 -> close/reopen
 -> verify lookup
 -> persist throttle state
 -> close/reopen
 -> rotate Aurora Key
 -> close/reopen
 -> verify old tag gone and new tag present
 -> add second identity
 -> inject durable-write failure
 -> prove generation/state unchanged
 -> corrupt newest slot
 -> recover previous valid generation
 -> corrupt both slots
 -> fail closed as CORRUPT
```

It also verifies POSIX slot files are owner-only (`0600`).

## 13. Explicitly not complete yet

This milestone does **not** yet provide:

- AuroraFS protected system-state adapter;
- production Argon2id provider;
- production keyed lookup-tag PRF;
- production CSPRNG;
- encrypted/authenticated database-at-rest protection;
- Ring 3 Identity Service lifecycle;
- capability-protected Identity IPC;
- Session Manager/profile bootstrap;
- live login integration;
- graphical UI resources.

Those remain separate gates.

## 14. Next gate

After this persistent engine is validated, the next high-value step is to implement the Aurora-native storage/crypto prerequisites in the correct order:

1. protected service-owned storage namespace/capability;
2. AuroraFS durable slot adapter;
3. production CSPRNG and cryptographic provider;
4. Identity Service process/IPC wrapper;
5. then Session Manager and live login integration.

The isolated persistent engine should not be bypassed by kernel/UI code.
