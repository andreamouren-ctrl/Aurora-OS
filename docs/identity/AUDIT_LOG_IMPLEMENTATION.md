# Aurora Identity Security Audit Log

Status: **Persistent backend + lifecycle + scoped read integration**
Version: **0.1**

## Purpose

Aurora Identity requires structured security events that can later back Security Activity UI, incident review and policy diagnostics without becoming a credential-leak channel.

The audit subsystem is separate from the primary identity/credential database.

## Current core

The current implementation provides a bounded in-memory audit log with a fixed capacity of 64 records. Every accepted record carries:

- record version;
- event type;
- outcome;
- non-secret reason code;
- monotonically increasing local audit sequence;
- current monotonic-clock observation;
- optional session generation;
- optional stable `user_id`.

The current event classes cover authentication success/failure, session start/lock/unlock/logout/termination, re-authentication success/failure and Aurora Key rotation.

## Security rules

Audit records must never contain:

- raw Aurora Keys;
- normalized Aurora Keys;
- lookup tags;
- salts or verifier bytes;
- session-grant tokens;
- re-authentication proof tokens;
- recovery secrets;
- private authenticator material;
- full challenge/response payloads.

Unknown-user authentication failure may be recorded without a `user_id`.

A supplied user identifier must be non-zero. The log is bounded and overwrites the oldest record rather than growing attacker-controlled memory.

## Time semantics

The current `monotonic_ms` value is only meaningful inside the active boot/clock epoch. It is not a wall-clock timestamp and must not be presented as one.

Persistent audit storage will pair durable sequence ordering with future trusted wall-clock/boot-epoch metadata when that platform contract exists.

## Durable protected-state snapshots

The persistence layer uses two independent bounded snapshots, `identity-audit.a` and `identity-audit.b`. Each snapshot is encoded explicitly in little-endian form with schema version, generation, record count, next sequence and CRC32 corruption detection.

A durable append is copy-on-write: the candidate log is serialized into the generation-selected slot and becomes live only after the atomic Protected State replacement succeeds. A failed write leaves the current in-memory generation unchanged. On reopen, Aurora selects the newest valid generation; if it is corrupt but the previous slot remains valid, the previous generation is recovered. If existing audit snapshots are present but none are valid, open fails closed rather than silently starting an empty log.

The maximum encoded snapshot remains below the current 8 KiB Protected State I/O bound.

CRC32 detects accidental corruption only. It is not a cryptographic authenticity mechanism; tamper authentication remains a future protected-storage hardening item.

## Runtime integration

The Ring 3 Identity Service opens the dual-slot audit store through its existing scoped Protected State transport. Existing-but-corrupt audit snapshots fail service initialization closed rather than silently resetting history.

The service emits durable records for:

- authentication success/failure, including throttling and service-side failures;
- re-authentication success/failure bound to the active session generation;
- rejected/replayed rotation proofs;
- Aurora Key rotation success/failure.

A successful authentication or re-authentication is not exposed to the caller unless its audit record has been durably published. Credential rotation is audited immediately after its atomic credential commit; an audit publication failure is surfaced as a storage failure rather than reported as a clean security operation.

Session Manager lifecycle integration persists start, lock, unlock and normal logout before exposing the corresponding state transition. Forced termination is distinct from normal logout and remains fail-safe: session/profile authority is revoked even if the termination audit append itself cannot be completed.

## Security Activity read access

The Identity Service exposes a bounded, capability-gated read path for the currently authenticated user. The kernel-side Identity client derives the stable user id and current session generation from Session Manager state; callers do not supply an arbitrary account identity.

`AURORA_CAP_IDENTITY_AUDIT_READ` is separate from the Session Manager's audit-emission authority. Each request receives only `READ`, never `WRITE`, `CONTROL` or `TRANSFER`, and the sender-side handle is revoked immediately after transfer.

Records are returned newest-first with an exclusive sequence cursor. Only records carrying the current stable `user_id` are eligible. Machine-wide or anonymous records are not exposed through the ordinary user's Security Activity path. The wire result contains only event class, outcome, non-secret reason code, sequence, monotonic observation and historical session generation. It does not contain credential ids, Aurora Keys, lookup tags, grants, proofs or authenticator material.

Remaining integration:

- trusted wall-clock/boot-epoch metadata when that platform contract exists;
- System App presentation once the interactive compositor/window-management layer is available;
- a separately authorized Administrator/security-operator view if later policy requires machine-wide events.
