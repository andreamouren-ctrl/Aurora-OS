# Aurora OS

Aurora OS is a proprietary operating system project focused on speed, efficiency, privacy, resilience, and a deeply customizable desktop experience.

## Core direction

Aurora OS is **not** intended to be a Linux distribution or a visual clone of Windows, macOS, or existing desktop environments.

Its identity is based on:

- an efficiency-first kernel and system architecture;
- a modern memory subsystem designed around active, warm, cold, compressed, frozen, and disposable memory states;
- a highly customizable desktop as the user's primary environment;
- web access integrated directly into the desktop through isolated Web Surfaces;
- local-first operation with no mandatory online account;
- privacy by default and explicit capability-based permissions;
- aggressive suspension of inactive work to reduce CPU, RAM, I/O, and energy consumption;
- immutable/recoverable system components and transactional updates;
- native Activity Spaces for persistent work contexts;
- asynchronous system services designed to preserve UI responsiveness.

## Current implementation

Aurora OS has moved beyond the architecture-only bootstrap stage. The current x86_64 kernel reaches the **M1 user-space bootstrap** in the automated QEMU smoke test and already includes:

- Limine-based BIOS/UEFI-capable boot media generation;
- native framebuffer boot UI with integrated 1280x720 Aurora artwork;
- physical and virtual memory management plus a kernel heap;
- x86_64 GDT/TSS, IDT, exception handling, APIC/I/O APIC, ACPI/MADT and clock/timer support;
- CPU hardening with CR0.WP and SMEP/SMAP/UMIP where supported;
- SMP bootstrap infrastructure;
- a preemptive kernel-thread scheduler prototype;
- isolated Ring 3 processes with private address spaces and per-thread kernel stacks;
- x86_64 SYSCALL/SYSRET and EXIT path;
- permission-checked user-memory copy primitives;
- typed capability, delegation/revocation and bounded IPC prototypes;
- a generic input-event queue and IRQ-driven PS/2 keyboard path;
- the first native Aurora Identity login surface with interactive Aurora Key entry.

The current login UI accepts and masks an Aurora Key, but **persistent authentication is intentionally not implemented yet**. The future Aurora Identity Service will live in user space and will own identity storage, verifier derivation/checking, throttling and session bootstrap.

## Build and verification

The repository CI builds the freestanding x86_64 kernel and bootable ISO, checks the kernel ELF for unresolved symbols, boots the ISO in QEMU and requires the serial log to reach:

```text
[kernel] M1 user-space bootstrap reached successfully
```

The resulting ISO and smoke-test logs are uploaded as workflow artifacts.

## Languages and toolchain

Aurora OS will **not** introduce a proprietary programming language.

Existing systems languages and toolchains are selected pragmatically according to the needs of the kernel, drivers, services, and user-space components. The current kernel is built as a freestanding x86_64 target with Clang/LLD.

## Current phase

The project is currently in **M1 — Kernel foundations**, with most bootstrap prototypes implemented and runtime-smoke-tested on the current single-vCPU QEMU CI path.

Major next foundations include persistent storage/VFS, user-space system services, Aurora Identity persistence/session bootstrap, broader device input support, and the later Aurora Memory Fabric prototype.

See:

- [Vision](docs/VISION.md)
- [Architecture Principles](docs/ARCHITECTURE_PRINCIPLES.md)
- [Roadmap](docs/ROADMAP.md)
- [Aurora Identity](docs/AURORA_IDENTITY.md)
- [Kernel Model](docs/KERNEL_MODEL.md)
- [Syscall ABI](docs/SYSCALL_ABI.md)

## Status

Active research and development. Kernel interfaces and architecture are still evolving and are not yet considered stable production ABI.
