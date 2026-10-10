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

Status: **Bootstrap + G4 V1 input foundation complete**

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

- layout/input-method abstraction;
- touch/multitouch/pen/gamepad;
- accessibility input paths;
- broader USB class/hardware coverage beyond the runtime-verified xHCI HID keyboard/mouse baseline.

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

Status: **Core lifecycle implemented; G5 WP-03/WP-04 desktop window bootstrap verified; Identity management UX pending**

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
- purpose-bound re-authentication proof core for sensitive actions;
- authenticated Ring 3 Shell bootstrap/session lifecycle above the User Session Host through the frozen G5 WP-03 baseline, including scene publication to compositor/display plus clean-stop, crash, stale-generation and fresh-reauthentication gates.

Remaining:

- production Ring 3 issuance/consumption of re-authentication proofs for each sensitive management operation;
- profile/settings service above the profile capability;
- full app/launcher/task-management and spatial camera services beyond the merged G5 WP-04 window-policy baseline (WP-05+);
- full desktop/service teardown once those higher-level session services exist.

Repository hygiene rule: historical experimental Identity re-authentication branches are not canonical integration targets. New Phase F work must start from the current `main` baseline and port only the required reviewed changes, avoiding merges from branches that predate the frozen G5/session baseline.

Acceptance gate:

Successful Identity authentication starts the correct persistent user profile without exposing credential material to the desktop or ordinary applications.

**Current result: core non-desktop acceptance path implemented.**

## Phase G — Aurora Identity System App

Status: **G0 official system font and native Identity typography integrated/runtime regression-tested; pre-session secure compositor Identity integration still pending**

### G0 — Canonical Aurora UI typography

- [x] Original Aurora Celestia UI 1.0 font binaries committed and SHA-256 verified.
- [x] Official family/style contract and host-generated bitmap atlas pipeline implemented on Identity typography feature branch.
- [x] Identity login, lock and state text connected to native Celestia drawing with independent bitmap recovery fallback (PR #188 merged; CI/QEMU validated).
- [x] Complete native kernel build and QEMU/session regression gates on the verified merge candidate.
- [ ] Complete font visual/readability acceptance for 10–18 px, contrast modes and localized accents; CI boot success alone does not certify typography appearance.
- [ ] Add secure compositor-backed pre-session Identity surface and reusable Ring 3 font service (do not place untrusted font parser inside Ring 0).

G0 changes only presentation. They must not change Aurora Key verification, IPC authority, session grants, rate limiting or the framebuffer recovery trust boundary.

### G1 — Trusted presentation handoff and input ownership (merged and QEMU verified, PR #189)

- [x] Introduce a pure trusted decision engine for pre-session, lock, authenticated desktop and quarantine (unknown/inconsistent states fail closed).
- [x] Gate production input dispatch on Session Manager state, live User Session Host and session generation; re-evaluate on every event so lock/logout revocation is immediate.
- [x] Treat a change of trusted input domain as a queue epoch boundary: drain stale desktop keystrokes before unlock/credential entry, including after an in-queue lock shortcut.
- [x] Ensure Identity framebuffer text updates no longer overwrite the active G5 compositor display during the session handoff.
- [x] Revoke a lingering desktop host before trusted credential UI is shown following an externally initiated lock/logout.
- [x] Add bounded host test matrix and a QEMU boot-validation policy test.
- [x] Verify all five CI workflows and QEMU first boot/cold reboot on accepted source commit `f9aa8de5` (merge `6fdfc1cd`, 2026-10-10).
- [ ] Build independent compositor-backed PRE_SESSION/LOCK surfaces with dedicated capability authority and zero profile delegation. This remains **not implemented** by G1 handoff policy.
- [ ] Prove compositor failure/restart and native fallback on real hardware.

CI caveat (2026-10-10): existing standalone Identity entropy QEMU accepts a trusted `-cpu max` configuration, while three storage workflows on `main` still ran QEMU's default CPU model and hit the intentional Ring 3 entropy boot-validation panic. The storage CI remediation must supply a trusted CPU model and retain a separate fail-closed negative test; do **not** suppress the panic in trusted-gate validation builds. Passing the feature PR's original gates is historical evidence, not a claim that every later `main` run is green.

Additional G1 hardening after PR #190: full input-decision and Session Manager-generation epoch fence for the native login pump, with queue purge and partial credential clearing on authority changes. This is not the secure compositor pre-session System App; normal login remains on the independent framebuffer until Phase G2 hosting is separately accepted.

G1 changes only ownership and routing; credentials remain in the kernel's existing login input buffer and are never sent over G5 window IPC. The current visual login and recovery path is **still direct framebuffer**; do not claim a Ring 3 Identity login System App or a trusted pre-session compositor until the isolated host and surfaces are implemented and verified.

Graphics G1-G4 and G5 WP-03/WP-04 are accepted on `main` (WP-04 merge PR #185, 2026-10-10). Two ordinary Ring 3 clients now have real compositor/window lifecycle and input routing. **This does not itself implement trusted pre-session/lock-screen Identity presentation**: the normal compositor Identity System App requires a dedicated secure pre-session mode, credential-input isolation, policy-driven handoff, and a tested framebuffer recovery fallback. Backend/security work can proceed independently.

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

- USB mass-storage/removable-device events above the existing xHCI HID host foundation;
- removable-media broker;
- production removable-filesystem/authenticator event integration.

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
