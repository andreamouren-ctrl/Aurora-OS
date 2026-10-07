# Aurora Identity Implementation Roadmap

Status: **Canonical implementation plan**
Version: **0.5**

This roadmap orders Aurora Identity work by hard technical dependencies. It supplements the global Aurora OS roadmap.

## Phase A — Bootstrap login surface

Status: **Complete for framebuffer bootstrap/recovery path**

Implemented:

- boot-to-login framebuffer handoff;
- dedicated Aurora Identity login UI;
- resolution-aware rendering;
- full-quality Aurora artwork pipeline;
- masked Aurora Key field;
- authentication/create/session/lock/error states;
- no pre-auth user enumeration;
- framebuffer fallback retained independently of the future compositor UI.

The framebuffer path is now a bootstrap/recovery surface, not the intended final desktop login presentation.

## Phase B — Input foundation

Status: **PS/2 bootstrap complete; modern input active under M4 G4**

Implemented:

- normalized kernel input-event queue;
- keyboard interrupt path;
- PS/2 Set 1 keyboard support;
- A-Z / 0-9 / Backspace / Enter / Esc translation;
- Aurora Key input controller;
- normalized uppercase credential buffer;
- secret clearing on submission/cancel paths;
- normalized device-independent event foundation;
- initial IRQ12 PS/2 mouse packet path;
- explicit separation of pointer events from credential handling.

Remaining before broad hardware support:

- USB HID keyboard/mouse;
- layout/input-method abstraction;
- compositor focus/capture completion;
- touch/multitouch/pen;
- accessibility input paths.

## Phase C — Persistent storage substrate

Status: **Foundation complete for Identity v1 path**

Implemented dependencies:

- generic block-device layer;
- AHCI and NVMe baseline persistent transports;
- VFS/mount manager;
- real AuroraFS v2 \`/system\` mount;
- explicit durability operations;
- protected service-owned system state;
- fail-closed lookup semantics;
- capability-gated Ring 3 Protected System State record bridge.

The remaining storage work is hardening/performance/hardware breadth rather than absence of a persistent Identity substrate.

## Phase D — Secure primitives

Status: **Production-path foundation implemented; continued review required**

Implemented:

- kernel entropy seed foundation with qualified-source policy;
- capability-authorized Ring 3 entropy handoff;
- HMAC-DRBG lifecycle;
- SHA-256 / HMAC-SHA256;
- constant-time comparison;
- domain-separated lookup tags;
- machine-root-secret provisioning foundation;
- official pinned Argon2 reference implementation;
- Argon2id provider and known-answer tests;
- explicit parameter bounds;
- secret-buffer clearing conventions.

Remaining maturity work:

- broader hardware entropy validation;
- final per-hardware-class Argon2 tuning policy;
- hardware-backed sealing/TPM integration;
- independent security review/fuzzing of production parsers and failure paths.

## Phase E — Aurora Identity Service v1

Status: **Live Ring 3 implementation integrated; not production-certified**

Implemented in the live OS:

- isolated Ring 3 Identity Service runtime;
- supervised service lifecycle;
- capability-protected IPC endpoint;
- protected persistent identity database path;
- stable user_id and independent credential_id;
- canonical Aurora Key normalization;
- opaque protected lookup tags;
- Argon2id verifier creation/check;
- reboot-persistent throttling/escalation foundation;
- first-user bootstrap creation policy;
- atomic identity + first credential publication;
- Aurora Key rotation core;
- one-time session-grant issue/consume core;
- capability-gated session-grant consume protocol;
- fail-closed storage/crypto/time handling;
- production login client integration;
- asynchronous account-creation path;
- service generation/restart handling.

The Phase E acceptance path exists: a local user can authenticate offline through the Ring 3 service and produce a one-time session grant.

Remaining before calling Aurora Identity production-ready:

- comprehensive audit/security event pipeline;
- hardware-backed machine-secret sealing;
- broader credential/recovery/authenticator support;
- full fuzzing and abuse review;
- real-hardware certification;
- production UI migration from framebuffer fallback to the compositor-backed Identity System App.

## Phase F — Session Manager and profile bootstrap

Status: **Core lifecycle implemented; desktop bootstrap still pending**

Implemented:

- separate Ring 3 Session Manager;
- direct one-time Identity session-grant consumption;
- stable user_id binding before session activation;
- bounded dependency-capability startup ABI;
- fail-closed service-to-service grant handling;
- persistent profile-root mechanism on AuroraFS;
- profile directory derived from stable user_id, never Aurora Key/display name;
- AURORA_CAP_FILE profile authority only after successful Identity binding;
- reduced profile delegation without CONTROL/TRANSFER to ordinary session processes;
- bounded session profile lease tracking;
- ordinary Ring 3 User Session Host;
- READY gate before session-active presentation;
- logout with source/delegated capability revocation;
- lock with interactive lease revocation;
- unlock requiring a fresh same-user Identity grant;
- wrong-user unlock rejection;
- User Session Host restart with fresh reduced authority after successful unlock;
- fail-closed abnormal session termination;
- Session Manager loss/restart invalidates the old session authority;
- purpose-bound re-authentication proof core for sensitive actions.

Remaining:

- production Ring 3 issuance/consumption of re-authentication proofs for each sensitive management operation;
- profile/settings service above the profile capability;
- compositor/Desktop Shell bootstrap above the User Session Host;
- full desktop/service teardown once those session services exist.

Acceptance gate:

Successful Identity authentication starts the correct persistent user profile without exposing credential material to the desktop or ordinary applications.

**Current result: core non-desktop acceptance path implemented.**

## Phase G — Aurora Identity System App

Status: **Waiting on G5/G6 desktop integration; graphics dependencies now substantially available**

Graphics G1-G3 are complete and G4 input is active.

Remaining:

- compositor-backed pre-session app mode;
- final login surface migration from framebuffer to compositor;
- profile/account-management UI;
- Aurora Key rotation UI;
- Access Devices UI;
- Recovery UI;
- purpose-bound re-authentication management UI;
- Security Activity UI;
- fallback to framebuffer/recovery when normal graphics is unavailable.

Acceptance gate:

Cold boot can authenticate through the normal compositor UI, enter a user desktop only after session readiness, lock/unlock safely, and still reach framebuffer recovery when the normal graphics stack is disabled.

## Phase H — Aurora Identity Drive v1

Dependencies still missing:

- USB/xHCI host stack;
- USB mass-storage/removable-device events;
- removable-media broker.

Identity-side policy/credential contracts are already documented.

Required:

- Identity Drive enrollment;
- versioned credential container;
- machine-bound standard-drive credential;
- insertion/removal authentication;
- optional PIN mode;
- multiple drives;
- friendly labels;
- revocation;
- malformed-media fuzzing.

## Phase I — Recovery v1

Required:

- local high-entropy recovery credential;
- recovery verification/throttling;
- forced Aurora Key rotation after recovery;
- recovery audit events;
- recovery mode in fallback/compositor UI;
- identity database integrity/recovery integration.

## Phase J — Secure hardware authenticators

Required:

- USB HID/security-key transport or equivalent;
- public-key authenticator records;
- challenge-response API;
- non-exportable-key UX classification;
- enrollment/revocation.

FIDO2-class interoperability remains a future standards decision.

## Phase K — Multi-device Aurora Identity

Optional future layer:

- trusted Aurora device enrollment;
- encrypted settings/profile synchronization;
- cross-device authenticator/recovery approval;
- device revocation;
- conflict-safe credential metadata synchronization;
- optional network federation.

Invariant: local login remains functional offline.

## Phase L — Advanced identity/privacy/continuity capabilities

Future capability families include:

- Ghost Session;
- Guest Identity;
- Identity Vault;
- Session Seal / Instant Lock / Lock Zones;
- one-time/temporary credentials;
- Travel Mode;
- device identity and pairing;
- Identity Capsule / Migration / Handoff;
- Trusted Circle / threshold recovery;
- local Risk Engine;
- Profile Layers.

No advanced feature may weaken the offline local authentication path or bypass Identity Service policy.

## Verification matrix

Every production phase should cover:

### Correctness

- valid/invalid credential;
- reboot persistence;
- first-user creation;
- credential rotation;
- session-grant single use/replay rejection;
- session profile binding;
- logout revocation;
- lock/wrong-user unlock/same-user unlock;
- service restart generation boundaries.

### Failure handling

- service crash mid-authentication;
- database write interruption;
- corrupted identity record;
- entropy/crypto failure;
- storage unavailable;
- malformed IPC;
- Session Manager failure;
- compositor failure;
- clock/time anomalies relevant to throttling;
- future malformed removable credentials.

## Production-readiness rule

A feature being integrated into the live Ring 3 path does not make the full Identity subsystem production-certified.

Production readiness requires real-hardware validation, fuzzing, abuse review, recovery validation, auditability and hardware-backed protection appropriate to the supported device class.
