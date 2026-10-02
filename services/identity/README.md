# Aurora Identity Core

Status: **isolated implementation foundation**

This directory contains the first implementation layer of Aurora Identity that is intentionally **not yet wired into Aurora OS login/session startup**.

The goal is to build and verify the security-sensitive identity logic behind explicit platform interfaces before binding it to the real Ring 3 service lifecycle, persistent protected storage, IPC transport, compositor UI, or session manager.

## What this milestone implements

- canonical Aurora Key normalization (`[A-Z0-9]{12,32}` with spaces/hyphens accepted as visual separators);
- bounded credential structures;
- stable opaque `user_id` type;
- versioned Argon2id parameter metadata contract;
- credential-record state (`active`, `disabled`, `recovery-required`);
- configurable progressive throttling policy;
- authentication flow that resolves a candidate, checks throttling, delegates cryptographic verification, records failures, clears prior failure state on success, and fails closed on backend errors;
- explicit interfaces for crypto, protected storage, and monotonic time;
- secret-buffer clearing helper;
- host-side unit tests for the core state/normalization/authentication behavior.

## What this milestone deliberately does not implement

- Argon2id itself;
- a production credential lookup primitive;
- cryptographic RNG;
- the persistent identity database;
- session-grant generation/consumption;
- account creation or credential rotation transactions;
- Aurora Identity Drive;
- OS IPC transport;
- Ring 3 process/service startup;
- graphical UI.

Those dependencies remain separate so no placeholder cryptography or fake persistence is accidentally promoted into the real authentication path.

## Credential lookup tag

Aurora's default login intentionally has no public username field. The service therefore needs a way to locate the candidate credential record before running its expensive verifier.

The core exposes `derive_lookup_tag()` as a crypto-provider operation. A production provider must derive an **opaque keyed lookup tag** from the normalized Aurora Key using a protected machine/service secret (for example, a reviewed PRF construction). Storing a plain deterministic hash of the Aurora Key is explicitly not acceptable because a copied database would then provide an efficient offline guessing oracle.

The exact cryptographic construction is intentionally not implemented here; it must be selected together with Aurora's reviewed crypto/secure-storage layer.

## Test providers

`tests/test_identity_core.c` contains deterministic stand-ins for lookup and verification. They exist only to exercise core control flow and are clearly test-only. They are not linked into Aurora OS and are not suitable for production authentication.

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
2. Aurora has a reviewed secure RNG and credential KDF provider;
3. the Ring 3 service lifecycle and capability-authorized IPC transport exist;
4. the persistent identity store can update verifier/throttle state transactionally;
5. a Session Manager can consume non-replayable session grants.

Until then, the existing framebuffer Aurora Identity UI remains a bootstrap/recovery prototype and must not claim production authentication.
