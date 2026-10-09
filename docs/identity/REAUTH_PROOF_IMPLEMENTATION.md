# Aurora Identity Purpose-Bound Re-authentication Proof

Status: **Ring 3 issuance path implemented; sensitive-operation consumption pending**
Version: **0.1**

## Purpose

Sensitive Aurora Identity and administrative operations may require a fresh authentication without locking or replacing the current session.

A re-authentication proof is therefore distinct from a normal login session grant. It proves that a stable authenticated `user_id` recently completed authentication for one explicitly named operation class.

## Security contract

A proof is:

- opaque outside the trusted Identity/authorization boundary;
- bound to exactly one stable `user_id`;
- bound to exactly one authenticated session generation;
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
- wrong session generation -> mismatch and the proof is burned;
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

## Ring 3 issuance integration

Identity protocol v5 adds a dedicated asynchronous re-authentication flow:

```text
BEGIN_REAUTH
 -> REAUTH_PENDING
 -> QUERY_REAUTH
 -> REAUTH_RESULT
```

Cancellation is available before the expensive verification step.

The kernel Identity bridge does not accept an arbitrary `user_id` or session generation from its UI caller. It derives both from the active Session Manager binding, mints a request-scoped `AURORA_CAP_IDENTITY_REAUTH`, delegates only `CONTROL`, and revokes its sender handle immediately after IPC send.

The Ring 3 Identity Service:

1. validates and revokes the received re-auth capability;
2. validates the purpose and expected stable identity;
3. executes fresh Aurora Key authentication;
4. compares the authenticated `user_id` against the session-bound expected identity;
5. collapses a valid credential for a different identity to ordinary `AUTH_FAILED`;
6. issues a short-lived proof only for a successful same-user match and binds it to the active session generation;
7. never emits a normal login session grant from the re-auth path.

Production runtime policy currently uses a 60-second proof TTL.

The proof HMAC key is derived separately from Machine Secret under `AURORA.IDENTITY.REAUTH-PROOF-KEY.V1`; token tagging also uses the separate `AURORA.IDENTITY.REAUTH-PROOF.V1` domain.

## Remaining integration gate

Issuance is production-wired, but a proof intentionally has no generic privileged effect. The next milestone is to make a concrete sensitive operation—starting with Aurora Key rotation—consume the proof internally while also requiring its own management capability. A generic UI-accessible “consume proof” endpoint must not be introduced.
