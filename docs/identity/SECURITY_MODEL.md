# Aurora Identity Security Model

Status: **Canonical design**
Version: **0.2**

## 1. Security objectives

Aurora Identity protects local account access while preserving Aurora OS's offline-first design.

Primary objectives:

- prevent plaintext credential persistence;
- resist offline guessing of stolen credential databases;
- limit online/local guessing through throttling;
- prevent accidental account enumeration;
- keep applications isolated from authentication material;
- allow credentials and authenticators to be individually changed or revoked;
- preserve a usable recovery path without storing reversible Aurora Keys;
- keep authentication available without cloud connectivity;
- fail closed when the identity service cannot establish trust.

## 2. Threat model

Aurora Identity should consider at least these adversaries:

- a person with physical access to a locked machine;
- malware running as an ordinary unprivileged application;
- a malicious or malformed removable USB device;
- an attacker who steals a copy of the identity database;
- an attacker who steals an Aurora Identity Drive;
- an attacker who clones the contents of a conventional USB flash drive;
- a process attempting to impersonate the Identity Service or Session Manager;
- a user attempting repeated high-rate Aurora Key guesses;
- corrupted or partially migrated identity storage.

The first releases do not claim protection against a fully compromised kernel, invasive hardware attacks, or an attacker with arbitrary physical memory extraction capability.

## 3. Core security invariants

1. A raw Aurora Key is never stored persistently.
2. A successful authentication produces a session grant, not credential disclosure.
3. The stable `user_id` is not itself an authentication secret.
4. UI processes do not directly read the protected identity database.
5. Ordinary applications cannot call privileged identity-management operations without explicit capabilities.
6. No authentication method can silently create a new identity unless machine policy explicitly allows it.
7. Revoking one authenticator does not revoke the user's identity unless policy explicitly disables the identity.
8. Recovery credentials are independent from the primary Aurora Key.
9. Audit logs contain event metadata, never credential values, verifier bytes, recovery codes, or private authenticator keys.
10. Network availability is not required for standard local authentication.

## 4. Aurora Key protection

Aurora Key verification uses a memory-hard password derivation function. Target: **Argon2id** with a unique random salt per credential and versioned parameters.

Stored material may include:

- algorithm/version identifier;
- salt;
- Argon2id parameters;
- verifier/output;
- creation and migration metadata.

Stored material must not include the original Aurora Key.

Verification comparisons must avoid obvious timing leaks where applicable.

Credential parameters must be upgradeable so a successful login can opportunistically re-derive a stronger verifier when policy changes.

## 5. Randomness

The following require a cryptographically secure random source before production implementation:

- generated Aurora Keys;
- salts;
- `user_id` values when randomly generated;
- session identifiers/grants;
- Identity Drive credential identifiers;
- authenticator private keys where software-generated;
- recovery credentials;
- challenge nonces.

A deterministic PRNG intended for visual effects, tests, or boot decoration must never be reused as the security RNG.

## 6. Rate limiting

Authentication failures are tracked by the Identity Service and machine policy.

The implementation should support progressive delay based on a combination of:

- candidate identity/credential context where safe;
- machine-wide failure pressure;
- authenticator-specific state;
- recent failure timestamps.

The UI must present generic waiting/error states without revealing excessive internal detail.

Rate-limit state must survive reboot once persistent storage exists, otherwise rebooting could bypass protection.

## 7. Account enumeration

The default login screen does not display account tiles.

For Aurora Key authentication, error messaging should minimize the distinction between:

- no matching credential;
- wrong credential;
- disabled identity;
- temporarily throttled identity.

The user-creation offer is a deliberate exception only on machines where local self-service account creation is permitted.

Managed machines may disable unknown-key account creation entirely.

## 8. Credential buffers

Processes handling secrets should:

- use bounded buffers;
- avoid unnecessary copies;
- clear buffers after verification or cancellation where practical;
- exclude secret buffers from debug formatting and structured logging;
- avoid persisting them in swap until the memory-security model defines secret-page handling;
- avoid sending them through general clipboard, notification, or accessibility channels.

Future hardening may include locked/non-pageable secret memory once memory-manager contracts support it.

## 9. Aurora Identity Drive security

A conventional USB flash drive is **not** equivalent to a hardware security key.

A standard Identity Drive can store a credential container and participate in login, but an attacker may be able to copy the drive contents. Therefore:

- the drive must never contain the user's Aurora Key;
- each drive uses a separate revocable credential;
- sensitive private material should be encrypted/wrapped where practical;
- drive serial number, vendor ID, product ID, filesystem UUID, or volume label must not be treated as sufficient cryptographic proof of possession;
- optional PIN/second-factor mode should be supported;
- auto-login on insertion is a user policy choice and should be disabled for higher-security configurations;
- a future secure-hardware authenticator uses non-exportable keys and challenge-response and is considered a stronger class.

A cloned conventional Identity Drive must be treated as a known limitation of the standard-drive security tier, not hidden by product wording.

## 10. Removable-media parsing

Identity Drive content is untrusted input.

Parsing must use:

- bounded file sizes;
- strict format/version validation;
- authenticated integrity checks;
- no executable content;
- no automatic shell/action execution;
- filesystem parsing outside the kernel where practical;
- rejection of malformed or unsupported records without affecting system availability.

## 11. Session grants

A successful authentication returns an opaque, short-lived session grant bound to:

- authenticated `user_id`;
- issuing Identity Service instance/key context;
- creation timestamp;
- expiry/consumption rules;
- optional target session request.

The grant should be single-use for session bootstrap unless a different explicitly designed token type is needed.

The Session Manager verifies the grant through a trusted IPC/capability path. It never receives the original Aurora Key or authenticator private key.

## 12. Lock, logout and re-authentication

Locking a session invalidates or suspends interactive access but does not require destroying all user processes unless policy chooses to do so.

Re-authentication should produce a fresh proof/grant for sensitive operations.

Logout terminates the session's user capabilities according to Session Manager policy and clears transient authentication state.

## 13. Recovery security

Recovery must not be implemented as reversible storage of the Aurora Key.

Permitted future mechanisms include:

- separate high-entropy recovery credential;
- previously enrolled trusted hardware key;
- trusted-device approval;
- managed administrator recovery;
- explicitly enabled encrypted escrow.

Recovery events should be prominently audited and may require forced credential rotation afterward.

## 14. Audit policy

Useful identity security events include:

- successful authentication method class;
- failed authentication count/state without secret values;
- credential created/changed/revoked;
- Identity Drive enrolled/revoked;
- recovery initiated/completed;
- identity disabled/enabled;
- session started/locked/unlocked/logged out;
- database migration success/failure.

Audit records must not include:

- Aurora Key characters or length if that leaks useful information unnecessarily;
- salts/verifiers;
- recovery credential values;
- private keys;
- full challenge/response payloads;
- raw removable-drive secrets.

## 15. Secure failure behavior

If the identity database is unavailable, corrupted, or cannot be authenticated/migrated safely, Aurora must not create a normal authenticated session by guessing or bypassing checks.

Instead, the system enters a controlled recovery path.

If the Identity Service crashes during authentication, the request fails and credential buffers are discarded.

If a removable authenticator is removed mid-authentication, the request is cancelled safely.

## 16. Security review gates

Before Aurora Identity is considered production-ready, the project must complete:

- threat-model review;
- cryptographic implementation/dependency review;
- Argon2id parameter benchmarking on supported hardware;
- secure RNG validation;
- IPC capability review;
- identity database corruption/migration tests;
- removable-media fuzzing;
- session-grant replay tests;
- rate-limit bypass tests;
- secret-leak audit across logging/crash paths;
- recovery-abuse review.
