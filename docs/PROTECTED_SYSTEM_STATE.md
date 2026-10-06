# Aurora OS Protected System State

Status: **kernel foundation with fail-closed durable records and a bounded Ring 3 bridge**  
Version: **0.4**

## Purpose

Protected System State is Aurora OS's privileged durable-storage boundary for security-sensitive system components.

It exists so that knowing a pathname is not sufficient authority to read or modify sensitive state. Ordinary applications must not gain access to Identity secrets, service credentials, trust databases, recovery state, or equivalent system-owned records merely because they can guess where those bytes live.

The first planned consumer is Aurora Identity.

## Canonical namespace

Protected state is rooted under:

```text
/system/.protected/<scope>/
```

A scope is a system component name such as `identity`. It is not a user-controlled arbitrary path.

The kernel foundation accepts only bounded alphanumeric, `-`, and `_` scope names and only simple relative record names. Absolute paths, path separators, control characters, `.` and `..` are rejected at the Protected State API boundary.

## Capability boundary

Aurora defines a dedicated capability type:

```text
AURORA_CAP_PROTECTED_STATE
```

A Protected State capability is bound to one exact namespace object. A handle for one namespace cannot be reused to access another namespace.

Current rights are:

- `READ` — read an existing record;
- `WRITE` — create, write, truncate, durability-sync, and publish create-once records;
- `CONTROL` — rename or remove records.

`TRANSFER` is deliberately not accepted by `protected_state_grant()`. A future Service Manager must make any cross-process authority assignment explicit rather than allowing a service to hand privileged system-state authority to arbitrary applications.

## Filesystem boundary

The backing filesystem remains AuroraFS through the ordinary VFS implementation. Protected State is an authorization layer above VFS, not a second filesystem.

The kernel prepares:

```text
/system/.protected
/system/.protected/<scope>
```

with root ownership and mode `0700`. Files created through the Protected State API are forced to root ownership and mode `0600` and are durability-synced.

Filesystem metadata is defense in depth. The canonical authorization decision is the typed capability bound to the namespace.

Direct VFS functions remain privileged kernel APIs. Aurora does not expose an unrestricted Ring 3 pathname syscall for `/system/.protected`.

## Fail-closed storage lookup

AuroraFS v2 and VFS expose explicit lookup results for security-sensitive paths:

```text
FOUND
NOT_FOUND
ERROR
```

`NOT_FOUND` is produced only after a clean, valid namespace traversal. Metadata corruption, checksum failure, inconsistent object references and backing I/O failures remain `ERROR`. Filesystem drivers that still expose only legacy boolean `stat` are conservatively mapped to `ERROR` when lookup fails.

Protected State creates directories or immutable first-publication records only after an explicit `NOT_FOUND`. An unreadable or inconsistent previous record is therefore never treated as permission to create a replacement security root.

## Durable create-once records

Security roots such as the Aurora Identity Machine Secret cannot safely use a naive `create -> write` sequence. A power loss between those two steps could leave the final record name present with incomplete bytes.

Aurora therefore provides `protected_state_create_record_once_durable()` for immutable first-publication records up to `AURORA_PROTECTED_STATE_RECORD_MAX` bytes.

Publication is:

```text
explicit NOT_FOUND
        -> hidden same-directory staging file
        -> root:root / 0600
        -> write complete record
        -> fdatasync + fsync
        -> re-check final name as explicit NOT_FOUND
        -> same-directory transactional rename
        -> fsync final record + sync namespace
```

The final name is never overwritten. If it is positively observed as already present, the operation returns `AURORA_PROTECTED_STATE_CREATE_ONCE_EXISTS`. If either the final record or staging record produces an ambiguous/error lookup, publication fails closed with `AURORA_PROTECTED_STATE_CREATE_ONCE_ERROR`.

The staging name is deterministic and publication is serialized inside the kernel. If a crash happens before rename begins, a later attempt removes a stale staging file only when it can positively observe it. If a crash happens during rename, AuroraFS v2's namespace transaction/recovery machinery resolves the operation to the pre-publication or post-publication namespace image. Because the staging inode is fully synced before rename, a published final name refers to complete staged record contents rather than a partially written destination file.

This primitive is intentionally different from a generic atomic-replace operation. The transactional Identity database still needs a separately specified durable replacement primitive before its rotating dual-slot snapshots move onto Protected State.

## Bounded Ring 3 record bridge

Aurora exposes a deliberately narrow Ring 3 bridge for service processes that already hold a Protected State capability. The bridge does **not** expose general VFS pathname operations.

Two syscalls are currently defined:

```text
AURORA_SYS_PROTECTED_STATE_READ = 6
AURORA_SYS_PROTECTED_STATE_CREATE_ONCE = 7
```

Both calls authorize against the current process capability table. The capability must be type `AURORA_CAP_PROTECTED_STATE`, must be bound to the exact namespace object, and must contain the required right.

### READ

Register ABI:

```text
rax = 6
rdi = protected-state capability handle
rsi = user pointer to record name
rdx = record-name byte length
r10 = user output buffer
r8  = output capacity
```

Results:

- `0..AURORA_SYS_PROTECTED_STATE_IO_MAX` — exact byte length read;
- `AURORA_SYS_RESULT_NOT_FOUND` — clean `NOT_FOUND` lookup;
- `AURORA_SYS_RESULT_ERROR` — capability, usercopy, metadata, bounds, I/O, or other failure.

### CREATE_ONCE

Register ABI:

```text
rax = 7
rdi = protected-state capability handle
rsi = user pointer to record name
rdx = record-name byte length
r10 = user pointer to complete record bytes
r8  = record byte length
```

Results:

- `0` — record durably published;
- `AURORA_SYS_RESULT_EXISTS` — final record was positively observed as already present;
- `AURORA_SYS_RESULT_ERROR` — any other failure.

### ABI bounds

The current service bridge intentionally uses smaller limits than the internal Protected State maximum:

```text
record name <= 64 bytes
record I/O   <= 512 bytes
```

This is sufficient for the 84-byte Aurora Identity Machine Secret record while keeping kernel syscall stack buffers bounded. The limits can be revised only through an explicit ABI change.

Record names use explicit lengths, embedded NUL bytes are rejected, and the normal Protected State relative-name validator still rejects separators, traversal names and control characters. User buffers are accessed only through checked usercopy. Temporary kernel buffers that may contain record bytes are explicitly zeroed before syscall return after data has been copied in or read from storage.

The Ring 3 bridge intentionally exposes no remove, rename, truncate, generic write, namespace creation or capability-transfer operation. The service process receives only the rights needed by policy.

## Fail-closed behavior

Protected State operations fail if any of the following is true:

- the namespace is invalid or not initialized;
- the capability handle is absent, stale, revoked, or the wrong type;
- required rights are missing;
- the capability belongs to a different Protected State namespace;
- the requested record name attempts pathname traversal or escape;
- a create-once record is empty or exceeds the bounded record limit;
- a required lookup returns `ERROR` instead of `FOUND` or `NOT_FOUND` as expected;
- checked usercopy fails;
- the backing VFS mutation/read/sync fails.

No operation falls back to ordinary unrestricted VFS access.

## Runtime verification

When `/system` is available, bootstrap first verifies the kernel Protected State capability boundary and then runs a real Ring 3 process through the record syscall bridge.

The Ring 3 probe receives a process-local Protected State capability with only `READ | WRITE` rights and verifies:

1. an invalid capability returns `ERROR`;
2. reading a cleanly absent record returns `NOT_FOUND`;
3. first create-once publication succeeds;
4. repeated publication returns `EXISTS` rather than overwriting;
5. a subsequent read returns the exact persisted byte count and payload;
6. the process reaches the normal SYSCALL/SYSRET and EXIT path.

The kernel test harness keeps `CONTROL` authority separately only to clean the dedicated `ring3-bridge` test namespace afterward. The Ring 3 process never receives `CONTROL`.

Successful runtime verification logs:

```text
[ring3-protected-state] capability-gated record syscall probe passed
```

If `/system` is unavailable, Protected State runtime tests are skipped. Absence of a system disk must not itself become a kernel panic.

## Identity integration

The intended Identity layout is conceptually:

```text
/system/.protected/identity/
    machine-secret.a
    machine-secret.b
    identity-store-a
    identity-store-b
    ...
```

The Aurora Identity Machine Secret layer already has a service-side transport adapter with `read_record` and `create_record_once_durable` operations. The Ring 3 bridge now supplies matching kernel primitives for a future Identity Service process.

This does **not** yet mean the live login path uses the Identity Service. A trusted service/bootstrap manager still has to instantiate the long-lived Identity process, bind the `identity` namespace, grant its non-transferable capability, and connect login/session IPC to it.

The mutable Identity database remains separate until durable atomic replacement is available.

## Security limits

This foundation protects against unauthorized running software only to the extent that Aurora's capability and syscall boundaries are respected.

It does **not** yet provide:

- TPM or secure-hardware sealing;
- encryption of protected records at rest;
- resistance to an offline attacker with unrestricted raw-disk access;
- a production Service Manager that launches/restarts Identity and grants its namespace capability;
- resource reclamation required for restartable long-lived services;
- a generic durable atomic-replace primitive for mutable transactional service state;
- general filesystem syscall mediation, which remains a separate future subsystem.

These are separate future hardening/integration stages.
