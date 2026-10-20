# Kernel boundary review — first pass (2026-10-20)

Scope: read-only inspection of current `main` source files `kernel/src/mm/usercopy.c`, `kernel/src/arch/x86_64/syscall.c`, `kernel/src/ipc/ipc.c`. **No vulnerability is confirmed; no tests were executed.** Review source may have advanced since this note; rebase and compare exact SHA before implementation.

## Confirmed implemented safeguards
- `usercopy` validates canonical lower-half range, mapped USER pages, writable destination pages, and preflights the entire span before copying.
- IPC syscall send validates payload/capability counts and uses checked usercopy.
- IPC receive preflights destination and revokes newly delegated capability handles if the final usercopy fails.
- IPC send/receive rollback temporary delegated capabilities on intermediate failure.

## Hypotheses requiring proof
1. **Usercopy mapping lifetime / concurrency:** preflight and copy are separate VMM queries. Determine whether process address-space mappings can change between passes or while the physical alias is accessed. Audit locks, scheduler and mapping mutation paths; only change code after proving the race is reachable. Add deterministic concurrent unmap/revoke fault injection if applicable.
2. **Event delivery semantics:** `dispatch_graphics_frame_callback_take` preflights destination, consumes callback, then performs final usercopy. A failure after consumption may lose an event; determine intended at-most-once delivery and whether faulting/unmapping between operations is reachable. Add negative tests and document contract.
3. **IPC receive delivery semantics:** on final usercopy failure the queue item has already been consumed; capability rollback is implemented, but message replay is not. Check whether this is explicitly accepted in the IPC ABI and whether error paths are observable/recoverable.
4. **Capability lifetime:** verify `cap_lookup` object lifetime and concurrent revocation, particularly endpoint and display object use after lookup. Review object ownership and lock ordering before changes.
5. **Usercopy zero length:** zero-byte copy returns true without process/pointer validation; confirm every syscall checks whether zero-length operation should be accepted. This is not automatically a bug.

## Required before a fix PR
- Read `docs/SYSCALL_ABI.md`, `docs/RING3_IPC_SYSCALLS.md`, `docs/RING3_USER_MEMORY.md`, `docs/PROCESS_LIFECYCLE.md`, `docs/KERNEL_MODEL.md` and relevant ADRs.
- Trace VMM mapping query and unmap, process lifecycle, capability table locking, graphics callback queue.
- Reproduce a failure or prove a contract violation with a deterministic test.
- Make minimal fix in separate branch; run targeted host tests and all applicable QEMU first/cold-reboot CI gates on exact commit; document residual risk.

## Environment limitation
Direct git clone failed due unavailable DNS/network access in the execution container. GitHub connector was used for source inspection; no build or runtime validation is claimed.
