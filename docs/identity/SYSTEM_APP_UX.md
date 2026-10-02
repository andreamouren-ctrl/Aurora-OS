# Aurora Identity System App UX

Status: **Canonical design**
Version: **0.3**

## 1. Product role

Aurora Identity is a system application with multiple operating modes rather than separate unrelated login/account/lock programs.

Primary modes:

- **Pre-session Login Mode**
- **First Profile Setup Mode**
- **Authenticated Account Management Mode**
- **Administrator Users & Access Mode**
- **Lock / Re-authentication Mode**
- **Recovery Mode**

The same visual language and identity contract are reused across modes, while privilege and available operations change with context.

## 2. Login Mode

Default composition:

```text
AURORA IDENTITY

[      AURORA KEY      ]

or insert Aurora Identity Drive

status / error area
```

Rules:

- no account tile list by default;
- keyboard focus starts in the Aurora Key field;
- entered key is masked;
- visual grouping may be shown every four normalized characters;
- Backspace edits;
- Esc clears entry;
- Enter submits;
- input is disabled while the specific request is being committed, but the UI remains responsive;
- removable-authenticator discovery runs asynchronously.

## 3. Identity Drive discovery states

Possible UI states:

```text
No drive present
Identity Drive detected
Verifying Identity Drive...
PIN required
Drive not recognized
Drive revoked
Unsupported authenticator
Authentication successful
```

The UI should avoid showing the associated display name before successful authentication unless the privacy policy explicitly permits it.

If multiple eligible drives are inserted, the UI may show anonymous device labels or request the user to choose a physical device without exposing account ownership.

## 4. First-user bootstrap and unknown Aurora Key creation

### Fresh installation

When Aurora has no committed persistent local human identity, the system enters **first-user setup**.

The first successfully committed identity becomes the initial **Administrator**.

The UI may clearly state that the profile being created will own initial administrative control of the installation.

Conceptual flow:

```text
No local identities
 -> enter/create Aurora Key
 -> identity creation
 -> profile setup
 -> role = Administrator
 -> recovery/security setup
 -> session starting
```

If creation fails before the identity transaction commits, Aurora remains in first-user setup and no Administrator is considered established.

### Existing installation

When at least one persistent local identity already exists, an unknown valid Aurora Key follows machine policy.

Possible outcomes include:

```text
OPEN_LOCAL_CREATION
 -> offer Standard User creation

ADMIN_APPROVAL_REQUIRED
 -> request Administrator approval

ADMIN_ONLY
 -> direct the user to an Administrator-managed creation flow

CREATION_DISABLED
 -> creation unavailable
```

Every later persistent account defaults to **Standard User** unless an authenticated Administrator explicitly changes the role.

`Create` transitions into profile setup only after the Identity Service provides a valid creation token/authorization context.

`Cancel` clears the candidate credential and returns to login.

## 5. First Profile Setup

Initial setup may collect:

- display name;
- avatar/background preferences later;
- language/locale;
- optional Aurora Identity Drive enrollment;
- recovery-method setup;
- privacy/default-session preferences.

The stable `user_id` is system-generated and never derived from the display name.

The setup must distinguish clearly between required security steps and optional personalization.

For the first persistent identity, the UI should state that the resulting account is the initial **Administrator**.

For all later persistent identities, the default role shown by the UI is **Standard User** unless an Administrator-authorized flow explicitly assigns otherwise.

## 6. Account Management Mode

After login, the System App becomes the user's central identity-management interface.

Suggested sections:

```text
Aurora Identity
├── Profile
├── Aurora Key
├── Access Devices
├── Recovery
├── Sessions
├── Security Activity
├── Users & Access        (Administrator only)
└── Advanced / Managed Policy
```

### Profile

- display name;
- avatar/personal identity presentation;
- profile metadata that is not an authentication secret;
- current local role where appropriate.

### Aurora Key

- change Aurora Key;
- generate a new Aurora Key;
- re-authentication before rotation;
- policy/strength information;
- never display the stored current key because Aurora does not know it reversibly.

### Access Devices

Shows enrolled authenticators:

```text
Portachiavi    Identity Drive    Last used: Today   [ Revoke ]
Backup         Identity Drive    Never used         [ Revoke ]
Secure Key     Hardware Key      Last used: ...     [ Revoke ]

[ + Add access device ]
```

Sensitive actions require re-authentication.

### Recovery

- create/replace recovery credential;
- add trusted recovery method;
- show whether recovery is configured;
- never redisplay unrecoverable secret values after the one-time creation step unless the chosen method explicitly supports secure regeneration.

### Sessions

Future view may show:

- current local session;
- other local sessions;
- trusted linked devices when federation exists;
- lock/logout/revoke actions.

### Security Activity

Shows non-secret security events such as:

- successful login method class;
- failed attempts/throttling events;
- credential changes;
- drive enrollment/revocation;
- recovery events;
- account-role changes;
- Administrator file-access grant/revoke events.

## 7. Administrator Users & Access Mode

This mode is available only to an authenticated Administrator with the required capability and may require fresh purpose-bound re-authentication.

It manages local identities without exposing their Aurora Keys.

Suggested functions:

```text
Users & Access
├── Local Users
│   ├── role: Administrator / Standard User / Guest
│   ├── status: active / disabled / recovery-required
│   └── promote / demote / disable where policy allows
├── New User Policy
│   ├── Open Local Creation
│   ├── Administrator Approval Required
│   ├── Administrator Only
│   └── Creation Disabled
├── File & Resource Access
│   ├── READ
│   ├── WRITE
│   ├── CREATE
│   ├── REMOVE
│   ├── ENUMERATE
│   ├── EXECUTE
│   └── CONTROL
└── Shared Resource Defaults
```

A newly created Standard User receives the minimum rights required for their own private profile, but no automatic access to another user's profile, existing shared data, or protected system state.

The Administrator can grant or revoke access to selected resources. Grants bind internally to stable `user_id`, not display name or Aurora Key.

The UI must clearly distinguish **administrative control** from **automatic ability to decrypt/read all private data**. Future Data Seal / Vault protection may remain inaccessible without the appropriate user-bound cryptographic material.

The UI must prevent ordinary management operations from accidentally removing the last usable Administrator without another valid administrative/recovery path.

## 8. Lock Mode

The lock screen is an Aurora Identity mode, not a second account picker.

It should preserve the same login methods available under policy:

- Aurora Key;
- enrolled Identity Drive;
- secure hardware key later;
- other approved methods later.

The lock surface may show the currently locked user's chosen presentation because the identity is already locally known in the active session. This differs from the cold-boot login surface, which avoids account enumeration.

## 9. Re-authentication Mode

Sensitive operations may invoke a compact Aurora Identity sheet/window rather than locking the whole session.

Examples:

- changing Aurora Key;
- enrolling/revoking an authenticator;
- changing recovery configuration;
- authorizing security-sensitive system changes;
- approving creation of a new persistent user;
- promoting/demoting a user;
- granting access to another user's private resource or broad shared storage.

The re-authentication result is purpose-bound and short-lived.

## 10. Recovery Mode

Recovery UI is deliberately minimal and explicit.

It may support:

- recovery credential entry;
- trusted recovery device;
- managed administrator recovery;
- future trusted-device approval.

After successful recovery, Aurora should normally require creation of a new Aurora Key and may recommend reviewing/revoking enrolled authenticators.

Recovery of administrative access must not silently reveal another user's credential or encrypted private data.

## 11. Bootstrap fallback UI

The existing framebuffer login surface remains a minimal fallback for:

- early development;
- compositor failure;
- recovery environment;
- graphics-stack diagnostics.

The final fallback does not need every account-management feature. It needs only the security-critical subset necessary to authenticate/recover and safely start a session or repair path.

## 12. Accessibility

The final System App should support:

- keyboard-only operation;
- high contrast;
- scalable text/UI;
- screen reader hooks after accessibility services exist;
- on-screen keyboard later;
- reduced-motion mode;
- clear focus indication;
- error text that does not rely on color alone.

Accessibility services must not receive secret credential text unless a specifically designed protected-input accessibility contract allows it.

## 13. Privacy rules

The UI must not:

- list users on cold login by default;
- reveal whether a specific key prefix exists;
- show full authenticator credential IDs unnecessarily;
- expose raw audit secrets;
- leave entered credentials visible after cancellation/failure;
- copy Aurora Keys into normal clipboard history;
- expose another user's private file names merely because the viewer is not authorized to enumerate the containing resource.

Authenticated Administrator management screens are a separate post-authentication context and may enumerate local users/resources only as permitted by authorization policy.

## 14. Visual direction

Aurora Identity follows Aurora OS visual language:

- deep navy/black base;
- cyan/blue/violet Aurora accents;
- minimal centered login composition;
- restrained system typography;
- subtle motion only when compositor support exists;
- no decorative element may obscure security state or input focus.

The framebuffer fallback may use a simplified procedural approximation of this language.

## 15. System controls

Later versions may expose small secondary controls on login/lock surfaces:

- power/restart;
- accessibility;
- keyboard/layout selection;
- network state where relevant;
- recovery entry.

These controls must not create a path around authentication.

## 16. UX acceptance criteria

The normal System App login/account milestone is complete when:

- it can replace the framebuffer prototype for ordinary login;
- it authenticates entirely through Identity Service IPC;
- Aurora Key and enrolled removable authenticators are supported;
- no account enumeration occurs by default on cold login;
- first-user setup clearly establishes the initial Administrator;
- later persistent identities default to Standard User;
- Administrator Users & Access mode can represent role and resource-grant management without exposing credentials;
- new users receive no automatic access to another user's private or pre-existing shared data;
- lock and re-authentication modes share the same identity contract;
- credential-management screens never expose stored secrets;
- compositor failure still leaves a functional fallback path.