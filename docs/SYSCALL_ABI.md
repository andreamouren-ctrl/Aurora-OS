# Aurora OS System Call ABI

Status: **Canonical active ABI draft**
Version: **0.3**

Aurora OS uses the x86-64 `SYSCALL/SYSRET` mechanism for native Ring 3 system calls.

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
- other general-purpose registers are preserved by Aurora's syscall entry/return path.

## Security rules

- user pointers are never dereferenced directly by kernel code;
- buffers cross the boundary through checked usercopy;
- user virtual addresses remain in the lower canonical user range;
- kernel mappings remain supervisor-only;
- SMAP/SMEP are enabled where supported;
- a Ring 3 CPU exception terminates the offending user execution context instead of panicking the kernel;
- privileged I/O is blocked from Ring 3;
- protected resources require process-local kernel capabilities with sufficient rights;
- purpose-specific syscalls are preferred over exposing generic privileged kernel/VFS internals.

## Current syscall numbers

| Number | Name | Purpose |
| ---: | --- | --- |
| 0 | `AURORA_SYS_BOOTSTRAP_SIGNAL` | bootstrap validation signal; not a long-term public service API |
| 1 | `AURORA_SYS_CLOCK_NS` | monotonic clock |
| 2 | `AURORA_SYS_CAP_CHECK` | capability rights/type validation support |
| 3 | `AURORA_SYS_EXIT` | terminate current Ring 3 process/thread path |
| 4 | `AURORA_SYS_IPC_SEND` | bounded IPC send with optional capability transfer |
| 5 | `AURORA_SYS_IPC_RECEIVE` | bounded non-blocking IPC receive |
| 6 | `AURORA_SYS_PROTECTED_STATE_READ` | capability-gated Protected State record read |
| 7 | `AURORA_SYS_PROTECTED_STATE_CREATE_ONCE` | durable create-once Protected State publication |
| 8 | `AURORA_SYS_IPC_WAIT` | block until endpoint work is available |
| 9 | `AURORA_SYS_ENTROPY_SEED` | capability-gated trusted seed handoff |
| 10 | `AURORA_SYS_PROTECTED_STATE_REPLACE_DURABLE` | durable bounded Protected State replacement |
| 11 | `AURORA_SYS_USER_MEMORY_ALLOC` | bounded anonymous Ring 3 allocation |
| 12 | `AURORA_SYS_USER_MEMORY_FREE` | release a process-owned anonymous allocation |
| 13 | `AURORA_SYS_CAP_REVOKE` | revoke a process capability |
| 14 | `AURORA_SYS_PROFILE_OPEN_OR_CREATE` | capability-gated persistent profile root access |
| 15 | `AURORA_SYS_GRAPHICS_BUFFER_MAP` | map a graphics buffer into the authorized client |
| 16 | `AURORA_SYS_GRAPHICS_BUFFER_UNMAP` | unmap a graphics buffer |
| 17 | `AURORA_SYS_GRAPHICS_SURFACE_ATTACH` | attach authorized buffer to surface |
| 18 | `AURORA_SYS_GRAPHICS_SURFACE_DAMAGE` | publish bounded damage |
| 19 | `AURORA_SYS_GRAPHICS_SURFACE_COMMIT` | atomically commit pending surface state |
| 20 | `AURORA_SYS_DISPLAY_PRESENT` | capability-gated display presentation |
| 21 | `AURORA_SYS_GRAPHICS_FRAME_CALLBACK_REQUEST` | request presentation callback |
| 22 | `AURORA_SYS_GRAPHICS_FRAME_CALLBACK_TAKE` | retrieve completed frame callback |

The ABI is still evolving and is not frozen as a third-party stable ABI.

## IPC

`AURORA_SYS_IPC_SEND` resolves an `AURORA_CAP_IPC_ENDPOINT` and requires `WRITE`.

Current bounds:

- payload: 256 bytes;
- transferred capabilities: 4.

A sender must hold `TRANSFER` on any capability it transfers.

`AURORA_SYS_IPC_RECEIVE` remains non-blocking. `AURORA_SYS_IPC_WAIT` is the blocking primitive used by long-lived Ring 3 services. This split avoids forcing polling while keeping receive semantics explicit.

The scheduler contains the BLOCKED/wakeup handshake required to prevent a lost wakeup when a sender races the final park operation.

## Protected System State

The syscall ABI exposes a narrow service boundary rather than generic privileged pathname access.

Current Protected State operations include:

- read;
- create-once durable publication;
- durable bounded replacement.

Capability rights and namespace binding are checked before VFS mutation.

Current ABI constants permit record names up to 64 bytes and bounded Protected State record I/O up to 8192 bytes.

## Ring 3 memory

Anonymous memory syscalls provide bounded private non-executable user mappings.

Graphics shared memory uses object/capability-backed mapping rather than exposing arbitrary physical mapping authority to Ring 3.

Aurora does not yet expose a general POSIX-style `mmap` ABI.

## Identity/session/profile boundary

Identity/session policy remains in Ring 3 services. The kernel supplies only bounded mechanisms such as IPC, entropy seed handoff, capability revocation, Protected State access and profile-root opening.

A successful login does not produce a privileged "logged-in" kernel mode. Instead, the Identity Service issues a one-time grant that Session Manager consumes to obtain the stable user binding and derive session/profile authority.

## Graphics boundary

Current G1-G3 syscalls expose capability-backed buffer/surface/display mechanisms without allowing ordinary clients to map the physical framebuffer or manage outputs globally.

Window-management/Desktop Shell policy remains outside the low-level syscall mechanism.

## Process exit

`EXIT` does not return through `SYSRET`. The scheduler terminates the current user execution context, selects another runnable frame, switches address space when necessary and returns through the common interrupt-frame/IRETQ path.

Thread/process resources are later reclaimed through the explicit reap lifecycle.

## Runtime validation

Aurora CI exercises syscall paths through actual Ring 3 probes and live services, including:

- IPC send/receive/wait;
- process exit;
- Protected State;
- entropy handoff;
- anonymous user memory;
- capability revocation/profile bootstrap;
- graphics buffer/surface/display operations.

## Direction

The syscall surface should remain intentionally small relative to the higher-level OS API surface.

New policy should normally live in capability-authorized Ring 3 services and IPC protocols rather than growing generic omnipotent kernel syscalls.
