# Aurora Identity Advanced Features

Status: **Forward-looking canonical capability catalogue**
Version: **0.1**

This document defines advanced capabilities that Aurora Identity should be architecturally prepared to support. It does **not** require every capability to ship in the first production release.

The purpose is to prevent short-term implementation choices from blocking stronger identity, privacy, recovery, device-trust, and session features later.

## 1. Scope classes

Features are grouped into three classes.

### Core extensions

Capabilities expected to become part of the normal Aurora Identity product once their dependencies exist:

- Aurora Identity Drive;
- multiple authenticators per identity;
- credential rotation and revocation;
- recovery kit;
- security activity history;
- purpose-bound re-authentication;
- trusted-device model;
- configurable authentication policy.

### Advanced security and privacy

Capabilities intended for users who want stronger privacy or security:

- Ghost Session;
- Session Seal;
- high-security multi-factor policy;
- Travel Mode;
- Lock Zones;
- one-time access credentials;
- secure-hardware authenticators.

### Future distributed identity

Capabilities that depend on mature networking, synchronization, device identity, and encrypted state transport:

- Aurora Presence;
- Aurora Handoff;
- Identity Capsule;
- trusted-device approval;
- cross-device recovery;
- encrypted profile/settings synchronization.

Local login must remain functional without any of these networked features.

## 2. Aurora Presence

Aurora Presence allows an enrolled trusted device, such as a phone, wearable, or secure hardware key, to participate in session presence policy.

Possible behaviors:

- lock the session when all approved presence devices leave a configured range;
- prepare the login surface when a trusted device returns;
- require an explicit final confirmation or another factor before unlock;
- optionally elevate assurance only while a trusted device remains present.

Presence alone must not silently become a universal authentication bypass.

Proximity signals are advisory unless backed by an authenticated cryptographic channel.

## 3. Aurora Ghost Session

A Ghost Session is an intentionally ephemeral user session.

Properties may include:

- temporary profile namespace;
- disposable caches;
- no persistent application history unless explicitly exported;
- temporary secrets destroyed at logout;
- optional temporary network identity;
- automatic cleanup after logout, shutdown, or timeout.

Ghost Session should be useful for shared computers, temporary work, demonstrations, repair environments, and privacy-sensitive workflows.

The system must clearly distinguish a Ghost Session from a normal persistent profile.

## 4. Aurora Identity Capsule

An Identity Capsule is an encrypted export package used to move or restore identity-related state without exposing raw credentials.

Possible content:

- stable identity metadata;
- encrypted profile-binding metadata;
- selected settings;
- trusted-device metadata;
- revocation state;
- optional encrypted application/session state;
- recovery metadata.

It must not contain a recoverable plaintext Aurora Key.

Import requires authentication and policy validation before any identity record is accepted.

Capsules must use a versioned format and integrity protection.

## 5. Aurora Handoff

Aurora Handoff allows an authenticated user to continue work on another trusted Aurora device.

Potential transferred state:

- Activity Space state;
- selected open documents;
- application continuation metadata;
- desktop/session layout;
- clipboard only when explicitly permitted;
- short-lived session continuation grants.

Handoff must never transmit the Aurora Key.

A receiving device must independently verify trust and user authorization before restoring state.

## 6. Aurora Trusted Circle

Trusted Circle is an optional recovery/approval model in which several independently enrolled devices or recovery holders can jointly authorize a sensitive operation.

Example policies:

- 2 of 3 trusted devices required;
- 3 of 5 recovery shares required;
- one local recovery credential plus one trusted device.

Threshold recovery must be implemented with established cryptographic secret-sharing or approval protocols rather than proprietary ad-hoc splitting.

Trusted Circle is optional and must never be required for ordinary local use.

## 7. Aurora Key Shards

Aurora Key Shards are recovery shares, not pieces of the user's actual Aurora Key.

Aurora may generate a separate high-entropy recovery secret and split it into threshold shares.

Example:

```text
5 recovery shares generated
3 shares required to recover
```

Possible storage locations:

- printed recovery card;
- encrypted USB device;
- trusted phone;
- secure offline backup;
- trusted family/member device when explicitly configured.

The original Aurora Key remains non-recoverable from stored verifier data.

## 8. One-Time Access

Aurora Identity may issue tightly scoped temporary credentials.

Possible use cases:

- temporary family access;
- technician/service access;
- one-time emergency access;
- temporary access to a limited guest environment.

A temporary credential must define:

- issuer;
- intended identity or guest profile;
- allowed scope;
- expiration;
- maximum use count;
- revocation state.

It must not automatically inherit the owner's full capabilities.

## 9. Aurora Guest Identity

Guest Identity is a controlled temporary identity, distinct from Ghost Session.

Possible policies:

- valid until logout;
- valid until reboot;
- valid for a configured duration;
- persistent guest profile with restricted capabilities;
- explicitly permitted folders/devices only.

The owner can create and revoke guest identities from Aurora Identity System App.

## 10. Aurora Identity Vault

Aurora Identity Vault is a future protected secret service bound to authenticated identity.

Potential contents:

- application tokens;
- private application credentials;
- certificates;
- SSH keys;
- encryption keys;
- secure notes/metadata where appropriate.

Applications should receive a narrow capability to request use of a secret, not broad access to all vault contents.

Where possible, the Vault should perform cryptographic operations without exporting the private secret to the caller.

Vault implementation is a separate security subsystem even if managed through Aurora Identity UI.

## 11. Application Identity

Every installed application should eventually have a stable application identity independent from the human user's identity.

Authorization can then be evaluated as:

```text
human identity
+ application identity
+ device/session context
+ requested resource
+ current permission grant
```

This allows Aurora to express policies such as:

> Writer For Me may access this project directory only while the authenticated owner is using it.

Application Identity integrates with the Aurora Permission Broker rather than replacing it.

## 12. Session Seal

Session Seal protects sensitive material when a session is locked.

Possible behavior:

- wipe selected derived keys from RAM;
- invalidate short-lived re-authentication proofs;
- pause or revoke access to Vault secrets;
- require fresh authentication to reconstruct sensitive session state.

The operating system must define which state can safely survive lock and which state must be re-derived.

## 13. Instant Lock

Aurora should support a fast trusted path that immediately locks the active session.

Possible triggers:

- keyboard shortcut;
- hardware button;
- trusted-device removal;
- Aurora Presence policy;
- administrative security event.

Instant Lock must not depend on the desktop application event loop being responsive.

## 14. Travel Mode

Travel Mode is an optional temporary high-security profile state.

Potential behavior:

- disable selected authenticators;
- hide or unmount selected protected data until re-authorized;
- require stronger authentication policy;
- suspend federation/synchronization;
- invalidate cached trust grants;
- suppress sensitive notification previews.

Travel Mode must be reversible through documented recovery paths and must not create hidden irreversible data loss.

## 15. Login Approval

A new Aurora device may request approval from an already trusted device.

The approval protocol must bind:

- requesting device identity;
- user identity;
- exact action being approved;
- freshness challenge;
- expiration.

An approval message such as "Authorize this device?" is valid only when backed by an authenticated channel and explicit challenge-response.

## 16. Device Trust

Aurora devices may eventually have their own cryptographic identity.

The user can inspect:

- device name;
- device key fingerprint/identifier;
- enrollment date;
- last trusted activity;
- trust class;
- revocation state.

Revoking a device invalidates future device-authenticated operations without changing the user's `user_id`.

## 17. Identity Timeline

Aurora Identity System App should expose a privacy-preserving security timeline.

Events may include:

- successful authentication method class;
- failed/throttled authentication attempts;
- Aurora Key rotation;
- authenticator enrollment/revocation;
- recovery activity;
- trusted-device enrollment/revocation;
- policy changes;
- new session creation.

Never log raw Aurora Keys, PINs, private authenticator material, or recovery secrets.

## 18. Local Risk Engine

A future Aurora Risk Engine may calculate local authentication risk using non-secret security context.

Signals may include:

- unusually high failed-attempt count;
- newly enrolled authenticator;
- new device trust relationship;
- recovery just performed;
- unusual session transition;
- revoked authenticator reuse attempt.

The engine may require a stronger factor for sensitive operations.

It should be deterministic/auditable enough that the user can understand why additional authentication was required.

Cloud scoring is not required.

## 19. Silent Identity Drive Detection

When policy permits, insertion of a valid Identity Drive may trigger authentication automatically.

Flow:

```text
USB inserted
 -> removable-media broker detects candidate
 -> Identity Service validates container
 -> authenticator challenge/verification
 -> optional PIN/confirmation
 -> session grant
```

Unknown or malformed media must never create an account or start a session automatically.

## 20. Multi-factor policy presets

Aurora Identity may expose understandable presets instead of forcing users to construct policy expressions manually.

Examples:

```text
Convenient
  Aurora Key OR Identity Drive

Protected
  Aurora Key OR (Identity Drive + PIN)

High Security
  Aurora Key + secure hardware authenticator

Travel
  Aurora Key + secure hardware authenticator + trusted-device restrictions
```

The Identity Service remains the source of truth for enforcement.

## 21. Emergency Recovery Kit

Aurora may generate an offline recovery kit containing:

- one-time recovery codes or recovery credential material;
- identity/recovery metadata required to locate the correct local identity record;
- human-readable instructions;
- integrity/version information.

The kit must never contain the current Aurora Key in plaintext.

The UI should encourage physically separate storage from the primary device.

## 22. QR device pairing

Aurora Identity may use a QR code for short-lived device enrollment.

The QR payload may contain:

- pairing endpoint/session identifier;
- ephemeral public key;
- challenge nonce;
- expiration metadata.

It must not contain a reusable Aurora Key or permanent private credential.

## 23. Profile Layers

One stable human identity may own several isolated usage contexts.

Examples:

- Personal;
- Work;
- Gaming;
- Development;
- Child-safe or restricted context.

Profile Layers are not separate authentication identities unless explicitly configured that way.

A policy layer controls which applications, data, and services are visible in each context.

## 24. Lock Zones

Sensitive areas may request fresh authentication even while the main session is unlocked.

Examples:

- Identity Vault;
- private keys;
- security settings;
- recovery configuration;
- full credential history;
- protected folders.

Successful re-authentication returns a short-lived, purpose-bound proof rather than a generic permanent elevation.

## 25. Security constraints across all advanced features

Every advanced feature must preserve these invariants:

1. Aurora Key is never transmitted merely to enable convenience features.
2. Networked features are optional; offline local login remains available.
3. Device identifiers alone are not authenticators.
4. Proximity alone is not sufficient cryptographic proof.
5. Trusted-device enrollment is explicit and revocable.
6. Temporary credentials are scope- and time-limited.
7. Recovery material is independent from the primary Aurora Key verifier.
8. Cross-device state is encrypted and integrity-protected.
9. Applications do not gain identity-service privilege through UI integration.
10. Any feature capable of starting/unlocking a session is enforced by Aurora Identity Service policy.

## 26. Dependency order

Advanced capabilities should not be implemented before their foundations.

Recommended order after core Aurora Identity v1:

1. recovery kit and security activity;
2. multiple authenticators and policy presets;
3. secure hardware authenticator support;
4. Vault / purpose-bound re-authentication;
5. Ghost Session and Guest Identity;
6. device cryptographic identity;
7. trusted-device model;
8. QR pairing and login approval;
9. Identity Capsule;
10. Handoff;
11. Presence;
12. threshold/Trusted Circle recovery;
13. encrypted multi-device synchronization.

## 27. Product principle

Aurora Identity should evolve from a login mechanism into the operating system's trusted identity boundary.

The user experience may become richer, but the architectural rule remains simple:

```text
Who is the person?
Which credential proved it?
Which device/session is trusted?
What exact capability is being requested?
```

Aurora should answer those questions without making cloud connectivity, account enumeration, or irreversible proprietary credentials mandatory.