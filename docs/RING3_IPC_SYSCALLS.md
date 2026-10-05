# Aurora Ring 3 IPC Syscalls

Status: **implementation milestone**

## Purpose

Aurora already has capability-aware in-kernel IPC channels. This milestone exposes the minimum Ring 3 syscall surface required for user-space services to exchange messages without exposing VFS/pathname authority directly.

## ABI

The syscall numbers are:

- `AURORA_SYS_IPC_SEND = 4`
- `AURORA_SYS_IPC_RECEIVE = 5`

`IPC_SEND` requires an `AURORA_CAP_IPC_ENDPOINT` capability with `AURORA_RIGHT_WRITE`.

`IPC_RECEIVE` requires an `AURORA_CAP_IPC_ENDPOINT` capability with `AURORA_RIGHT_READ`.

Payloads remain bounded to 256 bytes and capability transfers remain bounded to four handles, matching the kernel IPC queue contract.

All Ring 3 buffers are copied with the checked usercopy layer; kernel pointers are never accepted from user space.

## Capability transfer

The existing IPC escrow/delegation model remains authoritative. A transferred capability must include `AURORA_RIGHT_TRANSFER` in the sender's authority. Protected System State capabilities are intentionally granted without `TRANSFER`, so the Ring 3 IPC surface cannot be used to pass them arbitrarily to another process.

## Protected System State relationship

This ABI does **not** expose Protected System State, VFS operations, or pathnames to normal applications.

The intended service architecture is:

```text
Application
    |
    | IPC endpoint capability
    v
Identity Service (Ring 3)
    |
    | non-transferable Protected State capability
    v
Protected System State
```

A future trusted service bootstrap policy will grant the Identity Service its endpoint and Protected State authority.

## Runtime validation

The kernel boot self-test creates an isolated Ring 3 process with a single `READ|WRITE` IPC endpoint capability. The kernel pre-queues a fixed message, the user process receives it through `IPC_RECEIVE`, echoes the received bytes through `IPC_SEND`, and exits. The kernel validates the exact echoed payload before continuing boot.

Successful validation logs:

```text
[ring3-ipc] capability-gated send/receive syscall round-trip passed
```

Failure occurs before the normal user-space bootstrap completion marker, causing the existing QEMU smoke tests to fail.

## Current limits

- IPC send/receive are non-blocking at this stage; an empty receive or full queue returns an error.
- There is no user-space service registry/connect syscall yet.
- There is no user-space Protected State syscall.
- There is no Service Manager yet to create long-lived privileged daemons and assign their initial capabilities.
- Process/thread resource reclamation remains a separate lifecycle milestone.
