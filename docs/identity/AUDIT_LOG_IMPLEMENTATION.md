# Aurora Identity Security Audit Log

Status: **Persistent runtime integration in progress**
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

Remaining integration:

- Session Manager lifecycle transitions (start/lock/unlock/logout/termination);
- capability-gated Security Activity read access;
- trusted wall-clock/boot-epoch metadata when that platform contract exists.

Security Activity read access must never expose credential material or opaque authentication tokens.
