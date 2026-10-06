# Aurora Trusted Service Bootstrap

Status: **runtime-verified pre-Service-Manager foundation with blocking IPC and lifecycle reclamation**
Version: **0.4**

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

From Ring 3 the probe validates the startup ABI and capability type/rights, sends the fixed `AURIDRDY` readiness message through its private IPC endpoint, then exits successfully.

The kernel supervisor additionally verifies that the service capability does **not** satisfy `CONTROL` or `TRANSFER` lookups. Readiness is accepted only when the message is exact, contains no transferred capabilities, the user thread terminates, and the process exits with code zero.

After the READY proof, the bootstrap reclaims the probe's scheduler slot, kernel stack, process-owned image/user-stack frames, private page tables and process object. It then runs the repeated lifecycle reuse gate before reporting bootstrap success.

## Blocking IPC foundation

Aurora provides `AURORA_SYS_IPC_WAIT` in addition to send/receive. A trusted Ring 3 service can sleep while its endpoint queue is empty and be made runnable only after a sender enqueues work.

The current implementation supports one blocked waiter per endpoint and includes a `wake_pending` handshake so an enqueue racing with the final park operation cannot be lost. Its runtime proof sends only after the target thread has been observed in scheduler state `BLOCKED`.

## Lifecycle foundation

Aurora has explicit lifecycle reclamation for the current one-thread Ring 3 process model:

- terminated scheduler slots return to `THREAD_UNUSED`;
- kernel stacks return to reusable kernel-heap ranges;
- process-owned image and user-stack frames are freed by the Process Manager;
- the VMM frees private page-table pages only;
- process objects are released after reaching `AURORA_PROCESS_REAPED`;
- address-space destruction is refused while any online CPU still reports that space active.

The hardened runtime gate performs 96 sequential Ring 3 lifecycle cycles, exceeding the 64-slot scheduler table, and requires PMM accounting to return to the same baseline after every cycle.

## Protected State bridge

Aurora has a bounded Ring 3 Protected State bridge with capability-gated record operations:

- `AURORA_SYS_PROTECTED_STATE_READ`;
- `AURORA_SYS_PROTECTED_STATE_CREATE_ONCE`.

Those syscalls match the `read_record` / `create_record_once_durable` boundary used by the Aurora Identity Machine Secret adapter. They do not expose generic VFS path access, removal, rename, truncate, namespace creation, or unrestricted capability transfer.

This proves that a trusted Identity process can receive the exact namespace capability required by the Machine Secret transport. The full `services/identity` runtime is still not linked into the long-lived Ring 3 service process.

## Security boundary

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

- the current Identity bootstrap is still a proof process rather than the full `services/identity` executable;
- there is no service discovery or registry;
- there are no restart/dependency supervision policies yet;
- the Identity DRBG entropy handoff is not yet part of the startup ABI;
- the mutable Identity database still lacks a durable atomic-replace Protected State primitive;
- IPC wait currently supports one waiter per endpoint and sender-side queue backpressure remains non-blocking;
- general multi-thread process lifecycle and arbitrary shared-object reference counting remain future work.

## Next gates

1. add the durable mutable Protected State operation required by the Identity database;
2. define the controlled entropy/DRBG bootstrap handoff with explicit ready/unavailable semantics;
3. port the real Identity Service runtime so it consumes the Machine Secret and durable store through the capability boundary;
4. add service supervision/restart policy around the long-lived Identity process;
5. connect the login/session protocol only after those service-runtime guarantees exist.
