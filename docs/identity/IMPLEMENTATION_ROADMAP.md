# Aurora Identity Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.3**

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
- secure comparison helpers;
- cryptographic integrity/MAC/signature primitives needed by authenticators;
- secret-buffer handling conventions;
- benchmarked Argon2id parameters for supported hardware classes.

Acceptance gate:

No placeholder/test cryptography remains in any path described as production authentication.

## Phase E — Aurora Identity Service v1

Required:

- isolated Ring 3 service process;
- service startup/lifecycle model;
- capability-protected IPC endpoint;
- protected identity database;
- stable `user_id` creation;
- Aurora Key verifier creation/check;
- persisted rate limiting;
- local identity creation transaction;
- credential rotation;
- audit events;
- one-time session grant issuance.

Acceptance gate:

A local user can reboot, enter a valid Aurora Key, authenticate fully offline, and receive a valid session grant from the user-space Identity Service.

## Phase F — Session Manager and profile bootstrap

Required:

- Session Manager service;
- one-time identity grant consumption;
- profile reference/mount/open path;
- user capability issuance;
- session lifecycle;
- lock/unlock;
- logout;
- re-authentication proofs for sensitive actions.

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

### Security

- brute-force throttling;
- account enumeration;
- session replay;
- stale/revoked authenticator use;
- secret leakage in logs/crashes;
- unauthorized IPC caller;
- malicious removable-media parser input;
- recovery abuse.

### Performance

- cold authentication latency;
- Argon2id cost/memory consumption;
- login UI responsiveness;
- Identity Service idle wakeups;
- removable-drive detection latency;
- session startup latency.

## Current next development gate

The repository has completed the Phase A/B prototypes. The next production-enabling work is **not** more login UI logic inside the kernel.

The correct dependency order is:

1. storage/VFS foundation;
2. first durable protected system state;
3. isolated user-space service execution/lifecycle;
4. secure RNG + reviewed Argon2id path;
5. Aurora Identity Service v1;
6. Session Manager;
7. compositor-backed Aurora Identity System App;
8. USB mass-storage/removable broker integration for Identity Drive.
