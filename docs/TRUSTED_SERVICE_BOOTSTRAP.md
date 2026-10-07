# Aurora Trusted Service Bootstrap

Status: **runtime-verified trusted-service bootstrap foundation; live Identity/Session services now build on it**
Version: **0.4**

## Purpose

Aurora must be able to start privileged operating-system services in Ring 3 without moving their policy into the kernel and without giving ordinary applications the same authority.

This document records the trusted-service bootstrap contract that became the foundation for the live Ring 3 Identity Service and Session Manager. The bootstrap mechanism remains intentionally smaller than a general systemd/SCM-equivalent Service Manager.

## Startup model

A trusted service is described by a kernel-owned manifest containing:

- service name;
- immutable bootstrap image;
- Protected System State scope;
- requested Protected State rights.

The bootstrap path creates a private Ring 3 process and gives it exactly two initial capabilities:

1. a private IPC endpoint with `READ|WRITE`;
2. one `AURORA_CAP_PROTECTED_STATE` capability for its declared scope.

Protected State authority is non-transferable. The startup block is ABI-versioned and written only into the service's private user stack. Ordinary user processes do not pass through this trusted bootstrap path and receive no such authority implicitly.

## Identity Service runtime proof

The first runtime consumer was the isolated `identity-service-probe`. The same least-authority bootstrap model is now used by the live supervised Ring 3 Identity path.

Its manifest binds the canonical `identity` Protected State namespace with the minimum authority currently required by the Machine Secret transport:

```text
READ | WRITE
```

`CONTROL` is deliberately not granted. `TRANSFER` is also absent.

From Ring 3 the probe:

1. validates startup ABI version 1;
2. checks that its Protected State handle is type `AURORA_CAP_PROTECTED_STATE`;
3. verifies `READ|WRITE` authority;
4. verifies that `TRANSFER` authority is absent;
5. sends the fixed `AURIDRDY` readiness message through its private IPC endpoint;
6. exits successfully.

The kernel supervisor additionally verifies that the same service capability does **not** satisfy `CONTROL` or `TRANSFER` lookups. Readiness is accepted only when the message is exact, contains no transferred capabilities, the user thread terminates, and the process exits with code zero.

Successful boot logs:

```text
[service] trusted Ring 3 Identity bootstrap + least-privilege capability assignment passed
```

If `/system` is unavailable, the storage-dependent proof is skipped rather than crashing a diskless boot. When `/system` exists, failure is fail-closed and stops bootstrap.

## Blocking IPC foundation

Aurora now provides `AURORA_SYS_IPC_WAIT` in addition to send/receive. A trusted Ring 3 service can sleep while its endpoint queue is empty and be made runnable only after a sender enqueues work.

The first wait implementation supports one blocked waiter per endpoint and includes an explicit `wake_pending` handshake in the scheduler so an enqueue racing with the final park operation cannot be lost. The runtime proof sends only after the target user thread has been observed in scheduler state `BLOCKED`.

This removed busy-polling as a prerequisite for a long-lived Identity daemon. Subsequent lifecycle and supervision milestones added deterministic process/thread reclamation and bounded service restart.

## Protected State bridge

Aurora has a bounded Ring 3 Protected State bridge with capability-gated record operations:

- `AURORA_SYS_PROTECTED_STATE_READ`;
- `AURORA_SYS_PROTECTED_STATE_CREATE_ONCE`.

Those syscalls match the `read_record` / `create_record_once_durable` boundary used by the Aurora Identity Machine Secret adapter. They do not expose generic VFS path access, removal, rename, truncate, namespace creation, or unrestricted capability transfer.

This bootstrap milestone proved that a trusted Identity process can receive the exact namespace capability needed to use that bridge. The full `services/identity` Ring 3 runtime is now connected to this authority path.

## Security boundary

The intended path is:

```text
Application
    |
    | endpoint capability
    v
Identity Service (Ring 3)
    |
    | private IPC endpoint
    | non-transferable identity Protected State capability (READ|WRITE)
    v
Protected System State
```

Ordinary applications do not receive the Protected State capability. The trusted service manifest is kernel-owned and therefore not user-controlled input.

## Current limitations

The original blockers that followed this bootstrap milestone have now been closed:

- thread/process/address-space reclamation is implemented and runtime verified;
- bounded service supervision/restart exists;
- controlled entropy handoff into the Ring 3 Identity runtime exists;
- the real `services/identity` runtime is connected;
- the live login/session path consumes Identity service results through one-time grants;
- Session Manager and User Session Host foundations exist.

The generic service layer still lacks:

- a complete service registry/discovery model;
- declarative dependency graphs and health dependencies;
- time-based restart backoff/crash-rate windows;
- generic administrative stop/kill controls;
- broad watchdog/telemetry/audit integration;
- a mature production configuration model.

IPC wait remains intentionally bounded, and future endpoint/waiter scaling must preserve the existing lost-wakeup guarantees.

## Current role

Trusted Service Bootstrap is now a stable lower-layer mechanism used by later service-supervision and Identity/session milestones.

Future work should generalize orchestration above this bootstrap contract rather than moving service policy back into Ring 0.
