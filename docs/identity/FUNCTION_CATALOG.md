# Aurora Identity Function Catalogue

Status: **Canonical product capability map**
Version: **0.2**

This document is the consolidated functional map for Aurora Identity. It combines the core v1 design, advanced security/privacy capabilities, device/federation concepts, platform-identity extensions, and local account/authorization policy.

Not every capability ships in v1. The purpose is to keep one authoritative overview of what Aurora Identity is intended to become.

## 1. Core identity architecture

### Aurora Identity System App
The official user-facing system application. It presents login, first-profile setup, credential management, access-device management, recovery, lock/re-authentication, sessions, local-user administration, access management, and security activity.

### Aurora Identity Service
The privileged isolated user-space backend. It owns identity records, authentication policy, verifier operations, throttling, authenticator enrollment/revocation, recovery policy, role metadata, local-account creation policy, and authenticated session-grant issuance.

### Bootstrap / Recovery Login Surface
A minimal framebuffer-based fallback that remains available when the normal compositor or System App cannot start. It provides the security-critical subset needed for authentication and recovery.

### Stable `user_id`
Every person receives a system-generated opaque identity identifier independent from Aurora Key, display name, devices, and authenticators. Credentials can change without recreating the profile.

### Offline-first local identity
Normal login, local profile access, lock/unlock, and supported local recovery remain usable without cloud connectivity.

## 2. Aurora Key authentication

### Aurora Key
The baseline Aurora credential: a normalized alphanumeric secret used to authenticate a local identity without a separate visible username/password pair.

### Aurora Key normalization
Input is normalized to a canonical representation: letters become uppercase, visual separators can be ignored, and locale-dependent transformations are avoided.

### Secure verifier storage
Aurora never stores the plaintext Aurora Key. The target production verifier uses Argon2id with a unique salt and versioned parameters.

### Aurora Key generation
Aurora can generate strong random Keys using a cryptographically secure random source once that subsystem is available.

### Aurora Key rotation
The user can replace the Aurora Key without changing `user_id`, profile, files, role, or enrolled devices. The old verifier is invalidated.

### Credential masking and protected input
Login presentation masks credential characters, clears temporary buffers when practical, and prevents credential material from entering ordinary logs, clipboard history, diagnostics, or application APIs.

### Progressive rate limiting
Repeated failed authentication attempts trigger increasing throttling controlled by Identity Service policy, eventually persisted across reboot.

### Account-enumeration protection
Cold login does not display account tiles by default and should avoid unnecessarily revealing whether a guessed identity exists.

## 3. Removable and hardware authenticators

### Aurora Identity Drive
A normal USB flash drive can be enrolled as an independent removable authenticator. It never stores the user's Aurora Key and can be revoked without affecting the profile.

### Multiple Identity Drives
One identity can enroll several USB authenticators, for example a daily-use drive and an offline backup drive.

### Silent Identity Drive detection
When enabled by policy, inserting a valid enrolled drive can automatically begin authentication without requiring the user to select a login method manually.

### Identity Drive + PIN
A removable authenticator can require an additional PIN or factor before it is accepted, protecting against simple possession of the drive.

### Authenticator revocation
Each enrolled authenticator has its own record and can be individually disabled if lost, stolen, replaced, or no longer trusted.

### Secure hardware authenticator
Aurora can later support FIDO2-class or equivalent secure hardware in which private key material is non-exportable and authentication uses challenge-response.

### Aurora Biometric Bridge
A hardware-agnostic biometric interface for fingerprint, face, or future biometric devices. Where supported, matching happens in secure hardware and Aurora receives only an authenticated assertion, not raw biometric data.

## 4. Authentication policy and MFA

### Configurable authentication policy
Identity Service determines which factors are sufficient for each context. UI code cannot bypass or redefine the policy.

### Multi-factor policy presets
User-friendly modes such as Convenient, Protected, High Security, or Travel combine Aurora Key, Drive, PIN, hardware key, trusted device, or other factors.

### Purpose-bound re-authentication
Sensitive actions can request fresh authentication and receive a short-lived proof valid only for that exact operation instead of granting permanent elevation.

### Login Approval
A previously trusted device can approve enrollment or access by a new Aurora device through an authenticated challenge-bound request.

### Aurora Presence
A trusted phone, wearable, or hardware token can participate in presence policy, such as locking a session when the device leaves. Proximity alone is not treated as sufficient cryptographic proof.

## 5. Session lifecycle

### Authenticated session grant
Successful authentication produces an opaque one-time grant bound to the stable identity. Session Manager consumes it to start the profile without receiving the original credential.

### Session bootstrap
Session Manager opens the correct profile and assigns narrowly scoped capabilities for settings, user files, applications, and system services.

### Lock / unlock
Aurora Identity provides the authentication surface for locking and unlocking an active session while keeping credential policy outside the desktop shell.

### Logout
Logout tears down the authenticated session, releases session capabilities, and returns to the pre-session Aurora Identity surface.

### Session Seal
When a session is locked, selected derived secrets and sensitive grants can be wiped or invalidated and reconstructed only after fresh authentication.

### Instant Lock
A trusted low-latency path immediately locks the session using a shortcut, hardware event, Presence event, or administrative trigger without depending on a responsive desktop application loop.

### Ghost Session
An intentionally ephemeral privacy session with disposable state, caches, temporary secrets, and automatic cleanup at logout/shutdown unless data is explicitly exported.

### Guest Identity
A restricted temporary identity that can last until logout, reboot, expiration, or a configured period and can be given narrowly scoped access.

### One-Time Access
The owner or administrator can issue a temporary credential with defined scope, expiration, maximum-use count, and revocation state.

### Profile Layers
One human identity can contain separate usage contexts such as Personal, Work, Gaming, or Development without requiring separate primary identities.

### Lock Zones
Particularly sensitive areas—Vault, recovery settings, private keys, protected folders—can demand fresh authentication even when the main session is already unlocked.

## 6. Recovery and emergency access

### Recovery credential
A separate high-entropy recovery method allows replacement of a lost Aurora Key without Aurora storing a reversible copy of the old Key.

### Emergency Recovery Kit
Aurora can generate offline recovery material, one-time codes, metadata, and instructions for storage away from the primary computer.

### Aurora Trusted Circle
Several trusted devices or recovery authorities can jointly approve recovery or another sensitive operation using a threshold policy.

### Aurora Key Shards
Threshold recovery shares are generated from a separate recovery secret—not from pieces of the actual Aurora Key. For example, 3 of 5 shares may be required.

### Recovery-driven credential reset
After successful recovery, Aurora normally requires a new Aurora Key and can recommend or require review/revocation of existing authenticators.

### Emergency Access / Break Glass
An optional managed-system emergency mechanism for enterprises or critical environments. It is disabled by default on personal systems, uses a separate credential/trust path, is heavily audited, and is never a universal master password.

## 7. Trusted devices and multi-device identity

### Device Trust
Aurora devices can possess their own cryptographic identities, allowing users to enroll, inspect, classify, and revoke trusted machines independently from their human identity.

### Trusted-device revocation
Removing trust from a device blocks future device-authenticated actions without changing the user's `user_id` or primary credentials.

### QR device pairing
A short-lived QR challenge can pair a new device using ephemeral keys, nonce, session ID, and expiration without embedding reusable Aurora credentials.

### Aurora Identity Capsule
A versioned encrypted export package can move selected identity/profile metadata and settings without exposing the Aurora Key.

### Aurora Identity Migration
A formal transactional procedure moves or clones an identity/profile relationship to another Aurora installation. Device-bound authenticators are re-enrolled rather than blindly copied.

### Aurora Handoff
An authenticated user can continue selected work on another trusted Aurora device, including Activity Space state, selected documents, layout, and short-lived continuation metadata.

### Optional encrypted synchronization
A future federation layer can synchronize selected profile/settings/device-trust information between Aurora devices while preserving offline local login.

## 8. Secrets and data protection

### Aurora Identity Vault
A protected secret service for application tokens, certificates, SSH/private keys, encryption keys, and similar material. Applications should receive narrowly scoped use-capabilities rather than unrestricted secret extraction.

### Non-exportable secret operations
Where possible, Vault performs signing, decryption, or other cryptographic operations internally instead of handing the private secret to an application.

### Aurora Data Seal
Protected profile/data encryption keys can be released only after sufficient authentication and wiped/revoked at lock, logout, or security transitions. Aurora Key itself is never used directly as the data-encryption key.

### Travel Mode
A temporary hardened state can disable selected authenticators, suspend synchronization, require stronger factors, hide selected protected data, invalidate cached trust, and suppress sensitive previews.

## 9. Platform security identities

### Aurora Application Identity
Every installed application can have a stable verifiable identity based on its package/signature/install record. Permission grants can therefore be tied to the exact application rather than merely to a filename/process.

### Aurora Service Identity
Privileged system services receive identities distinct from human users and applications, allowing capability/IPC policy to differentiate Identity Service, Session Manager, Permission Broker, storage services, update services, and other system principals.

### Combined authorization context
Future Aurora authorization can evaluate human identity + application identity + service/device identity + active session + resource + permission grant instead of relying on a single broad privilege flag.

## 10. Security monitoring and adaptive protection

### Identity Timeline / Security Activity
The System App exposes non-secret events such as successful authentication method, failed/throttled attempts, key rotation, authenticator enrollment/revocation, recovery activity, trusted-device changes, role/access changes, and session creation.

### Local Risk Engine
A local auditable risk evaluator can detect conditions such as repeated failures, recent recovery, newly enrolled authenticators, unusual session transitions, or use of revoked devices and request stronger authentication.

### Audit without credential leakage
Security events may record what class of authentication occurred and when, but never raw Aurora Keys, PINs, private authenticator material, or recovery secrets.

## 11. Identity management UX

### First-profile creation
When no persistent local human identity exists, the first successfully committed identity becomes the installation's initial Administrator.

When at least one identity already exists, an unknown valid Aurora Key may offer creation only when machine policy permits it. Every later persistent identity defaults to Standard User unless an authenticated Administrator explicitly changes the role.

### Profile management
The System App manages display name, avatar/presentation metadata, locale, and other non-secret identity preferences independently from authentication credentials.

### Access Devices management
Users can view friendly labels, last-use metadata, type, status, and revocation controls for enrolled drives and hardware authenticators.

### Recovery management
Users can create, replace, inspect status of, and revoke recovery methods without Aurora ever redisplaying the current Aurora Key.

### Session management
The System App can later display active local sessions and linked trusted devices with appropriate lock, logout, or revoke actions.

### Users & Access administration
Authenticated Administrators can manage local users, roles, and resource grants without exposing Aurora Keys. Account and access operations use stable `user_id` values internally and require appropriate administrative authorization.

### Accessibility-aware secure login
The final UI supports keyboard-only operation, scalable/high-contrast presentation, clear focus, reduced motion, and future protected accessibility input without leaking secret text to ordinary accessibility consumers.

## 12. Local account roles and file authorization

### Initial Administrator bootstrap
The first successfully committed persistent local human identity on a fresh installation becomes the initial Administrator. Merely entering an unknown Aurora Key is not enough; the identity transaction must commit successfully.

### Standard User default
Every later persistent local identity starts as Standard User unless an authenticated Administrator explicitly assigns another supported role.

### Configurable additional-user creation
After the initial Administrator exists, the Identity Service applies machine policy such as open local creation, Administrator approval, Administrator-only creation, or creation disabled. The login UI cannot bypass this policy.

### Private-profile isolation
A newly created user receives the minimum rights required to use their own private profile. They do not automatically receive access to another user's profile, pre-existing shared files, or protected system state.

### Administrator-granted file access
Access to existing/shared resources is deny-by-default for a new user and is granted through explicit Administrator-controlled rights such as READ, WRITE, CREATE, REMOVE, ENUMERATE, EXECUTE, and CONTROL.

### Stable-principal ownership
File/resource ownership and grants bind to stable `user_id` or another stable Aurora security principal, never display name or Aurora Key. Rotating a credential therefore does not alter ownership or permissions.

### Administrator authority separation
Administrator role permits management of authorization policy but does not automatically reveal other users' credentials or act as a universal cryptographic bypass for Data Seal, Vault, or other identity-bound encrypted data.

### Last-Administrator protection
Ordinary management flows must not accidentally leave a personal installation with no usable Administrator. Demotion, disablement, or deletion of the last Administrator requires another valid administrative/recovery path.

Detailed policy is defined in [`ACCOUNT_ROLES_AND_FILE_ACCESS.md`](ACCOUNT_ROLES_AND_FILE_ACCESS.md).

## 13. System guarantees

Across all functions, Aurora Identity preserves these product guarantees:

1. Normal local authentication works offline.
2. Raw Aurora Keys are never persisted.
3. The stable human identity is independent from credentials.
4. Authentication policy stays outside Ring 0.
5. Credentials and authenticators are individually revocable.
6. Applications never receive login credentials.
7. Ordinary removable USB drives are not represented as physically unclonable hardware keys.
8. Recovery is a separate trust path, not reversible primary-key storage.
9. Network and multi-device features are optional.
10. Failures fail closed rather than creating unauthenticated sessions.
11. Biometric data is minimized and isolated.
12. Emergency administration is explicit, scoped, auditable, and never a hidden backdoor.
13. The first persistent local human identity becomes the initial Administrator.
14. Later persistent identities default to Standard User.
15. New users receive no automatic access to another user's private or pre-existing shared data.
16. File/resource rights are explicit, stable-principal-bound, and revocable.
17. Administrator status is not a universal cryptographic bypass for protected user data.

## 14. Product scope conclusion

With the core specification, advanced features, platform extensions, and account/access policy, Aurora Identity is intended to evolve from a login screen into Aurora OS's trusted identity boundary:

```text
Who is the person?
Which credential proved it?
Which role/session/device/application/service is acting?
What exact resource capability is requested?
Is the current assurance sufficient for that action?
```

The current feature set is considered broad enough to freeze the high-level architecture and focus development on implementing the core dependency chain rather than continuing to expand the conceptual scope.