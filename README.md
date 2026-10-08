# Aurora OS

Aurora OS is a proprietary operating-system project focused on speed, efficiency, privacy, resilience, explicit capability-based security, and a deeply customizable desktop experience.

Aurora is **not** a Linux distribution and is not intended to be a visual clone of Windows, macOS, or an existing desktop environment.

## Current status

Aurora is an active experimental x86_64 operating system with a verified vertical path from boot through kernel/SMP, Ring 3, storage/filesystems, Identity/session services and the first graphics/compositor foundations.

The current repository has progressed beyond the original M1-only description. Major implemented foundations now include:

### Kernel and execution

- Limine-based BIOS/UEFI-capable boot media generation;
- native framebuffer boot/recovery UI;
- PMM, VMM, reusable kernel heap and checked user-copy primitives;
- x86_64 GDT/TSS, IDT, exceptions, ACPI/MADT, Local APIC, I/O APIC and HPET/clock;
- CR0.WP plus SMEP/SMAP/UMIP activation where available;
- real SMP bring-up with CPU-local execution state;
- physical CPU topology discovery (package/core/SMT);
- preemptive multi-CPU scheduler with core-first / SMT-second soft placement;
- per-CPU Local APIC timer ownership;
- synchronous cross-CPU TLB shootdown;
- isolated Ring 3 processes with private CR3 and per-thread kernel stack;
- process/thread exit, reap and resource-reuse lifecycle;
- x86_64 SYSCALL/SYSRET;
- bounded anonymous/shared user-memory foundations.

### Security, IPC and services

- typed capability objects with explicit rights, transfer and revocation;
- bounded IPC with capability transfer;
- blocking Ring 3 IPC wait/wakeup with lost-wakeup protection;
- Protected System State with narrow capability-gated record access;
- trusted Ring 3 service bootstrap with explicit dependency capabilities;
- bounded service supervision and fresh authority reconstruction after restart.

### Storage and filesystems

- generic block-device layer;
- ATA PIO compatibility read/write/flush;
- AHCI SATA DMA read/write/flush baseline;
- NVMe Admin/I/O queue, read/write/flush baseline;
- MBR and GPT with CRC validation and primary/backup fallback;
- filesystem-driver registry, mount manager and VFS;
- FAT32/VFAT read-only support with Unicode long filenames;
- exFAT read-only support;
- AuroraFS v2 with scalable allocation, nested directories, bounded Level-3 extent trees, create/mkdir, write/truncate, remove/rename, reclaim, integrity/recovery, metadata/ACLs and explicit durability;
- real AuroraFS v2 mount at `/system`.

AHCI and NVMe filesystem paths are runtime exercised independently in QEMU through the common block/partition/filesystem/VFS stack.

### Aurora Identity and session

The live OS now contains a Ring 3 Aurora Identity path rather than only a framebuffer credential prototype.

Implemented foundations include:

- persistent local identity/credential state;
- Aurora Key normalization and Argon2id verifier provider;
- protected opaque lookup tags and machine-secret foundation;
- first-user bootstrap policy;
- authentication throttling foundations;
- one-time session grants;
- separate Ring 3 Session Manager;
- stable `user_id` binding;
- persistent user profile authority on AuroraFS;
- reduced session/profile capability delegation;
- ordinary Ring 3 User Session Host;
- logout revocation;
- lock/unlock with fresh same-user authentication;
- fail-closed abnormal session termination;
- purpose-bound re-authentication proof core.

The desktop/session host does not receive the Aurora Key or verifier material.

### Graphics and input

M4 graphics is now active implementation, not design-only.

- **G1 Display foundation — complete/runtime verified**
- **G2 Surface/buffer core — complete/runtime verified**
- **G3 Software compositor + mastering color pipeline — complete/runtime verified**
- **G4 Pointer/modern input — complete/runtime verified (V1 bounded polling)**
- **G5 Window protocol/Desktop Shell — in progress (core window policy only)**

G1-G3 currently include capability-gated Ring 3 display/surface paths, cross-client buffer isolation, software composition, damage/clipping/z-order, transforms/scaling, occlusion, secure-scene rules, RGB10A2/RGB12/RGBA16F paths, direct ST.2084, BT.2100 HLG, ICC matrix-shaper import, monitor calibration and perceptual tone mapping.

Display-link foundations include EDID/CTA/DisplayID parsing, HDMI/DisplayPort capability models, VRR/DSC contracts, link-training contracts and a QEMU Standard VGA/Bochs VBE native-driver foundation.

G4 includes PS/2 and live qemu-xhci USB HID keyboard/mouse input, descriptor-driven mouse wheel and five-button support, normalized routing and focus/capture policy. QEMU hot-unplug teardown and event-ring quiescence were runtime gated. MSI-X interrupt delivery and further HID device classes remain deferred; G4 V1 uses bounded polling.

G5 has a kernel window-policy foundation with configure/ack, activation controls, placement/stacking, bounded move and explicit toplevel destruction. This is **not** yet an integrated Ring 3 Desktop Shell or general-purpose window protocol; compositor input/scene integration, decorations, resize/close request semantics and Shell launch/task management remain pending.

## What Aurora is not yet

Aurora is not yet a daily-driver replacement for Ubuntu/Linux or Windows.

Major gaps still include:

- production networking/DNS/TLS stack;
- broader USB class support beyond the G4 xHCI HID foundation;
- audio;
- mature power management/suspend/resume;
- broad vendor GPU acceleration and GPU scheduling;
- complete window protocol/Desktop Shell;
- mature application/package ecosystem;
- broad Wi-Fi/Bluetooth/device-driver coverage;
- virtualization comparable with KVM/Hyper-V;
- real-hardware qualification across a broad matrix.

## Build and verification

Aurora uses separate **production boot** and **validation boot** behavior.

Production builds avoid deep synthetic validation probes on the normal startup path. CI builds retain extensive runtime probes.

Current workflow families include:

- Aurora OS Bootstrap Build;
- AHCI Filesystem End-to-End;
- NVMe Probe;
- NVMe Filesystem End-to-End;
- Identity core/runtime tests;
- entropy/Identity handoff validation.

Major milestones are not considered complete merely because they compile. Where practical, the project requires a QEMU runtime acceptance marker or equivalent executable test.

## Languages and toolchain

Aurora does not introduce a proprietary programming language.

The kernel and freestanding services are built primarily as C11/x86_64 targets with Clang/LLD, with assembly only where the architecture boundary requires it.

## Documentation

Start with [`docs/README.md`](docs/README.md).

Current high-level references:

- [Full System Audit and Competitive Position](docs/SYSTEM_AUDIT_2026-10-07.md)
- [Vision](docs/VISION.md)
- [Architecture Principles](docs/ARCHITECTURE_PRINCIPLES.md)
- [Roadmap](docs/ROADMAP.md)
- [Kernel Model](docs/KERNEL_MODEL.md)
- [AuroraFS](docs/AURORAFS.md)
- [Filesystem Compatibility](docs/FILESYSTEM_COMPATIBILITY.md)
- [Aurora Identity](docs/AURORA_IDENTITY.md)
- [Graphics and Desktop](docs/graphics/README.md)
- [Syscall ABI](docs/SYSCALL_ABI.md)

## Status

Active research and development.

Kernel interfaces, storage formats, graphics protocols and service contracts are still evolving and are not a stable production ABI. Runtime validation in QEMU is not equivalent to real-hardware certification.
