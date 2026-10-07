# Aurora Trusted Service Supervision

Status: **runtime verified in QEMU CI**

## Purpose

Aurora trusted Ring 3 services must be restartable without moving service policy back into the kernel or retaining stale authority from a previous process instance.

The first supervisor foundation builds on the verified process/thread lifecycle, capability bootstrap, blocking IPC and Protected System State boundaries.

## State model

The current supervisor exposes these states:

- `UNINITIALIZED`
- `STOPPED`
- `RUNNING`
- `EXITED`
- `FAULTED`
- `FAILED`

A service instance is considered restartable only after its primary scheduler thread has terminated and the previous process has been reaped through the established ownership order.

## Restart policies

The first policy set is deliberately small:

- `NEVER` — terminal services remain terminal;
- `ON_FAILURE` — restart after a fault or a non-zero clean exit;
- `ALWAYS` — restart after any terminal result.

Every supervisor has an explicit restart limit. A policy therefore cannot create an unbounded crash loop in this foundation.

Time-based backoff and restart-rate windows are not implemented yet.

## Instance reconstruction

A restart is **not** a resurrection of the old process. The previous instance is fully reaped, then `service_bootstrap_start_trusted()` creates a fresh instance with:

- a new process ID;
- a new scheduler thread;
- a freshly initialized IPC channel;
- a fresh process capability table;
- a newly granted process-local IPC endpoint capability;
- a newly granted Protected System State capability constrained by the manifest rights.

The protected storage namespace remains the same logical service scope, but the process-local capability authority is reconstructed from policy on every generation.

No `CONTROL` or `TRANSFER` right is implicitly added to the Identity Protected State capability.

## Reap ordering

When a supervised service terminates:

1. the supervisor observes the scheduler thread as terminated;
2. `scheduler_reap_thread()` returns its kernel stack and slot;
3. process live-thread accounting reaches zero;
4. `process_reap()` frees process-owned user frames and private page tables according to their ownership boundary;
5. `process_release()` returns the scrubbed process object range to the kernel heap;
6. only then may policy construct another service generation.

The terminal `aurora_process_result` is retained by the supervisor so restart policy can distinguish graceful exit, non-zero exit and fault.

## Identity runtime proof

The first runtime gate uses the existing trusted Identity service probe with policy `ALWAYS` and a restart limit of one:

1. generation 1 starts with only `READ|WRITE` Protected State authority;
2. it sends the exact Identity READY message and exits successfully;
3. the supervisor reaps the complete instance;
4. generation 2 starts with a different process ID and freshly rebuilt capability state;
5. minimum authority is checked again;
6. generation 2 sends READY and exits successfully;
7. the restart limit prevents generation 3;
8. final PMM accounting must match the pre-test baseline.

Expected success marker:

```text
[service-supervisor] bounded Identity restart + fresh capability bootstrap passed
```

The marker has been observed in the four-CPU q35 BIOS smoke path and in both the first and second ATA/AuroraFS boot paths. This verifies the QEMU software path; it is not real-hardware certification.

## Security properties

This foundation establishes:

- bounded restart attempts;
- no reuse of a previous process object or scheduler slot as the new identity of the service;
- fresh process-local capability tables per generation;
- reapplication of least-privilege capability policy on each restart;
- complete lifecycle reclamation before recreation;
- persistent service data remains behind Protected System State rather than process memory.

## Current limits

This is a real trusted-service supervision foundation, but it is **not yet a general systemd/SCM-equivalent service manager**.

Still missing from the generic supervisor layer:

- time-based restart backoff and crash-rate limiting;
- general service registry/discovery;
- declarative dependency ordering and health dependencies;
- external stop/kill/administrative control contract;
- generic service READY/health protocol for every future service class;
- persistent supervisor policy configuration;
- broad watchdog/telemetry/audit integration.

The earlier limitation stating that Aurora still used only a one-shot Identity bootstrap probe is obsolete. The live OS now has a long-lived Ring 3 Identity runtime, a separate Ring 3 Session Manager and an ordinary User Session Host.

## Current role

The supervisor is now one of the foundations used to keep privileged user-space services recoverable without retaining stale process-local authority.

The important invariant is:

\`\`\`text
old service generation terminates
 -> thread/process resources are reaped
 -> old process-local capabilities disappear
 -> new process is created
 -> dependencies/capabilities are reconstructed from policy
 -> new generation becomes authoritative only after its readiness contract
\`\`\`

Future work should generalize this pattern rather than moving service policy back into Ring 0.

