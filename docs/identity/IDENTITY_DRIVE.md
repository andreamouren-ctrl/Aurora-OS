# Aurora Identity Drive Specification

Status: **Canonical design**
Version: **0.2**

## 1. Product definition

**Aurora Identity Drive** is an optional removable USB authenticator associated with one Aurora Identity.

It allows a user to authenticate without typing the Aurora Key. When an enrolled drive is inserted, Aurora detects it, validates its authenticator record, and requests authentication through the Aurora Identity Service.

The drive never contains the user's Aurora Key.

## 2. User experience

The login surface supports two primary paths:

```text
AURORA IDENTITY

[ Enter Aurora Key ]

          OR

Insert Aurora Identity Drive
```

When an enrolled drive is inserted:

1. removable-media service detects the device;
2. Aurora looks for a valid Identity Drive credential container;
3. the Identity Service validates the credential and local enrollment;
4. optional PIN/confirmation policy is enforced;
5. successful authentication issues a session grant;
6. Session Manager starts the associated `user_id` session.

The system should not expose the account name before successful authentication unless the user has explicitly chosen a lower-privacy configuration.

## 3. Enrollment flow

An Identity Drive is created from an already authenticated Aurora session.

Canonical flow:

1. Open **Aurora Identity** System App.
2. Open **Access Devices**.
3. Choose **Create Aurora Identity Drive**.
4. Insert/select an eligible removable USB drive.
5. Re-authenticate with an existing trusted method.
6. Aurora generates a new independent authenticator credential.
7. The drive credential container is written transactionally.
8. The local Identity Service stores the corresponding enrollment record.
9. Aurora verifies the new drive before declaring enrollment complete.
10. User assigns an optional friendly label such as `Portachiavi` or `Backup`.

Aurora must clearly warn before formatting or overwriting a drive. Identity enrollment must not silently erase unrelated user files.

## 4. Multiple drives

One identity may enroll multiple drives:

```text
Andrea
├── Aurora Key
├── Identity Drive — Portachiavi
├── Identity Drive — Casa
└── Identity Drive — Backup
```

Each drive has a unique `authenticator_id` and separate credential material.

Revoking one drive must not affect the others.

## 5. Standard Identity Drive security tier

A normal USB mass-storage device cannot guarantee a non-exportable secret.

Therefore the standard Identity Drive is defined as a **portable software authenticator**. It provides convenience and revocability, but copied storage contents may be cloneable.

The standard design must therefore:

- use an independent credential, never the Aurora Key;
- use authenticated container integrity;
- use a unique credential ID;
- support optional PIN protection;
- support local revocation;
- record last-used metadata without storing secret payloads in logs;
- never trust USB serial number, VID/PID, filesystem UUID, or volume label as sole authentication proof;
- avoid claims that conventional flash media is physically unclonable.

## 6. Secure Identity Key tier

A future **Aurora Secure Identity Key** may use hardware with a non-exportable private key, such as a FIDO2-class authenticator or Aurora-specific secure element.

That stronger tier uses challenge-response:

```text
Identity Service -> random challenge
Secure Key       -> signature / proof
Identity Service -> verifies enrolled public key
```

This class is resistant to ordinary credential-file cloning and should be represented separately in the UI from a standard Identity Drive.

## 7. Credential container

A standard Identity Drive contains a small versioned Aurora credential container. Conceptual contents:

```text
AuroraIdentityDriveContainer
- format_magic
- format_version
- authenticator_id
- credential_type
- public_metadata
- wrapped_or_encrypted_secret_material
- integrity/authentication data
- creation metadata
```

The container must not include:

- Aurora Key;
- local password verifier;
- full user profile database;
- reusable session grant;
- recovery credential unless the user explicitly creates a separate recovery medium under a distinct specification.

The on-disk filename/path is not security-sensitive and may evolve. The parser must rely on validated format contents rather than cosmetic volume labels.

## 8. Authentication model for standard drives

The exact cryptographic construction will be frozen only after secure storage and RNG primitives exist. The intended model is:

1. drive presents `authenticator_id` and protected credential material;
2. Identity Service locates the enrolled authenticator record;
3. service validates integrity and proves possession using the enrolled secret/public data;
4. policy checks revocation, disabled state, optional PIN, rate limit, and machine binding;
5. successful result issues a fresh session grant.

A static file equality check is not sufficient as the final production design.

## 9. Machine binding

Enrollment policy may support:

- **Machine-bound** drive — valid only on the Aurora installation where enrolled;
- **Portable trusted-device** drive — future mode valid on multiple explicitly linked Aurora devices.

Machine binding must use cryptographic local device secrets or enrollment records. Hardware serial strings alone are not sufficient.

The first implementation should prefer machine-bound credentials because Aurora Identity federation does not yet exist.

## 10. Auto-login policy

Aurora may offer several behaviors:

- **Detect only** — show that an Identity Drive is present; user confirms login.
- **Automatic authenticate** — start authentication immediately when an enrolled drive is inserted.
- **Drive + PIN** — require a short local PIN after detecting the drive.
- **High-security mode** — standard mass-storage drives disabled; only Aurora Key or secure hardware authenticator allowed.

Automatic login is a user-controlled policy. It is convenient but means possession of a working enrolled drive can be sufficient to unlock the machine.

## 11. Drive scanning

Aurora must not indiscriminately parse every file on every removable drive.

The removable-media broker should perform a narrowly scoped lookup for the versioned Aurora credential container.

Scanning requirements:

- asynchronous insertion/removal events;
- strict maximum file/container size;
- bounded parser;
- no executable content;
- no autorun behavior;
- cancellation if media is removed;
- duplicate authenticator handling;
- deterministic handling of multiple enrolled drives connected simultaneously;
- no credential material copied into ordinary file-indexing services.

## 12. Revocation

Aurora Identity System App exposes all enrolled authenticators for the current user:

```text
Access Devices

Portachiavi      Last used: Today       [ Revoke ]
Backup           Never used             [ Revoke ]
```

Revocation marks the `authenticator_id` unusable locally even if the physical drive still contains its old container.

If the drive is later inserted, Aurora must reject it without revealing unnecessary identity metadata.

## 13. Lost or stolen drive

Recommended response:

1. authenticate using Aurora Key or another enrolled method;
2. open Aurora Identity -> Access Devices;
3. revoke the lost authenticator;
4. optionally review recent identity audit events;
5. create a replacement Identity Drive if desired.

Losing a drive must never force profile recreation.

## 14. Re-enrollment and cloning

Copying an Identity Drive container to another conventional USB device may result in a functional clone depending on the software credential construction. This is a known limitation of the standard tier.

Aurora should detect obvious duplicate simultaneous use where possible, but duplicate detection is not a substitute for non-exportable hardware keys.

The secure-hardware tier is the intended solution for users who require strong anti-cloning guarantees.

## 15. Dependency requirements

Standard Identity Drive implementation requires:

- USB host/controller support;
- USB mass-storage support;
- removable block-device events;
- VFS/filesystem support;
- protected Identity Service;
- secure RNG;
- protected identity database;
- cryptographic integrity primitive;
- session manager.

Secure Identity Key additionally requires a USB HID/security-key protocol stack or equivalent device service.

## 16. Acceptance criteria

The first production Identity Drive milestone is complete when:

- a drive can be enrolled without exposing the Aurora Key;
- the credential is individually revocable;
- malformed media cannot crash the kernel/service;
- insertion/removal is handled asynchronously;
- login works offline;
- a lost drive can be revoked using another credential;
- credential parsing is fuzz-tested;
- drive authentication never relies solely on cosmetic/hardware identifier strings;
- security tier and cloning limitations are documented in the UI and manual.
