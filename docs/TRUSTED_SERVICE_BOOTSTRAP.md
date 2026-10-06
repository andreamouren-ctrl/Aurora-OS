# Aurora Trusted Service Bootstrap

Status: **runtime-verified pre-Service-Manager foundation with blocking IPC wait**
Version: **0.3**

## Purpose

Aurora must be able to start privileged operating-system services in Ring 3 without moving their policy into the kernel and without giving ordinary applications the same authority.

This milestone introduces the first explicit trusted-service bootstrap contract. It is intentionally smaller than the future Service Manager.

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

The first runtime consumer is an isolated `identity-service-probe` process.

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

This removes busy-polling as a prerequisite for keeping a future Identity daemon alive. It does **not** yet make the service restartable; lifecycle reclamation remains separate.

## Protected State bridge

Aurora has a bounded Ring 3 Protected State bridge with capability-gated record operations:

- `AURORA_SYS_PROTECTED_STATE_READ`;
- `AURORA_SYS_PROTECTED_STATE_CREATE_ONCE`.

Those syscalls match the `read_record` / `create_record_once_durable` boundary used by the Aurora Identity Machine Secret adapter. They do not expose generic VFS path access, removal, rename, truncate, namespace creation, or unrestricted capability transfer.

This bootstrap milestone proves that a trusted Identity process can receive the exact namespace capability needed to use that bridge. It does not yet link the full `services/identity` runtime into the Ring 3 process.

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

## Deliberate limitations

This is not yet the production Service Manager:

- the current Identity bootstrap probe exits after validating startup;
- there is no service discovery or registry;
- there are no restart/dependency policies;
- terminated thread/process/address-space resources are not yet generally reclaimed;
- the Identity DRBG entropy handoff is not yet part of the startup ABI;
- the real Identity service executable is not yet linked from `services/identity`;
- the mutable Identity database still lacks a durable atomic-replace Protected State primitive;
- IPC wait currently supports one waiter per endpoint and sender-side queue backpressure remains non-blocking.

These limits are intentional. Lifecycle/reclamation and controlled entropy should be completed before Aurora treats Identity as a restartable production daemon.

## Next gates

1. implement thread/process/address-space reclamation needed for restartable services;
2. define the controlled entropy/DRBG bootstrap handoff;
3. port the real Identity Service runtime so it consumes the Machine Secret through the existing Protected State bridge;
4. replace the probe with the long-lived service;
5. connect the login/session protocol only after those lifecycle guarantees exist.
