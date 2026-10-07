# Aurora Identity Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.4**

This roadmap orders Aurora Identity work by hard technical dependencies. It supplements the global Aurora OS roadmap.

## Phase A — Bootstrap login surface

Status: **Implemented prototype**

Completed:

- boot-to-login framebuffer handoff;
- dedicated Aurora Identity login UI;
- resolution-aware framebuffer rendering;
- masked Aurora Key field;
- login status states;
- no fake production authentication.

## Phase B — Real keyboard input

Status: **Implemented prototype**

Completed:

- generic kernel input event queue;
- dedicated keyboard interrupt vector;
- IRQ-driven PS/2 Set 1 prototype;
- A-Z / 0-9 / Backspace / Enter / Esc translation;
- Aurora Key input controller;
- normalized uppercase credential buffer;
- minimum/maximum length policy handling;
- secret clearing on submission/cancel paths.

Remaining before broad hardware support:

- USB HID keyboard stack;
- layout/input-method abstraction;
- pointer/touch input where relevant;
- accessibility input path.

## Phase C — Persistent storage substrate

Dependency: global storage/VFS milestone.

Substantial generic storage foundations now exist in Aurora OS (block layer, VFS, AuroraFS, AHCI/NVMe paths), but the Identity production gate remains **protected durable system state with transactional semantics and service access control**.

Required:

- block-device layer suitable for persistent system state;
- VFS;
- initial filesystem support;
- protected system-data location;
- transactional file/database writes;
- system service access controls;
- monotonic timestamps suitable for persisted throttling metadata.

Acceptance gate:

Aurora can persist protected system records across reboot without exposing them to ordinary user processes.

## Phase D — Secure primitives

Required:

- cryptographically secure RNG service/primitive;
- reviewed Argon2id implementation or dependency;
- keyed PRF/provider for opaque Aurora Key lookup tags;
- secure comparison helpers;
- cryptographic integrity/MAC/signature primitives needed by authenticators;
- secret-buffer handling conventions;
- benchmarked Argon2id parameters for supported hardware classes.

Acceptance gate:

No placeholder/test cryptography remains in any path described as production authentication.

## Phase E — Aurora Identity Service v1

### Isolated core progress — implemented, not OS-connected

The repository now contains a host-testable C11 Identity core under `services/identity/` with no dependency on the live login path.

Implemented and CI-tested:

- canonical Aurora Key normalization;
- opaque lookup-tag provider boundary;
- stable `user_id` and independent `credential_id` contracts;
- KDF/verifier metadata contracts;
- authentication control flow with persisted-throttle adapter hooks;
- fail-closed crypto/storage/time error handling;
- secure temporary-buffer clearing;
- secure-RNG provider boundary;
- first identity + first Aurora Key creation flow;
- atomic `create_identity_with_key()` storage publication contract;
- duplicate preflight plus commit-time conflict handling;
- bounded rejection/retry of reserved all-zero generated identifiers;
- deterministic host tests using explicitly non-production crypto/RNG/store providers.

This does **not** make Phase E complete. The live OS still has no production Identity Service or real credential database.

### Remaining production requirements

- isolated Ring 3 service process;
- service startup/lifecycle model;
- capability-protected IPC endpoint;
- protected transactional identity database backend;
- production stable-ID generation via reviewed secure RNG;
- production opaque lookup-tag PRF;
- production Argon2id verifier creation/check;
- reboot-persisted rate limiting;
- credential rotation transaction;
- audit events;
- one-time session grant issuance.

Acceptance gate:

A local user can reboot, enter a valid Aurora Key, authenticate fully offline, and receive a valid session grant from the user-space Identity Service.

## Phase F — Session Manager and profile bootstrap

Status: **In progress**

Implemented:

- separate Ring 3 Session Manager service;
- one-time Identity session-grant consumption through the Identity Service;
- stable `user_id` binding before session activation;
- bounded service dependency-capability startup ABI;
- fail-closed service-to-service grant rejection paths;
- persistent profile-root mechanism on AuroraFS;
- profile directory derived from stable `user_id`, never Aurora Key or display name;
- `AURORA_CAP_FILE` profile capability issued only after successful Identity binding;
- validation probe confirming the transferred profile capability is bound to the authenticated `user_id`;
- logout path that revokes the Session Manager's source profile authority and the session bridge's delegated profile capability before returning to pre-session login;
- session-scoped profile capability delegation to user/desktop processes with reduced rights;
- bounded lease tracking for delegated profile capabilities;
- explicit revocation of all delegated process profile capabilities before the active session profile authority is dropped;
- ordinary Ring 3 User Session Host bootstrap from the active session;
- User Session Host receives only a private control IPC endpoint, stable user/session metadata, and a reduced profile capability;
- login reaches session-active presentation only after the User Session Host publishes READY;
- User Session Host is stopped and its delegated profile lease is revoked before Session Manager logout completes;
- session lock preserves the bound profile/user_id while revoking interactive delegated profile leases;
- unlock requires a fresh one-time Identity grant for the same stable user_id;
- wrong-user unlock grants are rejected without changing the locked session;
- successful same-user unlock recreates the profile lease and restarts the ordinary Ring 3 User Session Host.

Remaining:

- compositor/desktop process bootstrap above the User Session Host once M4 graphics foundations exist;
- complete session lifecycle state machine beyond active/lock/unlock/logout;
- purpose-bound re-authentication proofs for sensitive actions;
- final profile/settings service bootstrap above the profile capability.

Acceptance gate:

Successful Identity Service authentication starts the correct persistent user profile without exposing credential material to the desktop or applications.

## Phase G — Aurora Identity System App

Dependency: graphics/compositor/native UI platform.

Required:

- compositor-backed pre-session app mode;
- login UI using Identity Service IPC;
- profile setup;
- account-management UI;
- Aurora Key rotation UI;
- Access Devices UI;
- Recovery UI;
- lock/re-authentication mode;
- Security Activity UI;
- fallback to framebuffer/recovery surface if the normal graphics stack fails.

Acceptance gate:

The normal login path no longer depends on direct framebuffer UI, while the fallback remains functional.

## Phase H — Aurora Identity Drive v1

Dependencies:

- USB host controller support;
- USB mass-storage support;
- removable block-device events;
- VFS/filesystem support;
- secure RNG and crypto primitives;
- Identity Service authenticator APIs;
- Session Manager.

Required:

- Identity Drive enrollment;
- versioned credential container;
- brokered removable-media scanning;
- machine-bound standard-drive credential;
- insertion/removal authentication events;
- optional PIN mode;
- multiple enrolled drives;
- per-drive friendly label;
- revocation;
- malformed-media handling/fuzzing;
- security-activity logging without secrets.

Acceptance gate:

A user can enroll a standard USB drive, reboot, authenticate using the drive without typing the Aurora Key, revoke the drive later, and continue using the Aurora Key independently.

## Phase I — Recovery v1

Required:

- local high-entropy recovery credential;
- recovery verification and throttling;
- forced Aurora Key rotation after recovery;
- recovery audit events;
- recovery mode in the fallback UI;
- identity database integrity/recovery integration.

Acceptance gate:

A user who loses the Aurora Key but still holds an enrolled recovery credential can safely replace the Aurora Key without the system ever decrypting/revealing the old one.

## Phase J — Secure hardware authenticators

Required:

- USB HID/security-key protocol support or equivalent;
- public-key authenticator record support;
- challenge-response API;
- non-exportable-key UX classification;
- device enrollment/revocation.

Potential standards include FIDO2-class authenticators, subject to later interoperability decisions.

Acceptance gate:

Aurora supports at least one hardware-backed authenticator where the private key cannot be exported by ordinary host software.

## Phase K — Multi-device Aurora Identity

Optional future layer.

Potential scope:

- trusted Aurora device enrollment;
- encrypted settings/profile synchronization;
- cross-device authenticator/recovery approval;
- device revocation;
- conflict-safe credential metadata synchronization;
- optional network account/federation.

Invariant: local login remains functional when offline.

## Phase L — Advanced identity, privacy and continuity capabilities

These features are future capability families, not requirements for Aurora Identity v1. They are documented early so core interfaces do not block them later.

Potential scope:

- Aurora Ghost Session;
- Guest Identity;
- Identity Vault;
- Session Seal and Instant Lock;
- Lock Zones and purpose-bound re-authentication;
- one-time/temporary access credentials;
- Travel Mode;
- cryptographic Aurora device identity;
- QR device pairing;
- trusted-device login approval;
- Aurora Identity Capsule;
- Aurora Handoff;
- Aurora Presence;
- Trusted Circle / threshold recovery;
- local Risk Engine;
- Profile Layers.

Detailed contracts and safety invariants are defined in [`ADVANCED_FEATURES.md`](ADVANCED_FEATURES.md).

Implementation order inside this phase must follow actual subsystem maturity. No advanced feature may weaken the offline local authentication path or bypass Identity Service policy.

## Test matrix

Every production phase should include automated/manual tests for:

### Correctness

- successful login;
- invalid credential;
- reboot persistence;
- credential rotation;
- authenticator revocation;
- session grant consumption;
- lock/unlock/logout.

### Failure handling

- service crash mid-authentication;
- database write interruption;
- corrupted identity record;
- malformed USB credential container;
- USB removal during authentication;
- compositor failure;
- storage unavailable;
- clock/time anomalies relevant to throttling.
