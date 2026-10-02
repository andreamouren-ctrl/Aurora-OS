# Aurora Identity Account Roles and File Access Policy

Status: **Canonical design**
Version: **0.2**

## 1. Purpose

This document defines Aurora OS local-account bootstrap, default user roles, administrative access management, and the relationship between Aurora Identity and file/data authorization.

Aurora Identity answers **who the person is**. File access policy answers **which resources that identity may read, modify, enumerate, execute, share, or administer**. These responsibilities are related but remain separate security layers.

## 2. First-user bootstrap rule

On a fresh Aurora OS installation with no existing persistent local human identity, the first successfully committed identity becomes the initial **Administrator**.

The bootstrap promotion is a one-time installation-state transition:

```text
no local human identities
        ↓
first successful identity creation
        ↓
role = ADMINISTRATOR
        ↓
installation ownership established
```

The rule is based on the first identity transaction that commits successfully, not merely the first Aurora Key typed into the login surface.

If setup is interrupted before the identity transaction commits, Aurora remains in first-user bootstrap state.

## 3. Subsequent-user default

Every later persistent local identity is created as a **Standard User** unless an authenticated Administrator explicitly assigns another supported role.

Aurora must never silently promote a later account because of creation order, profile name, credential type, removable authenticator, or recovery method.

Baseline roles:

### Administrator

An Administrator may, subject to re-authentication and machine policy:

- approve or create additional local users when policy requires it;
- change another user's system role where permitted;
- grant, reduce, or revoke file/resource access rights;
- configure shared-storage policy;
- manage machine-wide security and account-creation policy;
- disable or suspend local identities;
- invoke explicitly supported administrative recovery flows;
- inspect security metadata that policy exposes to administrators.

Administrator status does **not** imply possession of another user's authentication credentials.

### Standard User

A Standard User:

- owns and manages their own profile data within policy;
- receives no automatic access to another user's private profile;
- cannot grant themselves administrative capabilities;
- cannot change machine-wide account/security policy;
- may share resources they own only through permitted sharing/authorization mechanisms.

### Guest

Guest Identity is a restricted temporary identity class. It receives only explicitly defined temporary resources and must not inherit Standard User or Administrator access by default.

## 4. User creation policy

The first-user bootstrap path is special because no Administrator exists yet. Once installation ownership has been established, creation of additional persistent users follows machine policy.

Supported policy classes may include:

- `OPEN_LOCAL_CREATION` — an unknown valid Aurora Key may offer creation of a Standard User;
- `ADMIN_APPROVAL_REQUIRED` — creation may be initiated at login but requires explicit Administrator approval before commit;
- `ADMIN_ONLY` — only an authenticated Administrator can start persistent-user creation;
- `CREATION_DISABLED` — no additional persistent local users may be created through normal flows.

Regardless of creation mode, every later persistent user defaults to **Standard User**.

An unknown Aurora Key never grants Administrator status merely because it is new.

The Identity Service, not the login UI, decides whether a creation path is available.

## 5. File-access model

Aurora file authorization is capability/ACL based rather than a global `administrator can read everything` shortcut.

The intended rights model includes at least:

- `READ` — read file contents/metadata allowed by policy;
- `WRITE` — modify existing content;
- `CREATE` — create resources within an authorized container;
- `REMOVE` — delete authorized resources;
- `ENUMERATE` — list directory/container contents;
- `EXECUTE` — execute an authorized program/resource where applicable;
- `CONTROL` — change sharing/access policy for a resource.

Exact kernel capability bits and filesystem ACL representation may evolve, but the semantic separation must remain.

## 6. Default profile isolation

A newly created persistent Standard User automatically receives the minimum rights required to use **their own private profile**.

They do **not** automatically receive read/write access to pre-existing files, another user's profile, shared workspaces, or protected system data.

Conceptual layout:

```text
/system/             system-owned; ordinary users cannot modify
/users/<user-A>/     user A private profile
/users/<user-B>/     user B private profile
/shared/             access only through explicit policy/grants
```

The physical path layout is illustrative; authorization must not depend solely on path naming.

Knowing another user's file path must never be sufficient to read or modify it.

## 7. Administrator-granted file access

Access beyond a user's own private profile is granted explicitly by an Administrator or by a policy that the Administrator controls.

A new user therefore starts from a deny-by-default posture for existing/shared resources.

Examples:

```text
/shared/projects     READ
/shared/projects     READ + WRITE
/shared/media        READ
/users/alice/private NONE
/system/config       NONE
```

The grant binds to stable `user_id`, never display name or Aurora Key.

Changing an Aurora Key therefore does not change file ownership or previously granted access.

An Administrator may revoke or reduce a grant later without deleting the identity.

## 8. Administrator authority vs private-data access

The Administrator manages authorization policy, but administrative authority and automatic data-reading authority are not identical concepts.

Aurora therefore distinguishes:

```text
administrative authority
        ≠
automatic decryption/read authority over all private user data
```

For ordinary unsealed files, policy may permit an Administrator to grant themselves or others access through an explicit auditable operation.

For future Aurora Data Seal, Identity Vault, non-exportable keys, and identity-bound encrypted data, administrative control must not silently bypass unavailable user-bound cryptographic keys.

This preserves machine administration without introducing a universal hidden master credential.

## 9. Ownership and sharing

Resources may have an owning identity or system/service principal plus an authorization policy.

Ownership should be associated with stable principals such as:

- human `user_id`;
- Aurora Service Identity;
- Aurora Application Identity where appropriate;
- system-owned resource principal.

Display names are presentation metadata and must never be used as security identifiers.

A user who owns a shareable resource may grant only rights allowed by machine policy. A grantee cannot automatically re-delegate rights unless `CONTROL` or an equivalent delegation right is explicitly present.

## 10. Administrative actions and re-authentication

Security-sensitive administrative actions should require purpose-bound re-authentication, including:

- approving or creating a new persistent user when policy requires Administrator involvement;
- promoting a Standard User to Administrator;
- demoting or disabling an Administrator;
- granting access to another user's private resource;
- granting broad read/write access to shared data;
- changing machine-wide sharing defaults;
- modifying protected system-state access policy.

The re-authentication proof must identify the operation being authorized and expire quickly.

## 11. Last-administrator safety

Aurora must prevent ordinary UI/policy operations from accidentally leaving a normal installation with no usable Administrator.

Before demoting, disabling, deleting, or irrecoverably locking the last active Administrator, Aurora should require one of:

- promotion/creation of another Administrator;
- an explicitly configured recovery/managed administration path;
- a dedicated recovery-mode procedure.

This rule does not create a backdoor and does not reveal another user's credentials or encrypted data.

## 12. Audit requirements

Administrative identity and authorization changes are security events.

Audit metadata should include, without logging secrets:

- acting Administrator `user_id` or authorized service identity;
- target identity/resource;
- action class;
- rights before/after where appropriate;
- success/failure;
- timestamp/boot context;
- purpose-bound re-authentication reference where applicable.

## 13. Required separation of responsibilities

Aurora Identity Service owns identity and role policy.

The filesystem/VFS/storage stack owns durable resource representation.

The Permission Broker / authorization layer evaluates whether the active human/session/application/service context may perform a requested operation.

Conceptually:

```text
Aurora Identity Service
        ↓ verified user_id + role/session context
Session / Permission Broker
        ↓ capability grant
VFS / filesystem
        ↓ authorized resource operation
storage
```

No filesystem driver should independently decide that an Aurora Key is valid, and no login UI should directly rewrite file ACLs.

## 14. Security invariants

1. First committed persistent local human identity becomes the initial Administrator.
2. Later persistent identities default to Standard User.
3. Additional-user creation follows explicit machine policy.
4. Roles bind to stable `user_id`, never Aurora Key or display name.
5. A new user automatically controls only the minimum private profile resources required for their own session.
6. Access to existing/shared/other-user resources is denied until explicitly granted by Administrator-controlled policy.
7. File rights are explicit and independently revocable.
8. Administrator role does not reveal Aurora Keys or recovery secrets.
9. Administrator role is not a universal cryptographic bypass for sealed/private data.
10. Sensitive role/access changes require auditable authorization and, where configured, fresh re-authentication.
11. The system prevents accidental loss of the last usable Administrator through ordinary management flows.

## 15. Implementation dependencies

This policy can be represented before the final filesystem authorization layer exists, but production enforcement depends on:

- stable persistent `user_id` records;
- role metadata in protected Identity state;
- Session Manager identity binding;
- capability lineage/revocation;
- Permission Broker/authorization policy;
- filesystem ownership/ACL metadata;
- protected system state;
- audit event persistence.

The first implementation should prefer minimal explicit rights and fail closed when role or access metadata is invalid.