# Aurora OS Filesystem Compatibility

Status: **active development**.

Aurora OS uses a filesystem-agnostic storage architecture. AuroraFS is the native filesystem direction, but removable and fixed media are not required to use it. Filesystems are implemented as independent drivers registered with the common filesystem-driver registry and mounted through the common mount manager.

## Architecture

Storage path:

`hardware transport -> block device -> partition manager -> filesystem detector -> filesystem driver -> mount manager -> VFS -> applications`

The common filesystem contract uses 64-bit offsets and file sizes. Individual filesystems may impose their own on-disk limits; Aurora must not add smaller artificial limits on top of those formats.

Aurora distinguishes four support states:

- **Detected**: Aurora can identify the on-disk format.
- **Read-only**: files/directories can be mounted and read safely.
- **Read-write**: mutation is enabled only after corruption-focused validation.
- **CI verified**: a runtime test exercises the format and common VFS path in QEMU.

Recognition alone must never be presented as full filesystem support.

## Current matrix

| Filesystem | Detected | Read-only | Read-write | CI verification |
| --- | --- | --- | --- | --- |
| AuroraFS bootstrap v1 | **Yes** | **Yes** | **Existing bootstrap files only** | **Yes — ATA/AHCI/NVMe paths + common VFS + synthetic 4096-byte logical block** |
| FAT32 / VFAT | Yes | **Yes** | No | **Yes — ATA/AHCI/NVMe paths + Unicode LFN + synthetic 4096-byte logical-block probe/mount** |
| exFAT | Yes | **Yes** | No | **Yes — ATA/AHCI/NVMe paths + synthetic 4096-byte probe/mount** |
| FAT12 | Yes | Pending | No | Pending |
| FAT16 | Yes | Pending | No | Pending |
| NTFS | Yes | Pending | No | Pending |
| ext2/ext3/ext4 | Yes | Pending | No | Pending |
| XFS | Yes | Pending | No | Pending |
| Btrfs | Yes | Pending | No | Pending |
| ISO9660 | Yes | Pending | N/A | Pending |
| UDF | Yes | Pending | No | Pending |
| HFS+ | Yes | Pending | No | Pending |
| APFS | Yes | Pending | No | Pending |

## Runtime-verified transport paths

Aurora currently has three storage transports exercised through the common block/filesystem/VFS architecture:

- **ATA PIO compatibility path**: read/write/flush plus AuroraFS/FAT32/exFAT integration;
- **AHCI SATA path**: IDENTIFY, DMA read/write, explicit flush, MBR partition discovery, AuroraFS/FAT32/VFAT/exFAT and VFS traversal;
- **NVMe path**: PCI/BAR/MMIO discovery, Admin Identify, polling I/O queues, NVM Read/Write/Flush, MBR partition discovery, AuroraFS/FAT32/VFAT/exFAT and VFS traversal.

Workflow **#322** (`36963317225`) is green and verifies the block-device registry plus ATA PIO signature read, write, CACHE FLUSH, readback, byte-for-byte comparison, AuroraFS persistence/mounting, FAT32/VFAT, exFAT, and common VFS routing. The same run also verifies a synthetic exFAT volume presented through a 4096-byte logical-block device.

Workflow **Aurora AHCI Filesystem End-to-End #2** (`36972045000`) is green and verifies the complete AHCI-backed chain through AuroraFS, MBR, FAT32/VFAT, exFAT, mount manager and VFS while ATA PIO is unavailable.

Workflow **Aurora NVMe Filesystem End-to-End #1** (`36975832885`) is green and verifies the complete NVMe-backed chain through AuroraFS, MBR, FAT32/VFAT, exFAT, mount manager and VFS while the AHCI controller has no active SATA ports and ATA PIO is unavailable. This demonstrates that the filesystem stack is genuinely transport-agnostic across both modern SATA and NVMe paths.

Workflow **#328** (`36964846162`) is green and verifies FAT32 probe, mount, and root `stat` on a synthetic 4096-byte logical-block device after separating FAT filesystem-sector geometry from the block device's logical-block geometry.

Workflow **#334** (`36965554498`) is green and verifies AuroraFS bootstrap format, probe-file creation, reopen, generation advancement, mount, root/file `stat`, and file read on a synthetic 4096-byte logical-block device.

## Partition-sector handling

The partition parser accepts logical block sizes of 512, 1024, 2048, and 4096 bytes. MBR metadata is interpreted from the standard first 512 bytes of LBA 0, while GPT header and entry-table addressing use the block device's actual logical block size.

GPT entries that cross a logical-block boundary are assembled from adjacent blocks. GPT header CRC32, partition-entry-array CRC32, and primary-to-backup GPT fallback are implemented.

The partition layer is **runtime-verified on both 512-byte and 4096-byte logical-block synthetic devices**. FAT32, exFAT, and AuroraFS bootstrap also have filesystem-layer 4096-byte synthetic verification. This still does not prove support for every physical 4Kn transport; hardware transport validation remains separate.

## AuroraFS bootstrap v1

Runtime-verified common-driver capabilities:

- filesystem probe;
- common registry registration;
- common mount manager integration;
- mount at `/system` through a synthetic whole-device partition view;
- root-directory `stat`;
- root-directory enumeration;
- file reads through the VFS;
- writes to existing bootstrap files;
- persistence verification across reboot;
- synthetic 4096-byte logical-block format/create/reopen/mount/read verification;
- runtime traversal over ATA PIO, AHCI and NVMe block devices in QEMU.

AuroraFS v1 preserves its existing 512-byte **logical filesystem block** on disk. The driver does not require the underlying block device to expose 512-byte blocks: it translates filesystem byte offsets to 512/1024/2048/4096-byte device blocks and uses read-modify-write when a 512-byte AuroraFS block occupies only part of a larger device block. This keeps compatibility with existing v1 volumes while removing the device-level 512-byte assumption.

Current write support does not imply a production filesystem. AuroraFS bootstrap v1 still lacks general create/remove operations through the filesystem-driver contract, scalable allocation, nested directories, multi-block files, sparse files, extents, crash-consistent transactions, permissions, ownership, timestamps, and recovery structures.

## FAT32 / VFAT

Runtime-verified driver capabilities:

- read-only mount;
- nested directories;
- `stat`;
- directory enumeration;
- multi-cluster file reads;
- 64-bit API offsets;
- VFAT Long File Name sequence validation;
- VFAT short-name checksum validation;
- UTF-16 to UTF-8 conversion;
- surrogate-pair handling;
- UTF-8 name buffers large enough to preserve the maximum VFAT filename;
- reading a file with a Unicode long filename generated by external tooling;
- file access through the common VFS mount path;
- synthetic 4096-byte logical-block probe/mount/root-stat verification;
- runtime traversal over ATA PIO, AHCI and NVMe in QEMU.

FAT32 stores file length in 32 bits, so its approximately 4 GiB per-file ceiling is a FAT32 format property, not an Aurora VFS limit.

Logical-block hardening tracks `bytes_per_sector` independently from `device_block_size` and routes FAT, directory, and file reads through byte-addressed translation over the block device. This is filesystem-layer 4Kn verification; physical 4Kn transport support remains a separate requirement.

## exFAT

Runtime-verified read-only support includes:

- probe and mount;
- 64-bit `DataLength` and `ValidDataLength` handling;
- file-entry set checksum validation;
- Unicode filename decoding;
- directory and nested-path traversal;
- FAT-chain files;
- contiguous `NoFatChain` files;
- 64-bit file offsets and lengths at the Aurora interface;
- reading a file from an externally generated exFAT image;
- file access through the common VFS mount path;
- runtime traversal over ATA PIO, AHCI and NVMe in QEMU.

Logical-block hardening is implemented in the exFAT driver:

- boot-record reads use a buffer sized for device blocks up to 4096 bytes;
- the previous `device->block_size == 512` requirement has been removed;
- exFAT filesystem sector size and device logical-block size are tracked independently;
- mount/probe reject incompatible geometry rather than reading past a 512-byte buffer;
- the generic byte-reading path translates filesystem byte offsets through the actual device block size.

Workflow **#322** runtime-verifies a synthetic 4096-byte logical-block exFAT device through probe, mount, and root `stat`, including rejection of incompatible sector geometry. This is a filesystem-layer 4Kn verification, not proof of support for every real 4Kn hardware transport.

## Current transport limitations

Filesystem support and hardware transport support are separate concerns.

Currently:

- ATA PIO is retained as a compatibility transport;
- AHCI baseline read/write/flush and filesystem traversal are runtime-verified in QEMU;
- NVMe baseline read/write/flush and filesystem traversal are runtime-verified in QEMU;
- AHCI still lacks advanced batching, robust port-reset recovery, interrupt-driven completion, NCQ and hot-plug;
- NVMe still lacks interrupt/MSI-X completion, larger transfer/PRP-list handling, multi-controller/multi-namespace support and hot-plug policy;
- USB/xHCI and USB mass-storage are not implemented yet;
- physical hardware validation remains separate from QEMU runtime verification.

Therefore the modern SATA and NVMe software paths are now functional at baseline, but Aurora must not claim universal hardware compatibility until physical-device testing and advanced recovery/error handling are completed.

## Write-safety policy

Foreign filesystem write support is never enabled merely because a parser can read the format. Before a foreign driver may advertise read-write access it must have:

1. externally generated test images;
2. create/modify/delete tests;
3. remount and reboot persistence tests;
4. dirty-volume handling tests where the format supports them;
5. malformed/corrupt metadata rejection tests;
6. boundary-size and allocation tests;
7. cross-validation by standard tools from another operating system.

Until those conditions are met, Aurora mounts that format read-only.

AuroraFS follows a separate native-format development path, but production write support must still meet equivalent integrity, persistence, corruption, and recovery requirements before being considered stable.

## Long-term target

Aurora should automatically discover storage devices, enumerate MBR/GPT partitions, identify supported filesystems, and mount them through one VFS namespace without forcing conversion to AuroraFS.

New filesystem support must remain modular: adding a driver must not require redesigning the kernel, block layer, partition manager, mount manager, or VFS.
