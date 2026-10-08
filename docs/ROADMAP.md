# Aurora OS Roadmap

Status: **Active implementation**
Version: **0.40**

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
- [x] Runtime-verify multi-vCPU SMP path in CI
- [x] CPU-local scheduler execution state
- [x] AP scheduler-stack handoff and per-CPU Local APIC timers
- [x] physical package/core/SMT topology discovery
- [x] core-first / SMT-second scheduler placement foundation
- [x] synchronous IPI-based TLB shootdown

### Memory
- [x] physical memory manager
- [x] virtual memory manager
- [x] kernel heap
- [x] checked usercopy layer
- [x] reusable kernel-heap free ranges and lifecycle scrubbing
- [x] anonymous Ring 3 memory allocation/free
- [x] refcounted shared-memory object foundation
- [x] user address-space destruction/reclamation

### Isolation and scheduling
- [x] preemptive kernel-thread scheduler prototype
- [x] scheduler runtime self-test
- [x] isolated Ring 3 process prototype with private CR3
- [x] per-thread kernel stack + TSS Ring 3 return path
- [x] x86_64 SYSCALL/SYSRET + EXIT path
- [x] Ring 3 runtime probe
- [x] thread/process exit and deterministic reap ordering
- [x] repeated create -> run -> exit -> reap -> reuse lifecycle verification
- [x] scheduler BLOCKED/wakeup state for blocking IPC

### Security and IPC
- [x] CR0.WP and SMEP/SMAP/UMIP activation where supported
- [x] typed capability prototype
- [x] bounded IPC capability-transfer prototype
- [x] capability and IPC runtime self-tests
- [x] Ring 3 IPC send/receive plus blocking wait/wakeup foundation
- [x] service dependency-capability bootstrap
- [x] session/profile capability lease + revocation foundation
- [ ] complete production capability lineage/audit policy across all future object classes

### Input and session bootstrap
- [x] generic input-event queue
- [x] IRQ-driven PS/2 keyboard prototype
- [x] boot-to-login framebuffer handoff
- [x] Aurora Identity login UI prototype
- [x] interactive Aurora Key entry
- [x] persistent Ring 3 Aurora Identity Service
- [x] Argon2id credential verifier provider and protected lookup tags
- [x] persistent local identity database
- [x] authenticated one-time session-grant bootstrap
- [x] separate Ring 3 Session Manager
- [x] persistent user profile capability on AuroraFS
- [x] ordinary Ring 3 User Session Host
- [x] logout profile-authority revocation
- [x] lock/unlock with fresh same-user authentication
- [x] fail-closed abnormal session termination

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
- [x] implement v2 multi-block bitmap traversal + 64-bit contiguous range allocate/query/free API
- [x] runtime-verify v2 multi-block bitmap allocator on synthetic 512/4096-byte devices
- [x] integrate scalable allocator into the v2 formatter/volume path
- [x] runtime-verify scalable multi-bitmap formatter + reopen/allocator integration
- [x] implement v2 two-level multi-extent/extent-tree overflow foundation
- [x] runtime-verify v2 two-level extent tree with >126 fragmented extents
- [x] integrate inline-to-tree promotion into persistent v2 inode/file mutation
- [x] runtime-verify persistent fifth-extent inline-to-tree publication
- [x] runtime-verify tree-backed sixth-extent COW append
- [x] runtime-verify 126→127 full-leaf COW growth and persistent level-1 publication
- [x] runtime-verify continued level-1 append through extent 128
- [x] runtime-verify full-last-leaf growth and persistent publication through extent 253
- [x] implement full level-1-root → level-2 COW growth at extent 15,877
- [x] runtime-verify full level-1-root → level-2 COW growth and persistent inode publication
- [x] unify the common extent resolver for levels 0/1/2
- [x] runtime-verify continued COW append under an existing level-2 root
- [x] runtime-verify durable inode root publication after replacement-tree durability
- [x] runtime-verify end-to-end level-2 COW append + inode publication + reopen lookup
- [x] implement and runtime-verify full-last-leaf structural growth under an existing level-2 root
- [x] implement persistent full-last-leaf level-2 append through inode publication
- [x] runtime-verify persistent full-last-leaf level-2 append + inode publication + reopen lookup
- [x] implement full final level-1 sibling COW growth while the level-2 root has spare child capacity
- [x] runtime-verify full final level-1 sibling COW growth on synthetic 512/4096-byte devices
- [x] implement publication of full-level-1 sibling growth through the inode
- [x] runtime-verify full-level-1 sibling growth + inode publication + reopen lookup
- [x] define and implement bounded level-2-root → level-3 COW growth at mapping 2,000,377
- [x] runtime-verify level-2-root → level-3 structural growth and persistent inode publication
- [x] extend unified lookup through bounded level-3 roots
- [x] runtime-verify unified level-3 tree/inode lookup
- [x] runtime-verify continued COW append under an existing level-3 root
- [x] runtime-verify persistent existing level-3 COW append + inode publication + reopen lookup
- [x] runtime-verify full-last-leaf structural growth under an existing level-3 root
- [x] runtime-verify persistent full-last-leaf level-3 append + inode publication + reopen lookup
- [x] implement full final level-1 sibling COW growth below an existing level-3 root
- [x] runtime-verify full final level-1 sibling COW growth below level-3 on synthetic 512/4096-byte devices
- [x] publish level-3 full-level1 sibling growth through the inode and runtime-verify reopen lookup
- [x] close the bounded Level-3 hierarchy contract through its supported maximum mapping
- [x] general create/mkdir/write/truncate/remove/rename operations
- [x] free-space reclaim
- [ ] sparse files
- [x] crash-consistent namespace transaction strategy
- [x] permissions/ownership/timestamps
- [x] persistent ACLs
- [x] corruption/integrity recovery gates
- [x] explicit fsync/fdatasync/sync durability path
- [x] mount real AuroraFS v2 at `/system`
- [ ] explicit v1-to-v2 migration tooling

### System integrity
- [x] protected durable system state
- [x] transactional/persistent Identity database backend
- [ ] system/data separation
- [ ] package model
- [ ] signed package metadata
- [ ] transactional deployment prototype
- [ ] rollback

Runtime reference: workflow **#297** (`36914326026`) runtime-verifies GPT CRC rejection and primary-to-backup fallback on the synthetic 512-byte logical-block test device. Workflow **#299** (`36914796680`) extends the same GPT integrity/fallback self-test to a synthetic 4096-byte logical-block device. Workflow **#322** (`36963317225`) is green and runtime-verifies the block-device registry + ATA PIO read/write/CACHE FLUSH path, AuroraFS persistence and common VFS routing, FAT32/VFAT and exFAT integration, and the synthetic 4096-byte logical-block exFAT probe/mount self-test. Workflow **#328** (`36964846162`) is green and runtime-verifies FAT32 probe/mount/root-stat handling on a synthetic 4096-byte logical-block device. Workflow **#334** (`36965554498`) is green and runtime-verifies AuroraFS bootstrap format/create/reopen/mount/read behavior on a synthetic 4096-byte logical-block synthetic device while preserving the v1 on-disk format. Workflow **#340** (`36966185493`) is green and runtime-verifies AHCI PCI Memory Space/Bus Master enable, ABAR MMIO mapping, AHCI mode enable and SATA-port enumeration. Workflow **#344** (`36969225148`) is green and runtime-verifies AHCI `IDENTIFY DEVICE` through the polling DMA command engine. Workflow **#347** (`36969765926`) is green and runtime-verifies AHCI `READ DMA EXT`, registration of `ahci-sata0`, and LBA0 access through the generic block layer. Workflow **#354** (`36971048569`) is green and runtime-verifies signed reversible AHCI `WRITE DMA EXT`, `FLUSH CACHE EXT`, readback and restoration on the dedicated q35 test disk. Workflow **Aurora AHCI Filesystem End-to-End #2** (`36972045000`) is green and runtime-verifies AHCI-backed AuroraFS format/mount/read, MBR partition discovery, FAT32/VFAT file access, exFAT file access, common mount-manager routing and VFS traversal while ATA PIO is unavailable. Workflow **Aurora NVMe Probe #2** (`36973751130`) runtime-verifies PCI/64-bit BAR/MMIO discovery. Workflow **Aurora NVMe Probe #7** (`36974618553`) runtime-verifies controller enable, Admin Queue setup and Identify Controller/Namespace. Workflow **Aurora NVMe Probe #10** (`36974935129`) runtime-verifies I/O queue creation, NVM Read, block-device registration and LBA0 access. Workflow **Aurora NVMe Probe #14** (`36975450797`) runtime-verifies NVM Write, NVM Flush, readback and restoration; the CI also verifies the original signed final sector directly in the backing image after QEMU exits. Workflow **Aurora NVMe Filesystem End-to-End #1** (`36975832885`) is green and runtime-verifies the complete NVMe-backed AuroraFS/MBR/FAT32/VFAT/exFAT/VFS path while AHCI has zero active SATA ports and ATA PIO is unavailable. Workflow **#394** (`36976628830`) runtime-verifies the AuroraFS v2 4 KiB layout foundation. Workflow **#399** (`36977213539`) runtime-verifies dynamic nested directories. Workflow **#407** (`36980285923`) runtime-verifies the multi-block bitmap allocator. Workflow **#411** (`36981472946`) runtime-verifies the scalable multi-bitmap formatter. Workflow **#414** (`36981973396`) runtime-verifies the two-level extent-tree foundation with 130 fragmented extents. Workflow **#422** (`36982594135`) runtime-verifies persistent fifth-extent inline-to-tree promotion. Workflow **#429** (`36983175716`) runtime-verifies post-promotion sixth-extent COW append. Workflow **#432** (`36983609681`) runtime-verifies 126→127 full-leaf growth. Workflow **#437** (`36984123103`) runtime-verifies persistent publication of extent 127. Workflow **#442** (`36984773089`) runtime-verifies existing level-1 COW append and persistent publication of extent 128. Workflow **#446** (`36986548697`) runtime-verifies full-last-leaf growth and persistent publication of extent 253. Workflow `36988686741` runtime-verifies level-1-full-root → level-2 growth and persistent publication at extent 15,877. Workflow `36989469332` runtime-verifies unified lookup across leaf/level-1/level-2 roots. Workflow `36990022065` runtime-verifies continued COW append under an existing level-2 root. Workflow `36992375163` runtime-verifies durable inode root publication. Workflow `36994012534` runtime-verifies end-to-end level-2 append + inode publication + reopen lookup. Workflow `36994521731` runtime-verifies full-last-leaf structural growth under level-2. Workflow `36996338607` runtime-verifies persistent full-last-leaf append + inode publication + reopen lookup. Workflow `36997766355` runtime-verifies full final level-1 sibling COW growth on the main q35/BIOS path plus ATA first/persistence boots; AHCI workflow `36997766251` also passes the same structural gate. Workflow `36999077531` runtime-verifies bounded unified lookup through level 3. Workflow `37000379420` runtime-verifies full level-2-root → level-3 structural growth; workflow `37001092435` runtime-verifies the corresponding persistent inode publication. Workflow `37004408218` runtime-verifies continued persistent COW append under an existing level-3 root after the kernel-stack self-test fix. Workflow `37005018110` runtime-verifies full-last-leaf structural growth under level-3. Workflow `37005580767` runtime-verifies persistent full-last-leaf level-3 publication. Workflow `37006271635` runtime-verifies full final level-1 sibling COW growth below an existing level-3 root while preserving AuroraFS v1 two-boot persistence.

## M4 — Graphics and Desktop foundation

Architecture contracts: [`graphics/README.md`](graphics/README.md).

- [x] graphics architecture and protocol contracts defined
- [x] G1 display subsystem foundation beyond direct boot-framebuffer ownership
- [x] G2 capability-backed surface/buffer protocol and cross-client isolation
- [x] G3 software compositor with damage/clipping/z-order/transforms/occlusion
- [x] mastering software color pipeline: ST.2084, BT.2100 HLG, ICC matrix-shaper, calibration and tone mapping
- [x] normalized device-independent input event foundation
- [x] PS/2 keyboard normalized-event path
- [x] live IRQ12 PS/2 mouse packet path
- [x] secure-scene-aware compositor hit testing
- [x] pointer/keyboard focus routing with private target queues
- [x] multi-client normalized-input isolation gate
- [x] explicit pointer capture/revocation completion
- [x] transport-agnostic USB HID boot keyboard/mouse decoder foundation
- [x] bounded HID binding/dispatch layer with stale-handle and disconnect hardening
- [x] xHCI PCI/BAR/MMIO discovery + reset/readiness runtime gate
- [x] xHCI DCBAA/command/event/ERST DMA bootstrap + Run-state QEMU gate
- [x] xHCI connected-port reset + Enable Slot command/completion runtime gate
- [x] xHCI Input/Device Context + Address Device + EP0 Running runtime gate
- [x] xHCI EP0 control transfer + Device/Configuration/HID endpoint descriptor runtime gate
- [ ] SET_CONFIGURATION + HID interrupt endpoint context + live HID report transport
- [ ] G5 window-management protocol and Desktop Shell
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
- [x] privileged trusted-service lifecycle/supervision foundation
- [x] Aurora Identity Service Ring 3 production-path host
- [x] Session Manager and ordinary User Session Host foundation

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

Aurora now has runtime-verified kernel/SMP/Ring 3 lifecycle foundations, baseline AHCI and NVMe storage paths, a real AuroraFS v2 `/system` mount with Level-3 bounded extent hierarchy and higher-level mutation/recovery/metadata/ACL/durability semantics, a live Ring 3 Identity/Session chain, and M4 graphics through the completed G3 software compositor/color milestone. G4 pointer/modern-input work is active.

Near-term dependency order:

1. complete G4 explicit pointer-capture semantics/revocation and destruction/session teardown handling;
2. add USB/xHCI + USB HID foundations so input/removable-media work is not PS/2-bound;
3. implement G5 toplevel/window protocol and Desktop Shell baseline;
4. migrate normal Identity presentation onto the compositor while retaining framebuffer recovery fallback;
5. continue vendor GPU scanout/acceleration and hardware color/VRR/DSC programming;
6. add networking/DNS/TLS foundations for M5;
7. extend the memory manager with demand paging/page-cache/swap-class facilities required by a mature desktop;
8. strengthen AHCI/NVMe recovery, interrupts/queue scaling and hot-plug policy;
9. expand real-hardware validation, fuzzing and fault-injection coverage.

Detailed Identity sequencing remains in `docs/identity/IMPLEMENTATION_ROADMAP.md`.

## Cross-cutting requirement

Every milestone is evaluated against performance, memory efficiency, latency, privacy, security, recoverability, and measurable resource cost. Documentation must distinguish designed, implemented, and runtime-verified states and must be updated together with material architecture or behavior changes.
