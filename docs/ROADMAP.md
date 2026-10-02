# Aurora OS Roadmap

Status: **Active implementation**
Version: **0.24**

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
- [x] block-device registry API and duplicate-name rejection
- [x] generic optional flush contract
- [x] ATA PIO exposes CACHE FLUSH through the block layer
- [x] PCI storage discovery foundation
- [x] AHCI controller + ABAR discovery
- [x] AHCI MMIO enable + SATA-port enumeration
- [x] AHCI polling command engine + `IDENTIFY DEVICE`
- [x] AHCI `READ DMA EXT` through a registered `block_device`
- [x] runtime-verify AHCI LBA0 read through the generic block layer
- [x] AHCI `WRITE DMA EXT` + `FLUSH CACHE EXT`
- [x] runtime-verify signed reversible AHCI write/flush/readback/restore probe
- [x] AHCI end-to-end partition/filesystem/VFS traversal
- [x] runtime-verify AHCI -> MBR -> AuroraFS/FAT32/exFAT -> VFS on q35
- [x] ATA PIO compatibility read/write path
- [x] runtime-verify registry + ATA read/write/flush integration in CI
- [ ] AHCI multi-sector batching / multi-entry PRDT
- [ ] AHCI robust timeout/error recovery and port reset
- [ ] AHCI interrupt-driven completion / NCQ / hot-plug
- [x] NVMe PCI discovery + 64-bit BAR0/MMIO capability probe
- [x] runtime-verify NVMe PCI/BAR/MMIO discovery
- [x] NVMe controller enable + Admin SQ/CQ + `Identify Controller` / `Identify Namespace`
- [x] runtime-verify NVMe Admin Queue and namespace geometry
- [x] NVMe I/O CQ/SQ + NVM Read + `nvme-ns1` generic block-device registration
- [x] runtime-verify NVMe LBA0 read through the generic block layer
- [x] NVMe NVM Write + NVM Flush through the read/write block layer
- [x] runtime-verify signed reversible NVMe write/flush/readback/restore probe
- [x] common partition/filesystem/VFS traversal for `nvme-ns1`
- [x] runtime-verify NVMe -> MBR -> AuroraFS/FAT32/exFAT -> VFS end-to-end
- [ ] NVMe interrupts/MSI-X, recovery hardening, multiple namespaces/controllers and hot-plug
- [ ] USB/xHCI + USB mass storage
- [x] remove fixed 512-byte assumptions from the current partition/FAT32/exFAT/AuroraFS bootstrap paths
- [ ] remove bootstrap static block-device registry limit

### Partition layer
- [x] MBR primary partition scan
- [x] GPT foundation
- [x] parser accepts 512/1024/2048/4096-byte logical blocks
- [x] GPT entries may cross a logical-block boundary
- [x] GPT header CRC32 validation
- [x] GPT partition-entry-array CRC32 validation
- [x] primary-to-backup GPT fallback
- [x] bounded variable GPT entry-size parsing up to 4096 bytes
- [x] runtime-verify GPT CRC rejection and backup-header fallback
- [x] runtime-verify 4096-byte logical block parsing
- [x] filesystem-layer 4Kn-safe path for partition/FAT32/exFAT/AuroraFS bootstrap on synthetic devices
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
- [x] FAT32 logical-block hardening with independent filesystem-sector/device-block geometry
- [x] runtime-verify FAT32 on a synthetic 4096-byte logical-block device
- [x] exFAT read-only driver
- [x] exFAT external-image runtime verification through VFS
- [x] exFAT boot-record reads are safe for 512/1024/2048/4096-byte device blocks when filesystem sector geometry is compatible
- [x] runtime-verify exFAT on a synthetic 4096-byte logical-block device
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
- [x] remove device-block 512-byte assumption while preserving the v1 512-byte logical filesystem block format
- [x] runtime-verify AuroraFS v1 on a 4096-byte logical-block synthetic device
- [x] define separately versioned AuroraFS v2 production-layout contract
- [x] implement initial v2 4 KiB superblock, CRC32, allocation bitmap, 256-byte 64-bit inode and inline extent foundation
- [x] implement v2 multi-block (>512 byte) extent persistence self-test on synthetic 512/4096-byte devices
- [x] runtime-verify AuroraFS v2 layout foundation in CI
- [x] implement v2 checksummed directory records, two-block root directory and nested traversal
- [x] runtime-verify v2 dynamic two-block root + nested directory traversal on synthetic 512/4096-byte devices
- [ ] v2 multi-block allocation bitmap + general free-range allocator
- [ ] v2 multi-extent/extent-tree overflow
- [ ] general create/truncate/remove/rename operations
- [ ] sparse files
- [ ] crash-consistent metadata strategy
- [ ] permissions/ownership/timestamps
- [ ] corruption recovery model
- [ ] explicit v1-to-v2 migration tooling

### System integrity
- [ ] protected durable system state
- [ ] transactional identity database backend
- [ ] system/data separation
- [ ] package model
- [ ] signed package metadata
- [ ] transactional deployment prototype
- [ ] rollback

Runtime reference: workflow **#297** (`36914326026`) runtime-verifies GPT CRC rejection and primary-to-backup fallback on the synthetic 512-byte logical-block test device. Workflow **#299** (`36914796680`) extends the same GPT integrity/fallback self-test to a synthetic 4096-byte logical-block device. Workflow **#322** (`36963317225`) is green and runtime-verifies the block-device registry + ATA PIO read/write/CACHE FLUSH path, AuroraFS persistence and common VFS routing, FAT32/VFAT and exFAT integration, and the synthetic 4096-byte logical-block exFAT probe/mount self-test. Workflow **#328** (`36964846162`) is green and runtime-verifies FAT32 probe/mount/root-stat handling on a synthetic 4096-byte logical-block device. Workflow **#334** (`36965554498`) is green and runtime-verifies AuroraFS bootstrap format/create/reopen/mount/read behavior on a synthetic 4096-byte logical-block device while preserving the v1 on-disk format. Workflow **#340** (`36966185493`) is green and runtime-verifies AHCI PCI Memory Space/Bus Master enable, ABAR MMIO mapping, AHCI mode enable and SATA-port enumeration. Workflow **#344** (`36969225148`) is green and runtime-verifies AHCI `IDENTIFY DEVICE` through the polling DMA command engine. Workflow **#347** (`36969765926`) is green and runtime-verifies AHCI `READ DMA EXT`, registration of `ahci-sata0`, and LBA0 access through the generic block layer. Workflow **#354** (`36971048569`) is green and runtime-verifies signed reversible AHCI `WRITE DMA EXT`, `FLUSH CACHE EXT`, readback and restoration on the dedicated q35 test disk. Workflow **Aurora AHCI Filesystem End-to-End #2** (`36972045000`) is green and runtime-verifies AHCI-backed AuroraFS format/mount/read, MBR partition discovery, FAT32/VFAT file access, exFAT file access, common mount-manager routing and VFS traversal while ATA PIO is unavailable. Workflow **Aurora NVMe Probe #2** (`36973751130`) runtime-verifies PCI/64-bit BAR/MMIO discovery. Workflow **Aurora NVMe Probe #7** (`36974618553`) runtime-verifies controller enable, Admin Queue setup and Identify Controller/Namespace. Workflow **Aurora NVMe Probe #10** (`36974935129`) runtime-verifies I/O queue creation, NVM Read, block-device registration and LBA0 access. Workflow **Aurora NVMe Probe #14** (`36975450797`) runtime-verifies NVM Write, NVM Flush, readback and restoration; the CI also verifies the original signed final sector directly in the backing image after QEMU exits. Workflow **Aurora NVMe Filesystem End-to-End #1** (`36975832885`) is green and runtime-verifies the complete NVMe-backed AuroraFS/MBR/FAT32/VFAT/exFAT/VFS path while AHCI has zero active SATA ports and ATA PIO is unavailable. Workflow **#394** (`36976628830`) is green and runtime-verifies the AuroraFS v2 4 KiB layout, CRC32 superblock, allocation bitmap, 256-byte 64-bit inode and multi-block extent persistence on synthetic 512/4096-byte devices. Workflow **#399** (`36977213539`) is green and runtime-verifies the AuroraFS v2 checksummed directory-record format, two-block root directory extent and nested `docs/note.txt` traversal on synthetic 512/4096-byte devices.

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

Aurora can boot through the M1 kernel foundation and enter the native login prototype. The storage stack has runtime-verified baseline modern SATA and NVMe paths through the generic block layer, partition manager, AuroraFS bootstrap, FAT32/VFAT, exFAT, mount manager and VFS. ATA PIO remains a compatibility transport. AuroraFS v2 now has runtime-verified layout, multi-block extent persistence and nested two-block directory traversal while v1 remains the mounted compatibility/bootstrap format.

Near-term dependency order:

1. implement and runtime-verify AuroraFS v2 multi-block allocation bitmap traversal and a general free-range allocator;
2. add multi-extent/extent-tree overflow;
3. add general create/truncate/remove/rename operations;
4. establish crash-consistent metadata updates and recovery behavior;
5. remove bootstrap static registry/mount/driver limits;
6. add NTFS and ext-family read-only support;
7. strengthen AHCI/NVMe with batching, recovery, interrupt-driven completion and hot-plug policy;
8. establish protected durable system state;
9. implement secure randomness, credential verification, Aurora Identity Service and Session Manager;
10. add USB/xHCI storage and Aurora Identity Drive support.

Detailed Identity sequencing remains in `docs/identity/IMPLEMENTATION_ROADMAP.md`.

## Cross-cutting requirement

Every milestone is evaluated against performance, memory efficiency, latency, privacy, security, recoverability, and measurable resource cost. Documentation must distinguish designed, implemented, and runtime-verified states and must be updated together with material architecture or behavior changes.
