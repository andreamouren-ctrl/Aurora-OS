# Aurora Identity Create Protocol

## Status

Canonical protocol contract for creating a local Aurora Identity through the trusted Ring 3 Identity Service.

The create path is capability-gated and must never be inferred from possession of the generic Identity IPC endpoint alone.

## Authority

A create request MUST transfer exactly one `AURORA_CAP_IDENTITY_CREATE` capability with `AURORA_RIGHT_CONTROL`.

The transferred handle MUST NOT retain `AURORA_RIGHT_TRANSFER` in the Identity Service. The service validates the capability type and rights and revokes the received handle immediately after authorization.

The caller-side authority is request-scoped and should be revoked immediately after the transfer succeeds.

## Protocol version

Create support is introduced in Identity Service protocol version 3.

Messages:

- `BEGIN_CREATE`
- `CREATE_PENDING`
- `QUERY_CREATE`
- `CREATE_RESULT`
- `CANCEL_CREATE`
- `CREATE_CANCELLED`

The bounded create request carries only the normalized Aurora Key candidate. The key length remains limited to 32 bytes and the full payload remains below Aurora IPC's 256-byte payload limit.

## State machine

1. The client submits `BEGIN_CREATE` with a nonzero request ID and one create authority capability.
2. The service validates and revokes the transferred authority.
3. If policy/runtime prerequisites are available, the service copies the candidate into process-owned protected working memory and replies `CREATE_PENDING`.
4. `QUERY_CREATE` executes or advances the queued creation work.
5. `CANCEL_CREATE` is valid while the queued job has not been committed and clears all candidate material.
6. `CREATE_RESULT` returns only a coarse public result plus opaque binary user and credential identifiers on success.

A successful durable commit is never cancellable after publication.

## Core operation

The production service MUST use `aurora_identity_create_with_key()` and the already-mounted production Identity core.

The persistent store is authoritative for bootstrap role assignment:

- the first committed local identity becomes `ADMINISTRATOR`;
- every later local identity becomes `STANDARD_USER`.

The GUI and kernel client MUST NOT choose or override the persistent role.

## Bootstrap creation boundary

The current pre-authentication CREATE authority exists only for first-user setup.

Before invoking the generic creation core, the production Identity runtime checks the persistent identity count:

- `identity_count == 0`: bootstrap CREATE may proceed;
- `identity_count >= 1`: the pre-authentication CREATE path fails closed with `POLICY_DENIED`.

This check is enforced by the Identity Service runtime, not by the login GUI. Possession of an IPC endpoint or an `AURORA_CAP_IDENTITY_CREATE` request capability therefore cannot bypass post-bootstrap account-creation policy.

Additional persistent users require a future authenticated machine-policy / Administrator authorization path with purpose-bound re-authentication. That path is deliberately separate from first-user bootstrap and may still call the generic Identity creation core after its stronger authorization checks.

## Randomness and degraded boot

Identity creation requires cryptographically secure random generation for identifiers, salt, and verifier creation. If the DRBG or other creation prerequisites are unavailable, creation fails closed with `SERVICE_UNAVAILABLE`.

Authentication of an already-existing identity may remain available in degraded mode when its prerequisites are satisfied. This does not authorize creation without fresh secure randomness.

## Public errors

The create protocol exposes only coarse public outcomes:

- success;
- already exists;
- invalid request;
- unauthorized;
- busy;
- policy denied;
- service unavailable;
- storage failure;
- internal failure.

Internal database, cryptographic, machine-secret, or record-layout details must not cross the IPC boundary.

## Credential handling

The Aurora Key candidate must be zeroed from:

- the received request object after validation/copy;
- queued service working memory immediately after execution or cancellation;
- client-side input memory after successful IPC transfer.

No raw Aurora Key is persisted.

## Session boundary

Creating an identity does not itself create a desktop session. A later Session Manager flow must authenticate or consume a purpose-bound session grant before establishing a user session.

The kernel login UI must not synthesize session authority after `CREATE_RESULT`.
