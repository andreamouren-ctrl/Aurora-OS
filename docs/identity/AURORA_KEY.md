# Aurora Key Specification

Status: **Canonical design**
Version: **0.2**

## 1. Definition

**Aurora Key** is the baseline local secret credential for Aurora Identity.

It replaces the traditional visible username + password login pair in the default Aurora OS experience. The user enters one secret alphanumeric value; Aurora uses it to identify and authenticate the corresponding local identity without first exposing a list of accounts.

Aurora Key is a credential, not the stable identity identifier.

## 2. Canonical normalization

Before verification, input is normalized deterministically:

- ASCII `a-z` becomes `A-Z`;
- ASCII `A-Z` is unchanged;
- ASCII `0-9` is unchanged;
- spaces and hyphens may be accepted as visual separators and discarded;
- all other characters are rejected by the initial policy;
- locale-specific case mappings are not used.

Normalized form:

```text
[A-Z0-9]{12,32}
```

Initial limits:

- minimum: **12** normalized characters;
- maximum: **32** normalized characters.

Policy is versioned so future releases can strengthen generation or length requirements without invalidating existing verifiers.

## 3. Presentation

The UI may group characters for readability:

```text
A7K9-M2PQ-84XR
```

Hyphens are presentation-only and are not part of the normalized secret.

The default login field displays masked characters. Temporary plaintext reveal, if ever supported, must be explicit, short-lived, and unavailable on secure/recovery surfaces by default.

## 4. Generation

Aurora should strongly encourage system-generated Aurora Keys.

Generated keys must use the system cryptographic RNG and must avoid biased selection.

The generator may use the full canonical alphabet or a documented reduced human-friendly alphabet if product testing shows substantial transcription benefit. If a reduced alphabet is adopted, it becomes a versioned generation profile; normalization still remains compatible with `[A-Z0-9]`.

The UI should:

- show the generated key only when the user explicitly requests/creates it;
- require confirmation that the user has safely recorded it when policy requires;
- never write it to ordinary logs or diagnostics;
- optionally allow secure copy only through a future protected-secret UI path.

## 5. User-created keys

User-created Aurora Keys are permitted if machine policy allows them and they satisfy the active credential policy.

Future policy may reject trivially weak keys using local strength checks or compromised-secret databases, but ordinary local login must not require a network query.

Strength checks must not store rejected candidate keys.

## 6. Verifier derivation

Target production verifier:

- algorithm: **Argon2id**;
- unique random salt per credential;
- versioned memory/time/parallelism parameters;
- output stored only in protected identity storage.

Conceptual record:

```text
AuroraKeyCredential
- credential_id
- user_id
- algorithm = ARGON2ID
- parameters_version
- memory_cost
- time_cost
- parallelism
- salt
- verifier
- created_at
- rotated_at
- status
```

The exact binary/database representation is defined by the storage schema, not by the UI.

## 7. Authentication flow

```text
KEY_ENTRY
  -> normalize
  -> validate format
  -> Identity Service authentication request
  -> rate-limit check
  -> verifier derivation/comparison
  -> success: issue session grant
  -> failure: generic failure/throttle state
```

The UI does not perform Argon2id verification itself.

## 8. Unknown-key creation flow

On machines that permit self-service local account creation, an unknown valid Aurora Key may lead to:

```text
User not found. Create a new Aurora profile with this key?

[ Create ] [ Cancel ]
```

Before creation, the Identity Service re-checks machine policy and uniqueness under the same normalized representation.

Managed or recovery-restricted systems may disable this behavior and return a generic authentication failure instead.

## 9. Changing an Aurora Key

Changing the Aurora Key:

1. requires an authenticated session and re-authentication according to policy;
2. creates a new salt/verifier record;
3. atomically activates the new credential;
4. revokes or retires the old verifier;
5. does not change `user_id`;
6. does not move or recreate the profile.

A failed rotation must leave the previous valid credential intact.

## 10. Credential migration

When parameters become obsolete, Aurora may upgrade the verifier after a successful authentication:

1. verify using stored parameters;
2. derive a new verifier with current parameters while the raw credential is still available in protected memory;
3. transactionally replace the verifier record;
4. clear the raw credential buffer.

Migration must never require recovering the old Aurora Key from storage.

## 11. Recovery relationship

Aurora Key recovery does not mean decrypting the old key.

If a user loses the Aurora Key but completes an authorized recovery flow, Aurora creates a **new** Aurora Key/verifier and revokes the previous one.

## 12. Interaction with Identity Drive

An Aurora Identity Drive never stores the Aurora Key.

The drive receives its own independent authenticator credential bound to the same `user_id`.

This allows:

- Aurora Key login if the drive is absent;
- Identity Drive login without typing the Aurora Key;
- independent revocation of a lost drive;
- Aurora Key rotation without rewriting every drive unless policy explicitly requires re-enrollment.

## 13. Logging and telemetry restrictions

Never log:

- raw or normalized Aurora Key;
- verifier output;
- salt + verifier as a combined debug dump;
- partial key prefixes/suffixes;
- generated key previews.

Login telemetry, if introduced, should be limited to non-secret aggregate states such as authentication method class, success/failure, and latency.

## 14. Acceptance criteria

Production Aurora Key support is complete only when:

- secure RNG is available;
- Argon2id implementation/dependency has been reviewed;
- parameters are benchmarked on supported hardware;
- verifier storage is persistent and protected;
- rate limiting survives reboot;
- credential rotation is transactional;
- crash/log paths are tested for secret leakage;
- offline login works with networking disabled.
