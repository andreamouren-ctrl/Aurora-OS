# Aurora Identity Authentication Policy

Status: **Canonical policy draft**
Version: **0.2**

## 1. Purpose

This document defines how Aurora Identity combines credentials, authenticators, machine policy, user convenience settings, local-account creation, role changes, and high-security modes.

It does not define cryptographic algorithms in detail; those belong to the credential-specific specifications.

## 2. Authentication classes

Aurora Identity recognizes these logical classes:

### Knowledge credential

- Aurora Key
- future local PIN when explicitly paired with another possession factor

### Possession credential

- Aurora Identity Drive
- Aurora Secure Identity Key
- future trusted Aurora device

### Recovery credential

- dedicated high-entropy recovery credential
- managed administrative recovery
- future explicitly enabled recovery escrow

Recovery credentials are not ordinary daily-login credentials unless policy explicitly promotes one.

## 3. Baseline personal-device policy

The initial personal-desktop policy supports:

```text
Aurora Key
OR
Enrolled Aurora Identity Drive
```

The Aurora Key remains available as the baseline fallback unless the user intentionally enters a stronger managed/high-security configuration with a validated recovery path.

## 4. Identity Drive modes

Per-authenticator policy may select:

- `CONFIRM_ON_INSERT` — drive is detected, user confirms authentication;
- `AUTO_AUTHENTICATE` — authentication starts automatically on insertion;
- `DRIVE_PLUS_PIN` — possession of the drive plus a local PIN is required;
- `DISABLED` — authenticator remains enrolled but cannot currently authenticate;
- `REVOKED` — authenticator is permanently rejected until separately re-enrolled as a new credential.

A revoked credential is not silently reactivated.

## 5. Multi-factor policy

Aurora must be capable of requiring more than one factor without changing the stable identity model.

Examples:

```text
Aurora Key + Secure Identity Key
Identity Drive + PIN
Secure Identity Key + local confirmation
```

MFA policy belongs to the Identity Service and machine policy, not to the UI.

The UI only renders the steps requested by the service.

## 6. High-security mode

A future high-security configuration may:

- disable ordinary mass-storage Identity Drives;
- require secure hardware authenticators;
- require MFA for cold login;
- require fresh re-authentication for credential management;
- disable automatic login on device insertion;
- disable local self-service account creation;
- shorten re-authentication proof lifetime;
- require stronger recovery enrollment.

High-security mode must never silently remove the last valid recovery path.

## 7. Managed-device policy

Managed systems may define centrally administered policy such as:

```text
local_creation = disabled
allowed_authenticator_classes = secure_hardware_only
mfa_required = true
auto_login = disabled
recovery = admin_or_secure_key
```

Policy enforcement belongs to the Identity Service and privileged policy store.

Ordinary applications cannot modify these controls.

## 8. First-user and unknown-credential behavior

A fresh Aurora OS installation with no persistent local human identity enters **first-user bootstrap**.

The first identity that is successfully created and committed becomes the initial **Administrator**.

After that bootstrap transition, every later persistent local identity defaults to **Standard User** unless an authenticated Administrator explicitly changes the role.

On personal systems, an unknown Aurora Key may produce a profile-creation offer only when machine policy allows it.

Possible post-bootstrap creation policies include:

- `OPEN_LOCAL_CREATION`;
- `ADMIN_APPROVAL_REQUIRED`;
- `ADMIN_ONLY`;
- `CREATION_DISABLED`.

An unknown Aurora Key never receives Administrator status merely because it is new.

An unknown Identity Drive never creates a new account automatically.

Drive enrollment always begins from an already authenticated identity or approved recovery/admin context.

## 9. Account enumeration policy

Before successful authentication, Aurora should avoid displaying:

- lists of local users;
- account names derived from failed credentials;
- profile photos tied to an unverified credential;
- detailed disabled/revoked-account state.

Where UX requires a creation offer, the service reveals only that machine policy permits creation from the submitted credential context.

The special first-user bootstrap state may indicate that initial setup is required, but it still must not expose a fabricated/default account name.

## 10. Auto-login safety

Automatic authentication from an inserted Identity Drive is convenience-oriented and must be opt-in or clearly surfaced during enrollment.

Aurora must explain that, for a standard mass-storage Identity Drive, possession of a valid enrolled drive may be sufficient for login unless PIN/MFA is enabled.

Auto-login must be disabled while:

- the authenticator is revoked or disabled;
- policy requires another factor;
- the system is in recovery/high-security mode;
- the service detects integrity or enrollment failure.

## 11. Re-authentication policy

Fresh re-authentication may be required for:

- rotating Aurora Key;
- enrolling a new authenticator;
- revoking the final remaining authenticator;
- changing recovery policy;
- changing high-security policy;
- exporting recovery material;
- disabling a required factor;
- approving creation of a new persistent user when machine policy requires Administrator approval;
- promoting or demoting a local user;
- granting broad read/write access to existing/shared/private resources;
- changing machine-wide file-access defaults.

The resulting proof is purpose-bound and short-lived.

## 12. Local roles and file-access authorization

Authentication proves identity; it does not automatically authorize every file operation.

Canonical local role behavior:

```text
first committed identity -> ADMINISTRATOR
later identities          -> STANDARD_USER by default
```

A new Standard User receives only the minimum rights required for their own private profile. Access to another user's files, pre-existing shared resources, or protected system state is not granted automatically.

Additional file/resource rights are granted through Administrator-controlled authorization policy and bind to stable `user_id`, not to Aurora Key or display name.

Administrator role does not reveal another user's Aurora Key, recovery secret, or automatically bypass future Data Seal / Vault cryptographic protection.

Detailed policy is defined in [`ACCOUNT_ROLES_AND_FILE_ACCESS.md`](ACCOUNT_ROLES_AND_FILE_ACCESS.md).

## 13. Credential-loss rules

Loss of one authenticator must not destroy the profile.

If another credential remains valid, the user can authenticate and revoke the lost method.

If only recovery remains, recovery must establish a fresh normal credential before restoring ordinary operation.

If all normal and recovery credentials are lost, Aurora does not bypass authentication.

## 14. Lock-screen behavior

Unlock may follow a different policy from cold boot while preserving the same service authority.

Examples permitted by policy:

- Aurora Key;
- enrolled secure key;
- Identity Drive + PIN;
- fresh multi-factor proof on high-security systems.

A pre-existing desktop session is not itself proof of identity after the system enters the locked state.

## 15. Policy persistence

Authentication, local-role, and account-creation policy are protected system state.

Updates must be:

- authenticated;
- transactional;
- versioned;
- auditable without logging secrets;
- recoverable after interrupted writes;
- protected from ordinary application modification.

## 16. Safe defaults

Until the user opts into a different policy, Aurora should prefer:

- offline-capable login;
- Aurora Key available;
- account enumeration disabled;
- first persistent identity becomes Administrator;
- later persistent identities default to Standard User;
- new users receive no automatic access to another user's private or pre-existing shared files;
- automatic drive login disabled by default or explicitly confirmed during enrollment;
- independent authenticator revocation;
- recovery enrollment encouraged before destructive credential changes;
- no cloud dependency.

## 17. Acceptance criteria

Authentication policy implementation is complete when:

- the Identity Service, not UI code, makes factor decisions;
- each authenticator has explicit status and policy;
- MFA can be represented without changing `user_id`;
- auto-login is user/policy controlled;
- first-user Administrator bootstrap is represented transactionally;
- later identities default to Standard User;
- creation policy is machine-configurable;
- role changes and sensitive access grants are capability protected and auditable;
- recovery cannot silently weaken normal authentication;
- managed/high-security policy is capability protected;
- policy updates survive crashes transactionally;
- tests cover conflicting policy, lockout, revocation, factor-loss, first-user bootstrap, last-Administrator safety, and unauthorized role/access changes.