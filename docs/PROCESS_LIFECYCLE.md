# Aurora Process and Thread Lifecycle

Status: **runtime-verified QEMU foundation; merge CI must preserve the lifecycle gate**

## Purpose

Aurora services must be able to terminate and restart without exhausting the fixed scheduler table, leaking kernel stacks, or permanently consuming process address-space pages.

The current milestone establishes explicit ownership and deterministic reaping for Aurora's present one-thread Ring 3 process model.

## Ownership model

Resources are reclaimed by the component that owns them:

- **Scheduler** owns the kernel stack and scheduler slot for each thread.
- **Process Manager** owns the physical frames allocated for the process image and user stack.
- **VMM** owns only the private page-table hierarchy of an address space.
- **Kernel heap** keeps backing pages mapped but reuses freed virtual ranges.

The VMM never implicitly frees mapped leaf frames. This preserves the ownership boundary required for future shared-memory and externally-owned mappings.

## Reap order

A completed user process is reclaimed in this order:

1. the user thread reaches `THREAD_TERMINATED`;
2. `scheduler_reap_thread()` verifies that no CPU still has the scheduler slot active;
3. the scheduler returns the kernel stack to the reusable kernel heap;
4. the scheduler slot becomes `THREAD_UNUSED` and the process live-thread reference is detached;
5. `process_reap()` requires zero live threads and a terminal process state;
6. the Process Manager frees only its owned image and user-stack frames;
7. the VMM frees only the now-quiescent private page-table hierarchy;
8. the process enters `AURORA_PROCESS_REAPED`;
9. `process_release()` wipes the process object and returns its kernel-heap range for reuse.

A process address space cannot be destroyed while an online CPU still reports it as active.

## Kernel-heap sanitization

Reusable kernel-heap ranges are sanitized before they can be handed to another allocation:

- newly mapped heap backing pages are zeroed before publication;
- a successful `kheap_free_sized()` clears the full returned range while the heap lock is held;
- if a free request is rejected, the live allocation is not cleared.

This gives reaped kernel stacks and process objects a common sanitization boundary rather than relying on every caller to remember its own wipe.

## Scheduler-slot reuse

Aurora currently has a bounded table of 64 scheduler slots. A terminated slot is no longer a permanent tombstone: after explicit reap it returns to `THREAD_UNUSED` and may be used by a new thread.

## Runtime verification gate

The lifecycle probe uses a real Ring 3 process that reaches the normal bootstrap signal and exits cleanly. One warm-up cycle establishes reusable heap ranges before the PMM baseline is captured.

The gate then requires **96 sequential create -> run -> exit -> thread reap -> process reap -> process release cycles**. This exceeds the 64-slot scheduler capacity and therefore proves actual slot reuse rather than merely successful termination.

After **every** cycle, `total_pages`, `free_pages`, and `allocated_pages` must match the post-warm-up PMM baseline. A leak that happens early and is later masked therefore fails at the first divergent cycle.

Successful boot emits:

```text
[lifecycle] 96 Ring 3 create/exit/reap cycles + stable PMM passed
```

The normal M1 success marker is emitted only after the trusted-service bootstrap and lifecycle proof complete, so lifecycle failure remains fail-closed for the bootstrap CI path.

This is QEMU software-path verification, not real-hardware certification.

## Current limits

- The production Service Manager and restart policy are not implemented yet.
- The current process model is effectively one Ring 3 thread per process; multi-thread process-exit semantics remain future work.
- Kernel heap backing pages are retained after first mapping; reclamation guarantees reuse, not shrinking of the mapped heap.
- General resource reference counting for arbitrary cross-process shared objects remains future work.
- A process with unreaped scheduler references cannot be reaped.

## Identity relationship

This lifecycle foundation removes a structural blocker for a restartable Aurora Identity Service. The Identity bootstrap probe is reclaimed after its READY proof instead of permanently consuming a scheduler slot, kernel stack, process object and address space.

It does not by itself make Identity production-ready. Controlled entropy/DRBG handoff, the real service runtime, service supervision and mutable durable Identity-store integration remain separate gates.
