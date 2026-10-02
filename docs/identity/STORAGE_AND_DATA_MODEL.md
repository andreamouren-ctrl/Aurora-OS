# Aurora Identity Storage and Data Model

Status: **Canonical design**
Version: **0.3**

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
- created_at
- last_successful_login_at
- policy_version
- record_version
```

`display_name` is not unique and is never used as the authentication key.

The isolated core already defines the security-critical subset needed before profile/session integration: `user_id`, status, policy version, and record version.

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
  insert Identity(user_id, ...)
  insert Credential(credential_id, user_id, type=AURORA_KEY, ...)
  insert AuroraKeyVerifier(credential_id, lookup_tag, salt, verifier, ...)
COMMIT
```

Production invariants:

- the stable Identity record and first Aurora Key credential become visible together;
- no record becomes visible if commit fails;
- `user_id` is unique;
- `credential_id` is unique;
- active `lookup_tag` is unique;
- a concurrent create race becomes a transaction/uniqueness conflict, never duplicate credentials;
- returning a creation error after partial durable publication is forbidden.

The isolated core expresses this through `create_identity_with_key()`. The current host test store implements the contract only for deterministic tests; it is not the production database.

Preflight lookup is an optimization and UX aid. Correctness still depends on commit-time uniqueness enforcement because another request may race between preflight and commit.

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

Rate-limit metadata must persist across reboot.

Conceptual bounded state:

```text
AuthThrottleState
- scope_id
- scope_type
- failure_count_window
- last_failure_at
- blocked_until
- version
```

The implementation must avoid unbounded creation of arbitrary records from attacker-controlled candidate keys. Unknown-key pressure should use bounded machine/global buckets or another memory-safe strategy.

## 10. Session metadata

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

## 11. Audit records

Identity security events may be stored in a separate protected audit stream.

Allowed metadata includes:

- event type;
- timestamp;
- user/session reference where safe;
- authenticator class or ID reference;
- success/failure category;
- policy/action source.

Audit records must not contain raw secrets, verifier bytes, private keys, recovery secrets, or full challenge/response material.

## 12. Database technology

The first production implementation may use an embedded transactional database or a small purpose-built protected store, provided it supports:

- atomic transactions;
- crash recovery;
- uniqueness constraints/indexes required by Identity;
- schema versioning;
- integrity checking;
- bounded queries;
- controlled locking/concurrency;
- secure file permissions/capabilities.

Aurora already has substantial VFS/AuroraFS/block-device foundations, but **protected durable system state** and the production transactional identity backend remain separate implementation gates. The database engine/storage format is therefore still intentionally unfrozen.

## 13. Encryption at rest

Filesystem/storage encryption and identity-database encryption are related but distinct concerns.

If Aurora adds a machine secret or TPM/secure-element-backed storage key, identity data may be encrypted/wrapped at rest. This is desirable but must not be confused with the Aurora Key verifier itself.

The system must still use a memory-hard verifier even if the database file is encrypted.

## 14. Integrity and corruption

The service must detect malformed or unsupported records.

On corruption:

- do not silently recreate an empty identity database over existing data;
- preserve the damaged store for controlled recovery/forensics where policy allows;
- enter a recovery-required state;
- avoid issuing unauthenticated sessions;
- expose only safe diagnostic information to the user.

## 15. Schema versioning

The database has a global schema version and individual credential format versions.

Migration rules:

- migrations are ordered and idempotent where practical;
- migrations run transactionally;
- backup/snapshot integration should exist before destructive migrations;
- failure restores the previous valid state or leaves the database in a clearly recoverable state;
- credentials can be migrated independently when cryptographic formats change.

## 16. Deletion and identity removal

Deleting an identity is a high-impact operation and requires explicit authenticated authorization.

Identity removal policy must define separately:

- credential/authenticator revocation;
- active session termination;
- profile data deletion or archival;
- encryption-key destruction where applicable;
- audit retention policy.

Deleting the login identity must not accidentally bypass secure profile-data handling.
