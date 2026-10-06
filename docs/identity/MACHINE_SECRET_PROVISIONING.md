# Aurora Identity Machine Secret Provisioning

Status: **Protected State transport adapter implemented; Ring 3 bridge pending**
Version: **0.3**

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

The canonical serialized record is 84 bytes and uses the `AURMSV1` magic. The codec is shared by Aurora-native transports so the stored representation does not depend on a host POSIX implementation.

The checksum detects accidental damage. It is **not** an authentication mechanism against an attacker who can rewrite protected system state.

## 4. Replica model

The foundation uses two create-once replicas:

- `machine-secret.a`
- `machine-secret.b`

A single valid replica is sufficient to recover the original secret if the second replica is missing or damaged.

If both valid replicas contain different secrets, Aurora fails closed rather than selecting one arbitrarily.

The host POSIX validation adapter remains available for persistence tests. The Aurora-native adapter now maps the same replica model onto a narrow Protected State transport contract:

- `read_record(name, ...)`;
- `create_record_once_durable(name, ...)`.

The adapter deliberately does not include kernel, VFS, or capability headers. The future Ring 3 Identity Service supplies those transport operations using its Protected State authority.

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

Aurora OS now has a kernel Protected System State foundation.

Sensitive system-service namespaces live conceptually under:

```text
/system/.protected/<scope>/
```

and are accessed through the dedicated `AURORA_CAP_PROTECTED_STATE` capability type rather than ordinary application authority. The kernel foundation separates READ, WRITE and CONTROL rights, binds a capability to one exact namespace object, rejects pathname escape attempts, and does not grant TRANSFER authority through the Protected State API.

The Machine Secret now has an Aurora-native Protected State **transport adapter**. Its two replicas are represented by the fixed record names `machine-secret.a` and `machine-secret.b`, and create-once conflicts map directly to the existing Machine Secret concurrency/fail-closed rules.

This does not yet mean the live Ring 3 Identity Service can access the namespace: the service lifecycle and capability bridge/syscall/IPC path still need to be implemented. Until then the adapter is an isolated, tested service-side boundary rather than a live login dependency.

The transactional Identity database is intentionally **not** migrated to Protected State by this milestone. Its dual-slot backend requires a true durable atomic-replace primitive. Current rename support is not being treated as equivalent without that guarantee.

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
- RNG failure leaving the store unprovisioned;
- Protected State adapter provisioning and reopen using a deterministic transport;
- Protected State adapter corruption with proof that no replacement secret is generated.

The kernel Protected State milestone separately verifies typed capability enforcement and mounted AuroraFS persistence semantics.

## 11. Remaining gates

Before this secret participates in live Aurora login:

1. Identity Service DRBG must be seeded from the kernel entropy service;
2. the Ring 3 Identity Service must receive the Protected State capability from trusted system policy;
3. the service must bridge the implemented Protected State transport operations to that capability;
4. secure secret-memory lifetime rules must be applied in the final service process;
5. offline-at-rest hardening policy must be selected;
6. rotation/recovery policy must be designed before any production root-secret replacement feature exists.

Separately, before the transactional Identity database can use the same Protected State namespace, Aurora needs a durable atomic-replace primitive with crash-consistency semantics strong enough for the dual-slot store contract.
