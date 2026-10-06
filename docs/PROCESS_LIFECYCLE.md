# Aurora Process and Thread Lifecycle

Status: **runtime verified in QEMU CI**

## Purpose

Aurora services must be able to terminate and restart without exhausting the fixed scheduler table, leaking kernel stacks, or permanently consuming process address-space pages.

This milestone establishes explicit ownership and a deterministic reap order for the current one-thread Ring 3 process model.

## Ownership model

Resources are reclaimed by the component that owns them:

- **Scheduler** owns the kernel stack and scheduler slot for each thread.
- **Process Manager** owns the physical frames allocated for the process image and user stack.
- **VMM** owns only the private page-table hierarchy of an address space.
- **Kernel heap** keeps backing pages mapped but reuses freed virtual ranges.

The VMM does not implicitly free mapped leaf frames. This is required so future shared-memory or externally-owned mappings do not become double-free hazards.

## Reap order

A completed Ring 3 process is reclaimed in this order:

1. the user thread reaches `THREAD_TERMINATED`;
2. `scheduler_reap_thread()` verifies that no CPU still has the scheduler slot active;
3. the scheduler returns the kernel stack to the reusable kernel heap;
4. the heap scrubs the released range before publishing it for reuse;
5. the scheduler slot returns to `THREAD_UNUSED` and the process live-thread reference is detached;
6. `process_reap()` requires zero live threads and a terminal process state;
7. the Process Manager frees only its owned image and user-stack frames;
8. the VMM frees only the now-quiescent private page-table hierarchy;
9. the process enters `AURORA_PROCESS_REAPED`;
10. `process_release()` returns the scrubbed process-object range to the kernel heap.

A process address space cannot be destroyed while an online CPU still reports it as active.

## Kernel-heap scrubbing

Kernel-heap pages are zeroed before they become reachable through the heap mapping. A valid `kheap_free_sized()` request is scrubbed while the caller still exclusively owns the range and only then inserted into the reuse list.

This means scheduler stacks, process objects and future sensitive kernel allocations cannot inherit stale contents from a previous heap owner through normal heap reuse.

Backing pages remain mapped after free. The current guarantee is **safe reuse and bounded repeated growth**, not heap shrinking.

## Runtime verification

The trusted-service bootstrap path executes a Ring 3 lifecycle stress proof:

- one warm-up lifecycle establishes reusable heap ranges;
- PMM allocation statistics are recorded;
- **80 additional complete create -> run -> exit -> thread reap -> process reap -> process release cycles** are executed;
- the cycle count exceeds the scheduler's fixed 64-slot capacity, proving actual slot reuse;
- after the loop, PMM total/free/allocated page counts must exactly match the post-warm-up baseline.

The same boot path also reaps and releases the trusted Identity bootstrap probe itself.

The complete bootstrap regression has passed the four-CPU BIOS smoke boot and the ATA/AuroraFS storage/reboot smoke path. This is a QEMU software-path verification, not real-hardware certification.

## Current limits

- The production Service Manager and restart policy are not implemented yet.
- The current Ring 3 process model is effectively one thread per process; multi-thread process-exit semantics remain future work.
- General reference counting for arbitrary cross-process shared objects is not implemented yet.
- Kernel-heap backing mappings do not currently shrink.
- A process with unreaped scheduler references cannot be reaped.

## Identity relationship

This lifecycle foundation removes the structural resource-leak blocker for a restartable Aurora Identity Service. A future supervisor can destroy a failed Identity process and construct a fresh instance without monotonically consuming scheduler slots, kernel stacks, process objects, user frames or page-table pages.

It does not by itself make Identity production-ready; service supervision, controlled secret/entropy handoff and the long-lived Identity request protocol remain separate gates.
