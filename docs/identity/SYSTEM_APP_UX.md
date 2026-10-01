# Aurora Identity System App UX

Status: **Canonical design**
Version: **0.2**

## 1. Product role

Aurora Identity is a system application with multiple operating modes rather than separate unrelated login/account/lock programs.

Primary modes:

- **Pre-session Login Mode**
- **First Profile Setup Mode**
- **Authenticated Account Management Mode**
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

## 4. Unknown Aurora Key creation flow

On personal systems where machine policy allows creation:

```text
User not found.
Create a new Aurora profile with this key?

[ Create ] [ Cancel ]
```

`Create` transitions into profile setup only after the Identity Service provides a valid creation token.

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
└── Advanced / Managed Policy
```

### Profile

- display name;
- avatar/personal identity presentation;
- profile metadata that is not an authentication secret.

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
- recovery events.

## 7. Lock Mode

The lock screen is an Aurora Identity mode, not a second account picker.

It should preserve the same login methods available under policy:

- Aurora Key;
- enrolled Identity Drive;
- secure hardware key later;
- other approved methods later.

The lock surface may show the currently locked user's chosen presentation because the identity is already locally known in the active session. This differs from the cold-boot login surface, which avoids account enumeration.

## 8. Re-authentication Mode

Sensitive operations may invoke a compact Aurora Identity sheet/window rather than locking the whole session.

Examples:

- changing Aurora Key;
- enrolling/revoking an authenticator;
- changing recovery configuration;
- authorizing security-sensitive system changes.

The re-authentication result is purpose-bound and short-lived.

## 9. Recovery Mode

Recovery UI is deliberately minimal and explicit.

It may support:

- recovery credential entry;
- trusted recovery device;
- managed administrator recovery;
- future trusted-device approval.

After successful recovery, Aurora should normally require creation of a new Aurora Key and may recommend reviewing/revoking enrolled authenticators.

## 10. Bootstrap fallback UI

The existing framebuffer login surface remains a minimal fallback for:

- early development;
- compositor failure;
- recovery environment;
- graphics-stack diagnostics.

The final fallback does not need every account-management feature. It needs only the security-critical subset necessary to authenticate/recover and safely start a session or repair path.

## 11. Accessibility

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

## 12. Privacy rules

The UI must not:

- list users on cold login by default;
- reveal whether a specific key prefix exists;
- show full authenticator credential IDs unnecessarily;
- expose raw audit secrets;
- leave entered credentials visible after cancellation/failure;
- copy Aurora Keys into normal clipboard history.

## 13. Visual direction

Aurora Identity follows Aurora OS visual language:

- deep navy/black base;
- cyan/blue/violet Aurora accents;
- minimal centered login composition;
- restrained system typography;
- subtle motion only when compositor support exists;
- no decorative element may obscure security state or input focus.

The framebuffer fallback may use a simplified procedural approximation of this language.

## 14. System controls

Later versions may expose small secondary controls on login/lock surfaces:

- power/restart;
- accessibility;
- keyboard/layout selection;
- network state where relevant;
- recovery entry.

These controls must not create a path around authentication.

## 15. UX acceptance criteria

The normal System App login milestone is complete when:

- it can replace the framebuffer prototype for ordinary login;
- it authenticates entirely through Identity Service IPC;
- Aurora Key and enrolled removable authenticators are supported;
- no account enumeration occurs by default;
- lock and re-authentication modes share the same identity contract;
- credential-management screens never expose stored secrets;
- compositor failure still leaves a functional fallback path.
