# Aurora OS Roadmap

Status: **Active implementation**
Version: **0.11**

The roadmap distinguishes architecture decisions, repository implementation, and runtime verification. A feature is not considered complete merely because a detector or interface exists.

## M0 — Definition and boot foundation

### M0.0 — Vision
- [x] Define project mission and efficiency-first philosophy
- [x] Define privacy-first and local-first direction
- [x] Define Desktop, Web Surfaces, Activity Spaces, and Aurora Memory Fabric concepts
- [x] Decide against a proprietary programming language

### M0.1 — Architecture contracts
- [x] Choose initial modular hybrid capability-kernel direction
- [x] Define Ring 0 / Ring 3 boundary
- [x] Define x86_64 SYSCALL/SYSRET direction
- [x] Define initial bounded IPC and typed-capability prototypes
- [x] Define Aurora Identity Service / System App / fallback split
- [x] Define Aurora Identity authentication-factor policy and logical IPC contract
- [ ] Freeze permission broker contract
- [ ] Freeze permission manifest schema
- [ ] Freeze driver model
- [ ] Freeze memory-manager contracts
- [ ] Freeze scheduler classes
- [ ] Freeze boot information contract
- [ ] Freeze logging and panic contracts

### M0.2 — Verified boot
- [x] Native framebuffer boot UI
- [x] Integrated Aurora boot artwork
- [x] Framebuffer panic/diagnostic path
- [x] Freestanding x86_64 kernel build
- [x] Bootable ISO
- [x] BIOS QEMU smoke boot reaches M1 user-space bootstrap
- [x] Serial logging verified in CI
- [ ] Runtime-verify UEFI QEMU boot
- [ ] Produce raw USB image

## M1 — Kernel foundations

### Platform and execution
- [x] ACPI / MADT discovery
- [x] Local APIC + I/O APIC bootstrap
- [x] HPET / monotonic clock bootstrap
- [x] x86_64 IDT and CPU exception handlers
- [x] one-shot timer bootstrap and runtime probe
- [x] SMP bring-up infrastructure
- [ ] Runtime-verify multi-vCPU SMP path in CI

### Memory
- [x] physical memory manager
- [x] virtual memory manager
- [x] kernel heap
- [x] checked usercopy layer

### Isolation and scheduling
- [x] preemptive kernel-thread scheduler prototype
- [x] scheduler runtime self-test
- [x] isolated Ring 3 process prototype with private CR3
- [x] per-thread kernel stack + TSS Ring 3 return path
- [x] x86_64 SYSCALL/SYSRET + EXIT path
- [x] Ring 3 runtime probe

### Security and IPC
- [x] CR0.WP and SMEP/SMAP/UMIP activation where supported
- [x] typed capability prototype
- [x] bounded IPC capability-transfer prototype
- [x] capability and IPC runtime self-tests
- [ ] production capability lineage/revocation policy

### Input and session bootstrap
- [x] generic input-event queue
- [x] IRQ-driven PS/2 keyboard prototype
- [x] boot-to-login framebuffer handoff
- [x] Aurora Identity login UI prototype
- [x] interactive Aurora Key entry
- [ ] persistent Aurora Identity Service
- [ ] secure credential verifier
- [ ] persistent local identity database
- [ ] authenticated session bootstrap

## M2 — Aurora Memory Fabric prototype

- [ ] working-set instrumentation
- [ ] memory pressure states
- [ ] reclaimable cache classes
- [ ] inactive process detection
- [ ] process freezing
- [ ] compressed memory pool
- [ ] storage-backed paging
- [ ] memory telemetry and pressure benchmarks

## M3 — Storage and system integrity

### Block and transport layer
- [x] generic block-device abstraction
- [x] runtime block-device self-test
- [x] PCI storage discovery foundation
- [x] AHCI controller + ABAR discovery
- [x] ATA PIO compatibility read/write path
- [ ] AHCI data I/O
- [ ] NVMe transport
- [ ] USB/xHCI + USB mass storage
- [ ] block-device registry + flush contract
- [ ] remove remaining fixed 512-byte-sector assumptions

### Partition layer
- [x] MBR primary partition scan
- [x] GPT foundation
- [x] parser accepts 512/1024/2048/4096-byte logical blocks
- [x] GPT entries may cross a logical-block boundary
- [x] GPT header CRC32 validation
- [x] GPT partition-entry-array CRC32 validation
- [x] primary-to-backup GPT fallback
- [x] bounded variable GPT entry-size parsing up to 4096 bytes
- [ ] runtime-verify GPT CRC rejection and backup-header fallback
- [ ] runtime-verify non-512-byte logical block parsing
- [ ] end-to-end 512e/4Kn-safe filesystem path
- [ ] extended/logical MBR partitions

### Filesystem framework
- [x] filesystem-driver registry
- [x] filesystem detector
- [x] mount manager
- [x] 64-bit common file offset/size contract
- [x] common VFS routing through mount manager for mounted filesystems
- [x] FAT32/VFAT read-only driver
- [x] VFAT Long File Names + Unicode conversion
- [x] FAT32/VFAT external-image runtime verification through VFS
- [x] exFAT read-only driver
- [x] exFAT external-image runtime verification through VFS
- [ ] remove bootstrap static mount/driver limits
- [ ] FAT12/FAT16 drivers
- [ ] NTFS read-only driver
- [ ] ext2/ext3/ext4 read-only drivers
- [ ] ISO9660/UDF support
- [ ] XFS/Btrfs read-only support
- [ ] HFS+/APFS support where practical

### AuroraFS
- [x] AuroraFS bootstrap v1 on-disk format
- [x] checksummed superblock and data probe
- [x] two-boot persistence verification in QEMU
- [x] register AuroraFS through the common filesystem-driver interface
- [x] mount AuroraFS through the common mount manager and VFS at `/system`
- [x] common-driver stat/readdir/read and existing-file write path
- [ ] scalable production allocation structures
- [ ] directories, multi-block files, sparse files, and extents
- [ ] crash-consistent metadata strategy
- [ ] permissions/ownership/timestamps
- [ ] corruption recovery model

### System integrity
- [ ] protected durable system state
- [ ] transactional identity database backend
- [ ] system/data separation
- [ ] package model
- [ ] signed package metadata
- [ ] transactional deployment prototype
- [ ] rollback

Runtime reference: combined ATA + AuroraFS + FAT32/VFAT + exFAT common-VFS storage smoke test is green in workflow **#290** (`36913468418`) at commit `7ee6073b8165ba9da6e473979e99090f24f27ff1`. Logical-sector-aware partition parsing is runtime-regression-tested on the existing 512-byte CI path. GPT CRC validation, backup-header fallback, and non-512 logical-block behavior are implemented but require dedicated runtime self-tests before being marked runtime-verified.

## M4 — Graphics and Desktop foundation

- [ ] display subsystem beyond boot framebuffer
- [ ] compositor
- [ ] modern USB HID input path
- [ ] pointer/mouse stack
- [ ] window/surface protocol
- [ ] Desktop shell
- [ ] Activity Space prototype
- [ ] persistent activity state
- [ ] customization framework
- [ ] compositor-backed Aurora Identity pre-session surface

## M5 — Network and Web Surfaces

- [ ] network stack integration
- [ ] DNS/TLS services
- [ ] Web Surface host API
- [ ] sandbox boundary
- [ ] per-surface capabilities
- [ ] privacy controls
- [ ] desktop-integrated navigation/search

## M6 — Native application platform

- [ ] application package format
- [ ] sandbox
- [ ] capability broker
- [ ] runtime permission prompts
- [ ] scoped file/device grants
- [ ] privacy and permission history UI
- [ ] native UI toolkit
- [ ] application lifecycle
- [ ] freeze/resume integration
- [ ] notifications/background policy
- [ ] privileged service lifecycle
- [ ] Aurora Identity Service production host
- [ ] Session Manager

## M7 — Recovery, installation and updates

- [ ] live image
- [ ] persistent live mode
- [ ] installer
- [ ] atomic system update
- [ ] rollback UI
- [ ] recovery environment
- [ ] integrity verification
- [ ] Aurora Identity recovery flow
- [ ] identity database integrity/repair flow

## M8 — Aurora Identity authenticators

### Aurora Identity Drive
- [ ] USB host/controller support
- [ ] USB mass-storage support
- [ ] removable-media broker
- [ ] versioned Identity Drive credential container
- [ ] machine-bound drive enrollment
- [ ] insertion/removal authentication flow
- [ ] multiple enrolled drives and independent revocation
- [ ] malformed-media fuzzing

### Secure authenticators
- [ ] security-key transport
- [ ] challenge-response authenticator API
- [ ] non-exportable hardware key support
- [ ] hardware authenticator enrollment/revocation

## Current development gate

Aurora can boot through the M1 kernel foundation and enter the native login prototype. Storage work has advanced ahead of the original milestone order because persistent Identity requires durable local state.

Near-term dependency order:

1. add dedicated GPT integrity/fallback and non-512 logical-block runtime tests;
2. continue removing filesystem-level 512-byte assumptions;
3. add a block-device flush contract and registry;
4. add NTFS and ext-family read-only support;
5. implement modern AHCI data I/O;
6. evolve AuroraFS from bootstrap v1 to scalable production allocation and directory structures;
7. establish protected durable system state;
8. implement secure randomness and credential verification;
9. implement Aurora Identity Service, persistent identity records, and Session Manager;
10. add USB storage and Aurora Identity Drive support.

Detailed Identity sequencing remains in `docs/identity/IMPLEMENTATION_ROADMAP.md`.

## Cross-cutting requirement

Every milestone is evaluated against performance, memory efficiency, latency, privacy, security, recoverability, and measurable resource cost. Documentation must distinguish designed, implemented, and runtime-verified states and must be updated together with material architecture or behavior changes.