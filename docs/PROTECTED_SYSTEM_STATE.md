# Aurora OS Protected System State

Status: **kernel foundation with fail-closed durable create-once record publication**  
Version: **0.3**

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

`TRANSFER` is deliberately not accepted by `protected_state_grant()` in this foundation. The future Service Manager must make any cross-process authority assignment explicit rather than allowing a service to hand privileged system-state authority to arbitrary applications.

## Filesystem boundary

The backing filesystem remains AuroraFS through the ordinary VFS implementation. Protected State is an authorization layer above VFS, not a second filesystem.

The kernel prepares:

```text
/system/.protected
/system/.protected/<scope>
```

with root ownership and mode `0700`. Files created through the Protected State API are forced to root ownership and mode `0600` and are durability-synced.

Filesystem metadata is defense in depth. The canonical authorization decision is the typed capability bound to the namespace.

At this stage direct VFS functions remain privileged kernel APIs. They are not exposed as an unrestricted Ring 3 pathname interface. When Aurora later exposes general file syscalls/brokers, `/system/.protected` must remain excluded from ordinary pathname access; user space must reach protected state only through an explicitly authorized system service path.

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

## Fail-closed behavior

Protected State operations fail if any of the following is true:

- the namespace is invalid or not initialized;
- the capability handle is absent, stale, revoked, or the wrong type;
- required rights are missing;
- the capability belongs to a different Protected State namespace;
- the requested record name attempts pathname traversal or escape;
- a create-once record is empty or exceeds the bounded record limit;
- a required lookup returns `ERROR` instead of `FOUND` or `NOT_FOUND` as expected;
- the backing VFS mutation/read/sync fails.

No operation falls back to ordinary unrestricted VFS access.

## Runtime verification

When `/system` is available, the bootstrap runtime self-test verifies:

- namespace preparation on AuroraFS;
- full read/write/control capability access;
- read-only access succeeds for reads and fails for writes;
- invalid handles fail;
- a capability for another namespace fails;
- traversal/absolute-name attempts fail;
- rename/truncate/fsync work through the gate;
- revocation immediately removes authority;
- the self-test namespace can be cleaned without removing the canonical `.protected` parent.

AuroraFS separately runtime-verifies the namespace transaction and recovery behavior used by durable create-once publication. The VFS/AuroraFS tri-state lookup gate is runtime-covered by existing filesystem/VFS smoke tests. A later Ring 3 Protected State bridge milestone will exercise `protected_state_create_record_once_durable()` end-to-end through a user-space capability.

If `/system` is unavailable, Aurora continues booting and reports that the runtime self-test was skipped. Absence of a system disk must not itself become a kernel panic.

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

The future Aurora Identity Service will receive the namespace capability from trusted system policy. Ordinary applications will not.

The Machine Secret's Protected State adapter can map its `create_record_once_durable` transport operation onto this kernel primitive once the Ring 3 bridge is installed. The Identity database remains separate until durable atomic replacement is available.

## Security limits

This foundation protects against unauthorized running software only to the extent that Aurora's capability and future syscall/broker boundaries are respected.

It does **not** yet provide:

- TPM or secure-hardware sealing;
- encryption of protected records at rest;
- resistance to an offline attacker with unrestricted raw-disk access;
- a production Service Manager that grants the Identity capability;
- Ring 3 Protected State syscalls/bridge;
- a generic durable atomic-replace primitive for mutable transactional service state;
- generic VFS syscall filtering, because unrestricted pathname syscalls do not yet exist.

These are separate future hardening/integration stages.
