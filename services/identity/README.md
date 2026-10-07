# Aurora Identity Core and Ring 3 Runtime

Status: **Live Aurora OS integration present; security-sensitive core remains independently host-tested**

This directory contains the Aurora Identity implementation shared by host tests and the live freestanding Ring 3 Identity Service.

The earlier description of this code as "not yet wired into Aurora OS login/session startup" is obsolete. The live OS now uses this implementation through the supervised Ring 3 Identity runtime, capability-authorized IPC, persistent protected state, one-time session grants and the Session Manager boundary.

## Architectural split

The Identity subsystem is intentionally divided into:

1. **portable/security-sensitive core**
   - identity and credential records;
   - Aurora Key normalization;
   - Argon2id verifier operations;
   - opaque lookup tags;
   - persistent-store logic;
   - throttling;
   - rotation;
   - session grants;
   - purpose-bound re-authentication proofs;

2. **Aurora Ring 3 runtime**
   - freestanding service entry/runtime;
   - kernel-provided startup/dependency capabilities;
   - entropy handoff;
   - Protected System State transport;
   - IPC request loop;
   - live AUTH/CREATE/session-grant operations;

3. **kernel/session integration**
   - service supervision;
   - login client;
   - Session Manager;
   - profile bootstrap/delegation;
   - lock/unlock/logout/terminal session lifecycle.

The portable core remains separately testable so cryptographic/storage behavior can be validated independently of the OS bootstrap path.

## Implemented core foundations

The implementation includes:

- canonical Aurora Key normalization;
- stable opaque \`user_id\` and independent \`credential_id\`;
- persistent identity roles;
- progressive authentication throttling;
- fail-closed authentication flow;
- atomic identity + first Aurora Key creation;
- first-user Administrator assignment inside the persistent transaction;
- duplicate-key conflict handling;
- Aurora Key rotation while preserving stable user identity;
- one-time bounded session grants;
- purpose-bound, single-use re-authentication proofs;
- dual-slot persistent identity store with corruption fallback;
- SHA-256 and HMAC-SHA256;
- constant-time comparison;
- HMAC-DRBG;
- domain-separated lookup/session token tags;
- pinned official Argon2 reference implementation;
- RFC 9106 Argon2id validation;
- create-once machine-root-secret provisioning;
- domain-separated derivation of the persistent lookup key;
- secret-buffer clearing.

## Live Aurora OS integration

The freestanding Identity runtime is now connected to the OS.

Current live path:

\`\`\`text
Aurora Key / create request
 -> kernel login client
 -> capability-authorized Ring 3 Identity Service
 -> protected persistent Identity state
 -> Argon2id / lookup-tag / throttling policy
 -> one-time session grant
 -> Ring 3 Session Manager
 -> stable user_id binding
 -> persistent profile capability
 -> ordinary Ring 3 User Session Host
\`\`\`

The Session Manager and User Session Host do not receive the raw Aurora Key or stored verifier material.

Implemented live integration includes:

- supervised Ring 3 Identity process lifecycle;
- blocking IPC service operation;
- capability-authorized protected-state access;
- capability-authorized entropy handoff;
- persistent identity/credential state;
- asynchronous authentication and first-user creation;
- one-time session-grant production path;
- service restart generation handling;
- Session Manager consume boundary;
- lock/unlock re-authentication through a fresh Identity grant.

## Persistent store

The Identity store uses generation-based durable publication and validates records before selecting the active generation.

Persisted state includes:

- stable identity ID;
- identity status/role/policy metadata;
- credential ID and user binding;
- opaque lookup tag;
- KDF metadata;
- salt/verifier;
- credential status;
- durable failed-attempt count.

Raw Aurora Keys are never persisted.

A monotonic throttle deadline is not reused across reboot epochs. Durable escalation state is persisted and the service re-arms the appropriate penalty in the new clock epoch.

## Cryptographic foundation

Current foundations include:

- qualified kernel entropy seed source policy;
- controlled Ring 3 entropy handoff;
- HMAC-DRBG;
- SHA-256/HMAC-SHA256;
- domain-separated lookup tags;
- Argon2id verifier provider;
- explicit KDF parameter bounds;
- constant-time comparison;
- machine-root-secret derivation.

The pinned Argon2 implementation remains a third-party dependency and must continue to be reviewed/updated deliberately rather than silently.

## Machine Identity root secret

Normal startup follows fail-closed rules:

\`\`\`text
no valid secret -> provision once
one valid generation -> load it
recoverable replica damage -> use valid replica
conflicting/corrupt state with no trustworthy choice -> fail closed
\`\`\`

The machine root derives the Identity lookup key using domain separation.

Hardware-backed sealing/TPM binding is not yet implemented and the current design must not claim resistance to an attacker with unrestricted offline raw-disk access equivalent to a hardware-sealed credential store.

## Session and re-authentication

Successful authentication produces an opaque one-time session grant rather than exposing identity secrets.

Purpose-bound re-authentication proof logic is also implemented as a bounded, single-use core for future sensitive management operations.

The remaining work is primarily live operation-by-operation integration and UI, not invention of the proof primitive.

## Tests

The Identity subsystem keeps host tests for security-sensitive logic, including:

- identity creation and conflict handling;
- authentication;
- throttling and reopen behavior;
- persistent-store corruption handling;
- Aurora Key rotation;
- session-grant issue/consume/replay;
- re-authentication proofs;
- SHA/HMAC/HMAC-DRBG known-answer tests;
- Argon2id known-answer tests and parameter rejection;
- machine-secret provisioning/recovery/fail-closed behavior.

The full Aurora OS CI additionally exercises the Ring 3 runtime and session boundaries in QEMU.

## Remaining production-readiness work

The subsystem is integrated, but not production-certified.

Important remaining work includes:

- hardware-backed secret sealing/TPM-class integration;
- complete security/audit event pipeline;
- broader fuzzing and parser abuse testing;
- independent security review;
- real-hardware entropy/storage failure validation;
- final Argon2 policy tuning by supported hardware class;
- recovery credentials;
- removable Identity Drive and hardware authenticator support;
- production Ring 3 use of purpose-bound re-authentication proofs for every sensitive management action;
- compositor-backed Identity System App;
- enterprise/multi-device features only after core local security is complete.

## Build and test

The Argon2 reference implementation is a pinned Git submodule.

A fresh checkout must initialize submodules:

\`\`\`sh
git submodule update --init --recursive
\`\`\`

Run:

\`\`\`sh
make identity-test
\`\`\`

or:

\`\`\`sh
make -C services/identity test
\`\`\`

Aurora-owned host code is compiled as strict C11 with warning-as-error settings.
