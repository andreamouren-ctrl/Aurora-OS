# Aurora Ring 3 IPC Syscalls

Status: **send/receive plus runtime-verified blocking wait foundation**
Version: **0.2**

## Purpose

Aurora has capability-aware in-kernel IPC channels. The Ring 3 syscall surface lets user-space services exchange bounded messages without exposing VFS/pathname authority directly and now lets a service sleep until its private endpoint becomes readable instead of polling.

## ABI

The syscall numbers are:

- `AURORA_SYS_IPC_SEND = 4`
- `AURORA_SYS_IPC_RECEIVE = 5`
- `AURORA_SYS_IPC_WAIT = 8`

`IPC_SEND` requires an `AURORA_CAP_IPC_ENDPOINT` capability with `AURORA_RIGHT_WRITE`.

`IPC_RECEIVE` and `IPC_WAIT` require an `AURORA_CAP_IPC_ENDPOINT` capability with `AURORA_RIGHT_READ`.

Payloads remain bounded to 256 bytes and capability transfers remain bounded to four handles, matching the kernel IPC queue contract.

All Ring 3 buffers are copied with the checked usercopy layer; kernel pointers are never accepted from user space.

## Blocking wait

`IPC_WAIT` does not consume a message. It waits until the endpoint is readable and returns `0`; the caller then uses the ordinary `IPC_RECEIVE` syscall.

If a message is already queued, `IPC_WAIT` returns immediately. If the queue is empty, the IPC layer registers the current scheduler thread as the endpoint waiter and the scheduler moves that Ring 3 thread to `BLOCKED`.

The first foundation accepts at most **one waiter per endpoint**. A second concurrent waiter is rejected rather than silently replacing the first.

### Lost-wakeup protection

Registration and parking are deliberately separate operations, so Aurora handles the race where a sender enqueues between them:

1. the waiter is registered under the IPC channel lock;
2. the sender enqueues the message, clears the registered waiter under that same lock, then releases the IPC lock;
3. only after releasing the IPC lock does the sender invoke the scheduler wake path;
4. if the target thread is already `BLOCKED`, it becomes `RUNNABLE`;
5. if the wake arrives just before the thread reaches `BLOCKED`, the scheduler records `wake_pending`;
6. the attempted park consumes `wake_pending` and returns to Ring 3 immediately instead of sleeping.

This closes the classic check/register/park lost-wakeup window without holding the IPC channel lock while taking the scheduler lock.

A blocked syscall stores a stable IRET-compatible resume frame in the scheduler thread record. It does not retain a pointer to the transient live SYSCALL stack frame.

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

The trusted-service bootstrap grants the Identity Service its private endpoint and least-privilege Protected State authority.

## Runtime validation

Aurora keeps the original non-blocking send/receive round-trip probe and adds a dedicated blocking-wait proof.

For the blocking proof, the kernel creates an isolated Ring 3 process with one `READ|WRITE` endpoint. The process enters `IPC_WAIT`; the kernel does **not** enqueue the test message until it has observed the user thread in scheduler state `BLOCKED`. The kernel then sends the payload, which must wake the thread. The user process receives and echoes the exact bytes, signals completion and exits successfully.

Successful validation logs include:

```text
[ring3-ipc] capability-gated send/receive syscall round-trip passed
[ring3-ipc] blocking wait/wakeup syscall probe passed
```

Failure causes the existing QEMU smoke path to stop before normal login bootstrap.

## Current limits

- `IPC_RECEIVE` itself remains non-blocking; callers use `IPC_WAIT` followed by `IPC_RECEIVE`.
- Only one blocked waiter is supported per endpoint in this foundation.
- Queue-full `IPC_SEND` still returns an error; sender-side blocking/backpressure is not implemented yet.
- There is no user-space service registry/connect syscall yet.
- The trusted Identity bootstrap exists, but the full long-lived `services/identity` runtime is not connected yet.
- Process/thread/address-space reclamation remains a separate lifecycle milestone required for restartable services.
