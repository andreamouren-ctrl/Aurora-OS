# IPC endpoint lifetime and service teardown audit

Status: **OPEN — design and concurrency validation required**.

## Verified source paths

- `kernel/src/arch/x86_64/syscall.c`: `lookup_ipc_endpoint` calls `cap_lookup` and returns a raw endpoint pointer. The lookup does not retain the endpoint object across `dispatch_ipc_send` or `dispatch_ipc_receive`.
- `kernel/src/service/service_bootstrap.c`: `service_bootstrap_start_trusted` initializes the IPC channel inside `struct aurora_trusted_service` and grants its endpoint to the service process.
- `kernel/src/service/service_supervisor.c`: `reap_terminal_instance` calls `process_reap`, `process_release`, then clears `supervisor->service`. No explicit channel escrow teardown is present in this path.
- `kernel/src/security/capability.c`: `cap_table_destroy` releases occupied entries through capability lifecycle hooks; `cap_lookup_retain` and `cap_view_release` exist, but endpoint-specific retain/release registration and ownership must be established before use.

## Risks to resolve

1. Outstanding escrow entries can retain delegated capability references when a service instance is cleared.
2. Raw endpoint pointers can outlive their backing channel during concurrent teardown or restart.
3. Registered waiters must be cancelled or safely woken before the channel is invalidated.
4. Reusing the same embedded service structure must not let stale handles address a new instance.

## Required implementation sequence

1. Define a channel state machine (ACTIVE, CLOSING, CLOSED) and explicit lifetime ownership.
2. Audit every endpoint grant, lookup, waiter, and delegation call site.
3. Establish a synchronization/refcount protocol preventing teardown while operations hold references.
4. Add a channel close/drain operation that revokes all escrow handles, wipes queued payloads and capability metadata, and handles waiters.
5. Integrate teardown only after outstanding references and scheduler ownership are proven safe.
6. Add host tests for queued capability cleanup, concurrent close/send/receive, waiter cancellation, and restart generation isolation.
7. Require successful kernel compile and runtime tests before marking this issue resolved.

Do not replace this with an unconditional `cap_table_destroy(&service->ipc_channel.escrow)` immediately before clearing the service: that alone does not solve concurrent use-after-free.
