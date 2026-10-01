# Aurora OS

Aurora OS is a proprietary operating-system project focused on speed, efficiency, privacy, resilience, and a deeply customizable desktop experience.

## Core direction

Aurora OS is **not** intended to be a Linux distribution or a visual clone of Windows, macOS, or an existing desktop environment.

Its direction is based on:

- an efficiency-first kernel and system architecture;
- a future Aurora Memory Fabric with active, warm, cold, compressed, frozen, and disposable memory states;
- a highly customizable desktop as the user's primary environment;
- isolated Web Surfaces integrated into the desktop;
- local-first operation with no mandatory online account;
- explicit capability-based security;
- aggressive suspension of inactive work;
- recoverable system components and transactional-update goals;
- native Activity Spaces for persistent work contexts;
- asynchronous system services designed to preserve UI responsiveness.

## Current implementation

The x86_64 kernel reaches the **M1 user-space bootstrap** in automated QEMU runtime tests.

Implemented foundations currently include:

- Limine-based BIOS/UEFI-capable boot media generation;
- native framebuffer boot UI with integrated Aurora artwork;
- physical memory manager, virtual memory manager, kernel heap, and checked user-copy primitives;
- x86_64 GDT/TSS, IDT, exception handling, ACPI/MADT, Local APIC, I/O APIC, HPET/clock, and one-shot timer support;
- CR0.WP plus SMEP/SMAP/UMIP activation where the CPU exposes them;
- SMP bootstrap infrastructure (current CI runtime path remains single-vCPU);
- preemptive kernel-thread scheduler prototype;
- isolated Ring 3 process prototype with private address space and per-thread kernel stack;
- x86_64 SYSCALL/SYSRET and EXIT path;
- typed capability primitives and bounded IPC capability-transfer tests;
- generic input-event queue and IRQ-driven PS/2 keyboard support;
- native Aurora Identity framebuffer login prototype with Aurora Key entry;
- generic block-device abstraction;
- PCI storage discovery and AHCI controller/ABAR discovery;
- ATA PIO compatibility read/write path used by CI storage tests;
- MBR/GPT partition-manager foundations;
- filesystem driver registry and mount manager;
- AuroraFS bootstrap v1 persistence across reboot;
- FAT32/VFAT read-only support including Long File Names and UTF-16 to UTF-8 decoding;
- exFAT read-only support including 64-bit file-length handling.

### Storage verification

The storage CI creates externally formatted FAT32 and exFAT volumes, embeds them in an MBR disk image, boots Aurora in QEMU, mounts both formats through Aurora's filesystem-driver layer, reads known files, validates a Unicode VFAT Long File Name, and reboots the same disk to verify AuroraFS persistence.

The combined storage smoke test is runtime-verified green as of workflow run **#271** (`36888671373`) at commit `483ab55e803b7d22c061d938ffdd6b23e5e86bb7`.

Important scope limits:

- AHCI currently performs controller discovery/probing only; modern AHCI data I/O is not implemented yet.
- NVMe and USB mass-storage transports are not implemented yet.
- AuroraFS v1 is a deliberately small bootstrap persistence format, not the final production filesystem.
- FAT32/VFAT and exFAT are currently read-only.
- the legacy volatile VFS prototype has not yet been fully replaced by the new mount/filesystem-driver path.

## Aurora Identity

The current login UI accepts and masks an Aurora Key, but **persistent authentication is intentionally not implemented yet**. The planned Aurora Identity Service will live in user space and own identity storage, credential verification, throttling, recovery policy, and authenticated session bootstrap.

The canonical Identity documentation is indexed in [`docs/identity/README.md`](docs/identity/README.md).

## Build and verification

CI currently:

1. builds the freestanding x86_64 kernel and bootable ISO;
2. verifies the kernel ELF and unresolved-symbol state;
3. performs a BIOS QEMU smoke boot;
4. exercises ATA PIO and AuroraFS persistence;
5. scans an MBR containing FAT32 and exFAT partitions;
6. mounts and reads externally generated FAT32/VFAT and exFAT filesystems;
7. performs a second boot against the same disk image to verify persistence.

The generic boot path must reach:

```text
[kernel] M1 user-space bootstrap reached successfully
```

## Languages and toolchain

Aurora OS will **not** introduce a proprietary programming language.

Existing systems languages and toolchains are selected pragmatically. The current kernel is built as a freestanding x86_64 target with Clang/LLD.

## Current phase

The project remains in **M1 — Kernel foundations**, while selected M3 storage foundations are being implemented early because Aurora Identity requires durable local state.

Primary next storage work includes:

- production VFS integration over the filesystem mount manager;
- registering native AuroraFS through the same filesystem-driver interface;
- sector-size-independent block/partition/filesystem parsing;
- hardened GPT validation;
- NTFS and ext-family read-only drivers;
- modern AHCI data I/O, followed later by NVMe and USB mass storage.

## Documentation

Start with [`docs/README.md`](docs/README.md).

Key specifications:

- [Vision](docs/VISION.md)
- [Architecture Principles](docs/ARCHITECTURE_PRINCIPLES.md)
- [Roadmap](docs/ROADMAP.md)
- [AuroraFS](docs/AURORAFS.md)
- [Filesystem Compatibility](docs/FILESYSTEM_COMPATIBILITY.md)
- [Aurora Identity](docs/AURORA_IDENTITY.md)
- [Kernel Model](docs/KERNEL_MODEL.md)
- [Syscall ABI](docs/SYSCALL_ABI.md)

## Status

Active research and development. Kernel interfaces, storage formats, and service contracts are still evolving and are not yet stable production ABI.