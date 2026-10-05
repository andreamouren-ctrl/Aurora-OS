# Aurora Identity Crypto Foundation

Status: **isolated implementation foundation**
Version: **0.1**

## 1. Purpose

This milestone provides the first reusable cryptographic building blocks for Aurora Identity without claiming that Aurora OS already has a complete production entropy or password-verification subsystem.

Implemented in `services/identity/`:

- SHA-256;
- HMAC-SHA256;
- constant-time byte comparison helper;
- HMAC-DRBG using HMAC-SHA256;
- domain-separated HMAC lookup tags for Aurora Key indexing;
- domain-separated HMAC tags for one-time Session Grant tokens;
- an Identity random-provider adapter backed by an already-instantiated HMAC-DRBG.

## 2. Security boundary

### HMAC-DRBG is not an entropy source

The DRBG expands trusted seed material. It does not create entropy.

Production Aurora Identity must not instantiate or reseed the DRBG until Aurora has a reviewed entropy subsystem that can provide sufficient unpredictable input.

The current API therefore requires entropy and nonce bytes explicitly. Tests use deterministic inputs only for known-answer verification.

### No blind dependence on CPU random instructions

This milestone does not treat `RDRAND`, timing jitter, TSC values, device serials, or other convenient machine values as a production entropy source merely because they are available.

Future platform work must define how Aurora collects, health-checks, combines, and exposes entropy before the Identity Service can label its random provider production-ready.

## 3. HMAC-DRBG policy

The current implementation:

- requires at least 256 bits of entropy input at instantiation/reseed;
- requires at least 128 bits of nonce input at instantiation;
- bounds entropy, nonce, personalization, additional input, and request sizes;
- maintains an explicit reseed counter;
- refuses generation past the configured reseed interval;
- wipes transient keying buffers where practical;
- clears the whole DRBG state on explicit destruction.

The DRBG is intended to produce values such as:

- `user_id`;
- `credential_id`;
- verifier salts;
- Session Grant bearer tokens;
- recovery/challenge nonces;
- future authenticator identifiers.

## 4. Aurora Key lookup tag

Aurora login intentionally has no public username field. Identity must therefore find the candidate Aurora Key record before running Argon2id.

The crypto provider now derives:

```text
lookup_tag = HMAC-SHA256(
    lookup_key,
    "AURORA.IDENTITY.LOOKUP.V1" || 0x00 || normalized_aurora_key
)
```

Properties:

- the raw Aurora Key is not persisted;
- the lookup database does not expose a plain unkeyed hash oracle;
- the label provides domain separation from other HMAC uses;
- equal normalized Keys under the same lookup-key domain produce equal lookup tags for indexed lookup.

The `lookup_key` is a protected machine/service secret and **must remain stable across reboot**. Regenerating it on every boot would make existing lookup tags unreproducible.

Provisioning, protected persistence, rotation, and migration of this key remain future Protected System State work.

## 5. Session Grant token tags

Raw one-time bearer tokens should not be stored by the grant table. The provider derives:

```text
token_tag = HMAC-SHA256(
    session_grant_key,
    "AURORA.IDENTITY.SESSION-GRANT.V1" || 0x00 || token
)
```

The independent domain label and independent provider key prevent accidental cross-protocol reuse with Aurora Key lookup tags.

Session grants are transient and should not survive Identity Service generation changes. Their HMAC key therefore has different lifecycle requirements from the persistent Aurora Key lookup key.

## 6. Key separation

The provider deliberately accepts distinct keys for:

- Aurora Key lookup tags;
- Session Grant token tags.

Do not reuse one HMAC key for unrelated Identity protocols.

Future recovery, audit authentication, storage integrity, or device-pairing protocols should receive their own derived/key-separated domains as required by their threat models.

## 7. Argon2id remains separate

This milestone does **not** implement the Aurora Key verifier itself.

The target remains Argon2id with:

- unique random salt per credential;
- versioned memory/time/parallelism parameters;
- constant-time comparison where applicable;
- parameters selected against real Aurora hardware targets.

The HMAC lookup tag is an index only. It is never sufficient to authenticate a user.

## 8. Test coverage

Host-side tests include:

- SHA-256 known-answer vectors;
- HMAC-SHA256 known-answer vectors;
- deterministic HMAC-DRBG known-answer output;
- state clearing and pre-instantiation rejection;
- fixed lookup-tag vector;
- fixed Session Grant tag vector;
- domain/key separation checks;
- random-provider generation through the provider adapter.

## 9. Remaining production gates

Before this foundation can be used by the live Aurora Identity Service, Aurora still needs:

1. a reviewed kernel/platform entropy collection subsystem;
2. secure DRBG initial seeding and periodic reseeding policy;
3. protected provisioning/storage of the persistent lookup HMAC key;
4. Argon2id implementation and parameter calibration;
5. protected AuroraFS system state;
6. Ring 3 Identity Service lifecycle and capability-authorized IPC;
7. secure memory/secret-lifetime hardening appropriate to the final service runtime.

Until those gates are complete, deterministic tests and host adapters remain validation infrastructure, not live authentication.
