# Aurora Identity Security Audit Log

Status: **Implementation foundation**
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

## Next integration step

The next step is runtime opening/emission through the protected-state adapter, followed by events from:

- authentication success/failure;
- re-authentication success/failure;
- Aurora Key rotation;
- Session Manager lifecycle transitions.

Security Activity read access will be a separate capability and must not expose credential material.
