# Aurora OS System Call ABI

Status: **Canonical draft**
Version: **0.2**

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

## Current syscalls

- `0` — temporary bootstrap signal
- `1` — monotonic clock in nanoseconds
- `2` — capability rights check
- `3` — process exit
- `4` — bounded IPC send
- `5` — bounded IPC receive

The bootstrap signal exists only to verify the first Ring 3 path and is not intended as a permanent public ABI.

### IPC send

`AURORA_SYS_IPC_SEND` resolves an `AURORA_CAP_IPC_ENDPOINT` from the caller's own capability table and requires `AURORA_RIGHT_WRITE`.

The current ABI accepts:

- endpoint handle;
- user payload pointer;
- payload length;
- optional capability-transfer array;
- transfer count.

Payloads are bounded to 256 bytes and capability transfers to four handles, matching the in-kernel IPC queue contract.

### IPC receive

`AURORA_SYS_IPC_RECEIVE` resolves an `AURORA_CAP_IPC_ENDPOINT` from the caller's own capability table and requires `AURORA_RIGHT_READ`.

The kernel copies the bounded received message into a fixed user result structure through checked usercopy. The current operation is non-blocking: an empty queue returns an error rather than sleeping the caller.

Capability transfers retain the kernel IPC escrow/delegation rules. A sender must possess `AURORA_RIGHT_TRANSFER` on any capability it attempts to transfer. Protected System State capabilities are intentionally issued without that right.

## Process exit

`EXIT` does not return through `SYSRET`. The scheduler terminates the current user thread, selects another runnable context, switches address space when necessary, and returns through the common interrupt-frame `IRETQ` path.

## Runtime validation

The Ring 3 IPC milestone includes a boot-time user process that receives a kernel-queued IPC payload and echoes it back using only its endpoint capability. The kernel validates the exact bytes before the normal user-space bootstrap can complete.

See [`RING3_IPC_SYSCALLS.md`](RING3_IPC_SYSCALLS.md).

## Future direction

The syscall surface should remain intentionally small. Higher-level OS services should normally use IPC and capabilities rather than continually expanding privileged kernel APIs. Service discovery, blocking waits and service bootstrap policy should be layered on this IPC foundation instead of exposing ordinary filesystem or Protected System State path operations directly to applications.
