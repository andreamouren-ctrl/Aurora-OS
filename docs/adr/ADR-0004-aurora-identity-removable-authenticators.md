# ADR-0004 — Aurora Identity removable authenticators

Status: **Accepted design decision**
Date: 2026-10-01

## Context

Aurora Identity supports logging in either by typing an Aurora Key or by inserting an enrolled removable authenticator. A naive implementation could store the Aurora Key itself on a USB flash drive and replay it during login.

That design would make theft/copying of the drive equivalent to disclosure of the user's primary credential, would complicate revocation, and would falsely suggest that an ordinary flash drive is a secure non-clonable token.

## Decision

Aurora Identity Drive uses an **independent authenticator credential** bound to the stable Aurora `user_id`.

It never stores the user's Aurora Key.

Each enrolled drive receives its own `authenticator_id` and credential material. The Identity Service stores the corresponding enrollment state and can revoke one drive without rotating the Aurora Key or affecting other authenticators.

Ordinary USB mass-storage media is classified as a **portable software authenticator**. Aurora does not claim that this tier is physically non-clonable.

A separate future **Aurora Secure Identity Key** tier may use FIDO2-class hardware or another secure element with a non-exportable private key and challenge-response authentication.

Device serial number, VID/PID, filesystem UUID, and volume label are metadata only and are never sufficient authentication proof.

## Consequences

Positive:

- loss of a drive does not disclose the Aurora Key;
- individual drives can be revoked independently;
- multiple drives may be enrolled safely under one identity model;
- the security UI can accurately distinguish software and secure-hardware authenticators;
- future challenge-response hardware can be added without changing `user_id` or the session model.

Costs:

- Aurora needs a protected authenticator database;
- standard drives require a versioned credential container and careful parsing;
- USB/removable-storage infrastructure is required before the feature can ship;
- conventional mass-storage credentials may remain clonable if storage contents are copied.

## Rejected alternatives

### Store Aurora Key on the USB drive

Rejected because compromise of the removable medium compromises the primary credential and makes revocation/rotation unnecessarily coupled.

### Authenticate only by USB hardware serial number

Rejected because such identifiers are not reliable cryptographic proof and may be missing, spoofable, duplicated, or exposed.

### Treat ordinary flash drives as equivalent to secure hardware keys

Rejected because ordinary mass storage cannot guarantee a non-exportable private key.

## Related specifications

- `docs/AURORA_IDENTITY.md`
- `docs/identity/IDENTITY_DRIVE.md`
- `docs/identity/AUTHENTICATION_POLICY.md`
- `docs/identity/SECURITY_MODEL.md`
- `docs/identity/IDENTITY_SERVICE.md`
