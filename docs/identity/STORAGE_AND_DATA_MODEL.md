# Aurora Identity Storage and Data Model

Status: **Canonical design**
Version: **0.4**

## 1. Goals

Aurora Identity storage must be persistent, transactional, versioned, and isolated from ordinary applications.

The storage model separates stable identities from replaceable credentials and authenticators.

## 2. Protected storage location

The final path depends on AuroraFS/VFS conventions, but identity data belongs in protected system state, not inside a normal user-writable profile directory.

Only Aurora Identity Service should receive direct read/write capability to the primary identity database in ordinary operation.

Backups or snapshots containing identity data must preserve access controls and must never downgrade the data to plaintext credential storage.

## 3. Stable identity record

Conceptual schema:

```text
Identity
- user_id                  opaque stable primary identifier
- display_name             optional presentation label
- profile_ref              stable profile/storage reference
- status                   active / disabled / recovery-required
- role                     administrator / standard-user / guest
- created_at
- last_successful_login_at
- policy_version
- record_version
```

`display_name` is not unique and is never used as the authentication key.

The isolated core currently defines the security-critical subset needed before profile/session integration: `user_id`, status, role, policy version, and record version.

`UNASSIGNED` is a transient pre-commit role only. It must never exist in committed persistent state.

### First-user bootstrap rule

The persistent store is authoritative for initial role assignment. During atomic identity creation:

- if no identity is committed yet, the new identity becomes `ADMINISTRATOR`;
- every later newly created persistent identity becomes `STANDARD_USER`;
- the caller submits `UNASSIGNED` and cannot self-assign administrator through the low-level creation contract.

Role assignment occurs in the same transaction that publishes the stable identity and its first Aurora Key credential. This prevents independent preflight checks from producing two "first" administrators.

Future administrator promotion/demotion policy is a separate authenticated management operation and is not implied by account creation.

## 4. Credential records

Credentials are separate rows/objects bound to `user_id`.

Conceptual common fields:

```text
Credential
- credential_id
- user_id
- type
- status
- created_at
- last_used_at
- version
- policy_metadata
```

Credential-specific secret/verifier data is stored in type-specific protected records.

`credential_id` is independent from `user_id`. Rotating or replacing an Aurora Key creates/replaces credential state without changing the stable human identity.

## 5. Aurora Key verifier record

```text
AuroraKeyVerifier
- credential_id
- user_id
- lookup_tag
- algorithm
- parameters_version
- memory_cost
- time_cost
- parallelism
- salt
- verifier
- rotated_at
```

Raw Aurora Key is never stored.

### Opaque lookup tag

Because the default Aurora login does not expose a separate public username, the Identity Service needs an index that can locate the candidate credential before executing the expensive memory-hard verifier.

`lookup_tag` is therefore a **keyed opaque index**, not a plain hash of the Aurora Key.

Production requirements:

- derived from the normalized Aurora Key with a protected service/machine secret and a reviewed PRF construction;
- fixed-size and suitable for indexed equality lookup;
- unique for active Aurora Key credentials under the active lookup-key domain;
- never sufficient by itself to authenticate;
- never generated with an unkeyed fast hash of the Aurora Key;
- rotatable through a versioned migration if the protected lookup secret changes.

A copied database must not expose a cheap deterministic oracle that lets an attacker test guessed Aurora Keys without also possessing the protected lookup secret.

## 6. Atomic identity + first credential creation

Creating a local identity is one transactional publication boundary.

Conceptually:

```text
BEGIN
  determine bootstrap role from committed identity state
  insert Identity(user_id, role, ...)
  insert Credential(credential_id, user_id, type=AURORA_KEY, ...)
  insert AuroraKeyVerifier(credential_id, lookup_tag, salt, verifier, ...)
COMMIT
```

Production invariants:

- role assignment, stable Identity record, and first Aurora Key credential become visible together;
- no record becomes visible if commit fails;
- `UNASSIGNED` is rejected as persisted state;
- `user_id` is unique;
- `credential_id` is unique;
- active `lookup_tag` is unique;
- a concurrent create race becomes a transaction/uniqueness conflict, never duplicate credentials;
- returning a creation error after partial durable publication is forbidden.

The isolated core expresses this through `create_identity_with_key()`. The current host-tested persistent store now implements this transaction with dual-slot snapshots, but it is still not the final protected AuroraFS backend.

Preflight lookup is an optimization and UX aid. Correctness still depends on commit-time uniqueness and role assignment because another request may race between preflight and commit.

## 7. Authenticator record

```text
Authenticator
- authenticator_id
- user_id
- class                    identity_drive / secure_key / future
- friendly_label
- status                   active / revoked / disabled
- machine_binding
- public_or_verifier_data
- credential_version
- created_at
- last_used_at
- revoked_at
- policy_flags
```

For hardware-backed authenticators, public keys and attestation metadata may be stored according to later standards. Private hardware keys must not be exportable into this database.

## 8. Recovery records

```text
RecoveryCredential
- recovery_id
- user_id
- method
- verifier_or_public_data
- status
- created_at
- used_at
- rotation_required
```

Recovery methods remain independent from the Aurora Key verifier.

## 9. Rate-limit state

Rate-limit escalation must persist across reboot, but monotonic-clock deadlines must not be reused across different boot/service clock epochs.

The isolated persistent schema v2 therefore uses the following split:

```text
Durable throttle state
- failed_attempts

Volatile current-epoch state
- throttle_until_monotonic_ms
```

On reopen/reboot:

1. durable `failed_attempts` is restored;
2. the previous monotonic deadline is intentionally absent;
3. before another verifier attempt, the authentication core re-arms the penalty associated with the persisted failure level in the new monotonic epoch;
4. changing only this volatile deadline does not require another durable database generation.

This prevents reboot from erasing escalation while also preventing invalid comparisons between monotonic timestamps from unrelated epochs.

Longer-term policy may add durable wall-clock or secure-time metadata when Aurora has a trustworthy time source, but the current design does not depend on one.

The implementation must also avoid unbounded creation of arbitrary records from attacker-controlled candidate keys. Unknown-key pressure should use bounded machine/global buckets or another memory-safe strategy.

## 10. Current isolated persistent schema v2

The host-tested store uses two complete snapshot slots and an explicit global schema version.

Each committed image contains:

- schema/version metadata;
- monotonic database generation number;
- bounded identity count and key-record count;
- stable identity records including role;
- Aurora Key verifier records including durable failure count;
- CRC32 corruption detection.

The current implementation intentionally does **not** serialize `throttle_until_ms`.

Transactions are staged in memory, serialized into the inactive slot, durably published, and only then become live. Reopen selects the newest valid generation. If the newest slot is corrupt but the previous slot is valid, the previous generation can be recovered. If no valid existing snapshot remains, open fails closed instead of silently creating an empty identity database.

CRC32 is corruption detection only; it is not cryptographic tamper protection.

## 11. Session metadata

Long-lived reusable session credentials should not be stored casually in the identity database.

Persisted session metadata, if needed, may include:

```text
SessionAuditMetadata
- session_id
- user_id
- started_at
- ended_at
- authentication_method_class
- termination_reason
```

One-time session grants should normally be transient and non-replayable.

## 12. Audit records

Identity security events may be stored in a separate protected audit stream.

Allowed metadata includes:

- event type;
- timestamp;
- user/session reference where safe;
- authenticator class or ID reference;
- success/failure category;
- policy/action source.

Audit records must not contain raw secrets, verifier bytes, private keys, recovery secrets, or full challenge/response material.

## 13. Database technology

The isolated implementation now has a small purpose-built, versioned dual-slot store that proves the required transaction semantics on a POSIX host adapter.

This does **not** finish production storage. The Aurora-native implementation still requires:

- protected service-owned AuroraFS system state;
- an AuroraFS durable-slot adapter preserving atomic publication and ordering;
- capability isolation from ordinary applications;
- cryptographic integrity/authentication as required by the final threat model;
- controlled migration strategy for future schema versions.

Aurora already has substantial VFS/AuroraFS/block-device foundations, but protected durable system state remains a separate integration gate.

## 14. Encryption at rest

Filesystem/storage encryption and identity-database encryption are related but distinct concerns.

If Aurora adds a machine secret or TPM/secure-element-backed storage key, identity data may be encrypted/wrapped at rest. This is desirable but must not be confused with the Aurora Key verifier itself.

The system must still use a memory-hard verifier even if the database file is encrypted.

## 15. Integrity and corruption

The service must detect malformed or unsupported records.

On corruption:

- do not silently recreate an empty identity database over existing data;
- preserve the damaged store for controlled recovery/forensics where policy allows;
- enter a recovery-required state;
- avoid issuing unauthenticated sessions;
- expose only safe diagnostic information to the user.

## 16. Schema versioning

The database has a global schema version and individual credential format versions.

The current isolated store is schema v2. Schema v1 snapshots are not silently interpreted as v2.

Migration rules:

- migrations are ordered and idempotent where practical;
- migrations run transactionally;
- backup/snapshot integration should exist before destructive migrations;
- failure restores the previous valid state or leaves the database in a clearly recoverable state;
- credentials can be migrated independently when cryptographic formats change.

## 17. Deletion and identity removal

Deleting an identity is a high-impact operation and requires explicit authenticated authorization.

Identity removal policy must define separately:

- credential/authenticator revocation;
- active session termination;
- profile data deletion or archival;
- encryption-key destruction where applicable;
- audit retention policy.

Deleting the login identity must not accidentally bypass secure profile-data handling.
