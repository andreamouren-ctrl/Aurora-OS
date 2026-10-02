# Aurora Identity Core

Status: **isolated implementation foundation**

This directory contains the implementation layer of Aurora Identity that is intentionally **not yet wired into Aurora OS login/session startup**.

The goal is to build and verify the security-sensitive identity logic behind explicit platform interfaces before binding it to the real Ring 3 service lifecycle, persistent protected storage, IPC transport, compositor UI, or session manager.

## Implemented foundations

The isolated core now implements:

- canonical Aurora Key normalization (`[A-Z0-9]{12,32}` with spaces/hyphens accepted as visual separators);
- bounded credential structures;
- stable opaque `user_id` and independent `credential_id` types;
- versioned Argon2id parameter metadata contract;
- identity/credential record state (`active`, `disabled`, `recovery-required`);
- configurable progressive throttling policy;
- authentication flow that resolves a candidate, checks throttling, delegates cryptographic verification, records failures, clears prior failure state on success, and fails closed on backend errors;
- explicit interfaces for crypto, secure randomness, protected storage, and monotonic time;
- atomic first-identity/first-key creation contract;
- bounded retry when a random identifier resolves to the reserved all-zero value;
- duplicate Aurora Key preflight plus mandatory commit-time uniqueness handling;
- explicit secret-buffer clearing helper;
- host-side tests for normalization, authentication, throttling, creation, duplicate/race handling, and backend/crypto/RNG failures.

## Transactional identity creation

`aurora_identity_create_with_key()` performs the first core account-creation transaction without depending on a concrete database.

Conceptual flow:

```text
candidate Aurora Key
 -> normalize
 -> derive protected lookup tag
 -> preflight uniqueness check
 -> generate nonzero user_id
 -> generate nonzero credential_id
 -> generate random salt
 -> derive versioned verifier
 -> atomic store create(identity + first key credential)
 -> publish success
```

The storage adapter exposes `create_identity_with_key()` as one atomic publication boundary. A production backend must guarantee that:

- identity and first credential become visible together;
- neither becomes visible when the transaction fails;
- lookup-tag uniqueness is enforced at commit time;
- `user_id` and `credential_id` uniqueness are enforced at commit time;
- a race between preflight lookup and commit becomes `CREATE_CONFLICT`, never duplicate state.

The core maps a commit conflict to `AURORA_IDENTITY_ALREADY_EXISTS` and fails closed on every other storage failure.

## Secure randomness boundary

The core does not provide its own PRNG. `fill_random()` is an explicit platform provider and production account creation must not be enabled until Aurora has a reviewed secure RNG.

Secure randomness is required for at least:

- `user_id`;
- `credential_id`;
- Aurora Key verifier salt;
- future session grants, recovery credentials, challenges and authenticator keys.

An all-zero generated identifier is reserved as invalid. The core retries a bounded number of times and fails with `AURORA_IDENTITY_RANDOM_ERROR` rather than publishing an invalid record.

## Credential lookup tag

Aurora's default login intentionally has no public username field. The service therefore needs a way to locate the candidate credential record before running its expensive verifier.

The core exposes `derive_lookup_tag()` as a crypto-provider operation. A production provider must derive an **opaque keyed lookup tag** from the normalized Aurora Key using a protected machine/service secret (for example, a reviewed PRF construction). Storing a plain deterministic hash of the Aurora Key is explicitly not acceptable because a copied database would then provide an efficient offline guessing oracle.

The exact cryptographic construction is intentionally not implemented here; it must be selected together with Aurora's reviewed crypto/secure-storage layer.

## Test providers

The files under `tests/` contain deterministic stand-ins for lookup, verifier derivation/verification, randomness, time, and transactional storage. They exist only to exercise core control flow.

They are **not** linked into Aurora OS and are **not** suitable for production authentication.

The creation tests verify, among other cases:

- successful identity + first credential publication;
- immediate authentication of the newly created record;
- duplicate-key rejection before expensive verifier work;
- commit-time uniqueness races;
- no partial publication on store failure;
- failure on RNG or verifier-provider errors;
- bounded retry/rejection of reserved zero identifiers;
- rejection of invalid production creation policy.

## Not implemented yet

This layer still deliberately does not implement:

- production Argon2id itself;
- the production opaque lookup-tag primitive;
- production cryptographic RNG;
- a durable identity database backend;
- credential rotation transactions;
- session-grant generation/consumption;
- recovery credentials;
- Aurora Identity Drive;
- OS IPC transport;
- Ring 3 process/service startup;
- graphical UI.

Those dependencies remain separate so no placeholder cryptography or fake persistence is accidentally promoted into the real authentication path.

## Build and test

From the repository root:

```sh
make identity-test
```

or directly:

```sh
make -C services/identity test
```

The host build uses ordinary C11 and has no dependency on the kernel. This is intentional: the same logic can later be embedded in the Aurora Identity Ring 3 service behind Aurora-native platform adapters.

## Integration gate

The isolated core should only be connected to the real Aurora OS login path after at least:

1. protected durable system state exists;
2. Aurora has a reviewed secure RNG and credential KDF/lookup-tag provider;
3. the Ring 3 service lifecycle and capability-authorized IPC transport exist;
4. the persistent identity store implements the atomic contracts and crash recovery;
5. a Session Manager can consume non-replayable session grants.

Until then, the existing framebuffer Aurora Identity UI remains a bootstrap/recovery prototype and must not claim production authentication.
