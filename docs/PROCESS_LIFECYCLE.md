# Aurora Process and Thread Lifecycle

Status: **runtime verified in QEMU CI**

## Purpose

Aurora services must be able to terminate and restart without exhausting the fixed scheduler table, leaking kernel stacks, or permanently consuming process address-space pages.

This milestone establishes explicit ownership and a deterministic reap order for the current one-thread Ring 3 process model.

## Ownership model

Resources are reclaimed by the component that owns them:

- **Scheduler** owns the kernel stack and scheduler slot for each thread.
- **Process Manager** owns the physical frames allocated for the process image, user stack, and private anonymous mappings.
- **Shared Memory Object** owns physical frames that may be mapped into multiple processes and tracks owner/mapping references.
- **VMM** owns only the private page-table hierarchy of an address space.
- **Kernel heap** keeps backing pages mapped but reuses freed virtual ranges.

The VMM does not implicitly free mapped leaf frames. Shared memory now relies on this rule: removing or destroying one process address space cannot free a physical frame still mapped by another process.

## Reap order

A completed Ring 3 process is reclaimed in this order:

1. the user thread reaches `THREAD_TERMINATED`;
2. `scheduler_reap_thread()` verifies that no CPU still has the scheduler slot active;
3. the scheduler returns the kernel stack to the reusable kernel heap;
4. the heap scrubs the released range before publishing it for reuse;
5. the scheduler slot returns to `THREAD_UNUSED` and the process live-thread reference is detached;
6. `process_reap()` requires zero live threads and a terminal process state;
7. the Process Manager frees its owned image, user-stack and private-anonymous frames;
8. shared mappings drop mapping references without freeing shared physical frames directly;
9. the VMM frees only the now-quiescent private page-table hierarchy;
10. the process enters `AURORA_PROCESS_REAPED`;
11. `process_release()` returns the scrubbed process-object range to the kernel heap.

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

- The current ordinary Ring 3 process model remains effectively one primary thread per process; mature multi-thread process-exit/join semantics remain future work.
- Kernel-heap backing mappings do not currently shrink back to the PMM.
- A process with unreaped scheduler references cannot be reaped.
- Process groups/jobs, rich accounting, debugging/ptrace-class facilities and advanced resource controls are future work.
- Shared-memory semantics exist, but the full general-purpose synchronization/mapping ecosystem remains much smaller than Linux/Windows.

## Identity and service relationship

This lifecycle foundation is now actively used by the trusted-service supervision path.

A failed or terminated trusted service generation can be fully reaped before a fresh process/thread/capability generation is constructed. The live Identity Service and Session Manager stack therefore no longer depend on monotonically consuming scheduler slots, kernel stacks, process objects, user frames or page-table pages.

Lifecycle correctness is necessary but not sufficient for production security; service policy, credential handling, recovery and real-hardware validation remain separate gates.
