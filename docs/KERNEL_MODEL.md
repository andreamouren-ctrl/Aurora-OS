# Aurora OS Kernel Model

Status: **Canonical**
Version: **0.2**

Aurora OS uses a **modular hybrid capability-kernel architecture**.

The goal is not to reproduce a classic monolithic kernel or a pure microkernel. Aurora keeps latency-critical mechanisms in privileged space while pushing policy and failure-prone functionality behind isolated Ring 3 interfaces where practical.

## Kernel responsibilities

The privileged kernel currently owns:

- CPU and interrupt control;
- SMP bring-up and per-CPU execution state;
- physical and virtual memory;
- page-table manipulation and TLB coherence;
- scheduling;
- process/thread isolation and lifecycle primitives;
- IPC primitives and blocking/wakeup mechanics;
- capability enforcement;
- low-level timekeeping;
- low-level block/storage transports currently required by the boot/system path;
- VFS/filesystem mechanisms currently required by the mounted system volume;
- low-level display/input mechanisms required before full service migration;
- security boundaries that Ring 3 components cannot bypass.

Policy should not move into Ring 0 merely because a kernel mechanism exists.

## Current SMP model

Aurora is no longer BSP-only.

The current kernel includes:

- application-processor bring-up;
- CPU-local execution state;
- real scheduler ownership on BSP and APs;
- explicit AP handoff from bootstrap context to scheduler-owned idle stacks;
- per-CPU Local APIC timer state;
- package/core/SMT topology discovery;
- topology-aware core-first / SMT-second soft placement;
- work-stealing fallback;
- synchronous IPI-based TLB shootdown;
- four-logical-CPU QEMU validation paths using a 1-socket / 2-core / 2-thread topology.

The current implementation remains substantially simpler than production Linux/Windows scheduling and does not yet claim NUMA, heterogeneous-core, realtime/deadline-class or large-machine scalability.

## Memory model

Implemented kernel memory foundations include:

- PMM;
- VMM/private process address spaces;
- checked Ring 3 usercopy;
- reusable kernel-heap free ranges;
- process/address-space destruction;
- anonymous Ring 3 allocations;
- refcounted shared-memory objects;
- cross-CPU TLB invalidation.

Still outside the current production-equivalent scope are mature demand paging, swap/pagefile, integrated page cache, general file-backed mmap, full process copy-on-write semantics, NUMA policy and advanced reclaim.

## Process/thread model

The current Ring 3 lifecycle is runtime verified for:

```text
create
 -> map image/user memory
 -> scheduler execution
 -> exit or fault
 -> scheduler thread termination
 -> thread reap
 -> process/address-space reap
 -> object/heap reuse
```

The current ordinary user-process model remains effectively one primary thread per process. Full multi-thread process-exit semantics are future work.

## User-space system services

Where latency and hardware constraints allow, higher-level policy belongs outside the kernel.

Aurora already uses Ring 3 system components for:

- Aurora Identity Service;
- Session Manager;
- ordinary User Session Host;
- trusted service probes/supervision paths.

Future Ring 3 policy/services include:

- Desktop Shell;
- compositor/display service split where practical;
- package management;
- application lifecycle;
- permission/user-consent UI;
- indexing;
- network policy;
- update orchestration;
- non-critical device services.

A service receives only the capabilities explicitly assigned by bootstrap/session policy.

## Driver direction

Aurora prefers isolated driver services where practical, especially for complex or high-risk device stacks.

Boot-critical and latency-sensitive hardware may remain privileged when measurements justify it.

The current repository still contains kernel-resident storage, display and input foundations because the OS is in active bring-up. This must not be mistaken for a permanent requirement that all future drivers live in Ring 0.

The driver boundary is a performance/security decision, not an ideology.

## Capability rule

Possessing an object name, path or numeric identifier does not by itself grant access.

A process must hold a valid kernel-issued capability with sufficient rights for the requested protected operation.

Capabilities are:

- unforgeable from Ring 3;
- scoped;
- revocable;
- typed;
- rights-limited;
- transferable only when explicitly permitted;
- attenuable: delegation can remove rights but cannot invent new ones.

This capability layer is the enforcement foundation beneath Aurora application permissions, trusted-service dependencies, session profile delegation and graphics ownership.

## Policy vs mechanism

Example:

1. an application asks for microphone access;
2. a future Permission Broker decides whether policy/user consent allows it;
3. if allowed, the broker delegates a reduced microphone capability;
4. the kernel validates that capability on every protected operation.

The prompt is policy and user experience.
The capability is the security mechanism.

## Performance rule

Moving functionality out of the kernel must not introduce avoidable copies or context switches.

Aurora IPC and object contracts therefore favor:

- bounded messages;
- explicit capability transfer;
- shared-memory data paths;
- zero-copy buffers where safe;
- asynchronous operation;
- blocking wait/wakeup instead of busy polling.

## Fail-closed rule

For security-sensitive state, an ambiguous failure is not interpreted as success or clean absence.

Examples already implemented include:

- tri-state trusted-state lookup;
- capability validation before privileged service operations;
- one-time session-grant consumption;
- session/profile authority revocation;
- TLB shootdown acknowledgement;
- filesystem corruption/integrity gates.

## Current architecture boundary

The present kernel is a **hybrid bring-up kernel with capability-oriented service boundaries**, not yet a minimal production kernel.

The long-term direction remains:

```text
Ring 0:
    mechanism + isolation + minimum hardware-critical path

Ring 3:
    identity/session policy
    compositor/shell policy
    permission policy
    network/update/package policy
    recoverable higher-level services
```

Migration is performed only after a Ring 3 replacement has a measured, capability-safe and recoverable path.
