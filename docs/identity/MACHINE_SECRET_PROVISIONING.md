# Aurora Identity Machine Secret Provisioning

Status: **isolated implementation foundation**
Version: **0.1**

## 1. Purpose

Aurora Identity needs a persistent secret that is independent from every user's Aurora Key.

The first consumer is the opaque Aurora Key lookup index. The database stores a keyed lookup tag rather than a plain hash of the Aurora Key, so the key behind that HMAC must remain stable across reboot.

Aurora therefore provisions one 256-bit **machine Identity root secret** and derives service keys from it with explicit domain separation.

The root secret is not a user credential and must never be displayed, logged, exported through ordinary account APIs, or regenerated merely because a read failed.

## 2. Core invariant

Automatic regeneration after corruption is forbidden.

If Aurora silently replaced the root secret, the derived lookup-HMAC key would change and every existing Aurora Key lookup tag would become unreachable.

Therefore:

```text
no replicas exist
    -> first provisioning is allowed

at least one valid replica exists
    -> load the existing secret

replicas exist but none validate
    -> fail closed as CORRUPT

two valid replicas disagree
    -> fail closed as CONFLICT
```

Recovery/rotation must be an explicit future migration procedure, not a fallback inside normal boot.

## 3. Record model

Version 1 stores:

- record version;
- generation (`1` for the create-once foundation);
- 32-byte machine root secret;
- SHA-256 corruption checksum over a domain-separated canonical representation.

The checksum detects accidental damage. It is **not** an authentication mechanism against an attacker who can rewrite protected system state.

## 4. Replica model

The foundation uses two create-once replicas:

- `machine-secret.a`
- `machine-secret.b`

A single valid replica is sufficient to recover the original secret if the second replica is missing or damaged.

If both valid replicas contain different secrets, Aurora fails closed rather than selecting one arbitrarily.

The POSIX validation adapter publishes a replica by:

1. writing a temporary owner-only file;
2. `fsync` of the complete temporary record;
3. atomic create-if-absent publication with a hard link;
4. removal of the temporary name;
5. `fsync` of the containing directory.

This proves close/reopen and create-once semantics on a host filesystem. It is not the final Aurora runtime adapter.

## 5. Random generation

The provisioning core consumes `aurora_identity_random_ops`.

Production must provide that interface from the Identity Service's HMAC-DRBG after the DRBG has been instantiated/reseeded from Aurora's qualified entropy seed service.

The provisioning core:

- requests 256 bits;
- rejects the reserved all-zero secret;
- retries a bounded number of times;
- fails closed on RNG failure.

Deterministic test providers are validation-only.

## 6. Lookup-key derivation

The root secret is not used directly as the Aurora Key lookup HMAC key.

Version 1 derives:

```text
lookup_key = HMAC-SHA256(
    machine_root_secret,
    "AURORA.IDENTITY.LOOKUP-KEY.V1"
)
```

This creates an independent service key and leaves room for future domain-separated keys for database authentication, recovery, device enrollment, or other protocols.

Session Grant keys remain transient and have a separate lifecycle; they are not made persistent merely because a machine root secret now exists.

## 7. Protected System State boundary

AuroraFS/VFS now has the primitives required for a future native adapter, including create, rename, truncate, ownership/mode changes, `fsync`/`fdatasync`, filesystem sync, and AuroraFS v2 ACL support.

However, the final security boundary also requires the Ring 3 Identity Service lifecycle and capability-authorized access path.

The production adapter must ensure that ordinary applications cannot open or modify the machine-secret records simply by knowing their path.

The POSIX adapter in this milestone only proves persistence behavior and owner-only host permissions. It does not claim to be Aurora's final Protected System State implementation.

## 8. At-rest threat boundary

The current format does not encrypt or hardware-seal the root secret.

Filesystem ownership/capability isolation can protect it from ordinary running applications, but an offline attacker with unrestricted raw-disk access may still recover the bytes unless Aurora later adds one or more of:

- encrypted protected system state;
- TPM-backed sealing;
- equivalent secure-hardware key wrapping.

Do not describe the current foundation as resistant to offline disk extraction.

## 9. Rotation and migration

Root-secret rotation is intentionally not implemented yet.

A safe future rotation must coordinate at least:

1. generation of a replacement root secret;
2. derivation of the replacement lookup key;
3. re-computation of every stored Aurora Key lookup tag from an authenticated migration source or another migration design that does not require plaintext credential storage;
4. atomic database publication;
5. retirement of the old secret only after the new database is durable.

Because Aurora never stores plaintext Aurora Keys, this migration requires explicit architecture work and must not be improvised as a simple file replacement.

## 10. Tests

CI covers:

- first provisioning;
- close/reopen stability;
- stable derived lookup key across reopen;
- proof that existing state does not call the RNG again;
- owner-only directory/file modes in the POSIX adapter;
- bounded retry of an all-zero RNG candidate;
- recovery from one corrupt replica;
- fail-closed behavior when both replicas are corrupt;
- RNG failure leaving the store unprovisioned.

## 11. Remaining gates

Before this secret participates in live Aurora login:

1. Identity Service DRBG must be seeded from the kernel entropy service;
2. Aurora-native Protected System State adapter must replace the host POSIX adapter;
3. access must be capability-authorized to the Identity Service;
4. secure secret-memory lifetime rules must be applied in the final service process;
5. offline-at-rest hardening policy must be selected;
6. rotation/recovery policy must be designed before any production root-secret replacement feature exists.
