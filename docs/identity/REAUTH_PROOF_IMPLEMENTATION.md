# Aurora Identity Purpose-Bound Re-authentication Proof

Status: **isolated implementation foundation**
Version: **0.1**

## Purpose

Sensitive Aurora Identity and administrative operations may require a fresh authentication without locking or replacing the current session.

A re-authentication proof is therefore distinct from a normal login session grant. It proves that a stable authenticated `user_id` recently completed authentication for one explicitly named operation class.

## Security contract

A proof is:

- opaque outside the trusted Identity/authorization boundary;
- bound to exactly one stable `user_id`;
- bound to exactly one purpose;
- short-lived;
- single-use;
- transient and cleared on Identity Service generation loss/restart;
- derived from fresh random bearer material;
- represented in the transient store only by a cryptographic token tag;
- never a substitute for the operation's own capability authorization.

The current core enforces an absolute TTL ceiling of 120 seconds. Production policy may choose a shorter lifetime.

## Purpose classes

The v1 core defines:

- `ROTATE_PRIMARY_KEY`;
- `ENROLL_AUTHENTICATOR`;
- `REVOKE_AUTHENTICATOR`;
- `CHANGE_RECOVERY_POLICY`;
- `EXPORT_RECOVERY_MATERIAL`;
- `APPROVE_USER_CREATION`;
- `CHANGE_LOCAL_ROLE`;
- `GRANT_RESOURCE_ACCESS`.

Purpose identifiers are protocol/security identifiers, not presentation strings.

## Consumption rule

Consumption is intentionally destructive before user/purpose matching is returned to the caller.

Therefore:

- correct user + correct purpose -> success;
- replay -> not found;
- wrong purpose -> mismatch and the proof is burned;
- wrong user -> mismatch and the proof is burned;
- expired proof -> rejected and invalidated.

This prevents a stolen or misrouted bearer token from being probed repeatedly against different operations or identities.

## Storage

The first implementation uses a bounded 32-entry transient in-memory store.

Proofs are not identity database records and must not survive:

- reboot;
- Identity Service restart/generation change;
- explicit transient-store clearing.

The store enforces unique token tags and reclaims expired slots.

## Separation from login grants

A login session grant authorizes Session Manager to establish a session.

A re-authentication proof authorizes no session. It only provides fresh-auth evidence for a separately capability-authorized sensitive operation.

The two token classes must remain domain-separated and must not be accepted interchangeably.

## Current implementation

Code:

- `services/identity/include/aurora/identity/reauth_proof.h`
- `services/identity/src/reauth_proof.c`
- `services/identity/include/aurora/identity/reauth_proof_memory.h`
- `services/identity/src/reauth_proof_memory.c`

Host tests verify issuance, expiry, replay rejection, wrong-user/wrong-purpose destructive consumption, invalid policy and store clearing.

## Next integration gate

The isolated core does not yet make production re-authentication available.

The next milestone must add a dedicated Ring 3 Identity Service protocol/capability path that:

1. derives the trusted target `user_id` from active session context rather than an arbitrary UI assertion;
2. performs fresh authentication;
3. issues a proof for one requested purpose;
4. exposes proof consumption only to the corresponding capability-authorized sensitive operation;
5. keeps public failures coarse and secret-free.
