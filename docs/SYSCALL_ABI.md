# Aurora OS System Call ABI

Status: **Canonical draft**
Version: **0.1**

Aurora OS uses the x86-64 `SYSCALL/SYSRET` mechanism for native user-space system calls.

## Register contract

On entry:

- `RAX` — syscall number
- `RDI` — argument 1
- `RSI` — argument 2
- `RDX` — argument 3
- `R10` — argument 4
- `R8` — argument 5
- `R9` — argument 6

On normal return:

- `RAX` — return value
- `RCX` and `R11` are clobbered by the architectural SYSCALL/SYSRET path
- other general-purpose registers are preserved by Aurora

## Security rules

- user pointers are never dereferenced directly by kernel code;
- buffers cross the boundary through the checked `usercopy` layer;
- user virtual addresses must stay in the lower canonical half;
- process page tables map kernel memory supervisor-only;
- SMAP/SMEP are enabled when supported;
- a Ring 3 CPU exception terminates the offending user thread instead of panicking the kernel;
- privileged I/O is blocked from Ring 3 by the TSS/GDT configuration;
- protected resources are ultimately authorized through kernel capabilities.

## Current bootstrap syscalls

- `0` — temporary bootstrap signal
- `1` — monotonic clock in nanoseconds
- `2` — capability rights check
- `3` — process exit

The bootstrap signal exists only to verify the first Ring 3 path and is not intended as a permanent public ABI.

## Process exit

`EXIT` does not return through `SYSRET`. The scheduler terminates the current user thread, selects another runnable context, switches address space when necessary, and returns through the common interrupt-frame `IRETQ` path.

## Future direction

The syscall surface should remain intentionally small. Higher-level OS services should normally use IPC and capabilities rather than continually expanding privileged kernel APIs.
